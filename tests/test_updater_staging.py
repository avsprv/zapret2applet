#!/usr/bin/env python3
"""Regression tests for isolated updater staging-tree assembly."""

from pathlib import Path
import tempfile
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "packaging/helper"))

from updater_transaction import ManifestError, write_manifest
from updater_staging import build_staging_tree


class UpdaterStagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="zapret2control-staging-")
        self.base = Path(self.temp.name)
        self.active = self.base / "active"
        self.upstream = self.base / "upstream"
        self.stage = self.base / "staged"
        self.active.mkdir()
        self.upstream.mkdir()

    def tearDown(self):
        self.temp.cleanup()

    @staticmethod
    def put(root, relative, data=b"data", mode=0o644):
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        path.chmod(mode)
        return path

    def test_first_migration_preserves_unknown_and_user_data(self):
        self.put(self.active, "local/notes.txt", b"keep me")
        self.put(self.active, "config", b"user config", 0o600)
        self.put(self.active, "ipset/zapret-hosts-user.txt", b"user hostlist")
        self.put(self.active, "init.d/README.md", b"init notes")
        self.put(self.active, "init.d/linux/custom.d/hook.sh", b"user hook", 0o700)
        self.put(self.upstream, "config", b"upstream config")
        self.put(self.upstream, "docs/new.txt", b"new upstream")

        manifest = build_staging_tree(self.upstream, self.active, self.stage, "v1.2.3")

        self.assertEqual((self.stage / "local/notes.txt").read_bytes(), b"keep me")
        self.assertEqual((self.stage / "config").read_bytes(), b"user config")
        self.assertEqual((self.stage / "ipset/zapret-hosts-user.txt").read_bytes(), b"user hostlist")
        self.assertEqual((self.stage / "init.d/README.md").read_bytes(), b"init notes")
        self.assertEqual((self.stage / "init.d/linux/custom.d/hook.sh").read_bytes(), b"user hook")
        self.assertEqual((self.stage / "docs/new.txt").read_bytes(), b"new upstream")
        self.assertNotIn("config", [entry["path"] for entry in manifest["entries"]])

    def test_manifest_owned_obsolete_files_are_removed_but_new_untracked_files_survive(self):
        self.put(self.active, "docs/obsolete.txt", b"old upstream")
        write_manifest(self.active, "v1.0.0")
        self.put(self.active, "docs/local.txt", b"local addition")
        self.put(self.upstream, "docs/current.txt", b"new upstream")

        build_staging_tree(self.upstream, self.active, self.stage, "v1.1.0")

        self.assertFalse((self.stage / "docs/obsolete.txt").exists())
        self.assertEqual((self.stage / "docs/local.txt").read_bytes(), b"local addition")
        self.assertEqual((self.stage / "docs/current.txt").read_bytes(), b"new upstream")

    def test_tampered_previous_manifest_fails_before_staging(self):
        self.put(self.active, "docs/owned.txt", b"owned")
        write_manifest(self.active, "v1.0.0")
        (self.active / "docs/owned.txt").write_bytes(b"tampered")
        self.put(self.upstream, "docs/current.txt", b"new")
        with self.assertRaisesRegex(ManifestError, "checksum mismatch"):
            build_staging_tree(self.upstream, self.active, self.stage, "v1.1.0")
        self.assertEqual((self.active / "docs/owned.txt").read_bytes(), b"tampered")
        self.assertFalse(self.stage.exists())

    def test_new_upstream_file_wins_unknown_legacy_collision(self):
        self.put(self.active, "shared.txt", b"local stale file")
        self.put(self.upstream, "shared.txt", b"upstream replacement")

        build_staging_tree(self.upstream, self.active, self.stage, "v1.2.3")

        self.assertEqual((self.stage / "shared.txt").read_bytes(), b"upstream replacement")

    def test_untracked_legacy_symlink_fails_closed(self):
        self.put(self.active, "target.txt", b"inside")
        (self.active / "escape").symlink_to("target.txt")
        with self.assertRaisesRegex(ManifestError, "untracked legacy symlink"):
            build_staging_tree(self.upstream, self.active, self.stage, "v1.2.3")

    def test_symlink_in_preserved_user_config_fails_closed(self):
        (self.active / "config").symlink_to("/etc/passwd")
        with self.assertRaises(ManifestError):
            build_staging_tree(self.upstream, self.active, self.stage, "v1.2.3")

    def test_rejects_hardlinked_preserved_config(self):
        self.put(self.active, "config", b"shared config", 0o600)
        (self.active / "config-link").hardlink_to(self.active / "config")
        with self.assertRaisesRegex(ManifestError, "hard-linked regular file"):
            build_staging_tree(self.upstream, self.active, self.stage, "v1.2.3")

    def test_rejects_special_file_in_custom_directory(self):
        import os
        custom = self.active / "init.d/linux/custom.d"
        custom.mkdir(parents=True)
        os.mkfifo(custom / "named-pipe")
        with self.assertRaises(ManifestError):
            build_staging_tree(self.upstream, self.active, self.stage, "v1.2.3")

    def test_existing_staging_destination_is_never_overwritten(self):
        self.stage.mkdir()
        marker = self.stage / "marker"
        marker.write_text("keep", encoding="utf-8")
        with self.assertRaisesRegex(ManifestError, "already exists"):
            build_staging_tree(self.upstream, self.active, self.stage, "v1.2.3")
        self.assertEqual(marker.read_text(encoding="utf-8"), "keep")


if __name__ == "__main__":
    unittest.main()
