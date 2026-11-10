# Для разработчиков

## Архитектурные принципы

- Production-приложение использует Qt Quick и KDE Kirigami 6. Точка входа — `src/qml/main.cpp`, корневой интерфейс — `src/qml/Main.qml`.
- `zapret2control` — единственная production-цель; отдельной preview-цели и Qt Widgets GUI больше нет. Текущие критерии завершения миграции описаны в [MIGRATION_QML_KIRIGAMI.md](MIGRATION_QML_KIRIGAMI.md).
- Повторно используемая прикладная логика и интеграции с системой размещаются в `src/backend/`. Контроллеры QML — адаптеры между UI и backend; они не должны дублировать доменную логику.
- GUI работает **без root**. Привилегированные действия выполняет `zapret2control-helper` через `pkexec` и Polkit.
- QML не запускает shell-команды, не обращается к привилегированным файлам и не вызывает `pkexec` напрямую.
- Upstream подключён как Git submodule (`third_party/zapret2`) и не изменяется в рамках обычной разработки приложения.

## Структура репозитория

```
.github/
  ISSUE_TEMPLATE/       шаблоны bug report и feature request
  workflows/            CI и сборка пакетов
data/                   desktop entry, AppStream, иконки, Polkit и systemd-шаблоны
docs/                   архитектура, разработка, стандарты качества и миграция
i18n/                   Qt Linguist .ts-файлы
packaging/
  debian/               конфигурация DEB
  fedora/               spec для RPM
  helper/               привилегированный helper и его модули
src/
  backend/              прикладная логика и системные адаптеры
  qml/
    components/         общие переиспользуемые QML-компоненты
    pages/              страницы production-интерфейса Kirigami
    *controller.*       QObject-контроллеры
    Main.qml            корневой QML-интерфейс
    main.cpp            production-точка входа QGuiApplication / QQmlApplicationEngine
  i18n.*                загрузка переводов
  profileanalyzer.*     анализ профилей
tests/                  C++, Python и shell-тесты
third_party/zapret2/    закреплённый Git submodule upstream-проекта
```

Каталоги `.reuse/` и `LICENSES/` появятся на отдельном этапе внедрения REUSE compliance. Каталог `cmake/` добавляется только при появлении собственных CMake-модулей, которые действительно нужно вынести из корневого `CMakeLists.txt`.

## Версия Qt

Минимум — **Qt 6.8 LTS** (проверяется в `CMakeLists.txt`, переменная `QZAPRET_QT_MIN`). Используйте API, совместимые с этим baseline. Для QML/Kirigami применяются Qt 6 и KDE Frameworks 6; версии в QML import statements не указываются.

## Сборка и тесты

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
cmake --build build --target zapret2control_qmllint
```

CI дополнительно проверяет desktop/AppStream metadata и собирает DEB/RPM пакеты. Не ослабляйте lint/test проверки и не отключайте категории предупреждений ради зелёной сборки.

Без установки helper находится в `packaging/helper/zapret2control-helper`; путь можно переопределить переменной окружения `QZAPRET_HELPER`.

## Форматирование C++

Настройки форматирования определены в корневом `.clang-format`. Перед форматированием больших участков сначала проверьте diff: не включайте несвязанные массовые изменения в функциональные коммиты.

## QML и Kirigami

- Используйте актуальные Qt 6 и Kirigami API без версий в import.
- Цвета берите из семантической темы Kirigami; не задавайте HEX/RGB для стандартных элементов.
- Для размеров и отступов используйте `Kirigami.Units.gridUnit`, `smallSpacing` и `largeSpacing`.
- Сохраняйте нулевой уровень QML warnings и `Unqualified access`; не отключайте категории qmllint.
- Общий визуальный компонент выделяйте в `src/qml/components/`, если он повторно используется или имеет самостоятельный контракт/поведение. Не создавайте абстракции ради одного использования.
- Подробные критерии находятся в [QUALITY_STANDARDS.md](QUALITY_STANDARDS.md).

## Переводы

Исходные строки в коде — русские; английские переводы лежат в `i18n/*.ts` и компилируются Qt Linguist. После изменения строк обновите каталоги:

```bash
cmake --build build --target zapret2control_i18n_lupdate
ctest --test-dir build -R i18n
```

Новый язык добавляется отдельным `.ts`-файлом и регистрируется в `qt_add_translations()` в `CMakeLists.txt`.

## Обновление upstream

```bash
git -C third_party/zapret2 fetch --tags
git -C third_party/zapret2 checkout <тег>
git add third_party/zapret2
git commit -m "chore: bump zapret2 submodule"
```

Upstream обновляется отдельным осознанным изменением; проверяйте совместимость и закрепляйте конкретный тег/commit.

## Пакеты

Инструкции по сборке `.deb` и `.rpm` — в [packaging/README.md](../packaging/README.md).
