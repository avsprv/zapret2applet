#!/usr/bin/env python3
"""Isolated tests for crash-safe Blockcheck recovery state."""
import json
import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import uuid

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "packaging" / "helper"))
from blockcheck_state import StateError, StateStore

class StateTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        self.state = self.root / "state"
        self.fake = self.root / "systemctl"
        self.log = self.root / "calls"
        self.active = self.root / "active"
        self.fake.write_text(
            "#!/bin/sh\nset -eu\n"
            + "printf '%s\\\\n' \"$*\" >> " + repr(str(self.log)) + "\n"
            + "case \"$1\" in\n"
            + "  is-active) [ -e " + repr(str(self.active)) + " ] && exit 0 || exit 3 ;;\n"
            + "  start) : > " + repr(str(self.active)) + " ;;\n"
            + "  *) exit 64 ;;\n"
            + "esac\n"
        )
        self.fake.chmod(0o700)
        self.env = dict(os.environ, CALL_LOG=str(self.log), ACTIVE_FILE=str(self.active))
        self.store = StateStore(self.state, str(self.fake))

    def tearDown(self):
        self.tmp.cleanup()

    def prepare(self, active=True):
        if active:
            self.active.touch()
        with self.store.locked(create=True):
            return self.store.prepare_locked()

    def test_prepare_active_and_inactive(self):
        active = self.prepare(True)
        self.assertTrue(active["service_was_active"])
        self.assertEqual(stat.S_IMODE((self.state / "state.json").stat().st_mode), 0o600)
        self.assertFalse((self.state / "state.json").is_symlink())
        (self.state / "state.json").unlink()
        self.active.unlink()
        inactive = self.prepare(False)
        self.assertFalse(inactive["service_was_active"])

    def test_recovery_restores_active_and_removes_marker(self):
        self.prepare(True)
        with self.store.locked():
            self.store.recover_locked()
        self.assertTrue(self.active.exists())
        self.assertFalse((self.state / "state.json").exists())

    def test_inactive_recovery_never_starts_service(self):
        self.prepare(False)
        with self.store.locked():
            self.store.recover_locked()
        self.assertFalse(self.active.exists())
        self.assertNotIn("start zapret2.service", self.log.read_text())

    def test_recovery_failure_keeps_marker(self):
        self.prepare(True)
        env = dict(self.env, FAIL_START="1")
        # Override the fake executable's inherited environment for this invocation.
        store = StateStore(self.state, self._failing_systemctl(env))
        with store.locked():
            with self.assertRaises(StateError):
                store.recover_locked()
        self.assertTrue((self.state / "state.json").exists())

    def _failing_systemctl(self, env):
        wrapper = self.root / "systemctl-fail"
        wrapper.write_text("#!/bin/sh\n[ \"$1\" = start ] && exit 1\nexec \"" + str(self.fake) + "\" \"$@\"\n")
        wrapper.chmod(0o700)
        return str(wrapper)

    def test_malformed_marker_fails_closed(self):
        self.prepare(True)
        (self.state / "state.json").write_text("{broken")
        with self.store.locked():
            with self.assertRaises(StateError):
                self.store.recover_locked()
        self.assertTrue((self.state / "state.json").exists())
        # A malformed marker must not trigger service state changes.
        self.assertTrue(self.active.exists())

    def test_duplicate_marker_keys_fail_closed(self):
        self.prepare(True)
        (self.state / "state.json").write_text(
            '{"version":1,"operation_id":"00000000-0000-4000-8000-000000000001",'
            '"service_was_active":true,"service_was_active":false}'
        )
        with self.store.locked():
            with self.assertRaises(StateError):
                self.store.recover_locked()
        self.assertTrue(self.active.exists())
        self.assertTrue((self.state / "state.json").exists())

    def test_symlink_and_hardlink_markers_rejected(self):
        self.prepare(True)
        marker = self.state / "state.json"
        marker.unlink()
        target = self.root / "target"
        target.write_text("{}")
        marker.symlink_to(target)
        with self.store.locked():
            with self.assertRaises(StateError):
                self.store.recover_locked()
        marker.unlink()
        marker.write_text("{}")
        os.link(marker, self.root / "linked")
        with self.store.locked():
            with self.assertRaises(StateError):
                self.store.recover_locked()

    def test_wrong_mode_and_wrong_owner_rejected(self):
        self.prepare(True)
        marker = self.state / "state.json"
        os.chmod(marker, 0o640)
        with self.store.locked():
            with self.assertRaises(StateError):
                self.store.recover_locked()
        os.chmod(marker, 0o600)
        if os.geteuid() == 0:
            os.chown(marker, 65534, 65534)
            with self.store.locked():
                with self.assertRaises(StateError):
                    self.store.recover_locked()
            os.chown(marker, 0, 0)
        os.chmod(self.state, 0o750)
        with self.assertRaises(StateError):
            with self.store.locked():
                pass
    def test_recovery_is_idempotent(self):
        self.prepare(False)
        with self.store.locked():
            self.assertTrue(self.store.recover_locked())
        with self.store.locked():
            self.assertFalse(self.store.recover_locked())

    def test_lock_symlink_and_hardlink_rejected(self):
        self.state.mkdir(mode=0o700)
        target = self.root / "outside-lock"
        target.write_text("")
        (self.state / "lock").symlink_to(target)
        with self.assertRaises(StateError):
            with self.store.locked():
                pass

        (self.state / "lock").unlink()
        (self.state / "lock").write_text("")
        os.chmod(self.state / "lock", 0o600)
        os.link(self.state / "lock", self.root / "linked-lock")
        with self.assertRaises(StateError):
            with self.store.locked():
                pass

    def test_lock_wrong_owner_rejected(self):
        if os.geteuid() != 0:
            self.skipTest("requires root to change lock ownership")
        self.state.mkdir(mode=0o700)
        lock = self.state / "lock"
        lock.write_text("")
        os.chmod(lock, 0o600)
        os.chown(lock, 65534, 65534)
        with self.assertRaises(StateError):
            with self.store.locked():
                pass

    def test_state_directory_symlink_rejected(self):
        real_state = self.root / "real-state"
        real_state.mkdir(mode=0o700)
        self.state.symlink_to(real_state, target_is_directory=True)
        with self.assertRaises(StateError):
            with self.store.locked(create=True):
                pass

    def test_ancestor_directory_symlink_rejected(self):
        real_parent = self.root / "real-parent"
        real_parent.mkdir(mode=0o700)
        parent_link = self.root / "parent-link"
        parent_link.symlink_to(real_parent, target_is_directory=True)
        store = StateStore(parent_link / "state", str(self.fake))
        with self.assertRaises(StateError):
            with store.locked(create=True):
                pass
        self.assertFalse((real_parent / "state").exists())

    def test_nonblocking_lock_rejects_active_operation(self):
        self.state.mkdir(mode=0o700)
        contender = StateStore(self.state, str(self.fake))
        with self.store.locked():
            with self.assertRaisesRegex(StateError, "another Blockcheck lifecycle operation is active"):
                with contender.locked(blocking=False):
                    pass

    def test_lock_serializes_threads(self):
        self.store.directory.mkdir(mode=0o700)
        # Reuse one StateStore instance to verify per-thread descriptor ownership.
        entered = threading.Event()
        release = threading.Event()
        def holder():
            with self.store.locked():
                entered.set()
                release.wait(3)
        thread = threading.Thread(target=holder)
        thread.start()
        self.assertTrue(entered.wait(2))
        start = time.monotonic()
        def unlock():
            time.sleep(0.25)
            release.set()
        unlocker = threading.Thread(target=unlock)
        unlocker.start()
        with self.store.locked():
            elapsed = time.monotonic() - start
        thread.join()
        unlocker.join()
        self.assertGreaterEqual(elapsed, 0.20)

if __name__ == "__main__":
    unittest.main(verbosity=2)
