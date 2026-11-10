# Сборка пакетов zapret2control

Минимум для сборки — **Qt 6.8** (см. `QZAPRET_QT_MIN` в `CMakeLists.txt`). Подходят дистрибутивы с Qt >= 6.8:
Fedora 41+, Debian 13 (trixie)+, Ubuntu 25.04+. На Ubuntu 24.04 LTS и Debian 12 в репозиториях Qt 6.4 —
пакет там не соберётся. Проверка версии: `apt-cache policy qt6-base-dev` / `dnf info qt6-qtbase-devel`.

Пакет не содержит zapret2: он устанавливается из GUI (вкладка «Обновления») в `/opt/zapret2`.

## RPM (Fedora)

```bash
sudo dnf install rpm-build rpmdevtools mock
rpmdev-setuptree
packaging/make-dist.sh                      # нужен тег v<версия>: git tag -a v0.5.0 -m v0.5.0
cp zapret2control-0.5.0.tar.gz ~/rpmbuild/SOURCES/
cp packaging/fedora/zapret2control.spec ~/rpmbuild/SPECS/
rpmbuild -ba ~/rpmbuild/SPECS/zapret2control.spec

# чистая сборка в chroot (как в COPR)
mock -r fedora-$(rpm -E %fedora)-x86_64 ~/rpmbuild/SRPMS/zapret2control-0.5.0-1.*.src.rpm
```
Если тег уже на GitHub, архив можно скачать вместо `make-dist.sh`: `spectool -g -R packaging/fedora/zapret2control.spec`.

## DEB (Debian / Ubuntu)

Файлы лежат в `packaging/debian/`, а `dpkg-buildpackage` ищет каталог `debian/` в корне, поэтому перед
сборкой делается симлинк (он добавлен в `.gitignore`). Формат `3.0 (native)`: архив-оригинал не нужен.
В чистом контейнере:
```bash
docker run --rm -v "$PWD":/src -w /src debian:trixie bash -euxc '
  ln -sfn packaging/debian debian
  apt-get update
  apt-get install -y --no-install-recommends build-essential devscripts equivs
  mk-build-deps -ir -t "apt-get -y --no-install-recommends" debian/control
  dpkg-buildpackage -us -uc -b
  mkdir -p out && cp ../zapret2control_*.deb out/'
```
Или на самой машине (Debian 13 / Ubuntu 25.04+):
```bash
sudo apt install build-essential devscripts equivs
ln -sfn packaging/debian debian
sudo mk-build-deps -ir debian/control
dpkg-buildpackage -us -uc -b                # .deb окажется на уровень выше
```
Смена версии: обновить `project(VERSION)` в `CMakeLists.txt`, добавить запись в `packaging/debian/changelog` и изменить `Version:` в RPM spec. RPM workflow получает номер версии непосредственно из `CMakeLists.txt`.
Если понадобится PPA или Debian-архив — перевести на `3.0 (quilt)` с отдельным `.orig.tar.gz`.

## Проверка готовых пакетов

```bash
# deb
dpkg -I out/zapret2control_*.deb              # зависимости
dpkg -c out/zapret2control_*.deb              # состав: bin, libexec, polkit, desktop
lintian -I out/zapret2control_*.deb
sudo apt install ./out/zapret2control_*.deb   # установка с зависимостями

# rpm
rpm -qpl ~/rpmbuild/RPMS/x86_64/zapret2control-*.rpm
rpm -qpR ~/rpmbuild/RPMS/x86_64/zapret2control-*.rpm
rpmlint ~/rpmbuild/RPMS/x86_64/zapret2control-*.rpm ~/rpmbuild/SRPMS/*.src.rpm
sudo dnf install ~/rpmbuild/RPMS/x86_64/zapret2control-*.rpm

# после установки (любой дистрибутив)
pkaction | grep zapret2control                # два действия: helper и update
zapret2control --version
ls -l /usr/libexec/zapret2control/ /usr/lib/*/zapret2control/ 2>/dev/null   # хелпер root:root 0755
```
