#!/bin/sh
set -eu

application="${1:-/usr/bin/zapret2control}"
startup_log="$(mktemp)"
trap 'rm -f "$startup_log"' EXIT HUP INT TERM

if ! timeout --signal=TERM 30s dbus-run-session -- env QT_QPA_PLATFORM=offscreen ZAPRET2CONTROL_QML_SMOKE_TEST=1 "$application" >"$startup_log" 2>&1; then
    cat "$startup_log"
    echo "Kirigami application failed to load its production QML module" >&2
    exit 1
fi

if grep -E 'module ".*" is not installed|failed to load component|QQmlApplicationEngine failed|: (TypeError|ReferenceError|SyntaxError):' "$startup_log"; then
    cat "$startup_log"
    exit 1
fi
