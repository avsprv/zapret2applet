# Quality and engineering standards

This document defines the acceptance criteria for changes to zapret2control. It applies to the
production Qt Quick/Kirigami application and its C++ backend.

These rules use the official KDE Kirigami documentation and KDE Human Interface Guidelines as
the design and implementation baseline. They do not imply that zapret2control is an official KDE
project or has passed KDE project review.

## 1. Architecture

- QML owns presentation, navigation, layout, accessibility metadata, and user interaction.
- C++ owns business logic, validation, process lifecycle, filesystem access, and data models.
- Reusable domain logic belongs in backend classes and must not be duplicated between QML
  controllers and backend implementations.
- Expose backend functionality through typed QObject properties, signals, slots, and
  Q_INVOKABLE methods. Prefer explicit models (QAbstractItemModel) for growing or structured
  collections instead of encoding domain data as presentation-specific JavaScript objects.
- Long-running work must be asynchronous. Controllers must expose busy/progress/error state,
  prevent duplicate operations, and handle cancellation and process completion safely.
- QML must not execute shell commands, access privileged files, or invoke pkexec directly.
  All privileged operations must pass through the allowlisted Polkit helper.
- Keep Qt Quick/Kirigami as the sole production GUI. Do not reintroduce a parallel Widgets UI or
  retain obsolete Widgets-only application sources.

## 2. Kirigami and Qt Quick UI

- Use Kirigami components and Qt Quick Controls before implementing custom controls.
- Use Kirigami page/navigation patterns, actions, dialogs, form layouts, inline messages,
  progress indicators, and list delegates according to the task.
- Use Kirigami.Units for spacing and sizing conventions. Avoid arbitrary fixed dimensions
  unless required by an editor, terminal-like view, or other content with a justified minimum.
- Use Kirigami.Theme and the active platform palette. Do not hard-code colors for standard
  controls, window chrome, toolbars, or dialogs.
- Custom status colors are allowed only where they communicate semantic state. They must remain
  distinguishable in light and dark themes and must not be the only means of communicating state.
- Support keyboard navigation, visible focus, sensible tab order, screen-reader labels, and
  accessible names for icon-only actions.
- Ensure layouts remain usable at the supported minimum window size and when text is translated
  or scaled. Do not rely on fixed-width dialogs or controls that clip translated text.
- Use standard KDE icons where possible. Application-specific tray icons must be monochrome and
  adapt to the desktop theme; semantic Inactive/Failed states may use the approved yellow/red
  status treatment.
- Respect desktop conventions for window lifecycle, tray behavior, shortcuts, and confirmation
  dialogs. Do not silently discard user changes.

## 3. Localization and application metadata

- All user-visible strings must be translatable. Do not introduce untranslated literal UI text.
- Use one consistent localization pipeline for C++ and QML. QML translations must be extracted,
  compiled, included in packages, and tested; merely wrapping strings in qsTr() is not sufficient.
- Add translator context/comments for ambiguous strings and preserve placeholders in translations.
- Keep Russian and English translations in sync. A change is not complete while required
  translations are missing or unfinished.
- Register application metadata centrally, including application name, version, description,
  license, authorship, website/repository, and desktop integration data.
- The About page must expose version, license, project links, acknowledgements, and third-party
  components with their applicable licenses.

## 4. Settings and compatibility

- Preserve existing QSettings organization/application identity and established keys unless a
  deliberate migration is documented and tested.
- Settings shown as configurable must actually affect runtime behavior. If a setting is not
  implemented, it must be clearly marked unavailable and must not imply that it works.
- Test defaults, persistence, invalid values, and migration from existing installations.
- Do not change existing configuration formats or upstream zapret2 semantics without an explicit
  compatibility review.

## 5. Security and reliability

- The GUI must run as an unprivileged user.
- Keep the helper command surface allowlisted and validate every argument at the privilege
  boundary. Never trust validation performed only in the GUI.
- Treat configuration, profile, and list content as untrusted input. Do not evaluate shell, Lua,
  or arbitrary scripts to parse it.
- Protect writes against stale data, use safe temporary files, create backups where required,
  and replace files atomically.
