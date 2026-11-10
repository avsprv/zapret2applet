"""Command-line integration for the transactional updater.

The production shell helper calls this module only after validating and
extracting the release archive. This module owns staging, recovery and
service-aware atomic activation.
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import stat
import subprocess
import sys
import uuid
from pathlib import Path
from typing import Sequence

from updater_activation import ActivationError
from updater_service import activate_with_service, recover_with_service
from updater_staging import build_staging_tree
from updater_transaction import ManifestError, build_manifest, load_manifest, verify_manifest

_STATE_COMPONENT = "zapret2control"
_STATE_SUBDIR = "updater"


def _secure_dir(path: Path, uid: int, expected_mode: int | None = None) -> None:
    if not path.is_absolute():
        raise ActivationError(f"state path must be absolute: {path}")
    try:
        if path.resolve(strict=True) != path.absolute():
            raise ActivationError(f"state path contains a symlink component: {path}")
        info = path.lstat()
    except OSError as exc:
        raise ActivationError(f"cannot inspect state directory {path}: {exc}") from exc
    mode = stat.S_IMODE(info.st_mode)
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != uid:
        raise ActivationError(f"state directory must be owned by uid {uid}: {path}")
    if mode & 0o022 or mode & ~0o777:
        raise ActivationError(f"state directory has unsafe permissions: {path}")
    if expected_mode is not None and mode != expected_mode:
        raise ActivationError(f"state directory must have mode {expected_mode:04o}: {path}")


def ensure_journal_directory(path: str | os.PathLike[str], *, expected_uid: int | None = None) -> Path:
    uid = os.geteuid() if expected_uid is None else expected_uid
    target = Path(path)
    if not target.is_absolute() or target.name != _STATE_SUBDIR or target.parent.name != _STATE_COMPONENT:
        raise ActivationError("journal directory must be the updater subdirectory of zapret2control state")
    state_root = target.parent
    _secure_dir(state_root.parent, uid)
    if not state_root.exists():
        state_root.mkdir(mode=0o755)
    _secure_dir(state_root, uid)
    if not target.exists():
        target.mkdir(mode=0o700)
    _secure_dir(target, uid, 0o700)
    return target


def recover_pending(
    journal_dir: str | os.PathLike[str],
    active_root: str | os.PathLike[str],
    unit: str,
    *,
    expected_uid: int | None = None,
) -> list[str]:
    state = ensure_journal_directory(journal_dir, expected_uid=expected_uid)
    active_path = Path(active_root)
    parent = active_path.parent
    entries = sorted(state.iterdir())
    for item in entries:
        if (
            not re.fullmatch(r"[0-9a-f]{32}\.json", item.name)
            or item.is_symlink()
            or not item.is_file()
        ):
            raise ActivationError(f"unexpected object in transaction journal directory: {item.name}")
    journals = entries
    if len(journals) > 1:
        raise ActivationError("multiple pending updater journals require manual recovery")
    results: list[str] = []
    for journal in journals:
        results.append(
            recover_with_service(
                journal, parent, unit, expected_uid=expected_uid,
                expected_active_name=active_path.name
            )
        )
    return results


def _run_checked(
    command: list[str], *, input_text: str | None = None, cwd: Path | None = None
) -> subprocess.CompletedProcess[str]:
    try:
        result = subprocess.run(
            command,
            input=input_text,
            stdin=subprocess.DEVNULL if input_text is None else None,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
            timeout=60,
            cwd=cwd,
            env={"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "HOME": "/root", "LANG": "C.UTF-8"},
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise ActivationError(f"runtime validation command failed: {command[0]}: {exc}") from exc
    if result.returncode != 0:
        raise ActivationError(
            f"runtime validation failed ({' '.join(command)}): {result.stderr.strip()}"
        )
    return result


def _prepare_upstream(upstream: Path, release_tag: str, checksum_file: Path) -> None:
    if not re.fullmatch(r"v[0-9]+(?:\.[0-9]+)+", release_tag):
        raise ActivationError("invalid upstream release tag")
    install_script = upstream / "install_bin.sh"
    if not install_script.is_file() or install_script.is_symlink():
        raise ActivationError("upstream install_bin.sh is missing or unsafe")
    # Validate the extracted tree before executing any upstream helper code.
    build_manifest(upstream, release_tag)
    if not checksum_file.is_file() or checksum_file.is_symlink():
        raise ActivationError("release checksum manifest is missing or unsafe")
    _run_checked(["/bin/sh", str(install_script)], cwd=upstream)
    # The selector script may only create links; verify it did not alter the
    # checksummed upstream binaries before constructing the new ownership manifest.
    _run_checked(["sha256sum", "-c", str(checksum_file)], cwd=upstream.parent)
    for relative in ("nfq2/nfqws2", "ip2net/ip2net", "mdig/mdig"):
        binary = upstream / relative
        if not binary.is_symlink():
            raise ActivationError(f"generated runtime link is missing: {relative}")
        resolved = binary.resolve(strict=True)
        try:
            resolved.relative_to((upstream / "binaries").resolve(strict=True))
        except ValueError as exc:
            raise ActivationError(f"runtime link escapes binaries directory: {relative}") from exc
        if not resolved.is_file() or not os.access(resolved, os.X_OK):
            raise ActivationError(f"runtime binary is not executable: {relative}")
    _run_checked([str(upstream / "nfq2/nfqws2"), "--version"])
    probe = _run_checked([str(upstream / "ip2net/ip2net")], input_text="0.0.0.0\n")
    if not probe.stdout.strip():
        raise ActivationError("ip2net probe returned no output")


def _validate_staged(stage: Path, release_tag: str) -> None:
    manifest = load_manifest(stage)
    if manifest["release_tag"] != release_tag:
        raise ActivationError("staged manifest release tag does not match requested version")
    verify_manifest(stage, manifest)
    for relative in ("nfq2/nfqws2", "ip2net/ip2net", "mdig/mdig"):
        binary = stage / relative
        if not binary.is_symlink() or not binary.is_file() or not os.access(binary, os.X_OK):
            raise ActivationError(f"staged runtime link is missing or not executable: {relative}")
        resolved = binary.resolve(strict=True)
        try:
            resolved.relative_to((stage / "binaries").resolve(strict=True))
        except ValueError as exc:
            raise ActivationError(f"staged runtime link escapes binaries directory: {relative}") from exc
    _run_checked([str(stage / "nfq2/nfqws2"), "--version"])
    probe = _run_checked([str(stage / "ip2net/ip2net")], input_text="0.0.0.0\n")
    if not probe.stdout.strip():
        raise ActivationError("staged ip2net probe returned no output")
    unit = stage / "init.d/systemd/zapret2.service"
    if not unit.is_file() or unit.is_symlink():
        raise ActivationError("staged systemd unit is missing or unsafe")
    content = unit.read_text(encoding="utf-8")
    unit_lines = set(content.splitlines())
    if (
        "ExecStart=/opt/zapret2/init.d/sysv/zapret2 start" not in unit_lines
        or "ExecStop=/opt/zapret2/init.d/sysv/zapret2 stop" not in unit_lines
    ):
        raise ActivationError("staged systemd unit does not match fixed installation paths")
    config = stage / "config"
    if not config.is_file() or config.is_symlink():
        raise ActivationError("preserved configuration is missing or unsafe")


def _remove_partial_stage(stage: Path, parent: Path, expected_uid: int) -> None:
    try:
        info = stage.lstat()
    except FileNotFoundError:
        return
    parent_info = parent.lstat()
    if (
        stage.parent != parent
        or not re.fullmatch(r"\.zapret2control-stage-[0-9a-f]{32}", stage.name)
        or not stat.S_ISDIR(info.st_mode)
        or info.st_uid != expected_uid
        or parent_info.st_uid != expected_uid
        or stat.S_IMODE(parent_info.st_mode) & 0o022
        or info.st_dev != parent_info.st_dev
        or stat.S_IMODE(info.st_mode) & 0o022
        or stat.S_IMODE(info.st_mode) & ~0o777
    ):
        raise ActivationError("partial staging tree is unsafe; manual cleanup required")
    shutil.rmtree(stage)
    fd = os.open(parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def run_update(
    upstream_root: str | os.PathLike[str],
    active_root: str | os.PathLike[str],
    journal_dir: str | os.PathLike[str],
    release_tag: str,
    unit: str,
    checksum_file: str | os.PathLike[str],
    *,
    no_service: bool = False,
    expected_uid: int | None = None,
) -> Path:
    uid = os.geteuid() if expected_uid is None else expected_uid
    upstream, active = Path(upstream_root), Path(active_root)
    state = ensure_journal_directory(journal_dir, expected_uid=uid)
    recover_pending(state, active, unit, expected_uid=uid)
    _prepare_upstream(upstream, release_tag, Path(checksum_file))

    transaction_id = uuid.uuid4().hex
    staged = active.parent / f".zapret2control-stage-{transaction_id}"
    backup = active.parent / f".zapret2control-backup-{transaction_id}"
    journal = state / f"{transaction_id}.json"
    if staged.exists() or staged.is_symlink():
        raise ActivationError("transaction staging destination already exists")
    try:
        build_staging_tree(upstream, active, staged, release_tag)
        _validate_staged(staged, release_tag)
    except Exception as original:
        try:
            _remove_partial_stage(staged, active.parent, uid)
        except Exception as cleanup_error:
            raise ActivationError(
                f"staging failed and partial tree cleanup is incomplete: {cleanup_error}"
            ) from original
        raise
    activate_with_service(
        active, staged, backup, journal, release_tag, unit,
        no_service=no_service, expected_uid=uid,
    )
    return backup


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Transactional Zapret2 Control updater")
    subparsers = parser.add_subparsers(dest="command", required=True)
    recover_parser = subparsers.add_parser("recover")
    recover_parser.add_argument("--base", required=True)
    recover_parser.add_argument("--journal-dir", required=True)
    recover_parser.add_argument("--unit", required=True)
    update_parser = subparsers.add_parser("update")
    update_parser.add_argument("--upstream", required=True)
    update_parser.add_argument("--base", required=True)
    update_parser.add_argument("--journal-dir", required=True)
    update_parser.add_argument("--tag", required=True)
    update_parser.add_argument("--unit", required=True)
    update_parser.add_argument("--checksums", required=True)
    update_parser.add_argument("--no-service", action="store_true")
    args = parser.parse_args(argv)

    try:
        if args.command == "recover":
            results = recover_pending(
                args.journal_dir, Path(args.base), args.unit,
            )
            for result in results:
                print(f"Recovered updater transaction: {result}")
            return 0
        backup = run_update(
            args.upstream, args.base, args.journal_dir, args.tag, args.unit,
            args.checksums, no_service=args.no_service,
        )
        print(f"Updated to {args.tag}; previous installation retained at {backup}")
        return 0
    except (ActivationError, ManifestError, OSError, ValueError) as exc:
        print(f"zapret2control-updater: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
