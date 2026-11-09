#!/usr/bin/env bash
# Real systemd lifecycle integration test. Run only on a disposable systemd VM.
set -Eeuo pipefail

ROOT="${1:?usage: test_systemd_lifecycle.sh REPOSITORY_ROOT}"
ROOT="$(cd "$ROOT" && pwd)"

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

[[ "$(id -u)" -eq 0 ]] || fail "must run as root"
[[ "$(ps -p 1 -o comm= | tr -d '[:space:]')" == "systemd" ]] || fail "systemd is not PID 1"
command -v systemctl >/dev/null || fail "systemctl is unavailable"
command -v python3 >/dev/null || fail "python3 is unavailable"

HELPER_DIR=/usr/local/libexec/zapret2control
MODULE_DIR=/usr/share/zapret2control
WORKER_UNIT=/etc/systemd/system/zapret2control-blockcheck.service
RECOVERY_UNIT=/etc/systemd/system/zapret2control-blockcheck-recovery.service
UPSTREAM_UNIT=/etc/systemd/system/zapret2.service
E2E_DIR=/run/zapret2control-e2e
STATE_DIR=/var/lib/zapret2control/blockcheck
RUNTIME_DIR=/run/zapret2control
UPSTREAM_SCRIPT=/opt/zapret2/blockcheck2.sh

# Never overwrite a service, unit, or state directory on the runner.
for path in "$WORKER_UNIT" "$RECOVERY_UNIT" "$UPSTREAM_UNIT" "$STATE_DIR" "$RUNTIME_DIR" "$HELPER_DIR" "$MODULE_DIR" "$E2E_DIR" /opt/zapret2 "$UPSTREAM_SCRIPT" /usr/local/libexec/zapret2control-e2e-service; do
    [[ ! -e "$path" && ! -L "$path" ]] || fail "refusing to overwrite pre-existing path: $path"
done

cleanup() {
    set +e
    systemctl stop zapret2control-blockcheck.service zapret2control-blockcheck-recovery.service zapret2.service >/dev/null 2>&1
    systemctl disable zapret2control-blockcheck-recovery.service >/dev/null 2>&1
    rm -f "$WORKER_UNIT" "$RECOVERY_UNIT" "$UPSTREAM_UNIT"
    rm -f /etc/systemd/system/multi-user.target.wants/zapret2control-blockcheck-recovery.service
    rm -f "$UPSTREAM_SCRIPT" /usr/local/libexec/zapret2control-e2e-service
    rmdir /opt/zapret2 2>/dev/null
    rm -rf "$STATE_DIR" "$RUNTIME_DIR" "$E2E_DIR" "$HELPER_DIR" "$MODULE_DIR"
    rmdir /var/lib/zapret2control 2>/dev/null
    systemctl daemon-reload >/dev/null 2>&1
}
trap cleanup EXIT

