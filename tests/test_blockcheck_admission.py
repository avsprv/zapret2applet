#!/usr/bin/env python3
"""Admission guard tests for service actions and upstream update serialization."""
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import threading
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "packaging" / "helper"))
from blockcheck_admission import AdmissionError, admission_guard, run_guarded
from blockcheck_manager import BlockcheckOptions
from blockcheck_staging import StagedOptionsStore
from blockcheck_state import StateStore
from blockcheck_status import RECOVERY_UNIT, WORKER_UNIT


class AdmissionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temp.name)
        self.units = self.root / "units.json"
        self.units.write_text(json.dumps({WORKER_UNIT: "inactive", RECOVERY_UNIT: "inactive"}))
        self.commands = self.root / "commands"
        self.systemctl = self.root / "systemctl"
        self.systemctl.write_text("""#!/usr/bin/env python3
import json, pathlib, sys
units = json.loads(pathlib.Path(%r).read_text())
args = sys.argv[1:]
with pathlib.Path(%r).open("a") as stream:
    stream.write(" ".join(args) + "\\n")
if args[0] == "show":
    unit = args[-1]
    if unit not in units:
        sys.exit(4)
    props = ("LoadState", "ActiveState", "SubState", "Result", "ExecMainStatus")
    values = {"LoadState": "loaded", "ActiveState": units[unit],
              "SubState": "running" if units[unit] == "active" else "dead",
              "Result": "success", "ExecMainStatus": "0"}
    for key in props:
        print(key + "=" + values[key])
    sys.exit(0)
sys.exit(0)
""" % (str(self.units), str(self.commands)))
        self.systemctl.chmod(0o700)
        self.state = StateStore(self.root / "state", systemctl=str(self.systemctl))
        self.staged = StagedOptionsStore(self.state, self.root / "run")

    def tearDown(self):
        self.temp.cleanup()

    def test_admission_allows_idle_operation_and_holds_lock(self):
        def operation():
            with self.assertRaisesRegex(Exception, "another Blockcheck lifecycle operation"):
                with self.state.locked(create=False, blocking=False):
                    pass
            return 0
        self.assertEqual(run_guarded(self.state, self.staged, operation), 0)

    def test_active_worker_rejected(self):
        self.units.write_text(json.dumps({WORKER_UNIT: "active", RECOVERY_UNIT: "inactive"}))
        with self.assertRaisesRegex(AdmissionError, "not idle"):
            with admission_guard(self.state, self.staged):
                self.fail("guard admitted active worker")

    def test_active_recovery_rejected(self):
        self.units.write_text(json.dumps({WORKER_UNIT: "inactive", RECOVERY_UNIT: "activating"}))
        with self.assertRaisesRegex(AdmissionError, "not idle"):
            with admission_guard(self.state, self.staged):
                self.fail("guard admitted active recovery")

    def test_failed_worker_without_recovery_marker_does_not_block_service(self):
        self.units.write_text(json.dumps({WORKER_UNIT: "failed", RECOVERY_UNIT: "inactive"}))
        with admission_guard(self.state, self.staged):
            pass

    def test_failed_recovery_unit_remains_fail_closed(self):
        self.units.write_text(json.dumps({WORKER_UNIT: "inactive", RECOVERY_UNIT: "failed"}))
        with self.assertRaisesRegex(AdmissionError, "not idle"):
            with admission_guard(self.state, self.staged):
                self.fail("guard admitted failed recovery unit")

    def test_pending_recovery_marker_rejected(self):
        with self.state.locked(create=True):
            self.state._write_marker({
                "version": 1,
                "operation_id": "12345678-1234-4234-8234-123456789abc",
                "service_was_active": False,
            })
        with self.assertRaisesRegex(AdmissionError, "recovery is pending"):
            with admission_guard(self.state, self.staged):
                self.fail("guard admitted pending recovery")

    def test_staged_request_rejected(self):
        with self.state.locked(create=True):
            self.staged.stage_locked("12345678-1234-4234-8234-123456789abc", BlockcheckOptions())
        with self.assertRaisesRegex(AdmissionError, "request is staged"):
            with admission_guard(self.state, self.staged):
                self.fail("guard admitted staged request")

    def test_lifecycle_lock_contention_rejected(self):
        with self.state.locked(create=True):
            with self.assertRaisesRegex(AdmissionError, "another Blockcheck lifecycle operation"):
                with admission_guard(self.state, self.staged):
                    self.fail("guard admitted lock contention")

    def test_unit_command_executes_while_guard_is_held(self):
        def operation():
            return subprocess.run([str(self.systemctl), "start", "zapret2.service"]).returncode
        self.assertEqual(run_guarded(self.state, self.staged, operation), 0)
        self.assertIn("start zapret2.service", self.commands.read_text())


if __name__ == "__main__":
    unittest.main(verbosity=2)
