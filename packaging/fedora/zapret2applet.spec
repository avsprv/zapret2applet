Name:           zapret2applet
Version:        0.4.0
Release:        1%{?dist}
Summary:        Unofficial Qt6 GUI for zapret2
License:        MIT
URL:            https://github.com/avsprv/zapret2applet
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.21
BuildRequires:  extra-cmake-modules >= 6.0
BuildRequires:  kf6-kirigami-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  cmake(Qt6Widgets) >= 6.8
BuildRequires:  cmake(Qt6Network) >= 6.8
BuildRequires:  cmake(Qt6LinguistTools) >= 6.8
BuildRequires:  cmake(Qt6Test) >= 6.8
BuildRequires:  desktop-file-utils
BuildRequires:  systemd-rpm-macros
# test dependencies for the privileged helper
BuildRequires:  curl
BuildRequires:  tar
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
%systemd_post zapret2applet-blockcheck-recovery.service

%preun
%systemd_preun zapret2applet-blockcheck.service
%systemd_preun zapret2applet-blockcheck-recovery.service

%postun
%systemd_postun

%build
%cmake -G Ninja -DCMAKE_SKIP_RPATH=ON
%cmake_build

%install
%cmake_install

%check
export QT_QPA_PLATFORM=offscreen
%ctest
desktop-file-validate %{buildroot}%{_datadir}/applications/zapret2applet.desktop

%files
%license LICENSE NOTICE.md
%doc README.md
%{_bindir}/zapret2applet
%{_libexecdir}/zapret2applet/zapret2applet-helper
%{_libexecdir}/zapret2applet/blockcheck_*.py
%{_unitdir}/zapret2applet-blockcheck.service
%{_unitdir}/zapret2applet-blockcheck-recovery.service
%{_datadir}/polkit-1/actions/org.zapret2applet.policy
%{_datadir}/applications/zapret2applet.desktop
%{_datadir}/icons/hicolor/scalable/apps/zapret2applet.svg

%changelog
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
