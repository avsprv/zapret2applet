"""UUID-bound cancellation for systemd-managed Blockcheck jobs."""
from __future__ import annotations
import subprocess
import sys
import uuid
from blockcheck_staging import StagedOptionsStore
from blockcheck_state import StateError, StateStore

WORKER_UNIT = "zapret2control-blockcheck.service"
STATE_DIRECTORY = "/var/lib/zapret2applet/blockcheck"
RUNTIME_DIRECTORY = "/run/zapret2applet/blockcheck"

class CancelError(StateError):
    """Cancellation could not be performed safely."""

def _validate_operation_id(value: str) -> str:
    try:
        parsed = uuid.UUID(value)
    except (ValueError, TypeError, AttributeError) as exc:
        raise CancelError("invalid Blockcheck operation ID") from exc
    if str(parsed) != value:
        raise CancelError("operation ID must be a canonical UUID")
    return value

def cancel_operation(state: StateStore, staged: StagedOptionsStore,
                     operation_id: str) -> bool:
    """Stop only the requested operation; never acquire the worker's lifecycle lock."""
    operation_id = _validate_operation_id(operation_id)
    try:
        with state.queue_locked(create=True):
            marker_before = state.read_marker_snapshot()
            staged_id = staged.read_pending_operation_id_snapshot()
            marker_after = state.read_marker_snapshot()
            if marker_before != marker_after:
                # The worker may atomically publish its marker while consuming
                # this same staged UUID. Accept only that forward transition.
                same_worker_transition = (
                    marker_before is None
                    and marker_after is not None
                    and staged_id == marker_after["operation_id"]
                )
                if not same_worker_transition:
                    raise CancelError("Blockcheck recovery state changed during cancellation")
            marker_id = marker_after["operation_id"] if marker_after else None
            if marker_id and staged_id and marker_id != staged_id:
                raise CancelError("recovery marker and staged operation IDs disagree")
            current_id = marker_id or staged_id
            if current_id != operation_id:
                return False
            try:
                result = subprocess.run(
                    [state.systemctl, "stop", "--no-block", WORKER_UNIT],
                    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                    stderr=subprocess.PIPE, check=False, timeout=30,
                )
            except (OSError, subprocess.TimeoutExpired) as exc:
                raise CancelError(f"cannot queue Blockcheck cancellation: {exc}") from exc
            if result.returncode != 0:
                detail = result.stderr.decode("utf-8", "replace").strip()
                raise CancelError(
                    f"systemd rejected Blockcheck cancellation "
                    f"({result.returncode}): {detail}"
                )
            return True
    except StateError as exc:
        if isinstance(exc, CancelError):
            raise
        raise CancelError(str(exc)) from exc

def main(argv: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    if len(args) != 1:
        print("usage: blockcheck_cancel.py OPERATION_UUID", file=sys.stderr)
        return 2
    state = StateStore(STATE_DIRECTORY)
    staged = StagedOptionsStore(state, RUNTIME_DIRECTORY)
    try:
        cancelled = cancel_operation(state, staged, args[0])
    except StateError as exc:
        print(f"zapret2control blockcheck cancel: {exc}", file=sys.stderr)
        return 1
    print("cancelled" if cancelled else "stale")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
