#!/usr/bin/env python3
"""Isolated tests for Blockcheck submission and systemd admission control."""
import json
import pathlib
import sys
import tempfile
import textwrap
import unittest
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "packaging" / "helper"))
from blockcheck_manager import BlockcheckOptions
from blockcheck_staging import StagedOptionsStore, StagingError
from blockcheck_state import StateStore
from blockcheck_submit import BlockcheckSubmitter, SubmitError, WORKER_UNIT, RECOVERY_UNIT

class SubmitTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temp.name)
        self.units = self.root / "units.json"
        self.units.write_text(json.dumps({WORKER_UNIT: "inactive", RECOVERY_UNIT: "inactive"}))
        self.commands = self.root / "commands"
        self.fail_start = self.root / "fail-start"
        self.systemctl = self.root / "systemctl"
        script = """#!/usr/bin/env python3
import json, pathlib, sys
units_file = pathlib.Path(%r)
commands_file = pathlib.Path(%r)
fail_start = pathlib.Path(%r)
args = sys.argv[1:]
with commands_file.open("a") as out:
    out.write(" ".join(args) + "\\n")
units = json.loads(units_file.read_text())
if args[0] == "show":
    unit = args[-1]
    print(units.get(unit, ""))
    sys.exit(0 if unit in units else 4)
if args[0] == "start" and "--no-block" in args:
    sys.exit(9 if fail_start.exists() else 0)
sys.exit(1)
""" % (str(self.units), str(self.commands), str(self.fail_start))
        self.systemctl.write_text(script)
        self.systemctl.chmod(0o700)
        self.state = StateStore(self.root / "state", systemctl=str(self.systemctl))
        self.staged = StagedOptionsStore(self.state, self.root / "run")
        self.submitter = BlockcheckSubmitter(self.state, self.staged)

    def tearDown(self):
        self.temp.cleanup()

    def test_submit_stages_and_queues_one_operation(self):
        operation_id = self.submitter.submit(BlockcheckOptions(domains=("example.org",)))
        with self.state.locked():
            self.assertEqual(self.staged.read_locked(operation_id), BlockcheckOptions(domains=("example.org",)))
        self.assertIn("start --no-block " + WORKER_UNIT, self.commands.read_text())

    def test_active_worker_rejected(self):
        self.units.write_text(json.dumps({WORKER_UNIT: "activating", RECOVERY_UNIT: "inactive"}))
        with self.assertRaisesRegex(SubmitError, "not idle"):
            self.submitter.submit(BlockcheckOptions())
        self.assertFalse((self.root / "run" / "options.json").exists())

    def test_active_recovery_rejected(self):
        self.units.write_text(json.dumps({WORKER_UNIT: "inactive", RECOVERY_UNIT: "active"}))
        with self.assertRaisesRegex(SubmitError, "not idle"):
            self.submitter.submit(BlockcheckOptions())
        self.assertFalse((self.root / "run" / "options.json").exists())

    def test_pending_marker_rejected(self):
        with self.state.locked(create=True):
            self.state._write_marker({
                "version": 1,
                "operation_id": "12345678-1234-4234-8234-123456789abc",
                "service_was_active": False,
            })
        with self.assertRaisesRegex(SubmitError, "recovery is still pending"):
            self.submitter.submit(BlockcheckOptions())

    def test_failed_queue_discards_staged_request(self):
        self.fail_start.touch()
        with self.assertRaises(SubmitError):
            self.submitter.submit(BlockcheckOptions())
        self.assertFalse((self.root / "run" / "options.json").exists())

    def test_second_submit_rejected_while_request_is_pending(self):
        self.submitter.submit(BlockcheckOptions())
        with self.assertRaisesRegex(SubmitError, "already pending"):
            self.submitter.submit(BlockcheckOptions())

if __name__ == "__main__":
    unittest.main(verbosity=2)
