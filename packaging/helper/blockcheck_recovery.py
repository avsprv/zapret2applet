"""Idempotent recovery entry point for ExecStopPost and boot recovery."""
from __future__ import annotations

import sys

from blockcheck_state import StateError, StateStore

STATE_DIRECTORY = "/var/lib/zapret2control/blockcheck"


def recover(state: StateStore) -> bool:
    """Attempt recovery without waiting behind another lifecycle operation.

    If either lock is busy, fail closed and leave the durable marker untouched.
    The GUI or boot recovery unit can retry after the competing operation exits.
    """
    # Recovery runs from ExecStopPost with a bounded TimeoutStopSec. Never wait
    # indefinitely for submit/cancel or service-management lock holders here.
    with state.queue_locked(create=True, blocking=False):
        with state.locked(create=True, blocking=False):
            return state.recover_locked()


def main() -> int:
    state = StateStore(STATE_DIRECTORY)
    try:
        recover(state)
    except StateError as exc:
        print(f"zapret2control blockcheck recovery: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
