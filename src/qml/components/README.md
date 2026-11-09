# Shared QML components

Place reusable Qt Quick/Kirigami components here when they are used by multiple pages or expose a meaningful standalone UI contract.

Guidelines:

- Keep components focused on presentation and interaction.
- Keep domain logic, filesystem access, process management, and privileged operations in C++ backend/controllers.
- Prefer native Kirigami and Qt Quick Controls before introducing custom controls.
- Use semantic `Kirigami.Theme` colors and `Kirigami.Units` sizing.
- Declare required inputs explicitly with `required property`; avoid hidden context dependencies.
- Add accessibility metadata and verify keyboard/focus behavior.
- Add a component only when there is a real reuse or encapsulation benefit; do not extract one-off page fragments solely to populate this directory.
