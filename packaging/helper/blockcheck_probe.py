"""Read-only, fail-closed startup probe for Blockcheck recovery state."""
from __future__ import annotations

import os
import stat
from pathlib import Path

from blockcheck_manager import BlockcheckOptions
from blockcheck_staging import StagedOptionsStore
from blockcheck_state import StateError, StateStore
from blockcheck_status import (
    RECOVERY_UNIT, WORKER_UNIT, JobStatusError, _unit_properties,
)

STATE_DIRECTORY = "/var/lib/zapret2applet/blockcheck"
RUNTIME_DIRECTORY = "/run/zapret2applet/blockcheck"


class ProbeError(StateError):
    """The startup probe cannot establish a trustworthy safe state."""


def _open_directory_or_missing(path: str, owner: int) -> int | None:
    """Open every path component without following symlinks; None means absent."""
    flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC
    current = os.open(os.sep, flags)
    try:
        for component in Path(os.path.abspath(path)).parts[1:]:
            try:
                next_fd = os.open(component, flags, dir_fd=current)
            except FileNotFoundError:
                os.close(current)
                return None
            os.close(current)
            current = next_fd
        info = os.fstat(current)
        if not stat.S_ISDIR(info.st_mode) or info.st_uid != owner or stat.S_IMODE(info.st_mode) != 0o700:
            raise ProbeError("directory has unsafe owner or permissions")
        result = current
        current = -1
        return result
    except OSError as exc:
        raise ProbeError(f"cannot safely inspect {path}: {exc}") from exc
    finally:
        if current >= 0:
            os.close(current)


def _staged_request_exists(runtime_directory: str, owner: int) -> bool:
    directory_fd = _open_directory_or_missing(runtime_directory, owner)
    if directory_fd is None:
        return False
    try:
        try:
            fd = os.open("options.json", os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC,
                         dir_fd=directory_fd)
        except FileNotFoundError:
            return False
        except OSError as exc:
            raise ProbeError(f"cannot safely inspect staged Blockcheck options: {exc}") from exc
        try:
            info = os.fstat(fd)
            if (not stat.S_ISREG(info.st_mode) or info.st_uid != owner
                    or stat.S_IMODE(info.st_mode) != 0o600 or info.st_nlink != 1
                    or info.st_size > 4352):
                raise ProbeError("staged Blockcheck options have unsafe metadata")
            return True
        finally:
            os.close(fd)
    finally:
        os.close(directory_fd)


def recovery_probe(state: StateStore, staged: StagedOptionsStore) -> dict[str, object]:
    """Return safe, busy, recovery_required, or unknown without changing system state."""
    try:
        state_dir_fd = _open_directory_or_missing(str(state.directory), state.owner)
        if state_dir_fd is None:
            marker = None
            pending = _staged_request_exists(str(staged.directory), state.owner)
            worker = recovery = None
        else:
            os.close(state_dir_fd)
            try:
                with state.locked(create=False, blocking=False):
                    marker = state._read_marker()
                    pending = staged.read_pending_locked(missing_ok=True) is not None
                    worker = _unit_properties(state.systemctl, WORKER_UNIT)
                    recovery = _unit_properties(state.systemctl, RECOVERY_UNIT)
            except StateError as exc:
                if "another Blockcheck lifecycle operation is active" in str(exc):
                    return {"version": 1, "state": "busy", "reason": "lifecycle_lock"}
                raise
        if worker is None:
            worker = _unit_properties(state.systemctl, WORKER_UNIT)
            recovery = _unit_properties(state.systemctl, RECOVERY_UNIT)
        for unit_state in (worker, recovery):
            if unit_state["LoadState"] != "loaded":
                raise ProbeError("required Blockcheck systemd unit is not loaded")
        if pending:
            return {"version": 1, "state": "busy", "reason": "staged_request"}
        if (worker["ActiveState"] in {"active", "activating", "deactivating", "reloading"}
                or recovery["ActiveState"] in {"active", "activating", "deactivating", "reloading"}):
            result: dict[str, object] = {"version": 1, "state": "busy", "reason": "systemd_operation"}
            if marker is not None:
                result["operation_id"] = marker["operation_id"]
            return result
        if marker is not None:
            return {
                "version": 1,
                "state": "recovery_required",
                "reason": "durable_marker",
                "operation_id": marker["operation_id"],
                "service_was_active": marker["service_was_active"],
            }
        return {"version": 1, "state": "safe", "reason": "no_pending_recovery"}
    except (StateError, JobStatusError, OSError, ValueError) as exc:
        return {"version": 1, "state": "unknown", "reason": str(exc)[:512]}


def main() -> int:
    import json
    import sys

    state = StateStore(STATE_DIRECTORY)
    staged = StagedOptionsStore(state, RUNTIME_DIRECTORY)
    result = recovery_probe(state, staged)
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0 if result["state"] in {"safe", "busy", "recovery_required"} else 1


if __name__ == "__main__":
    raise SystemExit(main())
