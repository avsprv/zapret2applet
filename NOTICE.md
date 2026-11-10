# Third-party notices / Уведомления о сторонних компонентах

## zapret2

- Upstream project: https://github.com/bol-van/zapret2
- Maintainer / автор: bol-van
- Pinned submodule revision / зафиксированный коммит: `038c97e5819df10f8e8c22a2f1fffe57f2f7379b` (tag `v1.0.7`)
- Upstream license / лицензия upstream: MIT; license text: [third_party/zapret2/docs/LICENSE.txt](https://github.com/bol-van/zapret2/blob/038c97e5819df10f8e8c22a2f1fffe57f2f7379b/docs/LICENSE.txt)
- Submodule URL: `https://github.com/bol-van/zapret2.git`

The repository links zapret2 as a Git submodule. The zapret2 source tree is not copied into the zapret2control application source files, and the DEB/RPM packages built by this project do not bundle the zapret2 submodule or its binaries. The application operates on a separately installed zapret2 tree and can download upstream release files into that installation when the user initiates an update.

В репозитории zapret2 подключён как Git submodule. Исходники zapret2 не копируются в исходные файлы приложения zapret2control; собираемые этим проектом пакеты DEB/RPM не включают submodule zapret2 и его бинарные файлы. Приложение работает с отдельно установленным деревом zapret2 и может загружать файлы upstream-релиза в эту установку по инициативе пользователя.

The MIT license notice for zapret2 applies to the upstream code covered by that license. It does not automatically relicense third-party components that may be included in upstream release archives. Review the notices and license files distributed with the exact upstream release you install.

Уведомление MIT для zapret2 относится к коду upstream, охватываемому этой лицензией. Оно не переводит автоматически сторонние компоненты, которые могут входить в архивы upstream-релизов, под MIT. Проверяйте уведомления и лицензии, поставляемые с конкретным устанавливаемым upstream-релизом.

## Project independence / Независимость проектов

zapret2control is an independent project and is not affiliated with, endorsed by, or maintained by the zapret2 upstream maintainer.

zapret2control — независимый проект, не связанный с автором или сопровождающими upstream-проекта zapret2 и не одобренный ими.

## License scope / Область действия лицензии

The [LICENSE](LICENSE) file in this repository applies to zapret2control code authored for this project. It does not replace the licenses or copyright notices of zapret2 or any other third-party component.

Файл [LICENSE](LICENSE) в этом репозитории относится к коду zapret2control, созданному для данного проекта. Он не заменяет лицензии и уведомления об авторских правах zapret2 или иных сторонних компонентов.
