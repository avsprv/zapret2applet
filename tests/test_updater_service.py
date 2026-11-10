#!/usr/bin/env python3
"""Regression tests for systemd-aware updater transaction orchestration."""

from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest
import uuid
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "packaging/helper"))

from updater_activation import ActivationError
from updater_service import activate_with_service, recover_with_service


class UpdaterServiceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="zapret2control-service-")
        self.base = Path(self.temp.name)
        self.active = self.base / "active"
        self.transaction_id = uuid.uuid4().hex
        self.stage = self.base / f".zapret2control-stage-{self.transaction_id}"
        self.backup = self.base / f".zapret2control-backup-{self.transaction_id}"
        self.journal_dir = self.base / "journal"
        self.journal_dir.mkdir(mode=0o700)
        self.active.mkdir(mode=0o755)
        self.stage.mkdir(mode=0o755)
        (self.active / "old.txt").write_text("old", encoding="utf-8")
        (self.stage / "new.txt").write_text("new", encoding="utf-8")
        self.journal = self.journal_dir / f"{self.transaction_id}.json"
        self.state = "active"
        self.fail_start = False
        self.calls = []

    def tearDown(self):
        self.temp.cleanup()

    def fake_systemctl(self, arguments):
        self.calls.append(tuple(arguments))
        if arguments[0] == "show":
            return subprocess.CompletedProcess(arguments, 0, self.state + "\n", "")
        if arguments[0] == "stop":
            self.state = "inactive"
            return subprocess.CompletedProcess(arguments, 0, "", "")
        if arguments[0] == "start":
            if self.fail_start:
                return subprocess.CompletedProcess(arguments, 1, "", "simulated start failure")
            self.state = "active"
            return subprocess.CompletedProcess(arguments, 0, "", "")
        return subprocess.CompletedProcess(arguments, 1, "", "unsupported command")

    def test_active_service_is_stopped_switched_and_restarted(self):
        with patch("updater_service._systemctl", side_effect=self.fake_systemctl):
            activate_with_service(
                self.active, self.stage, self.backup, self.journal,
                "v1.2.3", "zapret2.service",
            )
        self.assertEqual(self.state, "active")
        self.assertEqual((self.active / "new.txt").read_text(encoding="utf-8"), "new")
        self.assertEqual((self.backup / "old.txt").read_text(encoding="utf-8"), "old")
        self.assertFalse(self.journal.exists())
        self.assertEqual([call[0] for call in self.calls], ["show", "stop", "show", "start", "show"])

    def test_failed_restart_keeps_journal_and_recovery_restarts_service(self):
        self.fail_start = True
        with patch("updater_service._systemctl", side_effect=self.fake_systemctl):
            with self.assertRaisesRegex(ActivationError, "simulated start failure"):
                activate_with_service(
                    self.active, self.stage, self.backup, self.journal,
                    "v1.2.3", "zapret2.service",
                )
        self.assertTrue(self.journal.exists())
        self.assertTrue(self.backup.exists())
        self.assertEqual(self.state, "inactive")
        self.fail_start = False
        with patch("updater_service._systemctl", side_effect=self.fake_systemctl):
            result = recover_with_service(self.journal, self.base, "zapret2.service")
        self.assertEqual(result, "completed_after_backup_rename")
        self.assertEqual(self.state, "active")
        self.assertFalse(self.journal.exists())

    def test_transitional_service_state_blocks_before_tree_mutation(self):
        self.state = "activating"
        with patch("updater_service._systemctl", side_effect=self.fake_systemctl):
            with self.assertRaisesRegex(ActivationError, "unsafe state"):
                activate_with_service(
                    self.active, self.stage, self.backup, self.journal,
                    "v1.2.3", "zapret2.service",
                )
        self.assertTrue(self.active.exists())
        self.assertTrue(self.stage.exists())
        self.assertFalse(self.journal.exists())

    def test_no_service_mode_does_not_call_systemctl(self):
        with patch("updater_service._systemctl", side_effect=AssertionError("must not call systemctl")):
            activate_with_service(
                self.active, self.stage, self.backup, self.journal,
                "v1.2.3", "zapret2.service", no_service=True,
            )
        self.assertFalse(self.journal.exists())


if __name__ == "__main__":
    unittest.main()
