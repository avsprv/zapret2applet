# QML and Kirigami migration

## Goals

- Replace the Qt Widgets presentation layer with Qt Quick and KDE Kirigami 6.
- Preserve existing C++ service, helper, configuration, profile, blockcheck, and update logic.
- Keep privileged operations behind the existing Polkit-protected helper.
- Follow KDE HIG and use the active KDE/Qt platform theme rather than hard-coded palettes.
- Make the Qt Quick/Kirigami application the only production GUI and keep the privileged backend independent of the presentation layer.

## Migration strategy

The production executable is now built from `src/qml/main.cpp` and the Kirigami QML module.
The former preview target and the Qt Widgets dependency have been removed. No parallel
Widgets implementation or Widgets-only regression target is maintained.
The installed desktop entry and package executable name remain `zapret2control`.

## Current implementation status

The following areas are implemented in the production QML/Kirigami application:

| Area | Status | Notes |
| --- | --- | --- |
| Application shell and dashboard | Implemented | Kirigami ApplicationWindow, navigation drawer, service status and service controls |
| Configuration | Implemented | Read/edit/save, shell syntax validation, SHA-256 stale-write protection, privileged atomic write |
| Profiles | Implemented | Add/edit/copy/remove, static analysis, validation and unsaved-change protection |
| Lists | Implemented | All nine list files; only the three source user hostlists are writable |
| Journal monitoring | Implemented | Read-only journal access, bounded output and optional refresh |
| Blockcheck | Implemented | Domain/protocol options, confirmation before stopping the service, result copying and log export |
| Settings | Implemented with platform caveat | System/light/dark preference is persisted and applied at runtime through Qt color-scheme hints; forcing a scheme depends on platform support |
| About | Implemented | Version, dependencies, licensing, project separation and relevant links |
| Update and clean install | Implemented | Release check, clean install, update, confirmation, bounded log and privileged helper integration |
| Tray and desktop lifecycle | Implemented | StatusNotifierItem, DBusMenu, watcher re-registration, notifications, close-to-tray, minimized startup fallback and quit guards |

“Implemented” means that the QML page and its current workflow are wired into the production
application. The obsolete Widgets UI source and its Widgets-specific tests have been removed;
package-level installation and runtime checks remain.

## Planned stages

1. Establish the Qt Quick/Kirigami build and application shell. **Complete**
2. Extract and expose typed C++ controller APIs for the migrated workflows. **Complete**
3. Migrate configuration and profile management. **Complete**
4. Migrate lists and journal monitoring. **Complete**
5. Migrate blockcheck2 orchestration and strategy results. **Implemented; reliability review continues**
6. Migrate update checking, clean installation, update and recovery workflows. **Implemented; final CI verification in progress**
7. Complete settings, theme, tray and desktop lifecycle integration. **Implemented; production CI verification pending**.
8. Switch the production executable and QML lint target to Kirigami. **Complete**
9. Remove obsolete Widgets-only implementation and tests after replacing coverage with QML/controller tests. **Complete**
10. Validate clean installation, supported current-version updates, runtime QML dependencies and package contents. **In progress**

## Architectural boundaries

- QML owns presentation, navigation, layouts and user interaction.
- C++ QObject controllers expose typed properties, invokable commands and signals.
- Backend classes remain responsible for network checks, process management, systemd, blockcheck2
  and helper invocation.
- QML must never execute shell commands or invoke pkexec directly.
- Privileged operations must use the fixed allowlisted helper actions.
- Preserve the current `zapret2control` settings schema and user data across supported updates; no migration from the unreleased predecessor is required.
- Potentially long operations must be asynchronous, bounded and cancellable where supported.
- Failure to confirm a service state must never be treated as a successful transition.

## Update/install behavior and safety

The QML update page now reuses the existing update checker, updater and privileged helper. The implementation preserves these behavior and safety properties:

- Check the latest upstream release without blocking the GUI and validate the release redirect.
- Preserve the existing update/lastCheck, update/notifiedTag and update/autoCheck settings.
- Support clean installation when zapret2 is absent and update when a supported installation exists.
- Require explicit confirmation before installation or update.
- Keep download, archive-path validation, binary checksum verification, installation, backup and
  rollback inside the privileged helper; do not duplicate these operations in QML.
- Preserve user configuration and hostlists during updates.
- Keep the clean-install default safe: NFQWS2_ENABLE=0, with the service neither started nor enabled.
- Report cancellation, failure, rollback and service-restoration outcomes distinctly.
- Prevent update/install while blockcheck or another conflicting operation is active.
- Bound log output and ensure helper process completion is handled exactly once.
- Lock conflicting Blockcheck and dashboard service controls during update/install, then refresh the displayed service state when maintenance finishes.
- Cover version parsing, network failure, helper failure, checksum rejection, rollback and clean
  installation with automated tests.

## Quality baseline

Theme selection uses the Qt 6.8+ `QStyleHints::setColorScheme()` API. The `system` value restores the desktop color scheme; explicit light/dark requests are platform hints and may be ignored by a platform plugin that does not support application-level overrides. Kirigami and Qt Quick Controls continue to use the active platform palette rather than a project-defined hard-coded palette.

The migration follows the repository-wide [quality and engineering standards](QUALITY_STANDARDS.md),
which use the official KDE Kirigami documentation and KDE Human Interface Guidelines as their
implementation baseline.

Each migrated page must have:

- a typed, testable C++ controller/model;
- native Kirigami interaction patterns and platform theme integration;
- complete Russian and English localization;
- accessible keyboard and focus behavior;
- loading, empty, success, cancellation and failure states where applicable;
- isolated regression tests for critical workflows;
- documentation and CI coverage.

## Production validation gate

The production implementation is considered fully validated for release only when all of the following are true:

- all existing user-visible features have functional parity;
- no navigation placeholders remain;
- settings, theme, language, tray and desktop lifecycle behavior work as documented;
- update, clean-install, backup and recovery workflows are migrated and tested;
- all QML and C++ tests pass, including security-sensitive helper tests;
- the application builds and installs from supported packages on clean systems;
- Russian and English translations are complete;
- accessibility, keyboard navigation and responsive layouts have been reviewed;
- CI and package validation pass on the candidate release commit.

The production executable is Kirigami, and the obsolete Widgets GUI sources and Widgets-only tests have been removed. Remaining release gates are package installation/runtime validation, CI, translation review and accessibility review.