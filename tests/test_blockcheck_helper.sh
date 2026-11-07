#!/bin/sh
# Runtime directory, PID identity, and lock lifecycle tests for the privileged helper.
set -eu
H="$1"
[ -n "$H" ] || { echo "usage: test_blockcheck_helper.sh /path/to/zapret2applet-helper"; exit 2; }
W=$(mktemp -d)
runner_pid=
cleanup() {
    if [ -n "$runner_pid" ]; then
        env QZAPRET_TEST_BASE="$W/base" QZAPRET_TEST_RUNDIR="$W/runtime-private" \
            "$H" blockcheck-cancel >/dev/null 2>&1 || true
        wait "$runner_pid" 2>/dev/null || true
    fi
    rm -rf "$W"
}
trap cleanup EXIT HUP INT TERM

mkdir "$W/base"
cat > "$W/base/blockcheck2.sh" <<'SH'
#!/bin/sh
trap 'exit 0' INT TERM
while :; do sleep 1; done
SH
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

mkdir "$W/runtime-private"
# A stale PID must not prevent a new scan; PID identity includes kernel start time.
printf '99999999 1\n' > "$W/runtime-private/blockcheck.pid"
chmod 600 "$W/runtime-private/blockcheck.pid"

env QZAPRET_TEST_BASE="$W/base" QZAPRET_TEST_RUNDIR="$W/runtime-private" \
    "$H" blockcheck --domains example.org > "$W/output" 2>&1 &
runner_pid=$!

attempt=0
while [ "$attempt" -lt 5 ]; do
    if [ -s "$W/runtime-private/blockcheck.pid" ] && \
       ! grep -q '^99999999 1$' "$W/runtime-private/blockcheck.pid"; then
        break
    fi
    sleep 1
    attempt=$((attempt + 1))
done
if [ ! -s "$W/runtime-private/blockcheck.pid" ] || \
   grep -q '^99999999 1$' "$W/runtime-private/blockcheck.pid"; then
    echo "FAIL: Blockcheck did not replace the stale PID file"
    cat "$W/output"
    exit 1
fi
[ "$(wc -w < "$W/runtime-private/blockcheck.pid" | tr -d ' ')" -eq 2 ] || {
    echo "FAIL: PID file must contain PID and process start time"
    exit 1
}
[ "$(stat -c '%a' "$W/runtime-private/blockcheck.pid")" = 600 ] || {
    echo "FAIL: PID file permissions are not 0600"
    exit 1
}
[ "$(stat -c '%a' "$W/runtime-private/blockcheck.lock")" = 600 ] || {
    echo "FAIL: lock file permissions are not 0600"
    exit 1
}

set +e
out=$(env QZAPRET_TEST_BASE="$W/base" QZAPRET_TEST_RUNDIR="$W/runtime-private" \
    "$H" blockcheck --domains example.org 2>&1)
rc=$?
set -e
[ "$rc" -eq 2 ] && printf '%s' "$out" | grep -q 'blockcheck already running' || {
    echo "FAIL: concurrent Blockcheck was not rejected (rc=$rc): $out"
    exit 1
}
echo "ok: concurrent Blockcheck rejected by the runtime lock"

env QZAPRET_TEST_BASE="$W/base" QZAPRET_TEST_RUNDIR="$W/runtime-private" \
    "$H" blockcheck-cancel
wait "$runner_pid"
runner_pid=
sleep 1
flock -n "$W/runtime-private/blockcheck.lock" -c true || {
    echo "FAIL: runtime lock remained held after Blockcheck exit"
    exit 1
}
echo "ok: cancellation releases the runtime lock"
