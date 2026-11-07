#!/usr/bin/env python3
"""Tests for UUID-bound Blockcheck job status reporting."""
from __future__ import annotations

import os
import tempfile
import unittest
import uuid
from pathlib import Path

from blockcheck_manager import BlockcheckOptions
from blockcheck_staging import StagedOptionsStore
from blockcheck_state import StateStore
from blockcheck_status import get_job_status


class BlockcheckStatusTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.state_dir = root / "state"
        self.runtime_dir = root / "runtime"
        self.state_dir.mkdir(mode=0o700)
        self.runtime_dir.mkdir(mode=0o700)
        self.states = {
            "zapret2applet-blockcheck.service": ("loaded", "inactive", "dead", "success", "0"),
            "zapret2applet-blockcheck-recovery.service": ("loaded", "inactive", "dead", "success", "0"),
        }
        self.systemctl = root / "systemctl"
        self.systemctl.write_text("""#!/usr/bin/env python3
import os, sys
states = {
    "zapret2applet-blockcheck.service": ("loaded", "inactive", "dead", "success", "0"),
    "zapret2applet-blockcheck-recovery.service": ("loaded", "inactive", "dead", "success", "0"),
}
try:
    with open(os.environ["QZAPRET_STATUS_FIXTURE"], encoding="ascii") as f:
        for line in f:
            unit, values = line.rstrip("\n").split("=", 1)
            states[unit] = tuple(values.split(","))
except (KeyError, OSError):
    pass
args = sys.argv[1:]
if args and args[0] == "is-active":
    sys.exit(3)
unit = args[-1]
if unit not in states:
    sys.exit(1)
keys = ("LoadState", "ActiveState", "SubState", "Result", "ExecMainStatus")
for key, value in zip(keys, states[unit]):
    print(f"{key}={value}")
""", encoding="utf-8")
        self.systemctl.chmod(0o755)
        self.fixture = root / "fixture"
        self._write_fixture()
        os.environ["QZAPRET_STATUS_FIXTURE"] = str(self.fixture)
        self.state = StateStore(self.state_dir, str(self.systemctl))
        self.staged = StagedOptionsStore(self.state, self.runtime_dir)
        self.operation_id = str(uuid.uuid4())

    def tearDown(self):
        os.environ.pop("QZAPRET_STATUS_FIXTURE", None)
        self.tmp.cleanup()

    def _write_fixture(self):
        lines = []
        for unit, values in self.states.items():
            lines.append(unit + "=" + ",".join(values))
        self.fixture.write_text("\n".join(lines) + "\n", encoding="ascii")

    def _status(self):
        return get_job_status(self.state, self.staged, self.operation_id)

    def test_pending_stage_is_queued(self):
        with self.state.locked():
            self.staged.stage_locked(self.operation_id, BlockcheckOptions())
        self._write_fixture()
        self.assertEqual(self._status()["phase"], "queued")

    def test_active_worker_with_staged_options_is_running(self):
        with self.state.locked():
            self.staged.stage_locked(self.operation_id, BlockcheckOptions())
        self.states["zapret2applet-blockcheck.service"] = ("loaded", "active", "start", "success", "0")
        self._write_fixture()
        self.assertEqual(self._status()["phase"], "running")

    def test_durable_marker_waits_for_recovery_verification(self):
        with self.state.locked():
            self.state.prepare_locked(self.operation_id)
        self._write_fixture()
        status = self._status()
        self.assertEqual(status["phase"], "recovery_pending")
        self.assertTrue(status["recovery_marker"])

    def test_active_recovery_is_reported(self):
        with self.state.locked():
            self.state.prepare_locked(self.operation_id)
        self.states["zapret2applet-blockcheck-recovery.service"] = ("loaded", "active", "start", "success", "0")
        self._write_fixture()
        self.assertEqual(self._status()["phase"], "recovering")

    def test_failed_recovery_retains_failed_state(self):
        with self.state.locked():
            self.state.prepare_locked(self.operation_id)
        self.states["zapret2applet-blockcheck-recovery.service"] = ("loaded", "failed", "failed", "exit-code", "1")
        self._write_fixture()
        self.assertEqual(self._status()["phase"], "recovery_failed")

    def test_completed_requires_both_units_successful(self):
        self._write_fixture()
        self.assertEqual(self._status()["phase"], "completed")
        self.states["zapret2applet-blockcheck.service"] = ("loaded", "failed", "failed", "exit-code", "1")
        self._write_fixture()
        self.assertEqual(self._status()["phase"], "failed")

    def test_different_operation_id_is_stale(self):
        with self.state.locked():
            self.staged.stage_locked(str(uuid.uuid4()), BlockcheckOptions())
        self._write_fixture()
        self.assertEqual(self._status()["phase"], "stale")


if __name__ == "__main__":
    unittest.main()
