#!/bin/sh
# Исходный архив для RPM: git archive по тегу (сабмодуль third_party/zapret2 в сборке
# не участвует и в архив не попадает).
# Использование: packaging/make-dist.sh [тег]   (по умолчанию v<версия из CMakeLists.txt>)
set -eu
cd "$(git rev-parse --show-toplevel)"
ver=$(sed -n 's/^project(zapret2control VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
tag=${1:-v$ver}
out=${OUT:-.}
git rev-parse -q --verify "refs/tags/$tag" >/dev/null || { echo "no such tag: $tag" >&2; exit 1; }
git archive --format=tar.gz --prefix="zapret2control-$ver/" -o "$out/zapret2control-$ver.tar.gz" "$tag"
echo "$out/zapret2control-$ver.tar.gz"