- Avoid symlink/path traversal vulnerabilities and unsafe ownership or permission assumptions.
- Do not log secrets, authorization data, or unnecessary personal/network data.
- Report authorization cancellation, helper failures, timeouts, and partial failures accurately.
  Never show success before the operation has completed successfully.

## 6. C++ and QML code quality

- Follow the repository's C++20 and Qt 6.8+ compatibility baseline.
- Prefer explicit types, clear ownership, RAII, const correctness, and Qt parent ownership
  where appropriate.
- Avoid blocking waits on the GUI thread. Use asynchronous QProcess and event-driven APIs.
- Guard process completion/error paths against double finalization and use-after-free.
- Keep QML components focused. Move reusable UI into separate components and domain behavior
  into C++ controllers/models.
- Avoid duplicated state and fragile string parsing of status messages. Expose typed status/error
  properties where practical.
- Keep public controller APIs documented and stable enough for their QML consumers.

## 7. Build system and repository structure

- Declare all build and runtime dependencies explicitly. QML imports must be discoverable and
  validated during configuration/build where supported.
- Keep the QML module URI, target names, install paths, and resource layout consistent.
- Use ECM/KDE CMake facilities where they materially improve integration, dependency checking,
  installation, translations, and compiler diagnostics.
- Keep build targets and tests independently understandable. The production target must not
  discover or link Qt Widgets; no Widgets GUI target is maintained.
- Install desktop entries, icons, translations, helper/polkit policy, and application metadata
  into standard locations.
- Keep README instructions, development documentation, package metadata, and actual build
  requirements consistent.
- Keep third-party code and upstream submodules pinned and clearly attributed. Do not silently
  patch vendored upstream sources.

## 8. Testing and CI

Every change must pass the relevant checks before the next migration stage proceeds.

Required baseline:
- Configure and build the production `zapret2control` Qt Quick/Kirigami target.
- Verify that Qt Widgets is absent from the production dependency graph.
- Run the complete CTest suite.
- Run QML static validation/linting and fail CI on QML syntax or import errors.
- Check translation extraction/compilation and placeholder consistency.
- Validate desktop and AppStream metadata.
- Build DEB and RPM packages and run package lint/smoke checks.
- Test helper behavior independently, including invalid arguments, permissions, symlinks,
  oversized input, stale configuration, and failure paths.
- Add regression tests for every fixed defect. Prefer isolated tests that never touch the host
  system configuration or system services.

Where practical, CI should cover the minimum supported Qt/KF versions as well as a current
distribution environment. A green build alone is not proof of runtime correctness.

## 9. Documentation and contribution hygiene

- Keep Russian and English README files equivalent in meaning and current with actual behavior.
- Document architecture, dependencies, build/test commands, security boundaries, packaging,
  supported platforms, and known limitations.
- Add or update user documentation when behavior, settings, installation, or troubleshooting
  changes.
- Use focused commits with descriptive messages. Keep unrelated changes separate.
- Never commit generated build output, secrets, local configuration, or unreviewed binaries.
- Record migration decisions and deviations from KDE recommendations in the migration document.

## 10. QML migration completion gate

The production QML/Kirigami application is considered fully migrated only when all of the
following are true:

- Every production feature has a QML equivalent or an explicitly approved documented removal.
- QML UI has no placeholder navigation pages or non-functional controls.
- Themes, language selection, tray behavior, startup behavior, and settings work end to end.
- Application metadata, About page, translations, desktop integration, and icons are complete.
- Accessibility, keyboard navigation, minimum-size layouts, and light/dark themes are reviewed.
- Controller/model unit and integration tests cover success, failure, cancellation, and stale data.
- The production QML target installs and launches correctly from both DEB and RPM packages.
- The repository contains no obsolete Qt Widgets GUI sources, targets, or Widgets-only tests.
- Full CI, package checks, and manual desktop smoke tests pass.
- Existing user settings and configuration remain compatible.

A migration stage is complete only when its implementation, tests, documentation, and CI checks
are all complete. Do not merge or release based solely on a successful compile.
