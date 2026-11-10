#!/usr/bin/env python3
"""Strict upstream ownership manifest primitives for the transactional updater.

This module is intentionally not wired into the production update path yet.
The legacy overlay updater remains active until tree construction, atomic
activation, and crash-recovery behavior have their own regression coverage.
"""

from __future__ import annotations

import hashlib
import json
import os
import posixpath
import re
import stat
import tempfile
from pathlib import Path
from typing import Any

MANIFEST_NAME = ".zapret2control-upstream-manifest.json"
SCHEMA_VERSION = 1
MAX_MANIFEST_BYTES = 16 * 1024 * 1024
MAX_ENTRIES = 100_000
_RELEASE_RE = re.compile(r"^v[0-9]+(?:\.[0-9]+)+$")
_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


class ManifestError(ValueError):
    """Raised when a manifest or the tree it describes is unsafe."""


def _fail(message: str) -> None:
    raise ManifestError(message)


def _relative_path(value: Any) -> str:
    if not isinstance(value, str) or not value or value.startswith("/"):
        _fail("manifest path must be a non-empty relative path")
    if "\\" in value or "\x00" in value:
        _fail("manifest path contains a forbidden character")
    parts = value.split("/")
    if any(part in ("", ".", "..") for part in parts):
        _fail("manifest path is not normalized")
    if value == MANIFEST_NAME:
        _fail("manifest cannot own itself")
    return value


def is_user_data_path(path: str) -> bool:
    """Return whether path belongs to the explicit user-data preservation set."""
    parts = path.split("/")
    if path == "config":
        return True
    if len(parts) == 2 and parts[0] == "ipset":
        return bool(re.fullmatch(r"zapret-hosts-user.*\.txt", parts[1]))
    return len(parts) >= 3 and parts[0] == "init.d" and parts[2] == "custom.d"


def _safe_symlink_target(path: str, target: Any) -> str:
    if not isinstance(target, str) or not target or target.startswith("/"):
        _fail("symlink target must be a non-empty relative path")
    if "\\" in target or "\x00" in target:
        _fail("symlink target contains a forbidden character")
    resolved = posixpath.normpath(posixpath.join(posixpath.dirname(path), target))
    if resolved in ("", ".", "..") or resolved.startswith("../"):
        _fail("symlink target escapes the installation root")
    return target


def _verify_symlink_containment(root: Path, path: str, target: str) -> None:
    candidate = root / posixpath.dirname(path) / target
    try:
        resolved_root = root.resolve(strict=True)
        resolved_target = candidate.resolve(strict=True)
        resolved_target.relative_to(resolved_root)
    except (OSError, RuntimeError, ValueError):
        _fail(f"symlink does not resolve to an existing path inside the installation: {path}")


def _mode(value: Any) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 0o777:
        _fail("invalid file mode")
    if value & 0o022:
        _fail("group/world-writable paths are not allowed")
    return value


