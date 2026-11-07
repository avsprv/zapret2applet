#!/bin/sh
# Runtime directory hardening tests for the privileged Blockcheck helper.
set -eu
H="${1:?usage: test_blockcheck_helper.sh /path/to/zapret2applet-helper}"
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT HUP INT TERM
mkdir "$W/base"
printf '#!/bin/sh\\nexit 0\\n' > "$W/base/blockcheck2.sh"
chmod 755 "$W/base/blockcheck2.sh"
mkdir "$W/target"
ln -s "$W/target" "$W/runtime-link"

expect_reject() {
    label=$1
    shift
    set +e
    out=$(env QZAPRET_TEST_BASE="$W/base" "$@" "$H" blockcheck --domains example.org 2>&1)
    rc=$?
    set -e
    if [ "$rc" -eq 2 ] && printf '%s' "$out" | grep -q "$label"; then
        echo "ok: rejected unsafe runtime path ($label)"
    else
        echo "FAIL: expected rejection ($label), got rc=$rc: $out"
        exit 1
    fi
}

expect_reject 'runtime directory' QZAPRET_TEST_RUNDIR="$W/runtime-link"
mkdir "$W/runtime-insecure"
chmod 755 "$W/runtime-insecure"
expect_reject 'runtime directory' QZAPRET_TEST_RUNDIR="$W/runtime-insecure"

# A private directory is accepted far enough to create the lock and PID state.
# The fake upstream exits immediately; no system service or network rule is touched.
mkdir "$W/runtime-private"
set +e
env QZAPRET_TEST_BASE="$W/base" QZAPRET_TEST_RUNDIR="$W/runtime-private" \
    "$H" blockcheck --domains example.org > "$W/output" 2>&1
rc=$?
set -e
if [ "$rc" -ne 0 ]; then
    echo "FAIL: private runtime directory was rejected (rc=$rc)"
    cat "$W/output"
    exit 1
fi
[ -f "$W/runtime-private/blockcheck.lock" ] || { echo "FAIL: lock file was not created"; exit 1; }
[ ! -L "$W/runtime-private/blockcheck.pid" ] || { echo "FAIL: PID file is a symlink"; exit 1; }
echo "ok: private runtime directory accepted and lock created"
