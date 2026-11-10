"""Crash-recoverable atomic activation primitives used by updater_service."""
from __future__ import annotations

import ctypes
import errno
import json
import os
import re
import shutil
import stat
import tempfile
from pathlib import Path
from typing import Any

SCHEMA_VERSION = 1
MAX_JOURNAL_BYTES = 64 * 1024
RENAME_NOREPLACE = 1
RENAME_EXCHANGE = 2
_NAME_RE = re.compile(r"^[A-Za-z0-9._-]{1,128}$")


class ActivationError(RuntimeError):
    """Raised when atomic activation or safe recovery cannot proceed."""


def _fail(message: str) -> None:
    raise ActivationError(message)


def _safe_directory(path: Path, expected_uid: int) -> os.stat_result:
    if not path.is_absolute():
        _fail(f"path must be absolute: {path}")
    try:
        if path.resolve(strict=True) != path.absolute():
            _fail(f"path contains a symlink component: {path}")
        info = path.lstat()
    except OSError as exc:
        _fail(f"cannot inspect directory {path}: {exc}")
    mode = stat.S_IMODE(info.st_mode)
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != expected_uid:
        _fail(f"directory must be real and owned by uid {expected_uid}: {path}")
    if mode & 0o022 or mode & ~0o777:
        _fail(f"directory has unsafe permissions: {path}")
    return info


def _identity(path: Path, expected_uid: int) -> tuple[int, int]:
    info = _safe_directory(path, expected_uid)
    return info.st_dev, info.st_ino


def _renameat2(source: Path, destination: Path, flags: int) -> None:
    try:
        libc = ctypes.CDLL(None, use_errno=True)
        function = libc.renameat2
    except (AttributeError, OSError):
        _fail("renameat2 is unavailable; refusing non-atomic directory replacement")
    function.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    function.restype = ctypes.c_int
    result = function(
        -100, os.fsencode(source), -100, os.fsencode(destination), flags
    )
    if result != 0:
        error = ctypes.get_errno()
        if error in (errno.ENOSYS, errno.EINVAL, errno.EOPNOTSUPP):
            _fail("atomic directory rename operation is unsupported")
        raise OSError(error, os.strerror(error), f"{source} -> {destination}")


def _validate_name(value: Any, label: str) -> str:
    if not isinstance(value, str) or not _NAME_RE.fullmatch(value) or value in (".", ".."):
        _fail(f"invalid {label} basename in transaction journal")
    return value


def _journal_document(
    transaction_id: str,
    release_tag: str,
    active_name: str,
    stage_name: str,
    backup_name: str,
    parent_dev: int,
    old_identity: tuple[int, int],
    new_identity: tuple[int, int],
    service_was_active: bool,
    phase: str = "prepared",
) -> dict[str, Any]:
    if not re.fullmatch(r"[0-9a-f]{32}", transaction_id):
        _fail("invalid transaction identifier")
    if stage_name != f".zapret2control-stage-{transaction_id}":
        _fail("staging basename does not match transaction identifier")
    if backup_name != f".zapret2control-backup-{transaction_id}":
        _fail("backup basename does not match transaction identifier")
    if not re.fullmatch(r"v[0-9]+(?:\.[0-9]+)+", release_tag):
        _fail("invalid release tag")
    if type(service_was_active) is not bool:
        _fail("invalid prior service state")
    return {
        "schema_version": SCHEMA_VERSION,
        "transaction_id": transaction_id,
        "release_tag": release_tag,
        "active_name": _validate_name(active_name, "active"),
        "stage_name": _validate_name(stage_name, "staging"),
        "backup_name": _validate_name(backup_name, "backup"),
        "parent_dev": parent_dev,
        "old_dev": old_identity[0],
        "old_ino": old_identity[1],
        "new_dev": new_identity[0],
        "new_ino": new_identity[1],
        "service_was_active": service_was_active,
        "phase": phase,
    }


def _validate_journal(value: Any) -> dict[str, Any]:
    keys = {
        "schema_version", "transaction_id", "release_tag", "active_name",
        "stage_name", "backup_name", "parent_dev", "old_dev", "old_ino",
        "new_dev", "new_ino", "service_was_active", "phase",
    }
    if not isinstance(value, dict) or set(value) != keys:
        _fail("invalid transaction journal structure")
    if type(value["schema_version"]) is not int or value["schema_version"] != SCHEMA_VERSION:
        _fail("unsupported transaction journal schema")
    _journal_document(
        value["transaction_id"], value["release_tag"], value["active_name"],
        value["stage_name"], value["backup_name"], value["parent_dev"],
        (value["old_dev"], value["old_ino"]), (value["new_dev"], value["new_ino"]),
        value["service_was_active"], value["phase"],
    )
    for key in ("parent_dev", "old_dev", "old_ino", "new_dev", "new_ino"):
        if type(value[key]) is not int or value[key] < 0:
            _fail(f"invalid transaction journal identity: {key}")
    if value["old_dev"] != value["parent_dev"] or value["new_dev"] != value["parent_dev"]:
        _fail("transaction directories are not on the recorded filesystem")
    if value["old_ino"] == value["new_ino"]:
        _fail("transaction directory identities must be distinct")
    if value["active_name"] == value["stage_name"] or value["active_name"] == value["backup_name"] or value["stage_name"] == value["backup_name"]:
        _fail("transaction directory names must be distinct")
    if value["phase"] not in ("prepared", "exchanged", "backup_saved", "aborted"):
        _fail("invalid transaction journal phase")
    return value