def _duplicate_rejecting_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            _fail(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def validate_manifest(document: Any) -> dict[str, Any]:
    if not isinstance(document, dict) or set(document) != {
        "schema_version", "release_tag", "entries"
    }:
        _fail("invalid manifest top-level structure")
    if isinstance(document["schema_version"], bool) or document["schema_version"] != SCHEMA_VERSION:
        _fail("unsupported manifest schema version")
    tag = document["release_tag"]
    if not isinstance(tag, str) or not _RELEASE_RE.fullmatch(tag):
        _fail("invalid manifest release tag")
    entries = document["entries"]
    if not isinstance(entries, list) or len(entries) > MAX_ENTRIES:
        _fail("invalid manifest entries")
    seen: dict[str, str] = {}
    normalized: list[dict[str, Any]] = []
    for entry in entries:
        if not isinstance(entry, dict):
            _fail("manifest entry must be an object")
        kind = entry.get("type")
        expected = {
            "file": {"path", "type", "mode", "sha256"},
            "directory": {"path", "type", "mode"},
            "symlink": {"path", "type", "mode", "target"},
        }.get(kind)
        if expected is None or set(entry) != expected:
            _fail("invalid manifest entry fields")
        path = _relative_path(entry["path"])
        if path in seen:
            _fail("duplicate manifest path")
        if kind == "symlink":
            if isinstance(entry["mode"], bool) or entry["mode"] != 0o777:
                _fail("symlink mode must be 0777")
            mode = 0o777
        else:
            mode = _mode(entry["mode"])
        if kind == "file":
            digest = entry["sha256"]
            if not isinstance(digest, str) or not _SHA256_RE.fullmatch(digest):
                _fail("invalid file checksum")
            normalized.append({"path": path, "type": kind, "mode": mode, "sha256": digest})
        elif kind == "directory":
            normalized.append({"path": path, "type": kind, "mode": mode})
        else:
            target = _safe_symlink_target(path, entry["target"])
            normalized.append({"path": path, "type": kind, "mode": mode, "target": target})
        seen[path] = kind

    for path in seen:
        parent = posixpath.dirname(path)
        while parent:
            if seen.get(parent) != "directory":
                _fail(f"missing or non-directory parent entry: {parent}")
            parent = posixpath.dirname(parent)
    if [entry["path"] for entry in normalized] != sorted(seen):
        _fail("manifest entries must be sorted by path")
    return {
        "schema_version": SCHEMA_VERSION,
        "release_tag": tag,
        "entries": normalized,
    }


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _walk_entries(root: Path) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []

    def visit(directory: Path, relative: str) -> None:
        try:
            children = sorted(os.scandir(directory), key=lambda item: item.name)
        except OSError as exc:
            _fail(f"cannot inspect directory {relative or '.'}: {exc}")
        for child in children:
            path = f"{relative}/{child.name}" if relative else child.name
            if path == MANIFEST_NAME:
                _fail("upstream tree contains the reserved manifest path")
            if is_user_data_path(path):
                continue
            info = child.stat(follow_symlinks=False)
            mode = stat.S_IMODE(info.st_mode)
            if mode & 0o022 or mode & ~0o777:
                _fail(f"unsafe permissions on {path}")
            if stat.S_ISDIR(info.st_mode):
                entries.append({"path": path, "type": "directory", "mode": mode})
                visit(Path(child.path), path)
            elif stat.S_ISREG(info.st_mode):
                if info.st_nlink != 1:
                    _fail(f"hard-linked regular file is not allowed: {path}")
                entries.append({
                    "path": path,
                    "type": "file",
                    "mode": mode,
                    "sha256": _sha256(Path(child.path)),
                })
            elif stat.S_ISLNK(info.st_mode):
                target = os.readlink(child.path)
                target = _safe_symlink_target(path, target)
                _verify_symlink_containment(root, path, target)
                entries.append({
                    "path": path,
                    "type": "symlink",
                    "mode": 0o777,
                    "target": _safe_symlink_target(path, target),
                })
            else:
                _fail(f"unsupported filesystem object: {path}")

    visit(root, "")
    entries.sort(key=lambda item: item["path"])
    if len(entries) > MAX_ENTRIES:
        _fail("upstream tree contains too many entries")
    return entries


def build_manifest(root: str | os.PathLike[str], release_tag: str) -> dict[str, Any]:
    root_path = Path(root)
    if not _RELEASE_RE.fullmatch(release_tag):
        _fail("invalid release tag")
    if root_path.is_symlink() or not root_path.is_dir():
        _fail("installation root must be a real directory")
    return validate_manifest({
        "schema_version": SCHEMA_VERSION,
        "release_tag": release_tag,
        "entries": _walk_entries(root_path),
    })


def _lstat_no_symlink_parents(root: Path, relative: str) -> os.stat_result:
    current = root
    parts = relative.split("/")
    for part in parts[:-1]:
        current = current / part
        try:
            info = current.lstat()
        except OSError as exc:
            _fail(f"missing manifest parent {current}: {exc}")
        if not stat.S_ISDIR(info.st_mode):
            _fail(f"manifest parent is not a real directory: {current}")
    try:
        return (current / parts[-1]).lstat()
    except OSError as exc:
        _fail(f"manifest path is missing: {relative}: {exc}")


def verify_manifest(
    root: str | os.PathLike[str],
    document: Any,
    *,
    expected_uid: int | None = None,
) -> dict[str, Any]:
    manifest = validate_manifest(document)
    root_path = Path(root)
    if root_path.is_symlink() or not root_path.is_dir():
        _fail("installation root must be a real directory")
    owner = os.geteuid() if expected_uid is None else expected_uid
    for entry in manifest["entries"]:
        info = _lstat_no_symlink_parents(root_path, entry["path"])
        if info.st_uid != owner:
            _fail(f"unexpected owner for {entry['path']}")
        kind = entry["type"]
        if kind == "directory":
            if not stat.S_ISDIR(info.st_mode):
                _fail(f"expected directory: {entry['path']}")
        elif kind == "file":
            if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
                _fail(f"expected single-link regular file: {entry['path']}")
            if _sha256(root_path / entry["path"]) != entry["sha256"]:
                _fail(f"checksum mismatch: {entry['path']}")
        else:
            if not stat.S_ISLNK(info.st_mode):
                _fail(f"expected symlink: {entry['path']}")
            target = os.readlink(root_path / entry["path"])
            if _safe_symlink_target(entry["path"], target) != entry["target"]:
                _fail(f"symlink target mismatch: {entry['path']}")
            _verify_symlink_containment(root_path, entry["path"], target)
        if kind != "symlink" and stat.S_IMODE(info.st_mode) != entry["mode"]:
            _fail(f"mode mismatch: {entry['path']}")
    return manifest


def write_manifest(root: str | os.PathLike[str], release_tag: str) -> dict[str, Any]:
    root_path = Path(root)
    if root_path.is_symlink() or not root_path.is_dir():
        _fail("installation root must be a real directory")
    manifest_path = root_path / MANIFEST_NAME
    try:
        manifest_path.lstat()
    except FileNotFoundError:
        pass
    else:
        _fail("upstream tree already contains the reserved manifest path")
    document = validate_manifest({
        "schema_version": SCHEMA_VERSION,
        "release_tag": release_tag,
        "entries": _walk_entries(root_path, allow_manifest=False),
    })
    payload = (json.dumps(document, sort_keys=True, indent=2) + "\n").encode("utf-8")
    if len(payload) > MAX_MANIFEST_BYTES:
        _fail("manifest exceeds size limit")
    fd, temporary_name = tempfile.mkstemp(prefix=f".{MANIFEST_NAME}.", dir=root_path)
    try:
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_name, manifest_path)
        directory_fd = os.open(root_path, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    except Exception:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise
    return verify_manifest(root_path, document)


def load_manifest(
    root: str | os.PathLike[str],
    *,
    expected_uid: int | None = None,
) -> dict[str, Any]:
    root_path = Path(root)
    manifest_path = root_path / MANIFEST_NAME
    try:
        info = manifest_path.lstat()
    except OSError as exc:
        _fail(f"manifest is missing: {exc}")
    if not stat.S_ISREG(info.st_mode) or info.st_uid != (
        os.geteuid() if expected_uid is None else expected_uid
    ):
        _fail("manifest must be an owned regular file")
    if stat.S_IMODE(info.st_mode) & 0o077:
        _fail("manifest permissions must be private (0600)")
    if info.st_size > MAX_MANIFEST_BYTES:
        _fail("manifest exceeds size limit")
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    try:
        fd = os.open(manifest_path, flags)
        opened = os.fstat(fd)
        if not stat.S_ISREG(opened.st_mode) or opened.st_uid != (
            os.geteuid() if expected_uid is None else expected_uid
        ):
            os.close(fd)
            _fail("opened manifest is not an owned regular file")
        if stat.S_IMODE(opened.st_mode) & 0o077 or opened.st_size > MAX_MANIFEST_BYTES:
            os.close(fd)
            _fail("opened manifest has unsafe permissions or size")
        with os.fdopen(fd, "rb") as stream:
            raw = stream.read(MAX_MANIFEST_BYTES + 1)
    except OSError as exc:
        _fail(f"cannot read manifest safely: {exc}")
    if len(raw) > MAX_MANIFEST_BYTES:
        _fail("manifest exceeds size limit")
    try:
        document = json.loads(raw.decode("utf-8"), object_pairs_hook=_duplicate_rejecting_object)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        _fail(f"manifest is not valid UTF-8 JSON: {exc}")
    return verify_manifest(root_path, document, expected_uid=expected_uid)
