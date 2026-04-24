#!/bin/sh
set -eu

LDAP_BASE_DN="${LDAP_BASE_DN:-dc=example,dc=org}"
LDAP_DOMAIN="${LDAP_DOMAIN:-example.org}"
LDAP_ORGANIZATION="${LDAP_ORGANIZATION:-Example Organization}"
LDAP_ADMIN_PASSWORD="${LDAP_ADMIN_PASSWORD:-changeme}"
LDAP_ENABLE_LDAPS="${LDAP_ENABLE_LDAPS:-false}"
LDAP_ENABLE_SYSLOG_NG="${LDAP_ENABLE_SYSLOG_NG:-false}"
LDAP_OPEN_FILES_LIMIT="${LDAP_OPEN_FILES_LIMIT:-1024}"
LDAP_TLS_CERT_FILE="${LDAP_TLS_CERT_FILE:-/etc/ldap/tls/tls.crt}"
LDAP_TLS_KEY_FILE="${LDAP_TLS_KEY_FILE:-/etc/ldap/tls/tls.key}"
LDAP_TLS_CA_FILE="${LDAP_TLS_CA_FILE:-/etc/ldap/tls/ca.crt}"
LDAP_INIT_MODE="${LDAP_INIT_MODE:-fresh}"
LDAP_RUNTIME_USER="${LDAP_RUNTIME_USER:-ldap-runtime}"
LDAP_RUNTIME_GROUP="${LDAP_RUNTIME_GROUP:-ldap-runtime}"
LDAP_RUNTIME_UID="${LDAP_RUNTIME_UID:-11050}"
LDAP_RUNTIME_GID="${LDAP_RUNTIME_GID:-11050}"
LDAP_INTERNAL_LDAP_PORT="${LDAP_INTERNAL_LDAP_PORT:-1389}"
LDAP_INTERNAL_LDAPS_PORT="${LDAP_INTERNAL_LDAPS_PORT:-1636}"
LDAP_DATA_DIR="/var/lib/ldap"
LDAP_CONFIG_DIR="/etc/ldap/slapd.d"
LDAP_LOG_DIR="/var/log/slapd"
LDAP_RUN_DIR="/run/slapd"
LDAP_TLS_DIR="/etc/ldap/tls"
LDAP_BOOTSTRAP_DIR="/opt/openldap/bootstrap"
LDAP_BOOTSTRAP_LDIF_DIR="${LDAP_BOOTSTRAP_DIR}/ldif"
LDAP_BOOTSTRAP_SCHEMA_DIR="${LDAP_BOOTSTRAP_DIR}/schema"

if [ "${LDAP_SKIP_INIT:-false}" = "true" ]; then
    LDAP_INIT_MODE="disabled"
fi

ensure_dir() {
    mkdir -p "$1"
}

