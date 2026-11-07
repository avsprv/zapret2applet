"""Serialize service management and upstream updates with Blockcheck recovery.

This is a fail-closed admission gate. The lifecycle lock is held until the
requested systemctl action or complete update subprocess has exited.
"""
from __future__ import annotations

import os
import subprocess
import sys
from contextlib import contextmanager
from pathlib import Path
from typing import Callable, Iterator

from blockcheck_staging import StagedOptionsStore
from blockcheck_state import StateError, StateStore
from blockcheck_status import RECOVERY_UNIT, WORKER_UNIT, JobStatusError, _unit_properties

STATE_DIRECTORY = "/var/lib/zapret2applet/blockcheck"
RUNTIME_DIRECTORY = "/run/zapret2applet/blockcheck"
SERVICE = "zapret2.service"
GUARD_ENV = "QZAPRET_BLOCKCHECK_GUARD"
GUARD_VALUE = "held-v1"
UNIT_ACTIONS = {"start", "stop", "enable", "disable"}


class AdmissionError(StateError):
    """A conflicting or unverifiable Blockcheck lifecycle operation exists."""


def _paths() -> tuple[str, str, str]:
    # Match the helper's test-mode boundary: production installs ignore all
    # QZAPRET_TEST_* overrides, even if an invoking environment supplies them.
    module_path = str(Path(__file__).resolve())
    installed = module_path.startswith(("/usr/", "/opt/", "/etc/", "/bin/", "/sbin/", "/lib/", "/var/"))
    if (not installed and os.environ.get("QZAPRET_TEST_BASE")
            and os.environ.get("QZAPRET_TEST_RUNDIR")):
        root = os.environ["QZAPRET_TEST_RUNDIR"]
        return (
            os.environ.get("QZAPRET_TEST_STATE_DIRECTORY", os.path.join(root, "blockcheck-state")),
            os.environ.get("QZAPRET_TEST_RUNTIME_DIRECTORY", os.path.join(root, "blockcheck")),
            os.environ.get("QZAPRET_TEST_SYSTEMCTL", "/usr/bin/systemctl"),
        )
    return STATE_DIRECTORY, RUNTIME_DIRECTORY, "/usr/bin/systemctl"


@contextmanager
def admission_guard(state: StateStore, staged: StagedOptionsStore) -> Iterator[None]:
    """Acquire the shared lock and reject all pending/active recovery work."""
    try:
        with state.locked(create=True, blocking=False):
            marker = state._read_marker()
            if marker is not None:
                raise AdmissionError(
                    "Blockcheck recovery is pending "
                    f"(operation_id={marker['operation_id']})"
                )
            if staged.read_pending_locked(missing_ok=True) is not None:
                raise AdmissionError("a Blockcheck request is staged or waiting to start")
            for unit in (WORKER_UNIT, RECOVERY_UNIT):
                properties = _unit_properties(state.systemctl, unit)
                if properties["LoadState"] != "loaded":
                    raise AdmissionError(f"required systemd unit is not loaded: {unit}")
                if properties["ActiveState"] != "inactive":
                    raise AdmissionError(
                        f"Blockcheck lifecycle unit is not idle: {unit} "
                        f"(state: {properties['ActiveState']})"
                    )
            yield
    except StateError as exc:
        if isinstance(exc, AdmissionError):
            raise
        raise AdmissionError(str(exc)) from exc
    except JobStatusError as exc:
        raise AdmissionError(str(exc)) from exc


def run_guarded(state: StateStore, staged: StagedOptionsStore,
                operation: Callable[[], int]) -> int:
    with admission_guard(state, staged):
        return operation()


def _run_command(command: list[str], env: dict[str, str] | None = None) -> int:
    try:
        return subprocess.run(command, stdin=subprocess.DEVNULL, check=False, env=env).returncode
    except (OSError, subprocess.TimeoutExpired) as exc:
        print(f"zapret2applet admission: cannot execute operation: {exc}", file=sys.stderr)
        return 1


def main(argv: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    if len(args) != 2 or args[0] not in {"unit", "update"}:
        print("usage: blockcheck_admission.py {unit ACTION|update TAG}", file=sys.stderr)
        return 2

    action, value = args
    state_dir, runtime_dir, systemctl = _paths()
    state = StateStore(state_dir, systemctl=systemctl)
    staged = StagedOptionsStore(state, runtime_dir)

    if action == "unit":
        if value not in UNIT_ACTIONS:
            print("zapret2applet admission: unsupported service action", file=sys.stderr)
            return 2
        command = [systemctl, value, SERVICE]
        operation = lambda: _run_command(command)
    else:
        if (len(value) > 24 or not value.startswith("v")
                or any(ch not in "0123456789.v" for ch in value)
                or ".." in value or value.endswith(".") or "vv" in value):
            print("zapret2applet admission: invalid update tag", file=sys.stderr)
            return 2
        helper = Path(__file__).with_name("zapret2applet-helper")
        if not helper.is_file():
            print("zapret2applet admission: privileged helper is missing", file=sys.stderr)
            return 1
        env = os.environ.copy()
        env[GUARD_ENV] = GUARD_VALUE
        operation = lambda: _run_command([str(helper), "update-locked", value], env=env)

    try:
        return run_guarded(state, staged, operation)
    except AdmissionError as exc:
        print(f"zapret2applet admission: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
