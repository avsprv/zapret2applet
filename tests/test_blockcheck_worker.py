#!/usr/bin/env python3
"""Isolated tests for the systemd-owned Blockcheck worker."""
import os
import pathlib
import stat
import sys
import tempfile
import textwrap
import unittest
import uuid

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "packaging" / "helper"))
from blockcheck_manager import BlockcheckOptions
from blockcheck_staging import StagedOptionsStore, StagingError
from blockcheck_state import StateStore
from blockcheck_worker import BlockcheckWorker, WorkerError


class BlockcheckWorkerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temp.name)
        self.state_file = self.root / "service-state"
        self.state_file.write_text("active")
        self.systemctl = self.root / "systemctl"
        self.systemctl.write_text(textwrap.dedent("""\
            #!/usr/bin/env python3
            import pathlib, sys
            state = pathlib.Path(%r)
            command = sys.argv[1]
            if command == "is-active":
                sys.exit(0 if state.read_text() == "active" else 3)
            if command == "stop":
                state.write_text("inactive")
                sys.exit(0)
            if command == "start":
                state.write_text("active")
                sys.exit(0)
            sys.exit(1)
        """) % str(self.state_file))
        self.systemctl.chmod(0o700)
        self.state = StateStore(self.root / "state", systemctl=str(self.systemctl))
        self.staged = StagedOptionsStore(self.state, self.root / "run")
        self.operation_id = str(uuid.uuid4())
        self.script = self.root / "blockcheck2.sh"
        self.script.write_text("#!/bin/sh\nexit 0\n")
        self.script.chmod(0o700)
        self.worker = BlockcheckWorker(self.state, self.staged, self.script)

    def tearDown(self):
        self.temp.cleanup()

    def submit(self, options=None):
        with self.state.locked(create=True):
            self.staged.stage_locked(self.operation_id, options or BlockcheckOptions())

    def recover(self):
        with self.state.locked():
            return self.state.recover_locked()

    def test_active_service_is_stopped_and_recovery_marker_retained(self):
        self.submit()
        self.assertEqual(self.worker.run(self.operation_id), 0)
        self.assertEqual(self.state_file.read_text(), "inactive")
        self.assertTrue((self.root / "state" / "state.json").exists())
        self.assertTrue(self.recover())
        self.assertEqual(self.state_file.read_text(), "active")
        self.assertFalse((self.root / "state" / "state.json").exists())

    def test_inactive_service_is_never_started(self):
        self.state_file.write_text("inactive")
        self.submit()
        self.assertEqual(self.worker.run(self.operation_id), 0)
        self.assertEqual(self.state_file.read_text(), "inactive")
        self.assertTrue(self.recover())
        self.assertEqual(self.state_file.read_text(), "inactive")

    def test_upstream_failure_keeps_recovery_marker(self):
        self.script.write_text("#!/bin/sh\nexit 17\n")
        self.script.chmod(0o700)
        self.submit()
        self.assertEqual(self.worker.run(self.operation_id), 17)
        self.assertTrue((self.root / "state" / "state.json").exists())
        self.recover()
        self.assertEqual(self.state_file.read_text(), "active")

    def test_missing_staged_request_does_not_stop_service(self):
        with self.assertRaises(StagingError):
            self.worker.run(self.operation_id)
        self.assertEqual(self.state_file.read_text(), "active")
        self.assertFalse((self.root / "state" / "state.json").exists())

    def test_symlink_upstream_script_rejected_before_state_change(self):
        self.submit()
        link = self.root / "script-link"
        link.symlink_to(self.script)
        worker = BlockcheckWorker(self.state, self.staged, link)
        with self.assertRaises(WorkerError):
            worker.run(self.operation_id)
        self.assertEqual(self.state_file.read_text(), "active")
        self.assertFalse((self.root / "state" / "state.json").exists())

    def test_group_writable_upstream_script_rejected(self):
        self.submit()
        self.script.chmod(0o720)
        with self.assertRaises(WorkerError):
            self.worker.run(self.operation_id)
        self.assertEqual(self.state_file.read_text(), "active")

    def test_operation_id_mismatch_does_not_stop_service(self):
        self.submit()
        with self.assertRaises(Exception):
            self.worker.run(str(uuid.uuid4()))
        self.assertEqual(self.state_file.read_text(), "active")
        self.assertFalse((self.root / "state" / "state.json").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
