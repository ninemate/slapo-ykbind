#!/bin/sh
set -eu

if [ -n "${LDAP_ADMIN_DN:-}" ] && [ -n "${LDAP_ADMIN_PASSWORD:-}" ]; then
    ldapwhoami -x -D "${LDAP_ADMIN_DN}" -w "${LDAP_ADMIN_PASSWORD}" -H ldap://127.0.0.1 >/dev/null 2>&1
else
    ldapwhoami -x -H ldap://127.0.0.1 >/dev/null 2>&1
fi
