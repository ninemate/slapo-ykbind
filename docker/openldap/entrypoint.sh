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

ensure_dir() {
    mkdir -p "$1"
}

dir_is_empty() {
    [ -z "$(find "$1" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]
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

ensure_dir /var/lib/ldap
ensure_dir /etc/ldap/slapd.d
ensure_dir /var/log/slapd
ensure_dir /run/slapd

if dir_is_empty /etc/ldap/slapd.d; then
    rm -rf /var/lib/ldap/* /etc/ldap/slapd.d/*
    bootstrap_slapd
fi

LDAP_URLS="ldap:/// ldapi:///"
if [ "${LDAP_ENABLE_LDAPS}" = "true" ]; then
    if [ -r "${LDAP_TLS_CERT_FILE}" ] && [ -r "${LDAP_TLS_KEY_FILE}" ] && [ -r "${LDAP_TLS_CA_FILE}" ]; then
        LDAP_URLS="${LDAP_URLS} ldaps:///"
    else
        echo "LDAPS requested, but TLS material is incomplete. Continuing without ldaps:// listener." >&2
    fi
fi

# Work around slapd calloc crashes seen in containers with very high nofile limits.
# Keeping it explicit here also makes the runtime independent from the host defaults.
ulimit -n "${LDAP_OPEN_FILES_LIMIT}" || true

if ! slaptest -u -F /etc/ldap/slapd.d >/dev/null 2>&1; then
    echo "slapd configuration validation failed under /etc/ldap/slapd.d" >&2
    slaptest -u -F /etc/ldap/slapd.d >&2 || true
    exit 1
fi

if [ "${LDAP_ENABLE_SYSLOG_NG}" = "true" ] && command -v syslog-ng >/dev/null 2>&1; then
    syslog-ng --no-caps -F &
fi

exec slapd -h "${LDAP_URLS}" -F /etc/ldap/slapd.d -d 0
