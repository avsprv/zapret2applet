# Privacy Policy / Политика конфиденциальности

**Дата редакции / Last updated:** 2026-11-07

Эта политика описывает сетевые взаимодействия и локальные настройки, выявленные в исходном коде zapret2control. Она относится к приложению и не заменяет политики GitHub, upstream-проекта zapret2, DNS-провайдера, интернет-провайдера или проверяемых сайтов.

This policy describes the network interactions and local settings identified in the zapret2control source code. It covers the application only and does not replace the policies of GitHub, the zapret2 upstream project, DNS providers, internet service providers, or tested websites.

## 1. Сбор данных разработчиком / Data collection by the developer

В проверенной версии приложения не обнаружены учётные записи, аналитика, рекламные идентификаторы, телеметрия или сервер разработчика, на который приложение отправляло бы пользовательские данные. Приложение не отправляет разработчику список проверяемых доменов, результаты тестирования или локальные настройки.

Это описание относится к поведению приложения в указанной версии и может измениться в будущих версиях. Оно не описывает данные, которые отдельно собирает платформа GitHub или стороннее программное обеспечение.

In the reviewed application version, no user accounts, analytics, advertising identifiers, telemetry, or developer-operated backend receiving user data were identified. The application does not send the developer the list of tested domains, test results, or local settings.

This description applies to the application's behavior in the stated version and may change in future versions. It does not describe data collected independently by GitHub or third-party software.

## 2. Проверка обновлений / Update checks

Автоматическая проверка обновлений включена по умолчанию и выполняется не чаще одного раза в сутки. Приложение отправляет HTTPS HEAD-запрос к адресу:

`https://github.com/bol-van/zapret2/releases/latest`

В запросе используется User-Agent вида `zapret2control/<версия>`. При ручном запуске обновления привилегированный helper загружает архив релиза и файл контрольных сумм с GitHub по HTTPS.

GitHub может получать IP-адрес устройства, время запроса, User-Agent и иные технические данные, предусмотренные его инфраструктурой и политиками. Эти данные передаются непосредственно GitHub; разработчик zapret2control не получает их через отдельный сервер приложения.

Automatic update checks are enabled by default and run no more than once per day. The application sends an HTTPS HEAD request to:

`https://github.com/bol-van/zapret2/releases/latest`

The request uses a User-Agent in the form `zapret2control/<version>`. When an update is initiated, the privileged helper downloads the release archive and checksum file from GitHub over HTTPS.

GitHub may receive the device's IP address, request time, User-Agent, and other technical data handled by its infrastructure and policies. This information is sent directly to GitHub; the zapret2control developer does not receive it through an application-operated server.

## 3. Сетевые тесты / Network diagnostics

При запуске проверки приложение передаёт указанные пользователем домены и параметры upstream-скрипту `blockcheck2.sh`, входящему в установленную версию zapret2. Скрипт выполняет сетевые запросы к выбранным доменам по выбранным протоколам и IP-версиям.

В зависимости от настроек и поведения upstream-скрипта, сетевые обращения могут быть видны:
- DNS-резолверу, используемому системой;
- интернет-провайдеру и операторам промежуточной сети;
- владельцам и серверам проверяемых доменов.

Такие запросы выполняются с устройства пользователя. Приложение не проксирует их через сервер разработчика и не загружает результаты тестирования разработчику автоматически. Сетевые журналы, создаваемые самим zapret2/blockcheck2, регулируются поведением и настройками соответствующей версии upstream-проекта.

When a test is started, the application passes the user-specified domains and options to the `blockcheck2.sh` script included in the installed zapret2 version. The script sends network test requests to the selected domains using the selected protocols and IP versions.

Depending on the selected options and upstream script behavior, network requests may be visible to:
- the DNS resolver configured on the system;
- the internet service provider and intermediate network operators;
- the owners and servers of the tested domains.

These requests originate from the user's device. The application does not proxy them through a developer-operated server and does not automatically upload test results to the developer. Any network logs created by zapret2/blockcheck2 are governed by the behavior and settings of the relevant upstream version.

## 4. Локальные настройки / Local settings

Приложение сохраняет локальную настройку автоматической проверки обновлений через механизм Qt `QSettings`. Она хранится в пользовательском профиле согласно конфигурации Qt и операционной системы. Приложение не использует эту настройку для идентификации пользователя.

Результаты и журнал текущего сеанса отображаются в окне приложения. Если пользователь самостоятельно публикует журналы в GitHub Issues или передаёт их разработчику, он сам определяет содержание передаваемой информации. Перед публикацией следует удалить IP-адреса, домены, имена пользователей, пути и другие сведения, которые пользователь не хочет раскрывать.

The application stores the automatic-update preference locally using Qt's `QSettings` mechanism. It is kept in the user's profile according to Qt and operating-system configuration. This setting is not used by the application to identify the user.

Current-session results and logs are displayed in the application window. If a user voluntarily posts logs in GitHub Issues or sends them to the developer, the user controls the information disclosed. Before sharing, remove IP addresses, domains, usernames, file paths, and other information that should not be made public.

## 5. Сторонние сервисы / Third-party services

При обращении к GitHub применяются условия и политика конфиденциальности GitHub: https://docs.github.com/en/site-policy/privacy-policies/github-privacy-statement

При сетевых тестах применяются условия и политики соответствующих DNS-провайдеров, сетевых операторов и проверяемых ресурсов. Разработчик zapret2control не контролирует их обработку данных.

GitHub's terms and privacy statement apply when the application connects to GitHub: https://docs.github.com/en/site-policy/privacy-policies/github-privacy-statement

The terms and privacy policies of the relevant DNS providers, network operators, and tested destinations apply to network tests. The zapret2control developer does not control their data processing.

## 6. Изменения политики / Policy changes

Политика может обновляться при изменении сетевых взаимодействий приложения. Актуальная версия публикуется в этом репозитории. Если в будущем появится сбор или передача данных разработчику, описание будет обновлено до выпуска такой функциональности.

This policy may be updated when the application's network behavior changes. The current version is published in this repository. If developer-side data collection or transmission is introduced in the future, this description will be updated before that functionality is released.
