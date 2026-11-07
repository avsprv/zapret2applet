#!/bin/sh
# Проверка валидации аргументов zapret2applet-helper (до любых привилегированных действий).
H="${1:?usage: test_helper.sh /path/to/zapret2applet-helper}"
fail=0

expect_reject() {
    out=$("$H" "$@" 2>&1); rc=$?
    if [ "$rc" -ne 2 ]; then
        echo "FAIL (rc=$rc, expected 2): $*"; echo "  $out"; fail=1
    else
        echo "ok: rejected: $*"
    fi
}

expect_reject
expect_reject bogus
expect_reject unit
expect_reject unit restart
expect_reject unit "start;id"
expect_reject unit start unexpected
expect_reject blockcheck-cancel unexpected
expect_reject blockcheck --domains 'a.org;id'
expect_reject blockcheck --domains '$(id)'
expect_reject blockcheck --domains '-rf'
expect_reject blockcheck --domains '.hidden'
expect_reject blockcheck --domains 'example..org'
expect_reject blockcheck --domains '-bad.example'
expect_reject blockcheck --domains 'bad-.example'
expect_reject blockcheck --domains '*'
expect_reject blockcheck --domains 'label-that-is-far-too-long-and-exceeds-the-sixty-three-character-dns-label-limit-which-must-be-rejected.example'
expect_reject blockcheck --domains 'a b c d e f g h i j k'
expect_reject blockcheck --ipvs 5
expect_reject blockcheck --http 2
expect_reject blockcheck --tls13 yes
expect_reject blockcheck --repeats 0
expect_reject blockcheck --repeats 99
expect_reject blockcheck --repeats 1x
expect_reject blockcheck --scanlevel insane
expect_reject blockcheck --unknown 1
expect_reject blockcheck --domains
expect_reject update
expect_reject update bogus
expect_reject update v1.0.7 unexpected
expect_reject update 1.0.7
expect_reject update 'v1.0.7;id'
expect_reject update 'v1.$(id)'
expect_reject update v1..7
expect_reject update ../v1.0.7
expect_reject update-locked v1.0.7

# blockcheck-cancel без запущенного blockcheck — не ошибка
"$H" blockcheck-cancel >/dev/null 2>&1 || { echo "FAIL: cancel should exit 0"; fail=1; }

exit $fail
