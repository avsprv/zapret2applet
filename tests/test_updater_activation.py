#!/usr/bin/env python3
"""Regression tests for atomic updater activation and journal recovery."""

from pathlib import Path
import os
import sys
import tempfile
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "packaging/helper"))

import updater_activation as activation
from updater_activation import ActivationError, activate_staged_tree, finalize_transaction, recover_transaction


class UpdaterActivationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="zapret2control-activation-")
        self.base = Path(self.temp.name)
        self.active = self.base / "active"
        self.stage = self.base / "stage"
        self.backup = self.base / "backup"
        self.journal_dir = self.base / "journal"
        self.journal_dir.mkdir(mode=0o700)
        self.active.mkdir(mode=0o755)
        self.stage.mkdir(mode=0o755)
        (self.active / "old.txt").write_text("old", encoding="utf-8")
        (self.stage / "new.txt").write_text("new", encoding="utf-8")
        self.journal = self.journal_dir / f"{uuid.uuid4().hex}.json"

    def tearDown(self):
        self.temp.cleanup()

    def prepare_journal(self):
        active_info = self.active.stat()
        stage_info = self.stage.stat()
        parent_info = self.base.stat()
        document = activation._journal_document(
            self.journal.stem, "v1.2.3", self.active.name, self.stage.name,
            self.backup.name, parent_info.st_dev,
            (active_info.st_dev, active_info.st_ino),
            (stage_info.st_dev, stage_info.st_ino), False,
        )
        activation._write_journal(self.journal, document, os.geteuid())
        return document

    def test_atomic_exchange_retains_previous_tree_as_backup(self):
        activate_staged_tree(
            self.active, self.stage, self.backup, self.journal,
            "v1.2.3", False,
        )
        self.assertEqual((self.active / "new.txt").read_text(encoding="utf-8"), "new")
        self.assertEqual((self.backup / "old.txt").read_text(encoding="utf-8"), "old")
        self.assertFalse(self.stage.exists())
        self.assertTrue(self.journal.exists())
        finalize_transaction(self.journal)
        self.assertFalse(self.journal.exists())

    def test_recovery_discards_only_recorded_unactivated_stage(self):
        self.prepare_journal()
        result = recover_transaction(self.journal, self.base)
        self.assertEqual(result, "discarded_unactivated_stage")
        self.assertEqual((self.active / "old.txt").read_text(encoding="utf-8"), "old")
        self.assertFalse(self.stage.exists())
        self.assertTrue(self.journal.exists())
        finalize_transaction(self.journal)
        self.assertFalse(self.journal.exists())

    def test_recovery_finishes_backup_rename_after_exchange(self):
        self.prepare_journal()
        activation._renameat2(self.active, self.stage, activation.RENAME_EXCHANGE)
        result = recover_transaction(self.journal, self.base)
        self.assertEqual(result, "completed_backup_rename")
        self.assertEqual((self.active / "new.txt").read_text(encoding="utf-8"), "new")
        self.assertEqual((self.backup / "old.txt").read_text(encoding="utf-8"), "old")
        self.assertFalse(self.stage.exists())
        self.assertTrue(self.journal.exists())
        finalize_transaction(self.journal)
        self.assertFalse(self.journal.exists())

    def test_recovery_refuses_unexpected_directory_identity(self):
        self.prepare_journal()
        (self.stage / "new.txt").unlink()
        self.stage.rmdir()
        self.stage.mkdir()
        marker = self.stage / "untrusted.txt"
        marker.write_text("leave", encoding="utf-8")
        with self.assertRaisesRegex(ActivationError, "manual recovery required"):
            recover_transaction(self.journal, self.base)
        self.assertEqual(marker.read_text(encoding="utf-8"), "leave")
        self.assertTrue(self.journal.exists())

    def test_existing_backup_blocks_activation_without_touching_trees(self):
        self.backup.mkdir()
        with self.assertRaisesRegex(ActivationError, "already exists"):
            activate_staged_tree(
                self.active, self.stage, self.backup, self.journal,
                "v1.2.3", False,
            )
        self.assertEqual((self.active / "old.txt").read_text(encoding="utf-8"), "old")
        self.assertEqual((self.stage / "new.txt").read_text(encoding="utf-8"), "new")
        self.assertFalse(self.journal.exists())

    def test_exchange_failure_leaves_journal_and_both_trees_for_recovery(self):
        original = activation._renameat2

        def fail_exchange(source, destination, flags):
            if flags == activation.RENAME_EXCHANGE:
                raise ActivationError("simulated unsupported exchange")
            return original(source, destination, flags)

        activation._renameat2 = fail_exchange
        try:
            with self.assertRaisesRegex(ActivationError, "simulated unsupported"):
                activate_staged_tree(
                    self.active, self.stage, self.backup, self.journal,
                    "v1.2.3", False,
                )
        finally:
            activation._renameat2 = original
        self.assertTrue(self.journal.exists())
        self.assertEqual((self.active / "old.txt").read_text(encoding="utf-8"), "old")
        self.assertEqual((self.stage / "new.txt").read_text(encoding="utf-8"), "new")
        self.assertEqual(
            recover_transaction(self.journal, self.base),
            "discarded_unactivated_stage",
        )
        finalize_transaction(self.journal)

    def test_duplicate_journal_keys_are_rejected(self):
        self.journal.write_text(
            '{"schema_version":1,"schema_version":1}', encoding="utf-8"
        )
        self.journal.chmod(0o600)
        with self.assertRaisesRegex(ActivationError, "duplicate transaction journal key"):
            recover_transaction(self.journal, self.base)

    def test_symlinked_journal_is_rejected(self):
        target = self.journal_dir / "target"
        target.write_text("{}", encoding="utf-8")
        self.journal.symlink_to(target.name)
        with self.assertRaises(ActivationError):
            recover_transaction(self.journal, self.base)


if __name__ == "__main__":
    unittest.main()
