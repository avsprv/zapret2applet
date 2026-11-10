#!/usr/bin/env python3
"""Integration tests for the transactional updater CLI."""

from pathlib import Path
import hashlib
import os
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "packaging/helper"))

from updater_cli import recover_pending, run_update
from updater_activation import ActivationError


class UpdaterCliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="zapret2control-cli-")
        self.base = Path(self.temp.name)
        self.active = self.base / "zapret2"
        self.upstream = self.base / "upstream"
        self.state = self.base / "var/lib/zapret2control/updater"
        (self.base / "var/lib").mkdir(parents=True, mode=0o755)
        self.active.mkdir(mode=0o755)
        self.upstream.mkdir(mode=0o755)
        (self.active / "config").write_text("NFQWS2_ENABLE=0\n", encoding="utf-8")
        (self.active / "config").chmod(0o600)
        (self.active / "legacy.txt").write_text("retain", encoding="utf-8")
        self.make_upstream()
        self.checksums = self.base / "sha256sum.txt"
        checksum_lines = []
        for relative in (
            "binaries/x86_64/nfqws2",
            "binaries/x86_64/ip2net",
            "binaries/x86_64/mdig",
        ):
            binary = self.upstream / relative
            checksum_lines.append(
                f"{hashlib.sha256(binary.read_bytes()).hexdigest()}  upstream/{relative}"
            )
        self.checksums.write_text("\n".join(checksum_lines) + "\n", encoding="utf-8")

    def tearDown(self):
        self.temp.cleanup()

    def put(self, relative, data, mode=0o644):
        path = self.upstream / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(data, encoding="utf-8")
        path.chmod(mode)
        return path

    def make_upstream(self):
        self.put("config.default", "NFQWS2_ENABLE=0\n")
        self.put("docs/changes.txt", "1.2.3\n")
        self.put("common/base.sh", "# common\n")
        self.put("blockcheck2.sh", "#!/bin/sh\nexit 0\n", 0o755)
        self.put("init.d/sysv/zapret2", "#!/bin/sh\nexit 0\n", 0o755)
        self.put(
            "init.d/systemd/zapret2.service",
            "[Service]\nExecStart=/opt/zapret2/init.d/sysv/zapret2 start\n"
            "ExecStop=/opt/zapret2/init.d/sysv/zapret2 stop\n",
        )
        self.put("binaries/x86_64/nfqws2", "#!/bin/sh\necho version\n", 0o755)
        self.put("binaries/x86_64/ip2net", "#!/bin/sh\nread line\necho result\n", 0o755)
        self.put("binaries/x86_64/mdig", "#!/bin/sh\nexit 0\n", 0o755)
        self.put(
            "install_bin.sh",
            "#!/bin/sh\nmkdir -p nfq2 ip2net mdig\n"
            "ln -s ../binaries/x86_64/nfqws2 nfq2/nfqws2\n"
            "ln -s ../binaries/x86_64/ip2net ip2net/ip2net\n"
            "ln -s ../binaries/x86_64/mdig mdig/mdig\n",
            0o755,
        )

    def test_update_builds_and_atomically_activates_manifest_tree(self):
        backup = run_update(
            self.upstream, self.active, self.state, "v1.2.3",
            "zapret2.service", self.checksums, no_service=True,
        )
        self.assertTrue((self.active / ".zapret2control-upstream-manifest.json").is_file())
        self.assertEqual((self.active / "legacy.txt").read_text(encoding="utf-8"), "retain")
        self.assertEqual((self.active / "config").read_text(encoding="utf-8"), "NFQWS2_ENABLE=0\n")
        self.assertTrue((self.active / "nfq2/nfqws2").is_symlink())
        self.assertEqual((backup / "config").read_text(encoding="utf-8"), "NFQWS2_ENABLE=0\n")
        self.assertTrue(backup.is_dir())
        self.assertEqual(list(self.state.glob("*.json")), [])

    def test_recovery_rejects_unrecognized_journal_directory_entries(self):
        self.state.mkdir(parents=True, mode=0o700)
        (self.state / ".orphaned-journal.tmp").write_text("partial", encoding="utf-8")
        with self.assertRaisesRegex(ActivationError, "unexpected object"):
            recover_pending(self.state, self.active, "zapret2.service")

    def test_invalid_staged_unit_cleans_partial_tree_without_touching_active(self):
        unit = self.upstream / "init.d/systemd/zapret2.service"
        unit.write_text("[Service]\nExecStart=/wrong/path\n", encoding="utf-8")
        with self.assertRaisesRegex(ActivationError, "systemd unit"):
            run_update(
                self.upstream, self.active, self.state, "v1.2.3",
                "zapret2.service", self.checksums, no_service=True,
            )
        self.assertFalse(any(self.base.glob(".zapret2control-stage-*")))
        self.assertEqual((self.active / "legacy.txt").read_text(encoding="utf-8"), "retain")

    def test_install_script_cannot_modify_checksummed_binaries(self):
        script = self.upstream / "install_bin.sh"
        script.write_text(
            "#!/bin/sh\n"
            "mkdir -p nfq2 ip2net mdig\n"
            "ln -s ../binaries/x86_64/nfqws2 nfq2/nfqws2\n"
            "ln -s ../binaries/x86_64/ip2net ip2net/ip2net\n"
            "ln -s ../binaries/x86_64/mdig mdig/mdig\n"
            "echo tampered >> binaries/x86_64/nfqws2\n",
            encoding="utf-8",
        )
        with self.assertRaisesRegex(ActivationError, "sha256sum"):
            run_update(
                self.upstream, self.active, self.state, "v1.2.3",
                "zapret2.service", self.checksums, no_service=True,
            )
        self.assertFalse((self.active / ".zapret2control-upstream-manifest.json").exists())

    def test_invalid_upstream_does_not_change_active_installation(self):
        (self.upstream / "install_bin.sh").write_text("#!/bin/sh\nexit 9\n", encoding="utf-8")
        with self.assertRaises(ActivationError):
            run_update(
                self.upstream, self.active, self.state, "v1.2.3",
                "zapret2.service", self.checksums, no_service=True,
            )
        self.assertFalse((self.active / ".zapret2control-upstream-manifest.json").exists())
        self.assertEqual((self.active / "legacy.txt").read_text(encoding="utf-8"), "retain")


if __name__ == "__main__":
    unittest.main()
