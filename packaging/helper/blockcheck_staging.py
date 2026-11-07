"""Secure one-shot Blockcheck option staging, serialized by StateStore.locked()."""
from __future__ import annotations
import json
import os
import secrets
import stat
import uuid
from pathlib import Path
from typing import Any
from blockcheck_manager import BlockcheckOptions, MAX_OPTIONS_SIZE
from blockcheck_state import StateError, StateStore

SCHEMA_VERSION = 1
MAX_STAGED_SIZE = MAX_OPTIONS_SIZE + 256
OPTIONS_FILE = "options.json"

class StagingError(StateError):
    """Unsafe, malformed, or inconsistent staged Blockcheck request."""

class StagedOptionsStore:
    def __init__(self, state_store: StateStore, runtime_directory: str | os.PathLike[str]) -> None:
        self.state_store = state_store
        self.directory = Path(runtime_directory)
        self.owner = state_store.owner

    def _require_lock(self) -> None:
        try:
            self.state_store._require_lock()
        except StateError as exc:
            raise StagingError(str(exc)) from exc

    def _open_directory(self, create: bool = False) -> int:
        path = Path(os.path.abspath(os.fspath(self.directory)))
        flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC
        current = -1
        try:
            current = os.open(os.sep, flags)
            for part in path.parts[1:]:
                try:
                    nxt = os.open(part, flags, dir_fd=current)
                except FileNotFoundError:
                    if not create:
                        raise
                    try:
                        os.mkdir(part, 0o700, dir_fd=current)
                    except FileExistsError:
                        pass
                    nxt = os.open(part, flags, dir_fd=current)
                os.close(current)
                current = nxt
        except FileNotFoundError as exc:
            if current >= 0:
                os.close(current)
            raise StagingError("runtime directory does not exist") from exc
        except OSError as exc:
            if current >= 0:
                os.close(current)
            raise StagingError(f"unsafe runtime directory: {exc}") from exc
        info = os.fstat(current)
        if not stat.S_ISDIR(info.st_mode) or info.st_uid != self.owner or stat.S_IMODE(info.st_mode) != 0o700:
            os.close(current)
            raise StagingError("runtime directory must be owned by the effective user and have mode 0700")
        return current

    @staticmethod
    def _operation_id(value: str) -> str:
        try:
            parsed = uuid.UUID(value)
        except (ValueError, TypeError, AttributeError) as exc:
            raise StagingError("invalid Blockcheck operation ID") from exc
        if str(parsed) != value:
            raise StagingError("operation ID must be a canonical UUID")
        return value

    def stage_locked(self, operation_id: str, options: BlockcheckOptions) -> None:
        self._require_lock()
        operation_id = self._operation_id(operation_id)
        if not isinstance(options, BlockcheckOptions):
            raise StagingError("options must be a validated BlockcheckOptions instance")
        payload = {"version": SCHEMA_VERSION, "operation_id": operation_id,
                   "options": json.loads(options.to_json().decode("ascii"))}
        raw = (json.dumps(payload, sort_keys=True, separators=(",", ":")) + "\n").encode("ascii")
        if len(raw) > MAX_STAGED_SIZE:
            raise StagingError("staged options exceed size limit")
        directory_fd = self._open_directory(create=True)
        temporary = ".options-" + secrets.token_hex(16) + ".tmp"
        fd = -1
        try:
            try:
                existing = os.open(OPTIONS_FILE, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=directory_fd)
            except FileNotFoundError:
                existing = -1
            except OSError as exc:
                raise StagingError(f"pending options path is unsafe: {exc}") from exc
            if existing >= 0:
                os.close(existing)
                raise StagingError("a staged Blockcheck operation is already pending")
            fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                         0o600, dir_fd=directory_fd)
            os.fchmod(fd, 0o600)
            view = memoryview(raw)
            while view:
                count = os.write(fd, view)
                if count <= 0:
                    raise StagingError("short write while staging options")
                view = view[count:]
            os.fsync(fd)
            os.close(fd)
            fd = -1
            # link() publishes atomically and refuses to replace a pending request.
            os.link(temporary, OPTIONS_FILE, src_dir_fd=directory_fd,
                    dst_dir_fd=directory_fd, follow_symlinks=False)
            os.unlink(temporary, dir_fd=directory_fd)
            os.fsync(directory_fd)
        except FileExistsError as exc:
            raise StagingError("a staged Blockcheck operation is already pending") from exc
        except OSError as exc:
            raise StagingError(f"cannot stage Blockcheck options: {exc}") from exc
        finally:
            if fd >= 0:
                os.close(fd)
            try:
                os.unlink(temporary, dir_fd=directory_fd)
            except FileNotFoundError:
                pass
            os.close(directory_fd)

    def _read_document_locked(self, expected_operation_id: str | None = None, missing_ok: bool = False) -> tuple[str, BlockcheckOptions] | None:
        self._require_lock()
        if expected_operation_id is not None:
            expected_operation_id = self._operation_id(expected_operation_id)
        try:
            directory_fd = self._open_directory()
        except StagingError as exc:
            if missing_ok and str(exc) == "runtime directory does not exist":
                return None
            raise
        try:
            try:
                fd = os.open(OPTIONS_FILE, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC,
                             dir_fd=directory_fd)
            except FileNotFoundError:
                if missing_ok:
                    return None
                raise StagingError("no staged Blockcheck operation is pending")
            except OSError as exc:
                raise StagingError(f"cannot safely open staged options: {exc}") from exc
            try:
                info = os.fstat(fd)
                if (not stat.S_ISREG(info.st_mode) or info.st_uid != self.owner
                    or stat.S_IMODE(info.st_mode) != 0o600 or info.st_nlink != 1
                    or info.st_size > MAX_STAGED_SIZE):
                    raise StagingError("staged options have unsafe type, owner, mode, link count, or size")
                chunks, remaining = [], MAX_STAGED_SIZE + 1
                while remaining:
                    chunk = os.read(fd, min(remaining, 1024))
                    if not chunk:
                        break
                    chunks.append(chunk)
                    remaining -= len(chunk)
                raw = b"".join(chunks)
                if len(raw) > MAX_STAGED_SIZE:
                    raise StagingError("staged options exceed size limit")
            finally:
                os.close(fd)
            def reject_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
                result = {}
                for key, value in pairs:
                    if key in result:
                        raise ValueError(f"duplicate JSON key: {key}")
                    result[key] = value
                return result
            try:
                value = json.loads(raw.decode("ascii"), object_pairs_hook=reject_duplicates)
            except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
                raise StagingError(f"staged options are malformed: {exc}") from exc
            if (not isinstance(value, dict) or set(value) != {"version", "operation_id", "options"}
                or type(value["version"]) is not int or value["version"] != SCHEMA_VERSION
                or not isinstance(value["operation_id"], str) or not isinstance(value["options"], dict)):
                raise StagingError("staged options schema is invalid")
            operation_id = self._operation_id(value["operation_id"])
            if expected_operation_id is not None and operation_id != expected_operation_id:
                raise StagingError("staged options operation ID mismatch")
            try:
                options_raw = (json.dumps(value["options"], sort_keys=True, separators=(",", ":")) + "\n").encode("ascii")
                options = BlockcheckOptions.from_json(options_raw)
                return operation_id, options
            except (UnicodeEncodeError, ValueError) as exc:
                raise StagingError(f"staged Blockcheck options are invalid: {exc}") from exc
        finally:
            os.close(directory_fd)

    def read_locked(self, expected_operation_id: str) -> BlockcheckOptions:
        return self._read_document_locked(expected_operation_id)[1]

    def read_pending_locked(self, missing_ok: bool = False) -> tuple[str, BlockcheckOptions] | None:
        # Read the next queued request without requiring a unit argument.
        return self._read_document_locked(missing_ok=missing_ok)

    def discard_locked(self, expected_operation_id: str) -> None:
        """Discard a failed submission only when no durable recovery marker exists."""
        self._require_lock()
        expected_operation_id = self._operation_id(expected_operation_id)
        if self.state_store._read_marker() is not None:
            raise StagingError("cannot discard options while recovery is pending")
        self.read_locked(expected_operation_id)
        directory_fd = self._open_directory()
        try:
            os.unlink(OPTIONS_FILE, dir_fd=directory_fd)
            os.fsync(directory_fd)
        except OSError as exc:
            raise StagingError(f"cannot discard staged options: {exc}") from exc
        finally:
            os.close(directory_fd)

    def consume_locked(self, expected_operation_id: str) -> None:
        """Consume only after prepare_locked(operation_id) persisted recovery state."""
        self._require_lock()
        self._operation_id(expected_operation_id)
        # Re-open and validate the UUID-bound document immediately before unlink.
        self.read_locked(expected_operation_id)
        directory_fd = self._open_directory()
        try:
            os.unlink(OPTIONS_FILE, dir_fd=directory_fd)
            os.fsync(directory_fd)
        except OSError as exc:
            raise StagingError(f"cannot consume staged options: {exc}") from exc
        finally:
            os.close(directory_fd)
