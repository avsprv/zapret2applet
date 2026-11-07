#!/bin/sh
set -eu
H="$1"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp/bin"
cat > "$tmp/bin/journalctl" <<'EOF'
#!/bin/sh
printf '%s\n' "$@" > "$QZAPRET_TEST_JOURNAL_ARGS"
if [ "${QZAPRET_TEST_JOURNAL_MODE:-normal}" = fail ]; then
    echo "fake journal permission denied" >&2
    exit 7
fi
if [ "${QZAPRET_TEST_JOURNAL_MODE:-normal}" = large ]; then
    head -c 1048577 /dev/zero
else
    printf '2026-11-07T04:00:00+00:00 fake zapret2 service log\n'
fi
EOF
chmod 755 "$tmp/bin/journalctl"
export QZAPRET_TEST_JOURNALCTL="$tmp/bin/journalctl"
export QZAPRET_TEST_JOURNAL_ARGS="$tmp/journal.args"

"$H" logs-read 100 > "$tmp/logs.out"
grep -q 'fake zapret2 service log' "$tmp/logs.out"
grep -qx -- '--unit=zapret2.service' "$tmp/journal.args"
grep -qx -- '--lines=100' "$tmp/journal.args"
grep -qx -- '--no-pager' "$tmp/journal.args"
grep -qx -- '--output=short-iso' "$tmp/journal.args"
echo "ok: reads a fixed unit with fixed journalctl options"

if "$H" logs-read 25 >/dev/null 2>&1; then
    echo "FAIL: unsupported line count was accepted"; exit 1
fi
if "$H" logs-read 100 --unit=ssh >/dev/null 2>&1; then
    echo "FAIL: caller-supplied journal arguments were accepted"; exit 1
fi
echo "ok: rejects unsupported counts and extra arguments"

if QZAPRET_TEST_JOURNAL_MODE=fail "$H" logs-read 50 >/dev/null 2>"$tmp/error.out"; then
    echo "FAIL: journalctl failure was reported as success"; exit 1
fi
grep -q 'fake journal permission denied' "$tmp/error.out"
echo "ok: reports journal access failures"

QZAPRET_TEST_JOURNAL_MODE=large "$H" logs-read 500 > "$tmp/large.out"
size=$(wc -c < "$tmp/large.out" | tr -d '[:space:]')
[ "$size" -le 1048576 ] || { echo "FAIL: journal output was not capped"; exit 1; }
head -n 1 "$tmp/large.out" | grep -q 'output truncated'
echo "ok: caps returned journal output at 1 MiB"

start_epoch=$(date +%s)
"$H" blockcheck-job-logs 200 "$start_epoch" > "$tmp/blockcheck.out"
grep -q 'fake zapret2 service log' "$tmp/blockcheck.out"
grep -qx -- '--unit=zapret2applet-blockcheck.service' "$tmp/journal.args"
grep -qx -- '--lines=200' "$tmp/journal.args"
grep -qx -- "--since=@$start_epoch" "$tmp/journal.args"
echo "ok: reads only the fixed Blockcheck unit journal"

if "$H" blockcheck-job-logs 25 "$start_epoch" >/dev/null 2>&1; then
    echo "FAIL: unsupported Blockcheck journal line count was accepted"; exit 1
fi
if "$H" blockcheck-job-logs 100 "$start_epoch" extra >/dev/null 2>&1; then
    echo "FAIL: extra Blockcheck journal arguments were accepted"; exit 1
fi
echo "ok: rejects unsupported Blockcheck journal arguments"

if QZAPRET_TEST_JOURNAL_MODE=fail "$H" blockcheck-job-logs 50 "$start_epoch" >/dev/null 2>"$tmp/blockcheck-error.out"; then
    echo "FAIL: Blockcheck journalctl failure was reported as success"; exit 1
fi
grep -q 'fake journal permission denied' "$tmp/blockcheck-error.out"
echo "ok: reports Blockcheck journal access failures"

QZAPRET_TEST_JOURNAL_MODE=large "$H" blockcheck-job-logs 500 "$start_epoch" > "$tmp/blockcheck-large.out"
size=$(wc -c < "$tmp/blockcheck-large.out" | tr -d '[:space:]')
[ "$size" -le 1048576 ] || { echo "FAIL: Blockcheck journal output was not capped"; exit 1; }
head -n 1 "$tmp/blockcheck-large.out" | grep -q 'output truncated'
echo "ok: caps Blockcheck journal output at 1 MiB"