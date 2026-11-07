#!/bin/sh
# Офлайн-тесты команд `install` и `update` хелпера: фейковый релиз по file://, без сети и без root.
# Переопределения QZAPRET_TEST_* работают, только если хелпер лежит вне системных каталогов.
H="${1:?usage: test_update_helper.sh /path/to/zapret2applet-helper}"
fail=0
W=$(mktemp -d "${TMPDIR:-/tmp}/qz-upd.XXXXXX") || exit 1
trap 'rm -rf "$W"' EXIT

# The admission guard queries both installed systemd units before updating.
SYSTEMCTL=$W/systemctl
cat > "$SYSTEMCTL" <<'EOS'
#!/bin/sh
[ "$1" = show ] || exit 1
for unit do :; done
case "$unit" in
    zapret2applet-blockcheck.service|zapret2applet-blockcheck-recovery.service) ;;
    *) exit 4 ;;
esac
printf 'LoadState=loaded\nActiveState=inactive\nSubState=dead\nResult=success\nExecMainStatus=0\n'
EOS
chmod 700 "$SYSTEMCTL"

ok()  { echo "ok: $*"; }
bad() { echo "FAIL: $*"; fail=1; }

# make_release <tag> [bad_sums] [unsafe] [symlink] [broken_bin]
make_release() {
    tag=$1; name=zapret2-$tag; rel=$W/rel/$tag
    rm -rf "$W/src" "$rel"; mkdir -p "$W/src/$name"/docs "$W/src/$name"/nfq2 "$W/src/$name"/ip2net "$W/src/$name"/mdig "$W/src/$name"/common "$W/src/$name"/init.d/sysv "$W/src/$name"/init.d/systemd "$W/src/$name"/binaries/linux-x86_64 "$rel"
    ver=${tag#v}
    printf '1.0.0\n\n* old\n\n%s\n\n* new\n' "$ver" > "$W/src/$name/docs/changes.txt"
    echo '#!/bin/sh' > "$W/src/$name/blockcheck2.sh"
    cat > "$W/src/$name/install_bin.sh" <<'EOS'
#!/bin/sh
D=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$D/nfq2" "$D/ip2net" "$D/mdig"
ln -fs ../binaries/linux-x86_64/nfqws2 "$D/nfq2/nfqws2"
ln -fs ../binaries/linux-x86_64/ip2net "$D/ip2net/ip2net"
ln -fs ../binaries/linux-x86_64/mdig "$D/mdig/mdig"
EOS
    if [ "$5" = broken_bin ]; then
        printf '#!/bin/sh\nexit 1\n' > "$W/src/$name/binaries/linux-x86_64/nfqws2"
    else
        printf '#!/bin/sh\necho "nfqws2 %s"\n' "$ver" > "$W/src/$name/binaries/linux-x86_64/nfqws2"
    fi
    printf '#!/bin/sh\ncat\n' > "$W/src/$name/binaries/linux-x86_64/ip2net"
    printf '#!/bin/sh\nexit 0\n' > "$W/src/$name/binaries/linux-x86_64/mdig"
    printf 'NFQWS2_ENABLE=0\n' > "$W/src/$name/config.default"
    printf '#!/bin/sh\nexit 0\n' > "$W/src/$name/init.d/sysv/zapret2"
    printf '[Unit]\nAfter=network-online.target\n[Service]\nType=forking\nExecStart=/opt/zapret2/init.d/sysv/zapret2 start\nExecStop=/opt/zapret2/init.d/sysv/zapret2 stop\n[Install]\nWantedBy=multi-user.target\n' > "$W/src/$name/init.d/systemd/zapret2.service"
    : > "$W/src/$name/common/base.sh"
    chmod +x "$W/src/$name"/*.sh "$W/src/$name/init.d/sysv/zapret2" "$W/src/$name/binaries/linux-x86_64/"*
    [ "$4" = symlink ] && ln -s /etc/passwd "$W/src/$name/docs/evil-link"
    if [ "$6" = oversized ]; then
        dd if=/dev/zero of="$W/src/$name/oversized.bin" bs=1 count=0 seek=536870913 2>/dev/null
    fi
    ( cd "$W/src" && sha256sum "$name/binaries/linux-x86_64/nfqws2" "$name/binaries/linux-x86_64/ip2net" "$name/binaries/linux-x86_64/mdig" > "$rel/sha256sum.txt" )
    if [ "$2" = bad_sums ]; then
        awk 'NR == 1 { first = substr($0, 1, 1); $0 = (first == "0" ? "1" : "0") substr($0, 2) } { print }' \
            "$rel/sha256sum.txt" > "$rel/sha256sum.tmp"
        mv "$rel/sha256sum.tmp" "$rel/sha256sum.txt"
    fi
    if [ "$3" = unsafe ]; then
        ( cd "$W/src" && tar -czf "$rel/$name.tar.gz" --transform "s|^$name/docs/changes.txt|$name/../evil|" "$name" )
    else
        ( cd "$W/src" && tar -czf "$rel/$name.tar.gz" "$name" )
    fi
}

# make_base — «установленный» старый zapret2 с пользовательскими файлами
make_base() {
    rm -rf "$W/opt" "$W/opt.bak"; B=$W/opt/zapret2; mkdir -p "$B/docs" "$B/ipset" "$B/nfq2"
    printf '1.0.0\n\n1.0.6\n' > "$B/docs/changes.txt"
    echo 'install_bin.sh' > "$B/install_bin.sh"
    echo 'MY_CONFIG=1' > "$B/config"
    echo 'mysite.example' > "$B/ipset/zapret-hosts-user.txt"
}

run_update() {
    QZAPRET_TEST_BASE=$W/opt/zapret2 QZAPRET_TEST_RUNDIR=$W/run QZAPRET_TEST_NO_SERVICE=1 \
    QZAPRET_TEST_SYSTEMCTL=$SYSTEMCTL QZAPRET_TEST_URLBASE="file://$W/rel" "$H" update "$1" 2>&1
}

expect_untouched() {
    [ "$(cat "$W/opt/zapret2/config")" = MY_CONFIG=1 ] && grep -q '^1.0.6$' "$W/opt/zapret2/docs/changes.txt" \
        && [ ! -e "$W/opt/zapret2/nfq2/nfqws2" ] || bad "$1: installation was modified"
}

# --- 1. успешное обновление
make_base; make_release v9.9.9
out=$(run_update v9.9.9); rc=$?
[ $rc -eq 0 ] && ok "update succeeds" || bad "update rc=$rc: $out"
[ "$(cat "$W/opt/zapret2/config")" = MY_CONFIG=1 ] && ok "user config preserved" || bad "config lost"
grep -q mysite.example "$W/opt/zapret2/ipset/zapret-hosts-user.txt" && ok "user hostlist preserved" || bad "hostlist lost"
grep -q '^9.9.9$' "$W/opt/zapret2/docs/changes.txt" && ok "new version installed" || bad "version not updated"
[ "$("$W/opt/zapret2/nfq2/nfqws2")" = "nfqws2 9.9.9" ] && ok "binary linked and runs" || bad "binary link broken"
grep -q '^1.0.6$' "$W/opt/zapret2.bak/docs/changes.txt" && ok "backup of previous version kept" || bad "no backup"
[ ! -e "$W/run/update.lock" ] && ok "lock released" || bad "lock left behind"

# --- 2. подменённый sha256sum.txt: ничего не меняется
make_base; make_release v9.9.9 bad_sums
out=$(run_update v9.9.9); rc=$?
[ $rc -eq 2 ] && echo "$out" | grep -q 'checksum' && ok "checksum mismatch rejected" || bad "bad sums rc=$rc: $out"
expect_untouched "bad sums"

# --- 3. небезопасные пути в архиве
make_base; make_release v9.9.9 "" unsafe
out=$(run_update v9.9.9); rc=$?
[ $rc -eq 2 ] && echo "$out" | grep -q "unsafe paths" && ok "unsafe archive rejected" || bad "unsafe archive rc=$rc: $out"
expect_untouched "unsafe"

# --- 4. символические ссылки в архиве
make_base; make_release v9.9.9 "" "" symlink
out=$(run_update v9.9.9); rc=$?
[ $rc -eq 2 ] && echo "$out" | grep -q 'links' && ok "symlink in archive rejected" || bad "symlink rc=$rc: $out"
expect_untouched "symlink"

# --- 5. распаковываемый объём ограничен до извлечения архива
make_base; make_release v9.9.9 "" "" "" "" oversized
out=$(run_update v9.9.9); rc=$?
[ $rc -eq 2 ] && echo "$out" | grep -q "extraction limits" && ok "oversized archive rejected before extraction" || bad "oversized archive rc=$rc: $out"
expect_untouched "oversized archive"

# --- 6. версия в архиве не совпадает с тегом
make_base; make_release v9.9.9; mkdir -p "$W/rel/v9.9.8"; cp "$W/rel/v9.9.9/"* "$W/rel/v9.9.8/"
out=$(run_update v9.9.8); rc=$?
[ $rc -eq 2 ] && ok "missing release rejected" || bad "missing release rc=$rc: $out"

# --- 6. бинарник не запускается -> откат на прежнюю версию
make_base; make_release v9.9.9 "" "" "" broken_bin
out=$(run_update v9.9.9); rc=$?
[ $rc -eq 2 ] && echo "$out" | grep -qi 'rolling back' && ok "broken binary triggers rollback" || bad "broken bin rc=$rc: $out"
[ "$(cat "$W/opt/zapret2/config")" = MY_CONFIG=1 ] && grep -q '^1.0.6$' "$W/opt/zapret2/docs/changes.txt" \
    && ok "previous version restored" || bad "rollback did not restore"

# --- 7. валидация тега и окружения
for t in "" v "1.0.7" "v1.0.7;id" 'v1.$(id)' "v1..7" "v1.0." "vv1" "v1v2" "../v1.0.7" "v1.0.7 x"; do
    out=$(run_update "$t"); rc=$?
    [ $rc -eq 2 ] && ok "bad tag rejected: '$t'" || bad "bad tag '$t' rc=$rc: $out"
done
out=$(QZAPRET_TEST_BASE=$W/nonexistent QZAPRET_TEST_RUNDIR=$W/run QZAPRET_TEST_NO_SERVICE=1 "$H" update v1.0.7 2>&1); rc=$?
[ $rc -eq 2 ] && ok "missing installation rejected" || bad "missing install rc=$rc"


prepare_install() {
    rm -rf "$W/opt" "$W/systemd" "$W/run" "$W/bin" "$W/systemctl.log"
    mkdir -p "$W/opt" "$W/systemd" "$W/run" "$W/bin"
    cat > "$W/bin/systemctl" <<'EOS'
#!/bin/sh
case "$1" in
    show)
        for unit do :; done
        case "$unit" in
            zapret2applet-blockcheck.service|zapret2applet-blockcheck-recovery.service) ;;
            *) exit 4 ;;
        esac
        printf 'LoadState=loaded\nActiveState=inactive\nSubState=dead\nResult=success\nExecMainStatus=0\n'
        ;;
    daemon-reload)
        printf '%s\n' "$*" >> "$SYSTEMCTL_LOG"
        ;;
    *)
        printf '%s\n' "$*" >> "$SYSTEMCTL_LOG"
        ;;
esac
EOS
    chmod +x "$W/bin/systemctl"
}

run_install() {
    PATH="$W/bin:$PATH" SYSTEMCTL_LOG="$W/systemctl.log" \
    QZAPRET_TEST_BASE=$W/opt/zapret2 QZAPRET_TEST_RUNDIR=$W/run \
    QZAPRET_TEST_UNIT_DIR=$W/systemd QZAPRET_TEST_SYSTEMCTL=$W/bin/systemctl \
    QZAPRET_TEST_URLBASE="file://$W/rel" "$H" install "$1" 2>&1
}

expect_no_install() {
    [ ! -e "$W/opt/zapret2" ] && [ ! -e "$W/systemd/zapret2.service" ] \
        || bad "$1: partial installation was left behind"
}

# --- 8. чистая установка: config безопасно выключен, unit установлен, сервис не включается
prepare_install; make_release v9.9.9
out=$(run_install v9.9.9); rc=$?
[ $rc -eq 0 ] && ok "clean install succeeds" || bad "clean install rc=$rc: $out"
grep -q 'NFQWS2_ENABLE=0' "$W/opt/zapret2/config" && ok "fresh config disables NFQWS2" || bad "fresh config is unsafe"
[ -x "$W/opt/zapret2/nfq2/nfqws2" ] && [ -x "$W/opt/zapret2/ip2net/ip2net" ] \
    && ok "compatible binaries linked" || bad "binary links missing"
[ -f "$W/systemd/zapret2.service" ] && ok "systemd unit installed" || bad "systemd unit missing"
[ "$(stat -c %a "$W/opt/zapret2/docs/changes.txt")" = 644 ] && ok "installed version metadata is world-readable" || bad "installed metadata permissions are too restrictive"
[ "$(stat -c %a "$W/opt/zapret2/config")" = 600 ] && ok "initial config is private" || bad "initial config permissions are unsafe"
[ ! -e "$W/run/install.lock" ] && ok "install lock released" || bad "install lock left behind"
[ "$(cat "$W/systemctl.log")" = "daemon-reload" ] \
    && ok "systemd reloaded without starting or enabling service" || bad "unexpected systemctl calls: $(cat "$W/systemctl.log")"

# --- 9. существующая установка никогда не перезаписывается
prepare_install; mkdir -p "$W/opt/zapret2"; echo keep > "$W/opt/zapret2/marker"; make_release v9.9.9
out=$(run_install v9.9.9); rc=$?
[ $rc -eq 2 ] && [ "$(cat "$W/opt/zapret2/marker")" = keep ] \
    && ok "existing installation protected" || bad "existing installation rc=$rc: $out"
[ ! -e "$W/systemd/zapret2.service" ] || bad "unit created over existing installation"

# --- 9a. существующий systemd unit никогда не перезаписывается
prepare_install; echo keep > "$W/systemd/zapret2.service"; make_release v9.9.9
out=$(run_install v9.9.9); rc=$?
[ $rc -eq 2 ] && [ "$(cat "$W/systemd/zapret2.service")" = keep ] \
    && ok "existing systemd unit protected" || bad "existing unit rc=$rc: $out"
[ ! -e "$W/opt/zapret2" ] || bad "installation created over existing unit"

# --- 10. checksum mismatch rejects clean install before touching system
prepare_install; make_release v9.9.9 bad_sums
out=$(run_install v9.9.9); rc=$?
[ $rc -eq 2 ] && echo "$out" | grep -qi checksum && ok "install checksum mismatch rejected" || bad "install bad sums rc=$rc: $out"
expect_no_install "install bad sums"

# --- 11. unsafe archive paths rejected
prepare_install; make_release v9.9.9 "" unsafe
out=$(run_install v9.9.9); rc=$?
[ $rc -eq 2 ] && echo "$out" | grep -q "unsafe paths" && ok "install unsafe paths rejected" || bad "install unsafe archive rc=$rc: $out"
expect_no_install "install unsafe archive"

# --- 12. symlink in archive rejected
prepare_install; make_release v9.9.9 "" "" symlink
out=$(run_install v9.9.9); rc=$?
[ $rc -eq 2 ] && echo "$out" | grep -q "links" && ok "install archive symlink rejected" || bad "install symlink rc=$rc: $out"
expect_no_install "install symlink"

# --- 13. broken binary leaves no partial installation
prepare_install; make_release v9.9.9 "" "" "" broken_bin
out=$(run_install v9.9.9); rc=$?
[ $rc -eq 2 ] && echo "$out" | grep -q "nfqws2" && ok "install rejects broken binary" || bad "install broken binary rc=$rc: $out"
expect_no_install "install broken binary"

exit $fail
