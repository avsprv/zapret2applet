#!/usr/bin/env python3
"""Validate the clean-start package identity and current systemd lifecycle."""

import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LEGACY_ID = "zapret2" + "applet"
CURRENT_UNITS = (
    "zapret2control-blockcheck.service",
    "zapret2control-blockcheck-recovery.service",
)


def main() -> None:
    preinst = (ROOT / "packaging/debian/preinst").read_text(encoding="utf-8")
    postinst = (ROOT / "packaging/debian/postinst").read_text(encoding="utf-8")
    spec = (ROOT / "packaging/fedora/zapret2control.spec").read_text(encoding="utf-8")
    control = (ROOT / "packaging/debian/control").read_text(encoding="utf-8")
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    main_cpp = (ROOT / "src/qml/main.cpp").read_text(encoding="utf-8")
    autostart = (ROOT / "src/qml/autostartcontroller.cpp").read_text(encoding="utf-8")
    autostart_tests = (ROOT / "tests/test_autostartcontroller.cpp").read_text(encoding="utf-8")
    package_workflow = (ROOT / ".github/workflows/packages.yml").read_text(encoding="utf-8")
    helper = (ROOT / "packaging/helper/zapret2control-helper").read_text(encoding="utf-8")
    worker_unit = (ROOT / "data/systemd/zapret2control-blockcheck.service.in").read_text(encoding="utf-8")
    recovery_unit = (ROOT / "data/systemd/zapret2control-blockcheck-recovery.service.in").read_text(encoding="utf-8")
    worker = (ROOT / "packaging/helper/blockcheck_worker.py").read_text(encoding="utf-8")
    lifecycle_doc = (ROOT / "docs/BLOCKCHECK_SYSTEMD_LIFECYCLE.md").read_text(encoding="utf-8")
    manpage = ROOT / "data/zapret2control.1"

    assert "DESTINATION ${CMAKE_INSTALL_DATADIR}/zapret2control)" in cmake
    assert "module_dir=/usr/share/zapret2control" in helper
    assert "%{_datadir}/zapret2control/*.py" in spec
    assert "RuntimeDirectory=zapret2control" in worker_unit
    assert "TimeoutStopSec=180" in worker_unit
    assert "TimeoutStartSec=180" in recovery_unit
    assert 'StagedOptionsStore(state, "/run/zapret2control/blockcheck")' in worker
    assert "`/var/lib/zapret2control/blockcheck/`" in lifecycle_doc
    assert "`/run/zapret2control/blockcheck/`" in lifecycle_doc
    assert "data/zapret2control.1" in cmake
    assert "%{_mandir}/man1/zapret2control.1*" in spec
    assert manpage.is_file()
    assert package_workflow.count("rpm -ql zapret2control | grep -Eq") == 1
    deb_workflow = package_workflow.split("\n  rpm:\n", 1)[0]
    assert "rpm -ql zapret2control" not in deb_workflow
    assert deb_workflow.count("dpkg-query -L zapret2control | grep -Eq") == 1

    assert LEGACY_ID not in package_workflow
    assert "legacy-source" not in package_workflow
    assert LEGACY_ID not in lifecycle_doc
    assert f"/var/lib/{LEGACY_ID}" not in lifecycle_doc
    assert f"/run/{LEGACY_ID}" not in lifecycle_doc

    for path in ROOT.rglob("*"):
        if not path.is_file() or any(part in {".git", "build", "dist", "__pycache__", "third_party"} for part in path.parts):
            continue
        try:
            content = path.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        if path == ROOT / "packaging/fedora/zapret2control.spec":
            content = content.split("%changelog", 1)[0]
        assert LEGACY_ID not in content, f"Unsupported legacy identifier found in {path.relative_to(ROOT)}"

    for source in (preinst, spec.split("%changelog", 1)[0], control, cmake,
                   main_cpp, autostart, autostart_tests):
        assert LEGACY_ID not in source
    assert "SettingsMigration" not in cmake + main_cpp
    assert not (ROOT / "src/settingsmigration.cpp").exists()
    assert not (ROOT / "src/settingsmigration.h").exists()
    assert not (ROOT / "tests/test_settingsmigration.cpp").exists()
    assert "migrateLegacy" not in autostart
    assert "Provides: " + LEGACY_ID not in control
    assert "Conflicts: " + LEGACY_ID not in control
    assert "Replaces: " + LEGACY_ID not in control
    assert "Obsoletes:" not in spec
    assert "Provides: " + LEGACY_ID not in spec

    for unit in CURRENT_UNITS:
        assert unit in preinst
        assert unit in spec
    assert "deb-systemd-invoke stop" in preinst
    assert "systemctl stop" not in preinst
    assert "systemctl daemon-reload" in postinst
    assert "%pretrans" in spec and "%posttrans" in spec

    with tempfile.TemporaryDirectory(prefix="zapret2control-package-test-") as temp:
        temp_path = Path(temp)
        bin_dir = temp_path / "bin"
        bin_dir.mkdir()
        log_path = temp_path / "commands.log"
        unit_dir = temp_path / "systemd" / "system"
        unit_dir.mkdir(parents=True)
        for unit in CURRENT_UNITS:
            (unit_dir / unit).write_text("[Unit]", encoding="utf-8")

        invoke = bin_dir / "deb-systemd-invoke"
        invoke.write_text(
            "#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"$CALL_LOG\"\n",
            encoding="utf-8",
        )
        invoke.chmod(0o755)
        env = os.environ.copy()
        env["PATH"] = f"{bin_dir}:/usr/bin:/bin"
        env["CALL_LOG"] = str(log_path)

        test_preinst = temp_path / "preinst"
        isolated = preinst.replace(
            '"/usr/lib/systemd/system/$1"', f'"{unit_dir}/$1"'
        ).replace(
            '"/lib/systemd/system/$1"', f'"{unit_dir}/$1"'
        )
        test_preinst.write_text(isolated, encoding="utf-8")
        test_preinst.chmod(0o755)

        subprocess.run([str(test_preinst), "upgrade"], check=True, env=env)
        calls = log_path.read_text(encoding="utf-8").splitlines()
        assert calls == [f"stop {unit}" for unit in CURRENT_UNITS], calls

        log_path.unlink()
        subprocess.run([str(test_preinst), "abort-upgrade"], check=True, env=env)
        assert not log_path.exists(), "Non-upgrade maintainer action ran"


if __name__ == "__main__":
    main()
    print("package layout tests passed")
