# Disclaimer / Отказ от ответственности

**Дата редакции / Last updated:** 2026-11-07

Этот документ описывает условия предоставления и использования zapret2control. Он не является юридической консультацией и не заменяет анализ законодательства юристом.

This document describes the terms under which zapret2control is provided and used. It is not legal advice and does not replace a legal review by a qualified lawyer.

## 1. Назначение и характер программы / Purpose and nature of the software

zapret2control — независимое программное обеспечение с открытым исходным кодом для Linux. Оно предоставляет графический интерфейс управления уже установленной службой zapret2, запускает сетевые тесты с использованием upstream-скрипта blockcheck2 и позволяет обновлять отдельную установку zapret2.

Программа взаимодействует с сетевыми механизмами zapret2, которые могут изменять обработку сетевого трафика и влиять на работу механизмов DPI. Документирование этой функциональности не является призывом использовать программу для нарушения закона или получения доступа к ресурсам вопреки применимым ограничениям.

zapret2control is independent open-source software for Linux. It provides a graphical interface for an already installed zapret2 service, runs network tests using the upstream blockcheck2 script, and can update a separate zapret2 installation.

The application interacts with zapret2 networking mechanisms, which may alter network traffic handling and affect DPI systems. Describing this functionality is not an invitation to break the law or to access resources in violation of applicable restrictions.

## 2. Соблюдение законодательства / Compliance with law

Пользователь самостоятельно обязан определить, допустимо ли использование программы в его юрисдикции, сети и конкретной ситуации, и соблюдать применимые законы, обязательные предписания, условия доступа к сетям и права третьих лиц.

Разработчик не даёт разрешений на доступ к каким-либо ресурсам и не гарантирует законность конкретного сценария использования. Пользователь не должен использовать программу для нарушения прав третьих лиц, обхода обязательных ограничений, совершения правонарушений или иных незаконных действий.

The user is responsible for determining whether use of the software is lawful in their jurisdiction, network, and circumstances, and for complying with applicable laws, binding orders, network access terms, and third-party rights.

The developer does not grant permission to access any resource and does not warrant the legality of any particular use case. Users must not use the software to infringe third-party rights, evade binding restrictions, commit offences, or engage in other unlawful activity.

## 3. Отсутствие гарантий / No warranty

Программа предоставляется «КАК ЕСТЬ» (AS IS) и «ПО МЕРЕ ДОСТУПНОСТИ» (AS AVAILABLE), без гарантий, насколько отказ от таких гарантий допускается применимым законодательством. Разработчик не гарантирует бесперебойную работу, отсутствие ошибок, совместимость со всеми системами, точность результатов проверок или постоянную работоспособность найденных параметров.

The software is provided "AS IS" and "AS AVAILABLE", without warranties to the extent permitted by applicable law. The developer does not warrant uninterrupted operation, absence of defects, compatibility with every system, accuracy of test results, or continued effectiveness of any detected settings.

## 4. Сетевые проверки и сторонние компоненты / Network tests and third-party components

Проверки выполняются на устройстве пользователя и могут создавать сетевые соединения с доменами, указанными пользователем, а также обращаться к DNS-резолверам и другим сетевым узлам, используемым его системой. Сетевые операторы и проверяемые ресурсы могут видеть соответствующий сетевой трафик и технические метаданные.

Программа использует сторонний проект zapret2. Сторонние компоненты предоставляются их правообладателями на условиях соответствующих лицензий и могут иметь собственные ограничения и условия использования. См. [NOTICE.md](NOTICE.md) и upstream-репозиторий.

Tests run on the user's device and may establish network connections to user-specified domains, as well as contact DNS resolvers and other network endpoints used by the system. Network operators and tested destinations may observe the resulting traffic and technical metadata.

The application uses the third-party zapret2 project. Third-party components are provided by their respective rightsholders under their own licenses and may have separate terms and restrictions. See [NOTICE.md](NOTICE.md) and the upstream repository.

## 5. Ограничение ответственности / Limitation of liability

В пределах, допускаемых применимым законодательством, разработчик не несёт ответственности за косвенные убытки, потерю данных, перерывы связи, повреждение конфигураций, недоступность ресурсов, действия сетевых операторов или последствия использования либо невозможности использования программы.

Ничто в этом документе не исключает и не ограничивает ответственность, которая не может быть исключена или ограничена по применимому законодательству.

To the extent permitted by applicable law, the developer shall not be liable for indirect losses, data loss, network interruptions, configuration damage, resource unavailability, actions of network operators, or consequences arising from the use or inability to use the software.

Nothing in this document excludes or limits liability that cannot be excluded or limited under applicable law.

## 6. Изменения документа / Changes to this document

Документ может обновляться вместе с развитием проекта. Пользователь должен проверять актуальную редакцию перед использованием новых версий программы.

This document may be updated as the project evolves. Users should review the current version before using new releases.
