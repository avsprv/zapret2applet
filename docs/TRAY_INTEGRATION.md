# Linux desktop tray integration

## Status

Architecture and acceptance criteria for the QML/Kirigami migration. This document does not claim that tray support is implemented. The production Widgets application remains unchanged until the QML implementation has passed protocol, lifecycle, packaging, and desktop integration tests.

## Decision

Implement the tray integration directly on QtDBus in the QML target. Do not use `QSystemTrayIcon` or KDE's `KStatusNotifierItem` for this target: the latter exposes a `QMenu`-based context-menu API and would pull the Widgets UI stack into the QGuiApplication process. The QML target must remain free of Qt Widgets.

Implement both protocols:

- `org.kde.StatusNotifierItem` / `org.freedesktop.StatusNotifierItem` on the session bus, registered through `org.freedesktop.StatusNotifierWatcher`.
- `com.canonical.dbusmenu` on a dedicated menu object path referenced by the StatusNotifierItem `Menu` property.

The SNI item must use a stable untranslated ID, `ApplicationStatus` category, a theme icon name, a valid tooltip structure, and `Active`/ `Passive` status transitions. It must expose activation, secondary activation, context-menu, and scroll methods. It must emit the appropriate `NewIcon`, `NewToolTip`, `NewTitle`, and `NewStatus` signals when exported values change.

## Registration and session-bus behavior

1. Export the SNI object and DBusMenu object before registering the item with the watcher.
2. Register the application's unique service name with `RegisterStatusNotifierItem`.
3. Detect watcher appearance/disappearance through D-Bus service-owner notifications. Retry registration when a watcher appears; do not busy-loop when no watcher is present.
4. Treat session-bus disconnection as a recoverable state. Do not crash or block the GUI thread.
5. Unregister exported objects and release the service name cleanly during application shutdown.
6. Do not implement a private tray host or assume that KDE Plasma is present. The watcher/host owns presentation.

## DBusMenu model

Expose a small, stable menu:

- Open / Show window
- Service status (informational, disabled)
- Start or Stop service (mutually exclusive and state-aware)
- separator
- Settings
- Quit

Menu item IDs are stable for the lifetime of the process. The menu is generated from current application state; disabled actions must be represented as disabled rather than silently omitted. The DBusMenu implementation must support the protocol's layout query, group properties, event dispatch, and about-to-show behavior. Menu actions must call the same application-controller methods as the QML interface; no privileged action may be invoked directly from the D-Bus adaptor.

## Window and quit lifecycle

- Left activation toggles or presents the main window.
- Context menu is provided through DBusMenu, not an in-process `QMenu`.
- Closing the window hides it only when `tray/closeToTray` is enabled and tray registration is active.
- If tray registration is unavailable, closing the last window must quit normally; never strand a headless process.
- Explicit Quit always exits, even when close-to-tray is enabled.
- Set `quitOnLastWindowClosed` only in a way that preserves the fallback behavior above.
- When the application is hidden, service monitoring and Blockcheck recovery status handling continue to work.

## Icon and state mapping

Use freedesktop theme icon names and the project's Breeze-inspired monochrome assets. Do not send a pre-colored raster icon as the only representation.

- Running/active service and unknown state: monochrome application icon without a status tint.
- Inactive service: retain the monochrome base icon and add a yellow status overlay/dot.
- Failed service or recovery required: retain the monochrome base icon, add a red status overlay/dot, and use the SNI attention state with a clear menu/status tooltip.
- Do not recolor the complete application glyph; status colors must be confined to the small overlay so the icon remains visually minimal.
- Light/dark icon rendering is delegated to theme-compatible symbolic assets where possible. Any bundled fallback must be monochrome and scalable.

## Settings and autostart

- GUI autostart is implemented in the QML preview through `AutostartController` and an XDG Autostart desktop entry. It does not enable or start `zapret2.service`.
- The controller writes atomically, uses the current user's config directory, marks its own entry, refuses to overwrite an unmanaged entry or follow a symlink, and removes only an entry whose ownership marker and executable match.
- The setting is exposed on the QML Settings page. It launches the application normally until tray support is implemented; it does not pass `--minimized` yet.
- Re-enable `tray/closeToTray` in QML settings only after the tray registration lifecycle and no-watcher fallback are tested.
- A missing tray watcher must not make GUI autostart unusable.

## Build and dependency boundary

- Add only `Qt6::DBus` to the QML preview target.
- Keep the production Widgets target and its dependency graph unchanged during migration.
- Do not link `Qt6::Widgets`, `KF6::StatusNotifierItem`, or `QMenu` into the QML target.
- Package the tray implementation only when the QML executable is promoted to the installed target.

## Required automated tests

- SNI introspection includes required properties, methods, and signals with exact D-Bus signatures.
- Tooltip and icon property marshalling follows the SNI specification.
- Watcher absent at startup, watcher appears later, watcher restarts, and session bus disconnects.
- DBusMenu layout returns the correct recursive structure, property filtering, revision, and stable IDs.
- Event dispatch rejects unknown IDs and unsupported events without invoking actions.
- Start/stop menu actions are enabled only for valid application states and route through ApplicationController.
- Close-to-tray hides only when registration is available; otherwise the application exits.
- Explicit Quit always exits.
- QML target links QtDBus and does not link Qt Widgets.
- XDG autostart creates and removes only a managed entry, refuses unmanaged files and symlinks, and preserves user-owned entries.
- No tests call the host session bus or control the host's real zapret2 service; use a private D-Bus test bus and fake service backend.

## Upstream specifications

- StatusNotifierItem: https://specifications.freedesktop.org/status-notifier-item/latest-single/
- DBusMenu interface: https://sources.debian.org/src/plasma-workspace/4%3A5.27.5-2%2Bdeb12u2/libdbusmenuqt/com.canonical.dbusmenu.xml
- KDE KStatusNotifierItem API (reference only; not used by the QML target): https://api.kde.org/kstatusnotifieritem.html