def _write_journal(
    path: Path, document: dict[str, Any], expected_uid: int, *, replace_existing: bool = False
) -> None:
    parent = path.parent
    _safe_directory(parent, expected_uid)
    payload = (json.dumps(_validate_journal(document), sort_keys=True, indent=2) + "\n").encode()
    if len(payload) > MAX_JOURNAL_BYTES:
        _fail("transaction journal exceeds size limit")
    if replace_existing:
        current = _read_journal(path, expected_uid)
        if current["transaction_id"] != document["transaction_id"]:
            _fail("refusing to replace a different transaction journal")
    elif path.exists() or path.is_symlink():
        _fail("transaction journal already exists")
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=parent)
    try:
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        if replace_existing:
            os.replace(temporary, path)
        else:
            _renameat2(Path(temporary), path, RENAME_NOREPLACE)
        directory_fd = os.open(parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    except Exception:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def _reject_duplicate_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            _fail(f"duplicate transaction journal key: {key}")
        result[key] = value
    return result


def _read_journal(path: Path, expected_uid: int) -> dict[str, Any]:
    _safe_directory(path.parent, expected_uid)
    try:
        info = path.lstat()
    except OSError as exc:
        _fail(f"cannot inspect transaction journal: {exc}")
    if not stat.S_ISREG(info.st_mode) or info.st_uid != expected_uid or info.st_nlink != 1:
        _fail("transaction journal must be an owned single-link regular file")
    if stat.S_IMODE(info.st_mode) != 0o600 or info.st_size > MAX_JOURNAL_BYTES:
        _fail("transaction journal has unsafe permissions or size")
    try:
        fd = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    except OSError as exc:
        _fail(f"cannot securely open transaction journal: {exc}")
    try:
        opened = os.fstat(fd)
        if (
            not stat.S_ISREG(opened.st_mode)
            or opened.st_uid != expected_uid
            or opened.st_nlink != 1
            or stat.S_IMODE(opened.st_mode) != 0o600
            or opened.st_size > MAX_JOURNAL_BYTES
            or (opened.st_dev, opened.st_ino) != (info.st_dev, info.st_ino)
        ):
            _fail("opened transaction journal changed or has unsafe metadata")
        with os.fdopen(fd, "r", encoding="utf-8", closefd=False) as stream:
            document = json.load(stream, object_pairs_hook=_reject_duplicate_pairs)
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        _fail(f"cannot read transaction journal: {exc}")
    finally:
        os.close(fd)
    validated = _validate_journal(document)
    if path.stem != validated["transaction_id"]:
        _fail("transaction journal filename does not match its identifier")
    return validated


def _fsync_directory(path: Path) -> None:
    fd = os.open(path, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def _matches(path: Path, identity: tuple[int, int], expected_uid: int) -> bool:
    try:
        return _identity(path, expected_uid) == identity
    except ActivationError:
        return False


def _unlink_journal(path: Path) -> None:
    path.unlink()
    _fsync_directory(path.parent)


def _prepare_activation_document(
    active_path: Path,
    stage_path: Path,
    backup_path: Path,
    journal_path: Path,
    release_tag: str,
    service_was_active: bool,
    expected_uid: int,
) -> dict[str, Any]:
    active_info = _safe_directory(active_path, expected_uid)
    stage_info = _safe_directory(stage_path, expected_uid)
    parent = active_path.parent
    parent_info = _safe_directory(parent, expected_uid)
    if stage_path.parent != parent or backup_path.parent != parent:
        _fail("active, staging, and backup directories must be siblings")
    if (active_info.st_dev, stage_info.st_dev) != (parent_info.st_dev, parent_info.st_dev):
        _fail("active, staging, and backup directories must share one filesystem")
    if backup_path.exists() or backup_path.is_symlink():
        _fail("backup destination already exists")
    if (
        active_path.name == stage_path.name
        or active_path.name == backup_path.name
        or stage_path.name == backup_path.name
    ):
        _fail("transaction directory names must be distinct")
    _safe_directory(journal_path.parent, expected_uid)
    return _journal_document(
        journal_path.stem, release_tag, active_path.name, stage_path.name,
        backup_path.name, parent_info.st_dev,
        (active_info.st_dev, active_info.st_ino), (stage_info.st_dev, stage_info.st_ino),
        service_was_active,
    )


def prepare_activation_journal(
    active: str | os.PathLike[str],
    staged: str | os.PathLike[str],
    backup: str | os.PathLike[str],
    journal: str | os.PathLike[str],
    release_tag: str,
    service_was_active: bool,
    *,
    expected_uid: int | None = None,
) -> dict[str, Any]:
    """Persist directory identities before the service is stopped."""
    uid = os.geteuid() if expected_uid is None else expected_uid
    active_path, stage_path, backup_path, journal_path = map(Path, (active, staged, backup, journal))
    document = _prepare_activation_document(
        active_path, stage_path, backup_path, journal_path,
        release_tag, service_was_active, uid,
    )
    _write_journal(journal_path, document, uid)
    return document


def activate_staged_tree(
    active: str | os.PathLike[str],
    staged: str | os.PathLike[str],
    backup: str | os.PathLike[str],
    journal: str | os.PathLike[str],
    release_tag: str,
    service_was_active: bool,
    *,
    expected_uid: int | None = None,
) -> dict[str, Any]:
    """Exchange prevalidated sibling trees; journal is durable before this call."""
    uid = os.geteuid() if expected_uid is None else expected_uid
    active_path, stage_path, backup_path, journal_path = map(Path, (active, staged, backup, journal))
    expected = _prepare_activation_document(
        active_path, stage_path, backup_path, journal_path,
        release_tag, service_was_active, uid,
    )
    if journal_path.exists() or journal_path.is_symlink():
        document = _read_journal(journal_path, uid)
        if document != expected:
            _fail("prepared transaction journal does not match current directory identities")
    else:
        _write_journal(journal_path, expected, uid)
        document = expected

    _renameat2(active_path, stage_path, RENAME_EXCHANGE)
    if not _matches(active_path, (expected["new_dev"], expected["new_ino"]), uid):
        _fail("active directory identity mismatch after atomic exchange")
    if not _matches(stage_path, (expected["old_dev"], expected["old_ino"]), uid):
        _fail("old installation identity mismatch after atomic exchange")
    _fsync_directory(active_path.parent)

    _renameat2(stage_path, backup_path, RENAME_NOREPLACE)
    if not _matches(backup_path, (expected["old_dev"], expected["old_ino"]), uid):
        _fail("backup directory identity mismatch after rename")
    _fsync_directory(active_path.parent)
    document["phase"] = "backup_saved"
    _write_journal(journal_path, document, uid, replace_existing=True)
    return document


def recover_transaction(
    journal: str | os.PathLike[str],
    parent: str | os.PathLike[str],
    *,
    expected_uid: int | None = None,
    expected_active_name: str | None = None,
) -> str:
    """Recover a prepared transaction by matching recorded device/inode identities."""
    uid = os.geteuid() if expected_uid is None else expected_uid
    journal_path, parent_path = Path(journal), Path(parent)
    _safe_directory(parent_path, uid)
    document = _read_journal(journal_path, uid)
    if expected_active_name is not None and document["active_name"] != expected_active_name:
        _fail("transaction journal refers to a different active installation")
    if document["parent_dev"] != parent_path.stat().st_dev:
        _fail("transaction parent filesystem identity changed")
    active = parent_path / document["active_name"]
    staged = parent_path / document["stage_name"]
    backup = parent_path / document["backup_name"]
    old_id = (document["old_dev"], document["old_ino"])
    new_id = (document["new_dev"], document["new_ino"])

    active_old = _matches(active, old_id, uid)
    active_new = _matches(active, new_id, uid)
    stage_new = _matches(staged, new_id, uid)
    stage_old = _matches(staged, old_id, uid)
    backup_old = _matches(backup, old_id, uid)

    if active_old and stage_new and not backup.exists() and not backup.is_symlink():
        # Exchange did not happen: discard only the exact staged inode recorded by the journal.
        shutil.rmtree(staged)
        _fsync_directory(parent_path)
        document["phase"] = "aborted"
        _write_journal(journal_path, document, uid, replace_existing=True)
        return "discarded_unactivated_stage"
    if active_old and not staged.exists() and not staged.is_symlink() and not backup.exists() and not backup.is_symlink():
        # A prior recovery may have removed the stage and crashed before
        # recording the terminal phase. The active inode proves no exchange occurred.
        if document["phase"] != "aborted":
            document["phase"] = "aborted"
            _write_journal(journal_path, document, uid, replace_existing=True)
        return "completed_abort_cleanup"
    if active_new and stage_old and not backup.exists() and not backup.is_symlink():
        _renameat2(staged, backup, RENAME_NOREPLACE)
        _fsync_directory(parent_path)
        document["phase"] = "backup_saved"
        _write_journal(journal_path, document, uid, replace_existing=True)
        return "completed_backup_rename"
    if active_new and not staged.exists() and not staged.is_symlink() and backup_old:
        if document["phase"] != "backup_saved":
            document["phase"] = "backup_saved"
            _write_journal(journal_path, document, uid, replace_existing=True)
        return "completed_after_backup_rename"
    _fail("transaction paths do not match journal identities; manual recovery required")


def finalize_transaction(
    journal: str | os.PathLike[str],
    *,
    expected_uid: int | None = None,
) -> None:
    """Remove a completed/aborted journal only after service-state handling succeeds."""
    uid = os.geteuid() if expected_uid is None else expected_uid
    path = Path(journal)
    document = _read_journal(path, uid)
    if document["phase"] not in ("backup_saved", "aborted"):
        _fail("cannot finalize a transaction that has not reached a terminal filesystem state")
    _unlink_journal(path)
