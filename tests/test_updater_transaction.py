#!/usr/bin/env python3
"""Regression tests for updater ownership manifest validation."""

import os
from pathlib import Path
import tempfile
import unittest
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "packaging/helper"))

from updater_transaction import (
    MANIFEST_NAME,
    ManifestError,
    build_manifest,
    load_manifest,
    validate_manifest,
    verify_manifest,
    write_manifest,
)


class UpdaterManifestTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="zapret2control-manifest-")
        self.root = Path(self.temp.name) / "tree"
        self.root.mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def put(self, relative, data=b"content", mode=0o644):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        path.chmod(mode)
        return path

    def test_rejects_group_or_world_writable_installation_root(self):
        self.put("docs/changes.txt")
        self.root.chmod(0o777)
        with self.assertRaisesRegex(ManifestError, "group/world-writable"):
            build_manifest(self.root, "v1.2.3")
        with self.assertRaisesRegex(ManifestError, "group/world-writable"):
            write_manifest(self.root, "v1.2.3")

    def test_manifest_tracks_upstream_files_directories_and_relative_links(self):
        self.put("docs/changes.txt", b"release notes")
        self.put("binaries/x86_64/nfqws2", b"binary", 0o755)
        (self.root / "nfq2").mkdir()
        (self.root / "nfq2/nfqws2").symlink_to("../binaries/x86_64/nfqws2")
        self.put("config", b"user config", 0o600)
        self.put("ipset/zapret-hosts-user.txt", b"user hosts")
        self.put("init.d/linux/custom.d/user.sh", b"user hook", 0o700)

        manifest = build_manifest(self.root, "v1.2.3")
        paths = [entry["path"] for entry in manifest["entries"]]
        self.assertIn("docs/changes.txt", paths)
        self.assertIn("nfq2/nfqws2", paths)
        self.assertNotIn("config", paths)
        self.assertNotIn("ipset/zapret-hosts-user.txt", paths)
        self.assertNotIn("init.d/linux/custom.d/user.sh", paths)
        self.assertEqual(verify_manifest(self.root, manifest), manifest)

    def test_write_and_load_manifest_is_atomic_private_and_verifiable(self):
        self.put("docs/changes.txt")
        manifest = write_manifest(self.root, "v1.2.3")
        path = self.root / MANIFEST_NAME
        self.assertEqual(path.stat().st_mode & 0o777, 0o600)
        self.assertEqual(load_manifest(self.root), manifest)

    def test_manifest_detects_modified_file(self):
        self.put("docs/changes.txt", b"original")
        manifest = build_manifest(self.root, "v1.2.3")
        (self.root / "docs/changes.txt").write_bytes(b"modified")
        with self.assertRaisesRegex(ManifestError, "checksum mismatch"):
            verify_manifest(self.root, manifest)

    def test_manifest_detects_mode_change(self):
        self.put("docs/changes.txt", mode=0o644)
        manifest = build_manifest(self.root, "v1.2.3")
        (self.root / "docs/changes.txt").chmod(0o600)
        with self.assertRaisesRegex(ManifestError, "mode mismatch"):
            verify_manifest(self.root, manifest)

    def test_rejects_symlink_in_preserved_config(self):
        (self.root / "config").symlink_to("/etc/passwd")
        with self.assertRaisesRegex(ManifestError, "single-link regular file"):
            build_manifest(self.root, "v1.2.3")

    def test_rejects_symlink_custom_directory(self):
        (self.root / "init.d").mkdir()
        (self.root / "init.d/linux").mkdir()
        (self.root / "init.d/linux/custom.d").symlink_to("/etc")
        with self.assertRaisesRegex(ManifestError, "custom.d must be a real directory"):
            build_manifest(self.root, "v1.2.3")

    def test_rejects_escaping_symlink(self):
        (self.root / "outside").symlink_to("../../etc/passwd")
        with self.assertRaisesRegex(ManifestError, "escapes"):
            build_manifest(self.root, "v1.2.3")

    def test_rejects_reserved_manifest_path_in_upstream_tree(self):
        self.put(MANIFEST_NAME, b"not a manifest", 0o600)
        with self.assertRaisesRegex(ManifestError, "reserved manifest path"):
            build_manifest(self.root, "v1.2.3")

    def test_rejects_symlink_that_escapes_through_untracked_user_data(self):
        user_data_link = self.root / "init.d/linux/custom.d/escape"
        user_data_link.parent.mkdir(parents=True)
        user_data_link.symlink_to("/etc")
        (self.root / "link").symlink_to("init.d/linux/custom.d/escape/passwd")
        with self.assertRaisesRegex(ManifestError, "inside the installation"):
            build_manifest(self.root, "v1.2.3")

    def test_write_rejects_preexisting_reserved_path(self):
        self.put(MANIFEST_NAME, b"upstream supplied this", 0o600)
        with self.assertRaisesRegex(ManifestError, "reserved manifest path"):
            write_manifest(self.root, "v1.2.3")

    def test_rejects_boolean_schema_version(self):
        with self.assertRaisesRegex(ManifestError, "schema version"):
            validate_manifest({"schema_version": True, "release_tag": "v1.2.3", "entries": []})

    def test_rejects_unsafe_modes(self):
        self.put("docs/unsafe.txt", mode=0o666)
        with self.assertRaisesRegex(ManifestError, "unsafe permissions"):
            build_manifest(self.root, "v1.2.3")

    def test_rejects_non_regular_files(self):
        (self.root / "fifo").touch()
        (self.root / "fifo").unlink()
        os.mkfifo(self.root / "fifo")
        with self.assertRaisesRegex(ManifestError, "unsupported filesystem object"):
            build_manifest(self.root, "v1.2.3")

    def test_rejects_path_traversal_and_duplicate_paths(self):
        base = {"schema_version": 1, "release_tag": "v1.2.3", "entries": []}
        for path in ("../escape", "/absolute", "a//b", "a/./b", "a/../b"):
            with self.subTest(path=path):
                item = {"path": path, "type": "directory", "mode": 0o755}
                with self.assertRaises(ManifestError):
                    validate_manifest({**base, "entries": [item]})
        item = {"path": "a", "type": "directory", "mode": 0o755}
        with self.assertRaisesRegex(ManifestError, "duplicate"):
            validate_manifest({**base, "entries": [item, item]})

    def test_rejects_missing_parent_or_conflicting_parent(self):
        child = {"path": "a/b", "type": "file", "mode": 0o644, "sha256": "0" * 64}
        with self.assertRaisesRegex(ManifestError, "parent"):
            validate_manifest({"schema_version": 1, "release_tag": "v1.2.3", "entries": [child]})

    def test_rejects_duplicate_json_keys(self):
        path = self.root / MANIFEST_NAME
        path.write_text(
            '{"schema_version":1,"schema_version":1,"release_tag":"v1.2.3","entries":[]}',
            encoding="utf-8",
        )
        path.chmod(0o600)
        with self.assertRaisesRegex(ManifestError, "duplicate JSON key"):
            load_manifest(self.root)

    def test_rejects_manifest_symlink(self):
        target = self.root / "manifest-target"
        target.write_text('{"schema_version":1}', encoding="utf-8")
        (self.root / MANIFEST_NAME).symlink_to(target.name)
        with self.assertRaises(ManifestError):
            load_manifest(self.root)

    def test_allows_untracked_extra_files_outside_manifest(self):
        self.put("docs/changes.txt")
        manifest = write_manifest(self.root, "v1.2.3")
        self.put("custom-local-note.txt", b"not tracked")
        self.assertEqual(verify_manifest(self.root, manifest), manifest)


if __name__ == "__main__":
    unittest.main()
