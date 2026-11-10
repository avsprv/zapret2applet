#!/bin/sh
set -eu

application="${1:-/usr/bin/zapret2control}"
startup_log="$(mktemp)"
trap 'rm -f "$startup_log"' EXIT HUP INT TERM

set +e
timeout --signal=TERM 8s dbus-run-session -- env QT_QPA_PLATFORM=offscreen "$application" >"$startup_log" 2>&1
startup_status=$?
set -e

if [ "$startup_status" -ne 124 ]; then
    cat "$startup_log"
    echo "Kirigami application did not stay running after startup (status $startup_status)" >&2
    exit 1
fi

if grep -E 'module ".*" is not installed|failed to load component|QQmlApplicationEngine failed' "$startup_log"; then
    cat "$startup_log"
    exit 1
fi
