Name:           zapret2control
Version:        0.5.0
Release:        1%{?dist}
Obsoletes:      zapret2applet < 0.5.0
Provides:       zapret2applet = %{version}-%{release}
Summary:        Network Control for Linux
License:        MIT
URL:            https://github.com/avsprv/zapret2control
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.21
BuildRequires:  extra-cmake-modules >= 6.0
BuildRequires:  kf6-kirigami-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  cmake(Qt6Widgets) >= 6.8
BuildRequires:  cmake(Qt6Network) >= 6.8
BuildRequires:  cmake(Qt6DBus) >= 6.8
BuildRequires:  cmake(Qt6LinguistTools) >= 6.8
BuildRequires:  cmake(Qt6Test) >= 6.8
BuildRequires:  desktop-file-utils
BuildRequires:  appstream
BuildRequires:  systemd-rpm-macros
# test dependencies for the privileged helper
BuildRequires:  curl
BuildRequires:  tar
BuildRequires:  dbus-daemon
Requires:       polkit
Requires:       curl
Requires:       python3
Requires:       tar
Requires:       coreutils
Requires:       systemd
Requires:       util-linux-core
Recommends:     qt6-qttranslations

%description
Qt6 graphical front-end for zapret2: service and configuration management,
profile and user host list editing, journal monitoring, strategy selection via
blockcheck2, system tray applet, and automatic startup. Privileged operations
use a small Polkit-protected helper. zapret2 is not bundled and must be
installed separately.

%prep
%autosetup

%post
%systemd_post zapret2control-blockcheck-recovery.service

%preun
%systemd_preun zapret2control-blockcheck.service
%systemd_preun zapret2control-blockcheck-recovery.service

%postun
%systemd_postun zapret2control-blockcheck.service zapret2control-blockcheck-recovery.service

# The previous package used different unit names. Stop and disable them after
# the transaction, once the replacement package and helper are installed.
%posttrans
if command -v systemctl >/dev/null 2>&1; then
    systemctl stop zapret2applet-blockcheck.service >/dev/null 2>&1 || :
    systemctl stop zapret2applet-blockcheck-recovery.service >/dev/null 2>&1 || :
    systemctl disable zapret2applet-blockcheck.service >/dev/null 2>&1 || :
    systemctl disable zapret2applet-blockcheck-recovery.service >/dev/null 2>&1 || :
    systemctl daemon-reload >/dev/null 2>&1 || :
fi

%build
%cmake -G Ninja -DCMAKE_SKIP_RPATH=ON
%cmake_build

%install
%cmake_install

%check
export QT_QPA_PLATFORM=offscreen
%ctest
desktop-file-validate %{buildroot}%{_datadir}/applications/zapret2control.desktop
appstream-util validate-relax --nonet %{buildroot}%{_datadir}/metainfo/org.avsprv.zapret2control.metainfo.xml

%files
%license LICENSE NOTICE.md
%doc README.md
%{_bindir}/zapret2control
%{_libexecdir}/zapret2control/zapret2control-helper
%{_libexecdir}/zapret2control/blockcheck_*.py
%{_unitdir}/zapret2control-blockcheck.service
%{_unitdir}/zapret2control-blockcheck-recovery.service
%{_datadir}/polkit-1/actions/org.zapret2control.policy
%{_datadir}/applications/zapret2control.desktop
%{_datadir}/metainfo/org.avsprv.zapret2control.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/zapret2control.svg
%{_datadir}/icons/hicolor/symbolic/apps/zapret2control-tray-symbolic.svg

%changelog
* Sun Nov 08 2026 avsprv <avsprv@users.noreply.github.com> - 0.5.0-1
- Rename the application, executable, helper, desktop, Polkit and systemd identifiers
- Migrate user settings and managed autostart entries from zapret2applet
- Preserve legacy Blockcheck recovery state across package upgrades

* Sat Nov 07 2026 avsprv <avsprv@users.noreply.github.com> - 0.4.0-1
- Add safe configuration and profile management with validation and backups
- Add user hostlist editing and read-only access to generated IP lists
- Add a read-only systemd journal monitor for zapret2.service
- Expand helper validation and regression test coverage

* Fri Nov 06 2026 avsprv <avsprv@users.noreply.github.com> - 0.3.0-1
- Project renamed to zapret2applet (binary, polkit actions, helper, translations)
- Install and update zapret2 from the latest GitHub release (sha256 verified)
- Qt 6.8 LTS is now the minimum; English localization
* Tue Nov 03 2026 avsprv - 0.2.0-1
- Strategy selection via blockcheck2, tray applet, boot and login autostart
- Privileged helper with polkit policy
* Tue Nov 03 2026 avsprv - 0.1.0-1
- Initial skeleton