dir_is_empty() {
    [ -z "$(find "$1" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]
}

ensure_runtime_identity() {
    if ! id "${LDAP_RUNTIME_USER}" >/dev/null 2>&1; then
        echo "Runtime user ${LDAP_RUNTIME_USER} is missing from the image." >&2
        exit 1
    fi

    if [ "$(id -u "${LDAP_RUNTIME_USER}")" != "${LDAP_RUNTIME_UID}" ] || [ "$(id -g "${LDAP_RUNTIME_USER}")" != "${LDAP_RUNTIME_GID}" ]; then
        echo "Runtime uid/gid env does not match the baked image user ${LDAP_RUNTIME_USER}." >&2
        exit 1
    fi
}

ensure_owned_tree() {
    path="$1"
    ensure_dir "${path}"
    if find "${path}" \( ! -user "${LDAP_RUNTIME_UID}" -o ! -group "${LDAP_RUNTIME_GID}" \) -print -quit 2>/dev/null | grep -q .; then
        chown -R "${LDAP_RUNTIME_UID}:${LDAP_RUNTIME_GID}" "${path}"
    fi
}

bootstrap_slapd() {
    cat <<EOF | debconf-set-selections
slapd slapd/no_configuration boolean false
slapd slapd/domain string ${LDAP_DOMAIN}
slapd shared/organization string ${LDAP_ORGANIZATION}
slapd slapd/password1 password ${LDAP_ADMIN_PASSWORD}
slapd slapd/password2 password ${LDAP_ADMIN_PASSWORD}
slapd slapd/backend select MDB
slapd slapd/purge_database boolean false
slapd slapd/move_old_database boolean true
slapd slapd/allow_ldap_v2 boolean false
EOF

    printf '#!/bin/sh\nexit 101\n' > /usr/sbin/policy-rc.d
    chmod 0755 /usr/sbin/policy-rc.d
    dpkg-reconfigure -f noninteractive slapd
    rm -f /usr/sbin/policy-rc.d
}

ensure_runtime_identity
ensure_dir "${LDAP_BOOTSTRAP_DIR}"
ensure_dir "${LDAP_BOOTSTRAP_LDIF_DIR}"
ensure_dir "${LDAP_BOOTSTRAP_SCHEMA_DIR}"
ensure_dir "${LDAP_TLS_DIR}"
ensure_owned_tree "${LDAP_DATA_DIR}"
ensure_owned_tree "${LDAP_CONFIG_DIR}"
ensure_owned_tree "${LDAP_LOG_DIR}"
ensure_owned_tree "${LDAP_RUN_DIR}"

if dir_is_empty "${LDAP_CONFIG_DIR}"; then
    case "${LDAP_INIT_MODE}" in
        fresh)
            rm -rf "${LDAP_DATA_DIR}"/* "${LDAP_CONFIG_DIR}"/*
            bootstrap_slapd
            ensure_owned_tree "${LDAP_DATA_DIR}"
            ensure_owned_tree "${LDAP_CONFIG_DIR}"
            ;;
        config-only)
            rm -rf "${LDAP_DATA_DIR}"/* "${LDAP_CONFIG_DIR}"/*
            bootstrap_slapd
            ensure_owned_tree "${LDAP_DATA_DIR}"
            ensure_owned_tree "${LDAP_CONFIG_DIR}"
            rm -rf "${LDAP_DATA_DIR}"/*
            echo "OpenLDAP config initialized without directory data (LDAP_INIT_MODE=config-only)."
            ;;
        disabled)
            echo "LDAP init disabled, but ${LDAP_CONFIG_DIR} is empty." >&2
            echo "Provide an existing cn=config volume or use LDAP_INIT_MODE=fresh/config-only." >&2
            exit 1
            ;;
        *)
            echo "Unsupported LDAP_INIT_MODE=${LDAP_INIT_MODE}; expected fresh, config-only or disabled." >&2
            exit 1
            ;;
    esac
else
    case "${LDAP_INIT_MODE}" in
        fresh|config-only|disabled)
            ;;
        *)
            echo "Unsupported LDAP_INIT_MODE=${LDAP_INIT_MODE}; expected fresh, config-only or disabled." >&2
            exit 1
            ;;
    esac
fi

LDAP_URLS="ldap://0.0.0.0:${LDAP_INTERNAL_LDAP_PORT}/ ldapi:///"
if [ "${LDAP_ENABLE_LDAPS}" = "true" ]; then
    if [ -r "${LDAP_TLS_CERT_FILE}" ] && [ -r "${LDAP_TLS_KEY_FILE}" ] && [ -r "${LDAP_TLS_CA_FILE}" ]; then
        LDAP_URLS="${LDAP_URLS} ldaps://0.0.0.0:${LDAP_INTERNAL_LDAPS_PORT}/"
    else
        echo "LDAPS requested, but TLS material is incomplete. Continuing without ldaps:// listener." >&2
    fi
fi

# Work around slapd calloc crashes seen in containers with very high nofile limits.
# Keeping it explicit here also makes the runtime independent from the host defaults.
ulimit -n "${LDAP_OPEN_FILES_LIMIT}" || true

if ! gosu "${LDAP_RUNTIME_USER}:${LDAP_RUNTIME_GROUP}" slaptest -u -F "${LDAP_CONFIG_DIR}" >/dev/null 2>&1; then
    echo "slapd configuration validation failed under ${LDAP_CONFIG_DIR}" >&2
    gosu "${LDAP_RUNTIME_USER}:${LDAP_RUNTIME_GROUP}" slaptest -u -F "${LDAP_CONFIG_DIR}" >&2 || true
    exit 1
fi

if [ "${LDAP_ENABLE_SYSLOG_NG}" = "true" ] && command -v syslog-ng >/dev/null 2>&1; then
    syslog-ng --no-caps -F &
fi

exec gosu "${LDAP_RUNTIME_USER}:${LDAP_RUNTIME_GROUP}" slapd -h "${LDAP_URLS}" -F "${LDAP_CONFIG_DIR}" -d 0
