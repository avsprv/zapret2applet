#!/usr/bin/env python3
"""Regression tests for UUID-bound Blockcheck cancellation."""
import pathlib
import sys
import tempfile
import unittest
import uuid
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "packaging" / "helper"))
from blockcheck_cancel import cancel_operation, CancelError, WORKER_UNIT
from blockcheck_manager import BlockcheckOptions
from blockcheck_staging import StagedOptionsStore
from blockcheck_state import StateStore

class CancelTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temp.name)
        self.state_dir = self.root / "state"
        self.state_dir.mkdir(mode=0o700)
        self.runtime_dir = self.root / "runtime"
        self.runtime_dir.mkdir(mode=0o700)
        self.calls = self.root / "systemctl.calls"
        self.systemctl = self.root / "systemctl"
        self.systemctl.write_text(
            "#!/usr/bin/env python3\n"
            "import pathlib, sys\n"
            f"pathlib.Path({str(self.calls)!r}).open('a').write(' '.join(sys.argv[1:]) + '\\n')\n"
            "sys.exit(0)\n", encoding="utf-8")
        self.systemctl.chmod(0o700)
        self.state = StateStore(self.state_dir, str(self.systemctl))
        self.staged = StagedOptionsStore(self.state, self.runtime_dir)
        self.operation_a = str(uuid.uuid4())
        self.operation_b = str(uuid.uuid4())

    def tearDown(self):
        self.temp.cleanup()

    def stage(self, operation_id):
        with self.state.queue_locked(create=True):
            with self.state.locked(create=True):
                self.staged.stage_locked(operation_id, BlockcheckOptions())

    def marker(self, operation_id):
        with self.state.queue_locked(create=True):
            with self.state.locked(create=True):
                self.state._write_marker({
                    "version": 1, "operation_id": operation_id,
                    "service_was_active": False,
                })

    def calls_text(self):
        return self.calls.read_text() if self.calls.exists() else ""

    def test_matching_staged_operation_is_cancelled(self):
        self.stage(self.operation_a)
        self.assertTrue(cancel_operation(self.state, self.staged, self.operation_a))
        self.assertIn(f"stop --no-block {WORKER_UNIT}", self.calls_text())

    def test_matching_recovery_marker_is_cancelled(self):
        self.marker(self.operation_a)
        self.assertTrue(cancel_operation(self.state, self.staged, self.operation_a))
        self.assertIn(f"stop --no-block {WORKER_UNIT}", self.calls_text())

    def test_mismatched_recovery_marker_is_not_cancelled(self):
        self.marker(self.operation_b)
        self.assertFalse(cancel_operation(self.state, self.staged, self.operation_a))
        self.assertEqual(self.calls_text(), "")

    def test_delayed_cancel_cannot_stop_replacement_operation(self):
        self.stage(self.operation_b)
        self.assertFalse(cancel_operation(self.state, self.staged, self.operation_a))
        self.assertEqual(self.calls_text(), "")
        with self.state.locked():
            self.assertEqual(self.staged.read_locked(self.operation_b), BlockcheckOptions())

    def test_conflicting_marker_and_staged_ids_fail_closed(self):
        self.marker(self.operation_a)
        with self.state.locked():
            self.staged.stage_locked(self.operation_b, BlockcheckOptions())
        with self.assertRaisesRegex(CancelError, "disagree"):
            cancel_operation(self.state, self.staged, self.operation_a)
        self.assertEqual(self.calls_text(), "")

    def test_queue_lock_serializes_cancel_against_replacement_submit(self):
        import threading
        import time
        cancel_started = threading.Event()
        cancel_finished = threading.Event()
        result = []

        def delayed_cancel():
            cancel_started.set()
            result.append(cancel_operation(self.state, self.staged, self.operation_a))
            cancel_finished.set()

        with self.state.queue_locked(create=True):
            with self.state.locked(create=True):
                self.staged.stage_locked(self.operation_b, BlockcheckOptions())
            thread = threading.Thread(target=delayed_cancel)
            thread.start()
            self.assertTrue(cancel_started.wait(2))
            time.sleep(0.1)
            self.assertFalse(cancel_finished.is_set())
        thread.join(2)
        self.assertTrue(cancel_finished.is_set())
        self.assertEqual(result, [False])
        self.assertEqual(self.calls_text(), "")

    def test_invalid_uuid_is_rejected_before_systemd(self):
        with self.assertRaises(CancelError):
            cancel_operation(self.state, self.staged, "not-a-uuid")
        self.assertEqual(self.calls_text(), "")

if __name__ == "__main__":
    unittest.main(verbosity=2)
