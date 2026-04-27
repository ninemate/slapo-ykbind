#!/bin/sh
set -eu

RADIUS_CONFIG_DIR="${RADIUS_CONFIG_DIR:-/etc/freeradius/3.0}"
RADIUS_LOG_DIR="${RADIUS_LOG_DIR:-/var/log/freeradius}"
RADIUS_RUN_DIR="${RADIUS_RUN_DIR:-/var/run/freeradius}"
RADIUS_MAIN_CONFIG_FILE="${RADIUS_MAIN_CONFIG_FILE:-$RADIUS_CONFIG_DIR/radiusd.conf}"
RADIUS_DAEMON_FLAGS="${RADIUS_DAEMON_FLAGS:--X}"

for required_dir in "$RADIUS_CONFIG_DIR" "$RADIUS_LOG_DIR" "$RADIUS_RUN_DIR"; do
    if [ ! -d "$required_dir" ]; then
        echo "Missing required directory: $required_dir" >&2
        exit 1
    fi
done

if [ ! -r "$RADIUS_MAIN_CONFIG_FILE" ]; then
    echo "Missing readable main FreeRADIUS config: $RADIUS_MAIN_CONFIG_FILE" >&2
    exit 1
fi

if [ "$#" -gt 0 ]; then
    exec "$@"
fi

freeradius -CX -d "$RADIUS_CONFIG_DIR"
set -- freeradius $RADIUS_DAEMON_FLAGS -d "$RADIUS_CONFIG_DIR"
exec "$@"
