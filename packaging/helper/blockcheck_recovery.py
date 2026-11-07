"""Idempotent recovery entry point for ExecStopPost and boot recovery."""
from __future__ import annotations

import sys

from blockcheck_state import StateError, StateStore

STATE_DIRECTORY = "/var/lib/zapret2applet/blockcheck"


def main() -> int:
    state = StateStore(STATE_DIRECTORY)
    try:
        with state.locked(create=True):
            state.recover_locked()
    except StateError as exc:
        print(f"zapret2applet blockcheck recovery: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
