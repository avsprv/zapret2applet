#!/usr/bin/env python3
"""Unit tests for secure one-shot Blockcheck option staging."""
import os
import pathlib
import sys
import tempfile
import unittest
import uuid
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "packaging" / "helper"))
from blockcheck_manager import BlockcheckOptions
from blockcheck_state import StateStore
from blockcheck_staging import StagedOptionsStore, StagingError

class StagedOptionsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temp.name)
        self.state = StateStore(self.root / "state")
        self.store = StagedOptionsStore(self.state, self.root / "run")
        self.operation_id = str(uuid.uuid4())
    def tearDown(self):
        self.temp.cleanup()
    def test_round_trip_and_one_shot_consumption(self):
        options = BlockcheckOptions(domains=("example.org",), ipvs="46", repeats=3)
        with self.state.locked(create=True):
            self.store.stage_locked(self.operation_id, options)
            self.assertEqual(self.store.read_locked(self.operation_id), options)
            self.store.consume_locked(self.operation_id)
            with self.assertRaises(StagingError):
                self.store.read_locked(self.operation_id)
    def test_requires_lifecycle_lock(self):
        with self.assertRaises(StagingError):
            self.store.stage_locked(self.operation_id, BlockcheckOptions())
    def test_pending_request_cannot_be_overwritten(self):
        with self.state.locked(create=True):
            self.store.stage_locked(self.operation_id, BlockcheckOptions())
            with self.assertRaisesRegex(StagingError, "already pending"):
                self.store.stage_locked(str(uuid.uuid4()), BlockcheckOptions())
    def test_operation_id_mismatch_rejected(self):
        with self.state.locked(create=True):
            self.store.stage_locked(self.operation_id, BlockcheckOptions())
            with self.assertRaisesRegex(StagingError, "mismatch"):
                self.store.read_locked(str(uuid.uuid4()))
    def test_symlink_staged_file_rejected(self):
        with self.state.locked(create=True):
            runtime = self.root / "run"
            runtime.mkdir(mode=0o700)
            (runtime / "options.json").symlink_to(self.root / "target")
            with self.assertRaises(StagingError):
                self.store.read_locked(self.operation_id)
    def test_hardlinked_staged_file_rejected(self):
        with self.state.locked(create=True):
            runtime = self.root / "run"
            runtime.mkdir(mode=0o700)
            source = runtime / "source"
            source.write_bytes(b"{}")
            source.chmod(0o600)
            os.link(source, runtime / "options.json")
            with self.assertRaisesRegex(StagingError, "link count"):
                self.store.read_locked(self.operation_id)
    def test_wrong_mode_staged_file_rejected(self):
        with self.state.locked(create=True):
            runtime = self.root / "run"
            runtime.mkdir(mode=0o700)
            target = runtime / "options.json"
            target.write_bytes(b"{}")
            target.chmod(0o644)
            with self.assertRaises(StagingError):
                self.store.read_locked(self.operation_id)
    def test_corrupt_document_rejected(self):
        with self.state.locked(create=True):
            runtime = self.root / "run"
            runtime.mkdir(mode=0o700)
            target = runtime / "options.json"
            target.write_text('{"version":1,"operation_id":"bad","options":{}}')
            target.chmod(0o600)
            with self.assertRaises(StagingError):
                self.store.read_locked(self.operation_id)
    def test_runtime_directory_symlink_rejected(self):
        with self.state.locked(create=True):
            real = self.root / "real"
            real.mkdir(mode=0o700)
            link = self.root / "link"
            link.symlink_to(real, target_is_directory=True)
            unsafe = StagedOptionsStore(self.state, link)
            with self.assertRaises(StagingError):
                unsafe.read_locked(self.operation_id)

if __name__ == "__main__":
    unittest.main(verbosity=2)
