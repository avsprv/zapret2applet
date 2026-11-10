"""Systemd lifecycle orchestration around journaled updater activation."""
from __future__ import annotations

import os
import re
import subprocess
from pathlib import Path
from typing import Any

from updater_activation import (
    ActivationError,
    activate_staged_tree,
    finalize_transaction,
    recover_transaction,
    _read_journal,
)

_UNIT_RE = re.compile(r"^[A-Za-z0-9_.@:-]+\.service$")


def _systemctl(arguments: list[str]) -> subprocess.CompletedProcess[str]:
    command = "systemctl"
    environment = {
        "PATH": "/usr/sbin:/usr/bin:/sbin:/bin",
        "HOME": "/root",
        "LANG": "C.UTF-8",
    }
    if os.environ.get("QZAPRET_TEST_MODE") == "1":
        command = os.environ.get("QZAPRET_TEST_SYSTEMCTL", command)
        environment.update({
            key: value for key, value in os.environ.items()
            if key.startswith("QZAPRET_TEST_")
        })
    try:
        return subprocess.run(
            [command, *arguments],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
            timeout=30,
            env=environment,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise ActivationError(f"systemctl {' '.join(arguments)} failed: {exc}") from exc


def _service_state(unit: str) -> str:
    result = _systemctl(["show", "--property=ActiveState", "--value", unit])
    if result.returncode != 0:
        raise ActivationError(f"cannot query service state for {unit}: {result.stderr.strip()}")
    state = result.stdout.strip()
    if state not in ("active", "inactive"):
        raise ActivationError(f"service {unit} is in unsafe state {state!r}")
    return state


def _set_service_state(unit: str, action: str, expected: str) -> None:
    result = _systemctl([action, unit])
    if result.returncode != 0:
        raise ActivationError(f"systemctl {action} {unit} failed: {result.stderr.strip()}")
    actual = _service_state(unit)
    if actual != expected:
        raise ActivationError(f"service {unit} did not reach {expected}: {actual}")


def _restore_service(unit: str, was_active: bool) -> None:
    if not was_active:
        return
    state = _service_state(unit)
    if state == "inactive":
        _set_service_state(unit, "start", "active")


def activate_with_service(
    active: str | os.PathLike[str],
    staged: str | os.PathLike[str],
    backup: str | os.PathLike[str],
    journal: str | os.PathLike[str],
    release_tag: str,
    unit: str,
    *,
    no_service: bool = False,
    expected_uid: int | None = None,
) -> dict[str, Any]:
    """Stop/verify, atomically activate, restart/verify, then finalize the journal."""
    if not _UNIT_RE.fullmatch(unit):
        raise ActivationError("invalid systemd unit name")
    was_active = False
    if not no_service:
        state = _service_state(unit)
        was_active = state == "active"
        if was_active:
            try:
                _set_service_state(unit, "stop", "inactive")
            except Exception as stop_error:
                try:
                    _restore_service(unit, True)
                except Exception as restore_error:
                    raise ActivationError(
                        f"service stop verification failed and service state could not be restored: {restore_error}"
                    ) from stop_error
                raise

    try:
        document = activate_staged_tree(
            active, staged, backup, journal, release_tag, was_active,
            expected_uid=expected_uid,
        )
    except Exception as original:
        journal_path = Path(journal)
        if journal_path.exists() or journal_path.is_symlink():
            try:
                recover_transaction(
                    journal_path, Path(active).parent,
                    expected_uid=expected_uid,
                    expected_active_name=Path(active).name,
                )
                _restore_service(unit, was_active)
                finalize_transaction(journal_path, expected_uid=expected_uid)
            except Exception as recovery_error:
                raise ActivationError(
                    f"activation failed and automatic recovery is incomplete: {recovery_error}"
                ) from original
        else:
            try:
                _restore_service(unit, was_active)
            except Exception as service_error:
                raise ActivationError(
                    f"activation failed and previous service state could not be restored: {service_error}"
                ) from original
        raise

    if was_active:
        # On failure, deliberately retain the journal and old backup for the
        # next invocation's recovery path.
        try:
            _set_service_state(unit, "start", "active")
        except Exception as exc:
            raise ActivationError(
                f"installation activated but {unit} failed to restart; previous version retained at {backup}: {exc}"
            ) from exc
    finalize_transaction(journal, expected_uid=expected_uid)
    return document


def recover_with_service(
    journal: str | os.PathLike[str],
    parent: str | os.PathLike[str],
    unit: str,
    *,
    expected_uid: int | None = None,
    expected_active_name: str | None = None,
) -> str:
    """Recover filesystem state, restore the recorded service state, then clear journal."""
    if not _UNIT_RE.fullmatch(unit):
        raise ActivationError("invalid systemd unit name")
    document = _read_journal(Path(journal), os.geteuid() if expected_uid is None else expected_uid)
    result = recover_transaction(
        journal, parent, expected_uid=expected_uid,
        expected_active_name=expected_active_name,
    )
    if document["service_was_active"]:
        _restore_service(unit, True)
    finalize_transaction(journal, expected_uid=expected_uid)
    return result
