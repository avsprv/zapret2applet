# Для разработчиков

## Принципы

- GUI работает **без root**. Привилегированные действия выполняет `zapret2control-helper`
  (`/usr/libexec/zapret2control/`) через `pkexec` и polkit-действия `org.zapret2control.helper` и
  `org.zapret2control.update` (второе спрашивает пароль каждый раз, без `auth_admin_keep`).
- Хелпер принимает только фиксированный набор команд (`unit start|stop|enable|disable`, `blockcheck`,
  `blockcheck-cancel`, `update`), строго валидирует аргументы, запускает blockcheck2 с чистым окружением,
  а обновление скачивает только по HTTPS и сверяет `sha256sum.txt` релиза.
- Вся работа с zapret2 изолирована в `src/backend/` (интерфейс `ZapretBackend`).
- Апстрим подключён как submodule (`third_party/zapret2`) и **не правится**; версия закреплена тегом.
  В сборку он не входит, это закреплённая ссылка на проверенную версию.

## Версия Qt

Минимум — **Qt 6.8 LTS** (проверяется в `CMakeLists.txt`, переменная `QZAPRET_QT_MIN`). API новее 6.8 не
используются. Сборка проверена на Qt 6.8.4; на Qt 6.12 LTS проверим, когда он появится в дистрибутивах.

## Структура

```
src/                  Qt6-код GUI (src/i18n.* — подключение переводов)
src/backend/          адаптер к zapret2 (systemd), blockcheck2, проверка и установка обновлений
i18n/                 переводы (.ts)
data/                 .desktop и шаблон polkit-политики
tests/                QtTest и shell-тесты хелпера
packaging/helper/     zapret2control-helper — привилегированная часть
packaging/fedora/     spec для RPM
packaging/debian/     файлы для DEB (для сборки линкуются в debian/, см. packaging/README.md)
third_party/zapret2/  апстрим, submodule
```

## Тесты

```bash
ctest --test-dir build --output-on-failure
# парсер blockcheck, переводы, сравнение версий, валидация и обновление в хелпере
```

Без установки хелпер находится по `../packaging/helper/zapret2control-helper`; путь можно переопределить
переменной окружения `QZAPRET_HELPER`.

## Переводы

Исходные строки в коде — русские (`tr("…")`), английский перевод лежит в `i18n/zapret2control_en.ts` и
вшивается в бинарник. Для остальных языков без собственного перевода показывается английский. После правки строк:

```bash
cmake --build build --target update_translations   # lupdate: обновит .ts
# дописать переводы в i18n/zapret2control_en.ts (Qt Linguist или любой редактор), затем:
ctest --test-dir build -R i18n                      # нет пустых и незавершённых, плейсхолдеры %1 совпадают
```

Новый язык: добавить `i18n/zapret2control_<код>.ts` в `qt_add_translations()` в `CMakeLists.txt`.

## Обновление апстрима

```bash
git -C third_party/zapret2 fetch --tags
git -C third_party/zapret2 checkout <тег>
git add third_party/zapret2 && git commit -m "Bump zapret2 to <тег>"
```

## Пакеты

Инструкции по сборке `.deb` и `.rpm` — в [packaging/README.md](../packaging/README.md).
