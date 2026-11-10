# Linux desktop tray integration

## Status

Implemented in the production Qt Quick/Kirigami application through `TrayController`,
`DBusMenu`, and QtDBus. The application exports StatusNotifierItem and DBusMenu interfaces,
tracks watcher availability, retries registration after watcher restarts, and provides a
no-watcher fallback. Automated private-session-bus tests cover protocol marshalling, menu
structure, watcher registration/restart and action dispatch. Final package startup validation
and manual desktop lifecycle checks remain part of the migration gate.

## Decision

Implement the tray integration directly on QtDBus in the production QML application. Do not use
`QSystemTrayIcon` or KDE's `KStatusNotifierItem`: the latter exposes a `QMenu`-based
context-menu API and would reintroduce a Widgets dependency into the `QGuiApplication` process.
The production target must remain free of Qt Widgets.

Implement both protocols:

- `org.kde.StatusNotifierItem` / `org.freedesktop.StatusNotifierItem` on the session bus,
  registered through `org.freedesktop.StatusNotifierWatcher`.
- `com.canonical.dbusmenu` on a dedicated menu object path referenced by the StatusNotifierItem
  `Menu` property.

The SNI item uses the stable untranslated ID `zapret2control`, `ApplicationStatus` category,
a theme icon name, a valid tooltip structure, and `Active` / `NeedsAttention` status
transitions. It exposes activation, secondary activation, context-menu, and scroll methods. It
emits the appropriate `NewIcon`, `NewToolTip`, `NewTitle`, and `NewStatus` signals when
exported values change.

## Registration and session-bus behavior

1. Export the SNI object and DBusMenu object before registering the item with the watcher.
2. Register the application's unique service name with `RegisterStatusNotifierItem`.
3. Detect watcher appearance/disappearance through D-Bus service-owner notifications. Retry
   registration when a watcher appears; do not busy-loop when no watcher is present.
4. Treat session-bus disconnection as a recoverable state. Do not crash or block the GUI thread;
   verify disconnect/reconnect behavior during manual desktop testing.
5. Unregister exported objects and release the service name cleanly during application shutdown.
6. Do not implement a private tray host or assume that KDE Plasma is present. The watcher/host
   owns presentation.

## DBusMenu model

The menu exposes:

- Open / Show window
- Service status (informational, disabled)
- Start or Stop service (mutually exclusive and state-aware)
- separator
- Settings
- Quit

Menu item IDs are stable for the lifetime of the process. The menu is generated from current
application state; disabled actions are represented as disabled rather than silently omitted.
The DBusMenu implementation supports layout queries, group properties, event dispatch, and
about-to-show behavior. Menu actions call the same application-controller methods as the QML
interface; no privileged action is invoked directly from the D-Bus adaptor.

## Window and quit lifecycle

- Left activation toggles or presents the main window.
- Context menu is provided through DBusMenu, not an in-process `QMenu`.
- Closing the window hides it only when `tray/closeToTray` is enabled and tray registration is
  active.
- If tray registration is unavailable, closing the last window quits normally; the application
  must never leave a stranded headless process.
- Explicit Quit always exits, even when close-to-tray is enabled.
- Service monitoring and Blockcheck recovery handling continue while the window is hidden.
- If the watcher disappears while the application is hidden, the window is presented again so
  the user can still reach the application.

## Icon and state mapping

Use freedesktop theme icon names and the project's monochrome symbolic assets. Do not send a
pre-colored raster icon as the only representation.

- Running/active service and unknown state: monochrome application icon without a status tint.
- Inactive service: retain the monochrome base icon and add a small overlay using the active
  platform palette's `QPalette::Highlight` role.
- Failed service or recovery required: retain the monochrome base icon, add a small overlay using
  `QPalette::BrightText`, and use the SNI attention state with a clear menu/status tooltip.
- Do not recolor the complete application glyph; status indicators remain confined to the small
  overlay.
- Tray pixmaps are regenerated when the application palette changes. No fixed HEX/RGB colors are
  used for tray states.

## Settings and autostart

- GUI autostart is implemented by the production `AutostartController` and an XDG Autostart
  desktop entry. It does not enable or start `zapret2.service`.
- The controller writes atomically, uses the current user's config directory, marks its own
  entry, refuses to overwrite an unmanaged entry or follow a symlink, and removes only an entry
  whose ownership marker and executable match.
- The desktop entry launches the application with `--minimized`; the main window remains
  available when tray registration is unavailable.
- The Settings page exposes the close-to-tray setting. It is honored only while tray registration
  is active; without a watcher, normal window-close behavior is preserved.
- A missing tray watcher must not make GUI autostart unusable.

## Build and dependency boundary

- QtDBus is linked to the production `zapret2control` executable.
- The production target uses Qt Quick/Kirigami and does not discover or link Qt Widgets.
- The former Widgets GUI sources and Widgets-only tests have been removed.
- Do not link `KF6::StatusNotifierItem` or `QMenu` into the application.
- The tray implementation is included in the installed DEB and RPM packages.

## Manual desktop verification

Before release, verify on a real Plasma 6 session and on a desktop without a StatusNotifierWatcher:

- Close-to-tray hides only while tray registration is active; otherwise the application exits.
- Explicit Quit always exits, including when close-to-tray is enabled.
- The application restores its window if the watcher disappears while it is hidden.
- Session-bus disconnect/reconnect does not leave a stranded process or permanently stale tray item.
- Inactive/failed tray overlays update after light/dark palette changes.
- Desktop autostart with `--minimized` shows the window when no watcher is available.

## Automated tests

- SNI introspection includes required properties, methods, and signals with exact D-Bus signatures.
- Tooltip and icon property marshalling follows the SNI specification.
- Watcher absence at startup, later appearance, and watcher restart.
- DBusMenu layout returns the correct recursive structure, property filtering, revision, and stable IDs.
- Event dispatch rejects unknown IDs and unsupported events without invoking actions.
- Start/stop menu actions are enabled only for valid application states and route through
  ApplicationController.
- Production target links QtDBus and has no Qt Widgets dependency.
- XDG autostart creates and removes only a managed entry, refuses unmanaged files and symlinks,
  and preserves user-owned entries.
- Tests use a private D-Bus session and fake service backend; they do not control the host's real
  zapret2 service.

## Upstream specifications

- StatusNotifierItem: https://specifications.freedesktop.org/status-notifier-item/latest-single/
- DBusMenu interface: https://sources.debian.org/src/plasma-workspace/4%3A5.27.5-2%2Bdeb12u2/libdbusmenuqt/com.canonical.dbusmenu.xml
- KDE KStatusNotifierItem API (reference only; not used by the application):
  https://api.kde.org/kstatusnotifieritem.html
