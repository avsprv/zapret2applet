#!/usr/bin/env python3
"""Report the lifecycle state of one UUID-bound systemd Blockcheck job."""
from __future__ import annotations

import json
import subprocess
import sys
import uuid

from blockcheck_staging import StagedOptionsStore
from blockcheck_state import StateError, StateStore

WORKER_UNIT = "zapret2applet-blockcheck.service"
RECOVERY_UNIT = "zapret2applet-blockcheck-recovery.service"
STATE_DIRECTORY = "/var/lib/zapret2applet/blockcheck"
RUNTIME_DIRECTORY = "/run/zapret2applet/blockcheck"
PROPERTIES = ("LoadState", "ActiveState", "SubState", "Result", "ExecMainStatus")


class JobStatusError(StateError):
    """Cannot safely determine the Blockcheck job state."""


def _validate_operation_id(value: str) -> str:
    try:
        parsed = uuid.UUID(value)
    except (ValueError, TypeError, AttributeError) as exc:
        raise JobStatusError("invalid Blockcheck operation ID") from exc
    if str(parsed) != value:
        raise JobStatusError("operation ID must be a canonical UUID")
    return value


def _unit_properties(systemctl: str, unit: str) -> dict[str, str]:
    try:
        result = subprocess.run(
            [systemctl, "show", "--no-pager", *[f"--property={key}" for key in PROPERTIES], unit],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            check=False, timeout=15,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise JobStatusError(f"cannot query {unit}: {exc}") from exc
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", "replace").strip()
        raise JobStatusError(f"cannot query {unit} ({result.returncode}): {detail}")
    try:
        lines = result.stdout.decode("ascii", "strict").splitlines()
    except UnicodeDecodeError as exc:
        raise JobStatusError(f"systemd returned non-ASCII properties for {unit}") from exc
    values: dict[str, str] = {}
    for line in lines:
        key, separator, value = line.partition("=")
        if not separator or key not in PROPERTIES or key in values:
            raise JobStatusError(f"unexpected systemd property output for {unit}")
        values[key] = value
    if set(values) != set(PROPERTIES):
        raise JobStatusError(f"incomplete systemd properties for {unit}")
    if values["LoadState"] not in {"loaded", "not-found", "error", "masked", "stub", "merged"}:
        raise JobStatusError(f"unexpected LoadState for {unit}")
    if values["ActiveState"] not in {"active", "inactive", "activating", "deactivating", "failed", "reloading"}:
        raise JobStatusError(f"unexpected ActiveState for {unit}")
    if not values["ExecMainStatus"].isdigit():
        raise JobStatusError(f"invalid ExecMainStatus for {unit}")
    return values


def get_job_status(state: StateStore, staged: StagedOptionsStore, operation_id: str) -> dict[str, object]:
    operation_id = _validate_operation_id(operation_id)
    try:
        lock = state.locked(create=False, blocking=False)
        with lock:
            marker = state._read_marker()
            pending = staged.read_pending_locked(missing_ok=True)
            worker = _unit_properties(state.systemctl, WORKER_UNIT)
            recovery = _unit_properties(state.systemctl, RECOVERY_UNIT)
    except StateError as exc:
        if "another Blockcheck lifecycle operation is active" in str(exc):
            return {"version": 1, "operation_id": operation_id, "phase": "busy"}
        raise JobStatusError(str(exc)) from exc

    marker_id = marker["operation_id"] if marker else None
    pending_id = pending[0] if pending else None
    if marker_id and marker_id != operation_id:
        phase = "stale"
    elif pending_id and pending_id != operation_id:
        phase = "stale"
    elif marker_id:
        if recovery["ActiveState"] in {"active", "activating", "deactivating"}:
            phase = "recovering"
        elif recovery["ActiveState"] == "failed" or recovery["Result"] != "success":
            phase = "recovery_failed"
        else:
            phase = "recovery_pending"
    elif pending_id:
        if worker["ActiveState"] in {"active", "activating", "deactivating"}:
            phase = "running"
        elif worker["ActiveState"] == "failed" or worker["Result"] not in {"success", "start-limit-hit"}:
            phase = "failed"
        else:
            phase = "queued"
    elif worker["ActiveState"] in {"active", "activating", "deactivating"}:
        phase = "running"
    elif recovery["ActiveState"] in {"active", "activating", "deactivating"}:
        phase = "recovering"
    elif worker["Result"] == "success" and recovery["Result"] == "success":
        phase = "completed"
    else:
        phase = "failed"

    return {
        "version": 1,
        "operation_id": operation_id,
        "phase": phase,
        "worker": worker,
        "recovery": recovery,
        "recovery_marker": marker_id == operation_id,
        "staged_options": pending_id == operation_id,
    }


def main(argv: list[str] | None = None) -> int:
    if argv is None:
        argv = sys.argv[1:]
    if len(argv) != 1:
        print("usage: blockcheck_status.py OPERATION_UUID", file=sys.stderr)
        return 2
    try:
        state = StateStore(STATE_DIRECTORY)
        staged = StagedOptionsStore(state, RUNTIME_DIRECTORY)
        status = get_job_status(state, staged, argv[0])
    except (StateError, ValueError, UnicodeError) as exc:
        print(f"zapret2applet blockcheck status: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(status, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
