#!/bin/sh
set -eu

LDAP_INTERNAL_LDAP_PORT="${LDAP_INTERNAL_LDAP_PORT:-1389}"
LDAP_HEALTHCHECK_URI="${LDAP_HEALTHCHECK_URI:-ldap://127.0.0.1:${LDAP_INTERNAL_LDAP_PORT}}"

if [ -n "${LDAP_ADMIN_DN:-}" ] && [ -n "${LDAP_ADMIN_PASSWORD:-}" ]; then
    ldapwhoami -x -D "${LDAP_ADMIN_DN}" -w "${LDAP_ADMIN_PASSWORD}" -H "${LDAP_HEALTHCHECK_URI}" >/dev/null 2>&1
else
    ldapwhoami -x -H "${LDAP_HEALTHCHECK_URI}" >/dev/null 2>&1
fi
