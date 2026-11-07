#!/usr/bin/env python3
"""Crash-safe, root-owned Blockcheck recovery state primitives.

The caller must hold StateStore.locked() from prepare_locked() until the
worker has exited. ExecStopPost/boot recovery acquires the same lock before
calling recover_locked(). This module deliberately does not stop the service.
"""
from __future__ import annotations

import contextlib
import fcntl
import json
import os
import secrets
import stat
import subprocess
from pathlib import Path
from typing import Iterator
import uuid

SERVICE = "zapret2.service"
SCHEMA_VERSION = 1
MAX_STATE_SIZE = 4096


class StateError(RuntimeError):
    pass


class StateStore:
    def __init__(self, directory: str | os.PathLike[str],
                 systemctl: str = "/usr/bin/systemctl") -> None:
        self.directory = Path(directory)
        self.systemctl = systemctl
        self.owner = os.geteuid()
        self._dir_fd: int | None = None

    def _open_directory(self, create: bool = False) -> int:
        path = self.directory
        if create:
            try:
                path.mkdir(mode=0o700, parents=True, exist_ok=True)
            except OSError as exc:
                raise StateError(f"cannot create state directory: {exc}") from exc
        try:
            fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        except OSError as exc:
            raise StateError(f"unsafe or missing state directory: {exc}") from exc
        info = os.fstat(fd)
        if not stat.S_ISDIR(info.st_mode) or info.st_uid != self.owner or stat.S_IMODE(info.st_mode) != 0o700:
            os.close(fd)
            raise StateError("state directory must be owned by the effective user and have mode 0700")
        return fd

    @contextlib.contextmanager
    def locked(self, create: bool = False) -> Iterator["StateStore"]:
        dir_fd = self._open_directory(create=create)
        self._dir_fd = dir_fd
        lock_fd = -1
        try:
            try:
                lock_fd = os.open("lock", os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW,
                                  0o600, dir_fd=dir_fd)
            except OSError as exc:
                raise StateError(f"cannot open lifecycle lock: {exc}") from exc
            info = os.fstat(lock_fd)
            if (not stat.S_ISREG(info.st_mode) or info.st_uid != self.owner
                    or stat.S_IMODE(info.st_mode) != 0o600 or info.st_nlink != 1):
                raise StateError("lifecycle lock has unsafe type, owner, mode, or link count")
            fcntl.flock(lock_fd, fcntl.LOCK_EX)
            yield self
        finally:
            if lock_fd >= 0:
                os.close(lock_fd)
            self._dir_fd = None
            os.close(dir_fd)

    def _require_lock(self) -> int:
        if self._dir_fd is None:
            raise StateError("state operation requires the lifecycle lock")
        return self._dir_fd

    def _service_is_active(self) -> bool:
        try:
            result = subprocess.run(
                [self.systemctl, "is-active", "--quiet", SERVICE],
                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE, check=False, timeout=30,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise StateError(f"cannot query {SERVICE}: {exc}") from exc
        if result.returncode == 0:
            return True
        if result.returncode == 3:
            return False
        detail = result.stderr.decode("utf-8", "replace").strip()
        raise StateError(f"systemctl is-active failed ({result.returncode}): {detail}")

    def _read_marker(self) -> dict | None:
        dir_fd = self._require_lock()
        try:
            fd = os.open("state.json", os.O_RDONLY | os.O_NOFOLLOW, dir_fd=dir_fd)
        except FileNotFoundError:
            return None
        except OSError as exc:
            raise StateError(f"cannot safely open recovery marker: {exc}") from exc
        try:
            info = os.fstat(fd)
            if (not stat.S_ISREG(info.st_mode) or info.st_uid != self.owner
                    or stat.S_IMODE(info.st_mode) != 0o600 or info.st_nlink != 1
                    or info.st_size > MAX_STATE_SIZE):
                raise StateError("recovery marker has unsafe type, owner, mode, link count, or size")
            chunks = []
            remaining = MAX_STATE_SIZE + 1
            while remaining:
                chunk = os.read(fd, min(remaining, 1024))
                if not chunk:
                    break
                chunks.append(chunk)
                remaining -= len(chunk)
            raw = b"".join(chunks)
            if len(raw) > MAX_STATE_SIZE:
                raise StateError("recovery marker exceeds size limit")
        finally:
            os.close(fd)
        try:
            value = json.loads(raw.decode("ascii"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise StateError(f"recovery marker is malformed: {exc}") from exc
        if (not isinstance(value, dict)
                or set(value) != {"version", "operation_id", "service_was_active"}
                or type(value["version"]) is not int or value["version"] != SCHEMA_VERSION
                or not isinstance(value["operation_id"], str)
                or not isinstance(value["service_was_active"], bool)):
            raise StateError("recovery marker has an unsupported schema")
        try:
            if str(uuid.UUID(value["operation_id"])) != value["operation_id"]:
                raise ValueError("non-canonical UUID")
        except ValueError as exc:
            raise StateError("recovery marker has an invalid operation ID") from exc
        return value

    def _write_marker(self, value: dict) -> None:
        dir_fd = self._require_lock()
        payload = (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode("ascii")
        temp_name = ".state-" + secrets.token_hex(16) + ".tmp"
        fd = -1
        try:
            fd = os.open(temp_name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                         0o600, dir_fd=dir_fd)
            os.fchmod(fd, 0o600)
            view = memoryview(payload)
            while view:
                written = os.write(fd, view)
                if written <= 0:
                    raise StateError("short write while persisting recovery marker")
                view = view[written:]
            os.fsync(fd)
            os.close(fd)
            fd = -1
            os.replace(temp_name, "state.json", src_dir_fd=dir_fd, dst_dir_fd=dir_fd)
            os.fsync(dir_fd)
        except OSError as exc:
            raise StateError(f"cannot persist recovery marker: {exc}") from exc
        finally:
            if fd >= 0:
                os.close(fd)
            try:
                os.unlink(temp_name, dir_fd=dir_fd)
            except FileNotFoundError:
                pass

    def prepare_locked(self, operation_id: str | None = None) -> dict:
        """Persist pre-scan state before the caller stops zapret2.service."""
        self._require_lock()
        existing = self._read_marker()
        if existing is not None:
            raise StateError("a previous Blockcheck recovery is still pending")
        active = self._service_is_active()
        record = {
            "version": SCHEMA_VERSION,
            "operation_id": operation_id or str(uuid.uuid4()),
            "service_was_active": active,
        }
        try:
            if str(uuid.UUID(record["operation_id"])) != record["operation_id"]:
                raise ValueError("non-canonical UUID")
        except ValueError as exc:
            raise StateError("invalid operation ID") from exc
        self._write_marker(record)
        return record

    def recover_locked(self) -> bool:
        """Restore and verify the recorded service state; retain marker on failure."""
        self._require_lock()
        record = self._read_marker()
        if record is None:
            return False
        expected_active = record["service_was_active"]
        if expected_active:
            try:
                result = subprocess.run(
                    [self.systemctl, "start", SERVICE],
                    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                    stderr=subprocess.PIPE, check=False, timeout=120,
                )
            except (OSError, subprocess.TimeoutExpired) as exc:
                raise StateError(f"cannot restore {SERVICE}: {exc}") from exc
            if result.returncode != 0:
                detail = result.stderr.decode("utf-8", "replace").strip()
                raise StateError(f"systemctl start failed ({result.returncode}): {detail}")
        actual_active = self._service_is_active()
        if actual_active != expected_active:
            expected = "active" if expected_active else "inactive"
            actual = "active" if actual_active else "inactive"
            raise StateError(f"recovery verification failed: expected {expected}, got {actual}")
        dir_fd = self._require_lock()
        try:
            os.unlink("state.json", dir_fd=dir_fd)
            os.fsync(dir_fd)
        except OSError as exc:
            raise StateError(f"service state verified but marker removal failed: {exc}") from exc
        return True