install -d -m 0755 "$HELPER_DIR" "$MODULE_DIR" /usr/local/libexec
install -m 0755 "$ROOT/packaging/helper/zapret2control-helper" "$HELPER_DIR/zapret2control-helper"
install -m 0644 "$ROOT"/packaging/helper/*.py "$MODULE_DIR"/
sed 's|@CMAKE_INSTALL_FULL_LIBEXECDIR@|/usr/local/libexec|g' \
    "$ROOT/data/systemd/zapret2control-blockcheck.service.in" > "$WORKER_UNIT"
sed 's|@CMAKE_INSTALL_FULL_LIBEXECDIR@|/usr/local/libexec|g' \
    "$ROOT/data/systemd/zapret2control-blockcheck-recovery.service.in" > "$RECOVERY_UNIT"
chmod 0644 "$WORKER_UNIT" "$RECOVERY_UNIT"

install -d -m 0755 "$E2E_DIR" /opt/zapret2
cat > "$UPSTREAM_SCRIPT" <<'SH'
#!/bin/sh
set -eu
exec /usr/bin/sleep infinity
SH
cat > /usr/local/libexec/zapret2control-e2e-service <<'SH'
#!/bin/sh
set -eu
printf 'start\\n' >> /run/zapret2control-e2e/start-attempts
if [ -e /run/zapret2control-e2e/fail-start ]; then
    echo "intentional E2E service startup failure" >&2
    exit 42
fi
exec /usr/bin/sleep infinity
SH
chmod 0755 "$UPSTREAM_SCRIPT" /usr/local/libexec/zapret2control-e2e-service

cat > "$UPSTREAM_UNIT" <<'UNIT'
[Unit]
Description=Disposable zapret2control E2E fixture service

[Service]
Type=simple
ExecStart=/usr/local/libexec/zapret2control-e2e-service
Restart=no
UNIT

systemctl daemon-reload
systemctl enable zapret2control-blockcheck-recovery.service >/dev/null
[[ "$(systemctl is-enabled zapret2control-blockcheck-recovery.service)" == enabled ]] || fail "boot recovery unit was not enabled"

stage_request() {
    install -d -m 0700 "$RUNTIME_DIR"
    PYTHONPATH="$MODULE_DIR" python3 <<'PY'
import uuid
from blockcheck_manager import BlockcheckOptions
from blockcheck_staging import StagedOptionsStore
from blockcheck_state import StateStore

state = StateStore("/var/lib/zapret2control/blockcheck")
staged = StagedOptionsStore(state, "/run/zapret2control/blockcheck")
with state.locked(create=True):
    staged.stage_locked(
        str(uuid.uuid4()),
        BlockcheckOptions(domains=("example.org",), scan_level="veryquick"),
    )
PY
}

wait_until() {
    local description="$1"
    shift
    local attempt
    for attempt in $(seq 1 100); do
        if "$@"; then
            return 0
        fi
        sleep 0.1
    done
    fail "timed out waiting for $description"
}

worker_is_running() {
    local state
    state="$(systemctl show --property=ActiveState --value zapret2control-blockcheck.service)"
    [[ "$state" == activating || "$state" == active ]]
}

upstream_is_active() {
    systemctl is-active --quiet zapret2.service
}

marker_exists() {
    [[ -f "$STATE_DIR/state.json" ]]
}

marker_absent() {
    [[ ! -e "$STATE_DIR/state.json" ]]
}

upstream_start_attempts_at_least_two() {
    [[ -f "$E2E_DIR/start-attempts" ]] && [[ "$(wc -l < "$E2E_DIR/start-attempts")" -ge 2 ]]
}

start_upstream() {
    systemctl reset-failed zapret2.service >/dev/null 2>&1 || true
    systemctl start zapret2.service
    wait_until "fixture service activation" upstream_is_active
}

start_worker() {
    systemctl reset-failed zapret2control-blockcheck.service >/dev/null 2>&1 || true
    systemctl start --no-block zapret2control-blockcheck.service
    wait_until "worker start and upstream stop" worker_is_running
    wait_until "worker stopping the fixture service" bash -c '! systemctl is-active --quiet zapret2.service'
}

# 1. Normal ExecStopPost recovery restores an active upstream service.
start_upstream
stage_request
start_worker
systemctl stop zapret2control-blockcheck.service
wait_until "ExecStopPost restoring active service" upstream_is_active
wait_until "successful recovery marker removal" marker_absent
echo "ok: ExecStopPost restores the previously active service and removes the marker"

# 2. Recovery preserves an originally inactive service.
systemctl stop zapret2.service
stage_request
start_worker
systemctl stop zapret2control-blockcheck.service
wait_until "inactive recovery completion" marker_absent
if systemctl is-active --quiet zapret2.service; then
    fail "recovery unexpectedly started a service that was originally inactive"
fi
echo "ok: ExecStopPost preserves the previously inactive service"

# 3. A failed restore leaves the durable marker; the boot recovery unit retries it.
rm -f "$E2E_DIR/start-attempts"
start_upstream
touch "$E2E_DIR/fail-start"
stage_request
start_worker
systemctl stop zapret2control-blockcheck.service >/dev/null 2>&1 || true
wait_until "ExecStopPost retrying the failed upstream service" upstream_start_attempts_at_least_two
wait_until "failed recovery marker retention" marker_exists
if systemctl is-active --quiet zapret2.service; then
    fail "fixture service unexpectedly became active while startup failure was enabled"
fi
rm -f "$E2E_DIR/fail-start"
systemctl reset-failed zapret2.service zapret2control-blockcheck-recovery.service >/dev/null 2>&1 || true
systemctl start zapret2control-blockcheck-recovery.service
wait_until "boot recovery unit restoring service" upstream_is_active
wait_until "boot recovery unit clearing marker" marker_absent
echo "ok: failed recovery retains the marker and boot recovery retries successfully"

# 4. SIGKILL of the worker still runs the real ExecStopPost recovery command.
systemctl stop zapret2.service
start_upstream
stage_request
start_worker
systemctl kill --kill-who=all --signal=SIGKILL zapret2control-blockcheck.service
wait_until "SIGKILL ExecStopPost restoring service" upstream_is_active
wait_until "SIGKILL recovery marker removal" marker_absent
echo "ok: SIGKILL triggers ExecStopPost and restores the service"

echo "PASS: real systemd Blockcheck lifecycle E2E"
