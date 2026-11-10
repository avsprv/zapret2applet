"""Build a complete updater staging tree without modifying the active installation.

The caller must provide an already extracted and validated upstream tree. This
module deliberately does not activate the result or alter the production updater.
"""
from __future__ import annotations

import json
import os
import posixpath
import re
import shutil
import stat
import tempfile
from pathlib import Path
from typing import Any

from updater_transaction import (
    MANIFEST_NAME,
    MAX_MANIFEST_BYTES,
    ManifestError,
    _validate_root,
    _walk_entries,
    build_manifest,
    is_user_data_path,
    verify_manifest,
)


def _safe_parent(root: Path, relative: str) -> Path:
    current = root
    parts = relative.split("/")
    for part in parts[:-1]:
        current = current / part
        try:
            info = current.lstat()
        except FileNotFoundError:
            current.mkdir(mode=0o755)
            info = current.lstat()
        if not stat.S_ISDIR(info.st_mode):
            raise ManifestError(f"staging parent is not a real directory: {current}")
    return current / parts[-1]


def _copy_regular(source: Path, destination: Path, mode: int) -> None:
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    source_fd = os.open(source, flags)
    try:
        info = os.fstat(source_fd)
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
            raise ManifestError(f"source changed while copying: {source}")
        destination_fd = os.open(
            destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0), mode
        )
        try:
            with os.fdopen(source_fd, "rb", closefd=False) as src, os.fdopen(destination_fd, "wb", closefd=False) as dst:
                shutil.copyfileobj(src, dst, 1024 * 1024)
                dst.flush()
                os.fsync(dst.fileno())
            os.fchmod(destination_fd, mode)
        finally:
            os.close(destination_fd)
    finally:
        os.close(source_fd)


def _copy_user_data(source_root: Path, stage: Path) -> None:
    """Copy only the explicit user-data allowlist, after the source was validated."""
    # The caller validates all active paths first. Discover user-data roots explicitly,
    # but never traverse a symlinked container directory.
    candidates = [source_root / "config"]
    ipset = source_root / "ipset"
    if ipset.exists() or ipset.is_symlink():
        if ipset.is_symlink() or not ipset.is_dir():
            raise ManifestError("ipset must be a real directory when preserving user hostlists")
        candidates.extend(sorted(
            p for p in ipset.iterdir()
            if re.fullmatch(r"zapret-hosts-user.*\.txt", p.name)
        ))
    init_root = source_root / "init.d"
    if init_root.exists() or init_root.is_symlink():
        if init_root.is_symlink() or not init_root.is_dir():
            raise ManifestError("init.d must be a real directory when preserving custom.d")
        for platform in sorted(init_root.iterdir()):
            if platform.is_symlink() or not platform.is_dir():
                raise ManifestError(f"unsafe init.d platform directory: {platform.name}")
            custom = platform / "custom.d"
            if custom.exists() or custom.is_symlink():
                candidates.append(custom)

    def copy_node(src: Path, relative: str) -> None:
        info = src.lstat()
        if stat.S_ISLNK(info.st_mode):
            raise ManifestError(f"refusing symlink in preserved user data: {relative}")
        mode = stat.S_IMODE(info.st_mode)
        if mode & 0o022 or mode & ~0o777:
            raise ManifestError(f"unsafe permissions on preserved user data: {relative}")
        dst = _safe_parent(stage, relative)
        if stat.S_ISDIR(info.st_mode):
            if dst.exists():
                if not dst.is_dir() or dst.is_symlink():
                    raise ManifestError(f"user data collides with non-directory: {relative}")
            else:
                dst.mkdir(mode=mode)
            for child in sorted(src.iterdir(), key=lambda p: p.name):
                copy_node(child, f"{relative}/{child.name}")
            os.chmod(dst, mode)
        elif stat.S_ISREG(info.st_mode) and info.st_nlink == 1:
            if dst.exists() or dst.is_symlink():
                if dst.is_dir() and not dst.is_symlink():
                    raise ManifestError(f"user file collides with directory: {relative}")
                dst.unlink()
            _copy_regular(src, dst, mode)
        else:
            raise ManifestError(f"unsupported preserved user-data object: {relative}")

    for source in candidates:
        if not source.exists() and not source.is_symlink():
            continue
        relative = source.relative_to(source_root).as_posix()
        copy_node(source, relative)


def _copy_unknown_legacy(
    active: Path, stage: Path, previous_paths: set[str], new_paths: set[str],
    active_entries: list[dict[str, Any]], new_manifest_entries: list[dict[str, Any]]
) -> None:
    """Retain untracked regular files/directories absent from the new upstream tree."""
    entries = {entry["path"]: entry for entry in active_entries}
    preserve: list[str] = []
    for path, entry in entries.items():
        if is_user_data_path(path) or path in previous_paths or path in new_paths:
            continue
        if entry["type"] == "symlink":
            raise ManifestError(f"untracked legacy symlink requires manual review: {path}")
        preserve.append(path)

    # Retain an otherwise-owned parent directory when it is needed by an unknown child.
    for path in list(preserve):
        parent = posixpath.dirname(path)
        while parent:
            if parent not in new_paths and parent not in preserve:
                preserve.append(parent)
            parent = posixpath.dirname(parent)

    new_types = {entry["path"]: entry["type"] for entry in new_manifest_entries}
    for path in sorted(preserve, key=lambda value: (value.count("/"), value)):
        parent = posixpath.dirname(path)
        blocked = False
        while parent:
            if parent in new_paths and new_types.get(parent) != "directory":
                blocked = True
                break
            parent = posixpath.dirname(parent)
        if blocked:
            continue
        entry = entries.get(path)
        if entry is None:
            # A parent may have been manifest-owned; its metadata is still available
            # from the active filesystem and it is recreated only as a container.
            source = active / path
            info = source.lstat()
            if not stat.S_ISDIR(info.st_mode):
                continue
            mode = stat.S_IMODE(info.st_mode)
            destination = _safe_parent(stage, path)
            if not destination.exists():
                destination.mkdir(mode=mode)
            continue
        if entry["type"] == "directory":
            destination = _safe_parent(stage, path)
            if destination.exists():
                if not destination.is_dir() or destination.is_symlink():
                    continue
                os.chmod(destination, entry["mode"])
            else:
                destination.mkdir(mode=entry["mode"])
        elif entry["type"] == "file":
            destination = _safe_parent(stage, path)
            if destination.exists() or destination.is_symlink():
                continue
            _copy_regular(active / path, destination, entry["mode"])


