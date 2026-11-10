# Zapret2 Control

**An independent graphical frontend for managing an installed [zapret2](https://github.com/bol-van/zapret2) service on Linux.**

[![CI](https://github.com/avsprv/zapret2control/actions/workflows/ci.yml/badge.svg)](https://github.com/avsprv/zapret2control/actions/workflows/ci.yml)
[![Packages](https://github.com/avsprv/zapret2control/actions/workflows/packages.yml/badge.svg)](https://github.com/avsprv/zapret2control/actions/workflows/packages.yml)
[![Latest release](https://img.shields.io/github/v/release/avsprv/zapret2control)](https://github.com/avsprv/zapret2control/releases)
[![License: MIT](https://img.shields.io/github/license/avsprv/zapret2control)](LICENSE)

[Русский](README.md) · **English**

The application provides a graphical interface for managing an existing zapret2 service, running blockcheck2 network diagnostics, and updating zapret2.

> [!WARNING]
> **Early-stage software.** Zapret2 Control is at an early stage of development. The application and individual features may contain bugs, behave unreliably, or terminate unexpectedly. Review configuration changes and updates carefully; do not rely on the application as the sole means of managing network availability.

> [!IMPORTANT]
> **Independent project.** zapret2control is not affiliated with the author or maintainers of [zapret2](https://github.com/bol-van/zapret2).
>
> The application does not bundle zapret2. The **Updates** page supports a clean installation from an upstream release. Strategies found by Blockcheck are not applied automatically; transfer them to your zapret2 configuration and verify them manually.

## Contents

- [Features](#features)
- [Compatibility and requirements](#compatibility-and-requirements)
- [Installation](#installation)
- [Usage](#usage)
- [Security](#security)
- [Building from source](#building-from-source)
- [Building packages](#building-packages)
- [Development and testing](#development-and-testing)
- [Report a bug](#report-a-bug)
- [Legal information](#legal-information)
- [Privacy](#privacy)
- [License](#license)

## Features

- **Service management:** view the status, start, and stop the zapret2 systemd service.
- **Configuration:** view and edit `/opt/zapret2/config`, validate shell syntax, create backups, save atomically, and roll back.
- **Profiles:** statically analyze `NFQWS2_OPT` profiles; create, edit, copy, delete, and safely apply profile changes.
- **Lists:** view user hostlists and generated IP lists; edit the three source user hostlists with input validation and backups.
- **Journal monitor:** read recent entries from the fixed `zapret2.service` journal through a read-only Polkit action.
- **Network diagnostics:** run the upstream `blockcheck2` script to test connectivity to user-specified domains over HTTP, TLS 1.2/1.3, and HTTP/3 (QUIC), with IPv4 and IPv6 support.
- **Results:** view and copy detected strategies.
- **System tray:** the application keeps running in the notification area after the main window is closed.
- **Autostart:** start the service at boot and the application at user login.
- **Updates:** check upstream releases and update zapret2 from the interface.
- **Localization:** Russian and English.

## Compatibility and requirements

Building requires **Qt 6.8+**, CMake 3.21+, a C++20 compiler, and Ninja. The application also requires systemd, Polkit, pkexec, curl, and tar.

| Distribution | Baseline in packaging configuration |
|---|---:|
| Fedora | 41+ |
| Debian | 13 (Trixie)+ |
| Ubuntu | 25.04+ |

These versions reflect the baseline availability of Qt 6.8; they do not guarantee support for every distribution release. **Ubuntu 25.04 has reached end of life** and should not be installed just for this application. On Ubuntu, use a currently supported release with Qt 6.8+ and validate the specific release separately.

**A clean zapret2 installation is available on the Updates tab.** The application downloads the latest stable release, validates the archive structure and SHA-256 checksums of binary components, creates `/opt/zapret2`, and installs the systemd unit. Existing installation directories and unit files are never overwritten. The initial configuration is created with `NFQWS2_ENABLE=0`; the service is neither started nor enabled at boot. Before starting it, check the dependencies, select and configure the firewall, and explicitly enable the required options in the configuration. The interactive upstream installer is not invoked.

## Installation

1. Download the zapret2control package from [Releases](https://github.com/avsprv/zapret2control/releases).
2. Install it:

**Fedora (.rpm):**
```bash
sudo dnf install ./zapret2control-*.rpm
```

**Debian / Ubuntu (.deb):**
```bash
sudo apt install ./zapret2control_*.deb
```

3. Launch zapret2control from your application menu. If zapret2 is not installed yet, open the Updates tab and perform a clean installation. Configure the service and its dependencies before starting it.

If a prebuilt package is unavailable, follow the [build from source](#building-from-source) instructions.

## Usage

### Service management

The **Status** tab displays the zapret2 service state. You can start or stop the service and configure its autostart. Application autostart at user login is configured separately.

### Configuration

The **Configuration** tab can read and edit `/opt/zapret2/config`, validate shell syntax without executing the file, save with a backup, and roll back the latest saved copy. Before writing, the helper validates syntax and atomically replaces the file. The service is not restarted automatically.

### Profiles

The **Profiles** tab statically analyzes profiles separated by `--new` in `NFQWS2_OPT`. You can create, edit, copy, and delete profiles. The editor does not execute config, shell code, or Lua. Before applying changes, the helper validates shell syntax, creates a backup, and atomically writes the updated configuration. The service is not restarted automatically.

### Lists

The **Lists** tab can display source user hostlists and generated IP lists. Only the three source user hostlists can be edited; entries are validated as domain names, IP addresses, or CIDR ranges. Generated IP files are read-only because zapret2 recreates them. A backup is created before saving.

### Journal monitor

The **Journal** tab displays the latest 50, 100, 200, or 500 entries from the fixed `zapret2.service` unit. Reading uses a separate read-only Polkit action. Optional 10-second auto-refresh is disabled by default.

### Strategy testing

1. Enter the domains you want to test.
2. Start testing from the strategy testing tab.
3. If the zapret2 service is running, the application will ask to stop it for the duration of the test.
4. Wait for the tests to finish and review the results.
5. Copy a suitable strategy and transfer it to the zapret2 configuration manually.

Testing covers HTTP, TLS 1.2/1.3, and HTTP/3 (QUIC), over IPv4 and IPv6. Results depend on the network and current filtering conditions; a successful test does not guarantee that a strategy will remain effective.

The engine supports 1–10 domains, scan levels veryquick, quick, standard, and force, and 1–10 repetitions. A thorough scan may take several minutes.

### Updates

The **Updates** tab lets you check for a new zapret2 release and start the update. Automatic checks are enabled by default and run no more than once per day; this can be disabled.

## Security

The main interface runs as a regular user. Operations requiring system privileges are performed by a separate helper through Polkit and pkexec. Elevated privileges may be required to manage the service, test strategies, and update zapret2.

The helper accepts a limited set of operations and validates its arguments. During an update, authorization is requested separately because files that may later run as root are being replaced.

zapret2 release files are checked against the SHA-256 checksums in sha256sum.txt. This helps detect corruption or mismatches against the published checksum list, but **it is not cryptographic release-signature verification** and does not protect against compromise of the upstream repository or its account. The updater validates the archive structure and version, creates a backup before replacing files, and provides rollback if installation fails.

## Building from source

### Dependencies

**Fedora:**
```bash
sudo dnf install cmake ninja-build gcc-c++ extra-cmake-modules kf6-kirigami-devel qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qttools-devel desktop-file-utils appstream util-linux-core dbus-daemon systemd curl tar python3 polkit
```

**Debian / Ubuntu:**
```bash
sudo apt install build-essential cmake ninja-build extra-cmake-modules libkirigami-dev qt6-base-dev qt6-declarative-dev qt6-tools-dev qt6-l10n-tools desktop-file-utils appstream dbus-daemon systemd curl tar python3 pkexec
```

### Build and test

```bash
git clone https://github.com/avsprv/zapret2control.git
cd zapret2control
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Run without installing: ./build/zapret2control.

Install system-wide:
```bash
sudo cmake --install build --prefix /usr
```

When launched from the build directory, the authorization dialog may not include the action description provided by the installed Polkit policy.

## Building packages

Manual instructions are available in [packaging/README.md](packaging/README.md).

GitHub Actions builds packages for Debian 13 and Fedora on pushes to branches configured in the workflow and on pull requests targeting main. The resulting files are available as workflow artifacts in [package build runs](https://github.com/avsprv/zapret2control/actions/workflows/packages.yml). Artifacts are intended for build verification; use packages attached to [Releases](https://github.com/avsprv/zapret2control/releases) for normal installation.

## Development and testing

The project uses C++20, Qt 6, CMake, and CTest. Tests cover strategy testing, profile analysis, localization, update versions, list and journal operations, and helper validation.

```bash
ctest --test-dir build --output-on-failure
```

Developer documentation: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## Report a bug

Before opening a report, check [existing Issues](https://github.com/avsprv/zapret2control/issues). Include the distribution and version, application and zapret2 versions, reproduction steps, expected and actual behavior, and relevant error messages with personal data removed.

[Open an issue](https://github.com/avsprv/zapret2control/issues/new).

## Legal information

Read the [Disclaimer](DISCLAIMER.md) before using the software. This documentation describes technical functionality and is not legal advice. A disclaimer does not remove obligations imposed by applicable law.

## Privacy

See the [Privacy Policy](PRIVACY.md) for information about network requests, local settings, and data that may be visible to GitHub and tested network destinations.

## License

The zapret2control source code is distributed under the [MIT License](LICENSE). This license applies to this application's code and does not replace licenses for third-party components. Information about the linked zapret2 project is available in [NOTICE.md](NOTICE.md).
