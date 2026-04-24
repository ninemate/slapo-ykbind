#!/bin/sh
set -eu

pgrep -x freeradius >/dev/null 2>&1 || pgrep -x radiusd >/dev/null 2>&1

if [ -n "${RADIUS_LDAP_HEALTHCHECK_URI:-}" ] && [ -n "${RADIUS_LDAP_BIND_DN:-}" ] && [ -n "${RADIUS_LDAP_BIND_PASSWORD:-}" ]; then
    ldapwhoami -x \
        -D "${RADIUS_LDAP_BIND_DN}" \
        -w "${RADIUS_LDAP_BIND_PASSWORD}" \
        -H "${RADIUS_LDAP_HEALTHCHECK_URI}" >/dev/null 2>&1
fi
