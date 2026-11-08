"""Validate, stage, and queue a single systemd-owned Blockcheck operation."""
from __future__ import annotations
import argparse
import subprocess
import sys
import uuid
from blockcheck_manager import BlockcheckOptions, OptionsError
from blockcheck_staging import StagedOptionsStore, StagingError
from blockcheck_state import StateError, StateStore

WORKER_UNIT = "zapret2control-blockcheck.service"
RECOVERY_UNIT = "zapret2control-blockcheck-recovery.service"
STATE_DIRECTORY = "/var/lib/zapret2applet/blockcheck"
RUNTIME_DIRECTORY = "/run/zapret2applet/blockcheck"

class SubmitError(StateError):
    """Raised when a Blockcheck request cannot be safely queued."""

class BlockcheckSubmitter:
    def __init__(self, state: StateStore, staged: StagedOptionsStore) -> None:
        self.state = state
        self.staged = staged

    def _unit_state(self, unit: str) -> str:
        try:
            result = subprocess.run(
                [self.state.systemctl, "show", "--property=ActiveState", "--value", unit],
                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, check=False, timeout=30,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise SubmitError(f"cannot query {unit}: {exc}") from exc
        if result.returncode != 0:
            detail = result.stderr.decode("utf-8", "replace").strip()
            raise SubmitError(f"cannot query {unit} ({result.returncode}): {detail}")
        value = result.stdout.decode("ascii", "strict").strip()
        if value not in {"active", "inactive", "activating", "deactivating", "failed", "reloading"}:
            raise SubmitError(f"unexpected systemd state for {unit}: {value!r}")
        return value

    def submit(self, options: BlockcheckOptions) -> str:
        operation_id = str(uuid.uuid4())
        try:
            with self.state.locked(create=True, blocking=False):
                marker = self.state._read_marker()
                if marker is not None:
                    raise SubmitError(
                        "a previous Blockcheck recovery is still pending "
                        f"(operation_id={marker['operation_id']})"
                    )
                for unit in (WORKER_UNIT, RECOVERY_UNIT):
                    unit_state = self._unit_state(unit)
                    if unit_state != "inactive":
                        raise SubmitError(f"{unit} is not idle (state: {unit_state})")
                self.staged.stage_locked(operation_id, options)
                try:
                    result = subprocess.run(
                        [self.state.systemctl, "start", "--no-block", WORKER_UNIT],
                        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                        stderr=subprocess.PIPE, check=False, timeout=30,
                    )
                except (OSError, subprocess.TimeoutExpired) as exc:
                    self.staged.discard_locked(operation_id)
                    raise SubmitError(f"cannot queue Blockcheck worker: {exc}") from exc
                if result.returncode != 0:
                    detail = result.stderr.decode("utf-8", "replace").strip()
                    self.staged.discard_locked(operation_id)
                    raise SubmitError(f"systemd rejected Blockcheck worker ({result.returncode}): {detail}")
        except StateError as exc:
            if isinstance(exc, SubmitError):
                raise
            raise SubmitError(str(exc)) from exc
        return operation_id

def _parse_bool(value: str) -> bool:
    if value not in {"0", "1"}:
        raise argparse.ArgumentTypeError("expected 0 or 1")
    return value == "1"

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Queue a systemd-managed Blockcheck scan")
    parser.add_argument("--domains", default="rutracker.org",
                        help="comma-separated ASCII domain names (1..10)")
    parser.add_argument("--ipvs", choices=("4", "6", "46"), default="4")
    parser.add_argument("--http", type=_parse_bool, default=True)
    parser.add_argument("--tls12", type=_parse_bool, default=True)
    parser.add_argument("--tls13", type=_parse_bool, default=False)
    parser.add_argument("--http3", type=_parse_bool, default=False)
    parser.add_argument("--repeats", type=int, choices=range(1, 11), default=1)
    parser.add_argument("--scanlevel", choices=("veryquick", "quick", "standard", "force"), default="standard")
    args = parser.parse_args(argv)
    try:
        options = BlockcheckOptions(
            domains=tuple(args.domains.split(",")), ipvs=args.ipvs, http=args.http,
            tls12=args.tls12, tls13=args.tls13, http3=args.http3,
            repeats=args.repeats, scan_level=args.scanlevel,
        )
        state = StateStore(STATE_DIRECTORY)
        staged = StagedOptionsStore(state, RUNTIME_DIRECTORY)
        operation_id = BlockcheckSubmitter(state, staged).submit(options)
    except (OptionsError, StateError, StagingError, UnicodeError) as exc:
        print(f"zapret2control blockcheck submit: {exc}", file=sys.stderr)
        return 1
    print(operation_id)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
