#!/usr/bin/env python3
"""Regression tests for legacy systemd unit cleanup during package upgrades."""

import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PREINST = ROOT / "packaging/debian/preinst"
POSTINST = ROOT / "packaging/debian/postinst"
RPM_SPEC = ROOT / "packaging/fedora/zapret2control.spec"
LEGACY_UNITS = (
    "zapret2applet-blockcheck.service",
    "zapret2applet-blockcheck-recovery.service",
)


def main() -> None:
    preinst = PREINST.read_text(encoding="utf-8")
    postinst = POSTINST.read_text(encoding="utf-8")
    spec = RPM_SPEC.read_text(encoding="utf-8")

    for unit in LEGACY_UNITS:
        assert unit in preinst, f"Debian preinst does not migrate {unit}"
        assert unit in spec, f"RPM upgrade script does not migrate {unit}"

    assert "deb-systemd-invoke stop" in preinst
    assert "deb-systemd-helper disable" in preinst
    assert "systemctl daemon-reload" in postinst
    assert "%pretrans" in spec
    assert "%posttrans" in spec
    assert "systemctl stop" in spec
    assert "systemctl disable" in spec
    assert "systemctl daemon-reload" in spec
    assert spec.index("%pretrans") < spec.index("%posttrans")

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
        assert "daemon-reload" in calls

        log_path.unlink()
        subprocess.run([str(PREINST), "abort-upgrade"], check=True, env=env)
        subprocess.run([str(POSTINST), "abort-upgrade"], check=True, env=env)
        assert not log_path.exists(), "Non-upgrade maintainer action ran cleanup"

        # Exercise Debian preinst against an isolated unit directory. The
        # script copy differs only in the two absolute systemd unit paths.
        unit_dir = temp_path / "systemd" / "system"
        unit_dir.mkdir(parents=True)
        (unit_dir / LEGACY_UNITS[0]).write_text("[Unit]\\n", encoding="utf-8")
        test_preinst = temp_path / "preinst"
        test_script = preinst.replace(
            '"/usr/lib/systemd/system/$1"',
            f'"{unit_dir}/$1"',
        ).replace(
            '"/lib/systemd/system/$1"',
            f'"{unit_dir}/$1"',
        )
        test_preinst.write_text(test_script, encoding="utf-8")
        test_preinst.chmod(0o755)

        log_path.unlink(missing_ok=True)
        subprocess.run([str(test_preinst), "upgrade"], check=True, env=env)
        calls = log_path.read_text(encoding="utf-8").splitlines()
        assert len(calls) == 2, calls
        assert calls[0].endswith("stop " + LEGACY_UNITS[0]), calls
        assert calls[1].endswith("disable " + LEGACY_UNITS[0]), calls
        assert not any(LEGACY_UNITS[1] in call for call in calls), calls

        # A failed stop must abort the upgrade and must not disable the unit.
        failing_bin = temp_path / "failing-bin"
        failing_bin.mkdir()
        for command in ("deb-systemd-invoke", "deb-systemd-helper"):
            executable = failing_bin / command
            executable.write_text(
                "#!/bin/sh\n"
                "printf '%s %s\\n' \"$0\" \"$*\" >> \"$CALL_LOG\"\n"
                + ("exit 1\n" if command == "deb-systemd-invoke" else "exit 0\n"),
                encoding="utf-8",
            )
            executable.chmod(0o755)
        failing_env = env.copy()
        failing_env["PATH"] = f"{failing_bin}:/usr/bin:/bin"
        log_path.unlink()
        failed = subprocess.run([str(test_preinst), "upgrade"], env=failing_env)
        assert failed.returncode != 0, "Failed stop unexpectedly allowed upgrade"
        failed_calls = log_path.read_text(encoding="utf-8").splitlines()
        assert len(failed_calls) == 1 and failed_calls[0].endswith(
            "stop " + LEGACY_UNITS[0]
        ), failed_calls

        # Execute the RPM %pretrans scriptlet with isolated systemd paths and
        # a stub systemctl. This exercises the actual scriptlet body without
        # touching the host system.
        rpm_pretrans = spec.split("%pretrans\\n", 1)[1].split("\\n%posttrans", 1)[0]
        rpm_unit_dir = temp_path / "rpm-systemd" / "system"
        rpm_unit_dir.mkdir(parents=True)
        (rpm_unit_dir / LEGACY_UNITS[0]).write_text("[Unit]\\n", encoding="utf-8")
        rpm_runtime_dir = temp_path / "rpm-run" / "systemd" / "system"
        rpm_runtime_dir.mkdir(parents=True)
        rpm_script = rpm_pretrans.replace(
            '"/usr/lib/systemd/system/$unit"',
            f'"{rpm_unit_dir}/$unit"',
        ).replace(
            '"/lib/systemd/system/$unit"',
            f'"{rpm_unit_dir}/$unit"',
        ).replace(
            "/run/systemd/system",
            str(rpm_runtime_dir),
        )
        systemctl = bin_dir / "systemctl"
        systemctl.write_text(
            "#!/bin/sh\\n"
            "printf '%s\\n' \"$*\" >> \"$CALL_LOG\"\\n"
            "if [ \"$FAIL_STOP\" = 1 ] && [ \"$1\" = stop ]; then exit 1; fi\\n"
            "exit 0\\n",
            encoding="utf-8",
        )
        systemctl.chmod(0o755)
        rpm_env = env.copy()
        rpm_env["PATH"] = f"{bin_dir}:/usr/bin:/bin"
        rpm_env["FAIL_STOP"] = "0"
        log_path.unlink(missing_ok=True)
        subprocess.run(["/bin/sh", "-c", rpm_script], check=True, env=rpm_env)
        rpm_calls = log_path.read_text(encoding="utf-8").splitlines()
        assert rpm_calls == [
            "stop " + LEGACY_UNITS[0],
            "disable " + LEGACY_UNITS[0],
        ], rpm_calls

        # A failed RPM stop must abort before attempting to disable the unit.
        rpm_env["FAIL_STOP"] = "1"
        log_path.unlink()
        rpm_failed = subprocess.run(["/bin/sh", "-c", rpm_script], env=rpm_env)
        assert rpm_failed.returncode != 0, "Failed RPM stop unexpectedly allowed upgrade"
        rpm_failed_calls = log_path.read_text(encoding="utf-8").splitlines()
        assert rpm_failed_calls == ["stop " + LEGACY_UNITS[0]], rpm_failed_calls

        # Execute the RPM %posttrans body and verify daemon-reload.
        rpm_posttrans = spec.split("%posttrans\\n", 1)[1].split("\\n%build", 1)[0]
        log_path.unlink()
        subprocess.run(["/bin/sh", "-c", rpm_posttrans], check=True, env=rpm_env)
        assert log_path.read_text(encoding="utf-8").splitlines() == ["daemon-reload"]
    print("package migration tests passed")


if __name__ == "__main__":
    main()