def _fsync_tree(root: Path, parent: Path) -> None:
    directories: list[Path] = []
    for current, child_dirs, _files in os.walk(root, topdown=True, followlinks=False):
        current_path = Path(current)
        directories.append(current_path)
        child_dirs[:] = [name for name in child_dirs if not (current_path / name).is_symlink()]
    for directory in reversed(directories):
        fd = os.open(directory, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(fd)
        finally:
            os.close(fd)
    parent_fd = os.open(parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(parent_fd)
    finally:
        os.close(parent_fd)


def _write_manifest(stage: Path, document: dict[str, Any]) -> None:
    payload = (json.dumps(document, sort_keys=True, indent=2) + "\n").encode("utf-8")
    if len(payload) > MAX_MANIFEST_BYTES:
        raise ManifestError("manifest exceeds size limit")
    fd, temporary = tempfile.mkstemp(prefix=f".{MANIFEST_NAME}.", dir=stage)
    try:
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, stage / MANIFEST_NAME)
        directory_fd = os.open(stage, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
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


def build_staging_tree(
    upstream_root: str | os.PathLike[str],
    active_root: str | os.PathLike[str],
    destination: str | os.PathLike[str],
    release_tag: str,
) -> dict[str, Any]:
    """Assemble a new tree; never mutate or remove anything from active_root.

    New upstream files win over unknown legacy collisions. Explicit user data
    wins over upstream collisions. Previous-manifest-owned paths absent from
    the new archive are omitted; unknown regular files/directories are retained.
    """
    upstream = Path(upstream_root)
    active = Path(active_root)
    stage = Path(destination)
    _validate_root(upstream)
    _validate_root(active)
    if stage.exists() or stage.is_symlink():
        raise ManifestError("staging destination already exists")
    if not stage.is_absolute():
        raise ManifestError("staging destination must be an absolute path")
    parent = stage.parent
    try:
        if parent.resolve(strict=True) != parent.absolute():
            raise ManifestError("staging parent path contains a symlink component")
        parent_info = parent.lstat()
    except OSError as exc:
        raise ManifestError(f"cannot inspect staging parent: {exc}") from exc
    if (
        not stat.S_ISDIR(parent_info.st_mode)
        or parent_info.st_dev != active.lstat().st_dev
        or parent_info.st_uid != os.geteuid()
        or stat.S_IMODE(parent_info.st_mode) & 0o022
        or stat.S_IMODE(parent_info.st_mode) & ~0o777
    ):
        raise ManifestError("staging parent must be privately owned and on the installation filesystem")

    new_manifest = build_manifest(upstream, release_tag)
    active_entries = _walk_entries(active, allow_manifest=True)
    previous_manifest_path = active / MANIFEST_NAME
    if previous_manifest_path.exists() or previous_manifest_path.is_symlink():
        from updater_transaction import load_manifest
        previous_manifest = load_manifest(active)
        previous_paths = {entry["path"] for entry in previous_manifest["entries"]}
    else:
        previous_paths = set()
    new_entries = new_manifest["entries"]
    new_paths = {entry["path"] for entry in new_entries}

    active_root_mode = stat.S_IMODE(active.lstat().st_mode)
    stage.mkdir(mode=0o700)
    try:
        for entry in new_entries:
            path = entry["path"]
            if is_user_data_path(path):
                continue
            destination_path = _safe_parent(stage, path)
            source_path = upstream / path
            if entry["type"] == "directory":
                if not destination_path.exists():
                    destination_path.mkdir(mode=entry["mode"])
                else:
                    if not destination_path.is_dir() or destination_path.is_symlink():
                        raise ManifestError(f"upstream directory collides with non-directory: {path}")
                    os.chmod(destination_path, entry["mode"])
            elif entry["type"] == "file":
                _copy_regular(source_path, destination_path, entry["mode"])
            else:
                os.symlink(entry["target"], destination_path)

        _copy_unknown_legacy(active, stage, previous_paths, new_paths, active_entries, new_entries)
        _copy_user_data(active, stage)
        _write_manifest(stage, new_manifest)
        _validate_root(stage)
        verify_manifest(stage, new_manifest)
        # Validate every preserved and untracked object in the completed tree.
        _walk_entries(stage, allow_manifest=True)
        os.chmod(stage, active_root_mode)
        _fsync_tree(stage, parent)
        return new_manifest
    except Exception:
        # This destination is a newly-created, private transaction path. The
        # caller owns its lifecycle; retain it on failure for forensic review.
        raise
