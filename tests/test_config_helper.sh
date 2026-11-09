#!/bin/sh
set -eu
H="$1"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp/base"
export QZAPRET_TEST_BASE="$tmp/base"
export QZAPRET_TEST_BACKUP_DIR="$tmp/backups"
export QZAPRET_TEST_LEGACY_BACKUP_DIR="$tmp/legacy-backups"
export QZAPRET_TEST_ENFORCE_SECURITY=1
printf 'NFQWS2_ENABLE=1\n# original\n' > "$tmp/base/config"
out=$("$H" config-read)
[ "$out" = "$(cat "$tmp/base/config")" ] || { echo "FAIL: config-read changed content"; exit 1; }
echo "ok: reads regular config"

chmod 777 "$tmp/base"
if "$H" config-read >/dev/null 2>&1; then
    echo "FAIL: writable config directory was accepted"; exit 1
fi
chmod 755 "$tmp/base"
chmod 666 "$tmp/base/config"
if "$H" config-read >/dev/null 2>&1; then
    echo "FAIL: group/world-writable config file was accepted"; exit 1
fi
chmod 644 "$tmp/base/config"
echo "ok: rejects unsafe config directory and file permissions"

initial_hash=$(sha256sum "$tmp/base/config" | awk '{print $1}')
printf 'NFQWS2_ENABLE=0\n# changed\n' | "$H" config-write "$initial_hash" > "$tmp/write.out"
grep -q 'backup:' "$tmp/write.out"
grep -q 'NFQWS2_ENABLE=0' "$tmp/base/config"
echo "ok: validates, backs up and writes config with expected SHA-256"

if printf 'NFQWS2_ENABLE=9\n' | "$H" config-write "$initial_hash" >/dev/null 2>&1; then
    echo "FAIL: stale config hash was accepted"; exit 1
fi
grep -q 'NFQWS2_ENABLE=0' "$tmp/base/config"
echo "ok: rejects stale writes without changing current config"

current_hash=$(sha256sum "$tmp/base/config" | awk '{print $1}')
if printf 'if then\n' | "$H" config-write "$current_hash" >/dev/null 2>&1; then
    echo "FAIL: invalid shell syntax was accepted"; exit 1
fi
grep -q 'NFQWS2_ENABLE=0' "$tmp/base/config"
echo "ok: rejects invalid syntax without changing config"

current_hash=$(sha256sum "$tmp/base/config" | awk '{print $1}')
if dd if=/dev/zero bs=1048577 count=1 2>/dev/null | "$H" config-write "$current_hash" >/dev/null 2>&1; then
    echo "FAIL: oversized config was accepted"; exit 1
fi
grep -q 'NFQWS2_ENABLE=0' "$tmp/base/config"
echo "ok: rejects config larger than 1 MiB"

current_hash=$(sha256sum "$tmp/base/config" | awk '{print $1}')
printf 'NFQWS2_ENABLE=1\n# newer\n' | "$H" config-write "$current_hash" >/dev/null
"$H" config-rollback >/dev/null
grep -q 'NFQWS2_ENABLE=0' "$tmp/base/config"
echo "ok: restores latest backup and preserves pre-rollback config"

rm "$tmp/base/config"
ln -s /etc/passwd "$tmp/base/config"
if "$H" config-read >/dev/null 2>&1; then
    echo "FAIL: symlink config was accepted"; exit 1
fi
if printf 'BAD=1\n' | "$H" config-write >/dev/null 2>&1; then
    echo "FAIL: symlink config was writable"; exit 1
fi
echo "ok: rejects symlink config"

# Restore a regular config before testing backup-root validation.
rm -f "$tmp/base/config"
printf 'SAFE=1\n' > "$tmp/base/config"

# The backup root must not be redirected through a symlink.
rm -rf "$tmp/backups"
ln -s "$tmp" "$tmp/backups"
current_hash=$(sha256sum "$tmp/base/config" | awk '{print $1}')
if printf 'NFQWS2_ENABLE=1\n' | "$H" config-write "$current_hash" >/dev/null 2>&1; then
    echo "FAIL: symlink backup root was accepted"; exit 1
fi
echo "ok: rejects symlink backup root"

# A valid legacy backup root is moved intact to the new application path.
rm -f "$tmp/backups"
mkdir -m 700 "$tmp/legacy-backups"
printf 'legacy backup\\n' > "$tmp/legacy-backups/legacy-sentinel"
chmod 600 "$tmp/legacy-backups/legacy-sentinel"
current_hash=$(sha256sum "$tmp/base/config" | awk '{print $1}')
printf 'AFTER_MIGRATION=1\\n' | "$H" config-write "$current_hash" >/dev/null
[ ! -e "$tmp/legacy-backups" ] || { echo "FAIL: legacy backup root still exists"; exit 1; }
grep -q 'legacy backup' "$tmp/backups/legacy-sentinel"
echo "ok: migrates legacy backup storage without losing existing backups"

# If both roots exist, migration must fail closed and preserve both copies.
mkdir -m 700 "$tmp/legacy-backups"
printf 'ambiguous legacy\\n' > "$tmp/legacy-backups/legacy-sentinel"
current_hash=$(sha256sum "$tmp/base/config" | awk '{print $1}')
if printf 'AMBIGUOUS=1\\n' | "$H" config-write "$current_hash" >/dev/null 2>&1; then
    echo "FAIL: ambiguous backup roots were accepted"; exit 1
fi
grep -q 'legacy backup' "$tmp/backups/legacy-sentinel"
grep -q 'ambiguous legacy' "$tmp/legacy-backups/legacy-sentinel"
echo "ok: refuses ambiguous backup roots without deleting either copy"

# A symlinked legacy backup root must not be followed.
rm -rf "$tmp/legacy-backups"
ln -s "$tmp" "$tmp/legacy-backups"
current_hash=$(sha256sum "$tmp/base/config" | awk '{print $1}')
if printf 'SYMLINK=1\\n' | "$H" config-write "$current_hash" >/dev/null 2>&1; then
    echo "FAIL: symlink legacy backup root was accepted"; exit 1
fi
echo "ok: rejects symlink legacy backup root"
