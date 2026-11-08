#!/usr/bin/env python3
"""Regression tests for legacy systemd unit cleanup during package upgrades."""

import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
POSTINST = ROOT / "packaging/debian/postinst"
RPM_SPEC = ROOT / "packaging/fedora/zapret2control.spec"
LEGACY_UNITS = (
    "zapret2applet-blockcheck.service",
    "zapret2applet-blockcheck-recovery.service",
)


def main() -> None:
    postinst = POSTINST.read_text(encoding="utf-8")
    spec = RPM_SPEC.read_text(encoding="utf-8")

    for unit in LEGACY_UNITS:
        assert unit in postinst, f"Debian postinst does not clean {unit}"
        assert unit in spec, f"RPM upgrade script does not clean {unit}"

    assert "deb-systemd-invoke stop" in postinst
    assert "deb-systemd-helper disable" in postinst
    assert "%posttrans" in spec
    assert "systemctl stop" in spec
    assert "systemctl disable" in spec
    assert "systemctl daemon-reload" in spec

    with tempfile.TemporaryDirectory(prefix="zapret2control-package-test-") as temp:
        temp_path = Path(temp)
        bin_dir = temp_path / "bin"
        bin_dir.mkdir()
        log_path = temp_path / "commands.log"

        for command in ("deb-systemd-invoke", "deb-systemd-helper", "systemctl"):
            executable = bin_dir / command
            executable.write_text(
                "#!/bin/sh\nprintf '%s %s\\n' \"$0\" \"$*\" >> \"$CALL_LOG\"\n",
                encoding="utf-8",
            )
            executable.chmod(0o755)

        env = os.environ.copy()
        env["PATH"] = f"{bin_dir}:/usr/bin:/bin"
        env["CALL_LOG"] = str(log_path)
        subprocess.run([str(POSTINST), "configure"], check=True, env=env)
        calls = log_path.read_text(encoding="utf-8")

        for unit in LEGACY_UNITS:
            assert f"stop {unit}" in calls, f"Debian upgrade did not stop {unit}"
            assert f"disable {unit}" in calls, f"Debian upgrade did not disable {unit}"
        assert "daemon-reload" in calls

        log_path.unlink()
        subprocess.run([str(POSTINST), "abort-upgrade"], check=True, env=env)
        assert not log_path.exists(), "Non-configure maintainer action ran cleanup"

    print("package migration tests passed")


if __name__ == "__main__":
    main()
