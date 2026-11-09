#!/usr/bin/env python3
"""Regression tests for legacy systemd unit cleanup during package upgrades."""

import os
from pathlib import Path
import shutil
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
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    package_workflow = (ROOT / ".github/workflows/packages.yml").read_text(encoding="utf-8")
    helper = (ROOT / "packaging/helper/zapret2control-helper").read_text(encoding="utf-8")
    worker_unit = (ROOT / "data/systemd/zapret2control-blockcheck.service.in").read_text(encoding="utf-8")
    worker = (ROOT / "packaging/helper/blockcheck_worker.py").read_text(encoding="utf-8")
    manpage = ROOT / "data/zapret2control.1"

    assert "DESTINATION ${CMAKE_INSTALL_DATADIR}/zapret2control)" in cmake
    assert "module_dir=/usr/share/zapret2control" in helper
    assert "%{_datadir}/zapret2control/*.py" in spec
    assert "RuntimeDirectory=zapret2control" in worker_unit
    assert 'StagedOptionsStore(state, "/run/zapret2control/blockcheck")' in worker
    assert "data/zapret2control.1" in cmake
    assert "%{_mandir}/man1/zapret2control.1*" in spec
    assert manpage.is_file()
    assert package_workflow.count("rpm -ql zapret2control | grep -Eq") == 2
    deb_workflow = package_workflow.split("\n  rpm:\n", 1)[0]
    assert "rpm -ql zapret2control" not in deb_workflow
    assert deb_workflow.count("dpkg-query -L zapret2control | grep -Eq") == 2
    assert "test -f /usr/share/man/man1/zapret2control.1\n" not in package_workflow

    for unit in LEGACY_UNITS:
        assert unit in preinst, f"Debian preinst does not migrate {unit}"
        assert unit in spec, f"RPM upgrade script does not migrate {unit}"

    assert "deb-systemd-invoke stop" in preinst
    assert "systemctl stop" not in preinst
    assert "/var/lib/zapret2applet/blockcheck" in preinst
    assert "/var/lib/zapret2control/blockcheck" in preinst
    assert "/var/lib/zapret2applet/blockcheck" in spec
    assert "/var/lib/zapret2control/blockcheck" in spec
    path_sources = [
        helper, worker,
        (ROOT / "packaging/helper/blockcheck_submit.py").read_text(encoding="utf-8"),
        (ROOT / "packaging/helper/blockcheck_cancel.py").read_text(encoding="utf-8"),
        (ROOT / "packaging/helper/blockcheck_status.py").read_text(encoding="utf-8"),
        (ROOT / "packaging/helper/blockcheck_recovery.py").read_text(encoding="utf-8"),
        (ROOT / "packaging/helper/blockcheck_probe.py").read_text(encoding="utf-8"),
        (ROOT / "packaging/helper/blockcheck_admission.py").read_text(encoding="utf-8"),
    ]
    for source in path_sources:
        assert "/run/zapret2applet" not in source
        assert "/var/lib/zapret2applet" not in source
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
                "#!/bin/sh\nprintf '%s %s\n' \"$0\" \"$*\" >> \"$CALL_LOG\"\n",
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
        (unit_dir / LEGACY_UNITS[0]).write_text("[Unit]", encoding="utf-8")
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

        # A valid legacy durable state directory is moved intact to the new path.
        old_parent = temp_path / "var" / "lib" / "zapret2applet"
        new_parent = temp_path / "var" / "lib" / "zapret2control"
        old_state = old_parent / "blockcheck"
        old_state.mkdir(parents=True, mode=0o700)
        old_state.chmod(0o700)
        (old_state / "state.json").write_text('{"version":1}\n', encoding="ascii")
        (old_state / "state.json").chmod(0o600)
        migration_preinst = temp_path / "preinst-state-migration"
        migration_script = test_script.replace(
            "/var/lib/zapret2applet", str(old_parent)
        ).replace(
            "/var/lib/zapret2control", str(new_parent)
        )
        migration_preinst.write_text(migration_script, encoding="utf-8")
        migration_preinst.chmod(0o755)
        subprocess.run([str(migration_preinst), "upgrade"], check=True, env=env)
        assert (new_parent / "blockcheck" / "state.json").read_text(encoding="ascii") == '{"version":1}\n'
        assert not old_state.exists()

        # Ambiguous dual state must stop the upgrade rather than discard either copy.
        old_state.mkdir(parents=True, mode=0o700)
        # The destination directory already exists from the successful move.
        # Recreate only the legacy copy to simulate an ambiguous dual-state upgrade.
        ambiguous = subprocess.run([str(migration_preinst), "upgrade"], env=env)
        assert ambiguous.returncode != 0
        assert old_state.is_dir() and (new_parent / "blockcheck").is_dir()

        # Dangling symlinks must not be mistaken for absent state paths.
        shutil.rmtree(old_state)
        old_state.symlink_to(old_parent / "missing-target")
        dangling_old = subprocess.run([str(migration_preinst), "upgrade"], env=env)
        assert dangling_old.returncode != 0, "Dangling legacy state symlink was accepted"
        old_state.unlink()

        new_state = new_parent / "blockcheck"
        shutil.rmtree(new_state)
        new_state.symlink_to(new_parent / "missing-target")
        old_state.mkdir(parents=True, mode=0o700)
        dangling_new = subprocess.run([str(migration_preinst), "upgrade"], env=env)
        assert dangling_new.returncode != 0, "Dangling destination state symlink was accepted"
        assert old_state.is_dir() and new_state.is_symlink()
        new_state.unlink()

        # A failed stop must abort the upgrade and must not disable the unit.
        failing_bin = temp_path / "failing-bin"
        failing_bin.mkdir()
        for command in ("deb-systemd-invoke", "deb-systemd-helper"):
            executable = failing_bin / command
            executable.write_text(
                "#!/bin/sh\n"
                "printf '%s %s\n' \"$0\" \"$*\" >> \"$CALL_LOG\"\n"
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
        rpm_pretrans = spec.split("%pretrans\n", 1)[1].split("\n%posttrans", 1)[0]
        rpm_unit_dir = temp_path / "rpm-systemd" / "system"
        rpm_unit_dir.mkdir(parents=True)
        (rpm_unit_dir / LEGACY_UNITS[0]).write_text("[Unit]", encoding="utf-8")
        rpm_runtime_dir = temp_path / "rpm-run" / "systemd" / "system"
        rpm_runtime_dir.mkdir(parents=True)
        rpm_old_parent = temp_path / "rpm-var" / "lib" / "zapret2applet"
        rpm_new_parent = temp_path / "rpm-var" / "lib" / "zapret2control"
        rpm_old_state = rpm_old_parent / "blockcheck"
        rpm_old_state.mkdir(parents=True, mode=0o700)
        rpm_old_state.chmod(0o700)
        (rpm_old_state / "state.json").write_text('{"version":2}\n', encoding="ascii")
        (rpm_old_state / "state.json").chmod(0o600)
        rpm_script = rpm_pretrans.replace(
            '"/usr/lib/systemd/system/$unit"',
            f'"{rpm_unit_dir}/$unit"',
        ).replace(
            '"/lib/systemd/system/$unit"',
            f'"{rpm_unit_dir}/$unit"',
        ).replace(
            "/run/systemd/system",
            str(rpm_runtime_dir),
        ).replace(
            "/var/lib/zapret2applet", str(rpm_old_parent)
        ).replace(
            "/var/lib/zapret2control", str(rpm_new_parent)
        )
        systemctl = bin_dir / "systemctl"
        systemctl.write_text(
            "#!/bin/sh" + chr(10)
            + 'echo "$*" >> "$CALL_LOG"' + chr(10)
            + 'if [ "$FAIL_STOP" = 1 ] && [ "$1" = stop ]; then exit 1; fi' + chr(10)
            + "exit 0" + chr(10),
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
        assert (rpm_new_parent / "blockcheck" / "state.json").read_text(encoding="ascii") == '{"version":2}\n'
        assert not rpm_old_state.exists()

        # RPM migration must also reject dangling legacy and destination symlinks.
        rpm_old_parent.mkdir(parents=True, exist_ok=True)
        rpm_old_state.symlink_to(rpm_old_parent / "missing-target")
        rpm_dangling_old = subprocess.run(["/bin/sh", "-c", rpm_script], env=rpm_env)
        assert rpm_dangling_old.returncode != 0, "RPM accepted dangling legacy state symlink"
        rpm_old_state.unlink()

        rpm_new_state = rpm_new_parent / "blockcheck"
        shutil.rmtree(rpm_new_state)
        rpm_new_state.symlink_to(rpm_new_parent / "missing-target")
        rpm_old_state.mkdir(parents=True, mode=0o700)
        rpm_old_state.chmod(0o700)
        rpm_dangling_new = subprocess.run(["/bin/sh", "-c", rpm_script], env=rpm_env)
        assert rpm_dangling_new.returncode != 0, "RPM accepted dangling destination state symlink"
        assert rpm_old_state.is_dir() and rpm_new_state.is_symlink()
        rpm_new_state.unlink()

        # A failed RPM stop must abort before attempting to disable the unit.
        rpm_env["FAIL_STOP"] = "1"
        log_path.unlink()
        rpm_failed = subprocess.run(["/bin/sh", "-c", rpm_script], env=rpm_env)
        assert rpm_failed.returncode != 0, "Failed RPM stop unexpectedly allowed upgrade"
        rpm_failed_calls = log_path.read_text(encoding="utf-8").splitlines()
        assert rpm_failed_calls == ["stop " + LEGACY_UNITS[0]], rpm_failed_calls

        # Execute the RPM %posttrans body and verify daemon-reload.
        rpm_posttrans = spec.split("%posttrans\n", 1)[1].split("\n%build", 1)[0]
        log_path.unlink()
        subprocess.run(["/bin/sh", "-c", rpm_posttrans], check=True, env=rpm_env)
        assert log_path.read_text(encoding="utf-8").splitlines() == ["daemon-reload"]
    print("package migration tests passed")


if __name__ == "__main__":
    main()
