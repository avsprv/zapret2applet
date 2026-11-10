#!/usr/bin/env python3
"""Tests for the read-only Blockcheck startup recovery probe."""
from __future__ import annotations

import json
import os
import tempfile
import threading
import unittest
import uuid
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "packaging" / "helper"))

from blockcheck_manager import BlockcheckOptions
from blockcheck_staging import StagedOptionsStore
from blockcheck_state import StateStore
from blockcheck_probe import recovery_probe


class BlockcheckProbeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.state_dir = root / "state"
        self.runtime_dir = root / "runtime"
        self.state_dir.mkdir(mode=0o700)
        self.runtime_dir.mkdir(mode=0o700)
        self.states = {
            "zapret2control-blockcheck.service": ("loaded", "inactive", "dead", "success", "0"),
            "zapret2control-blockcheck-recovery.service": ("loaded", "inactive", "dead", "success", "0"),
            "zapret2.service": ("loaded", "inactive", "dead", "success", "0"),
        }
        self.systemctl = root / "systemctl"
        self.systemctl.write_text("""#!/usr/bin/env python3
import os, sys
states = {
    "zapret2control-blockcheck.service": ("loaded", "inactive", "dead", "success", "0"),
    "zapret2control-blockcheck-recovery.service": ("loaded", "inactive", "dead", "success", "0"),
    "zapret2.service": ("loaded", "inactive", "dead", "success", "0"),
}
try:
    with open(os.environ["QZAPRET_PROBE_FIXTURE"], encoding="ascii") as f:
        for line in f.read().splitlines():
            unit, values = line.split("=", 1)
            states[unit] = tuple(values.split(","))
except (KeyError, OSError):
    pass
args = sys.argv[1:]
if args and args[0] == "is-active":
    unit = args[-1]
    sys.exit(0 if states.get(unit, ("", "inactive"))[1] == "active" else 3)
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
        os.environ["QZAPRET_PROBE_FIXTURE"] = str(self.fixture)
        self.state = StateStore(self.state_dir, str(self.systemctl))
        self.staged = StagedOptionsStore(self.state, self.runtime_dir)
        with self.state.locked():
            pass

    def tearDown(self):
        os.environ.pop("QZAPRET_PROBE_FIXTURE", None)
        self.tmp.cleanup()

    def _write_fixture(self):
        self.fixture.write_text("\n".join(
            unit + "=" + ",".join(values) for unit, values in self.states.items()
        ) + "\n", encoding="ascii")

    def _probe(self):
        return recovery_probe(self.state, self.staged)

    def test_safe_state(self):
        self.assertEqual(self._probe()["state"], "safe")

    def test_missing_state_and_runtime_directories_are_safe_when_units_idle(self):
        (self.state_dir / "lock").unlink()
        self.state_dir.rmdir()
        self.runtime_dir.rmdir()
        self.assertEqual(self._probe()["state"], "safe")

    def test_probe_does_not_create_missing_lock_file(self):
        (self.state_dir / "lock").unlink()
        self.assertEqual(self._probe()["state"], "unknown")
        self.assertFalse((self.state_dir / "lock").exists())

    def test_durable_marker_requires_recovery(self):
        operation_id = str(uuid.uuid4())
        with self.state.locked():
            self.state.prepare_locked(operation_id)
        result = self._probe()
        self.assertEqual(result["state"], "recovery_required")
        self.assertEqual(result["operation_id"], operation_id)

    def test_staged_request_is_busy(self):
        with self.state.locked():
            self.staged.stage_locked(str(uuid.uuid4()), BlockcheckOptions())
        self.assertEqual(self._probe()["state"], "busy")

    def test_active_worker_is_busy(self):
        self.states["zapret2control-blockcheck.service"] = ("loaded", "active", "running", "success", "0")
        self._write_fixture()
        self.assertEqual(self._probe()["state"], "busy")

    def test_lifecycle_lock_contention_is_busy(self):
        acquired = threading.Event()
        release = threading.Event()
        def holder():
            with self.state.locked():
                acquired.set()
                release.wait(3)
        thread = threading.Thread(target=holder)
        thread.start()
        try:
            self.assertTrue(acquired.wait(2))
            self.assertEqual(self._probe()["state"], "busy")
        finally:
            release.set()
            thread.join(3)

    def test_malformed_marker_fails_closed(self):
        with self.state.locked():
            fd = os.open("state.json", os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600,
                         dir_fd=self.state._require_lock())
            os.write(fd, b"{not-json}\n")
            os.close(fd)
        self.assertEqual(self._probe()["state"], "unknown")

    def test_unsafe_runtime_directory_fails_closed(self):
        self.runtime_dir.chmod(0o755)
        self.assertEqual(self._probe()["state"], "unknown")


if __name__ == "__main__":
    unittest.main()
