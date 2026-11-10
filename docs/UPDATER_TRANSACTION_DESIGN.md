# Upstream updater transaction design

Status: implementation design for the updater refactor. This document describes the required behavior; it does not claim that the current updater already implements it.

## Current behavior and verified constraints

The privileged `update-locked` path downloads and validates the upstream archive, extracts it under `/var/tmp`, copies the current installation to `$BASE.bak`, and overlays the new archive with `cp -a "$stage/." "$BASE/"`. The overlay does not remove obsolete upstream files.

The archive contains ordinary files and directories; the helper rejects archive symlinks and special files. The upstream `install_bin.sh` is run after extraction and generates relative symlinks for the selected architecture:

- `nfq2/nfqws2`
- `ip2net/ip2net`
- `mdig/mdig`

The upstream selector probes `ip2net` with `0.0.0.0` input, then creates all three links from the selected architecture directory. The clean-install path verifies all three links and executes both `nfqws2 --version` and the `ip2net` probe. The updater hardening now also requires all three links to be executable, resolves each target and rejects targets outside `binaries/`, executes the `nfqws2` version check, and repeats the known-safe `ip2net` probe. It deliberately does not invent a `mdig` invocation.

The service unit uses fixed paths under `/opt/zapret2`. Replacing the installation with a symlink-based version pointer would therefore require a broader security and path-resolution redesign. The preferred design keeps `/opt/zapret2` a real directory.

## Required ownership model

The new installation carries a root-owned manifest at:

```
/opt/zapret2/.zapret2control-upstream-manifest.json
```

The manifest records only paths owned by the upstream archive and the generated architecture links. It must not claim user data or untracked legacy files. The manifest itself is reserved: an upstream archive containing this path is rejected.

The manifest must be versioned and strictly validated. At minimum it records:

- schema version;
- upstream release tag;
- normalized relative path for each owned regular file;
- file type and mode;
- SHA-256 for each regular file;
- relative target for each generated symlink.

Reject absolute paths, empty components, `.`, `..`, duplicate paths, unsupported file types, invalid modes, malformed hashes, and symlink targets that resolve outside the installation root. The manifest is not a substitute for validating the downloaded archive.

### Explicit user data

The following paths are preserved independently of the manifest:

- `config`;
- user-owned source hostlists under `ipset/` (the existing allowlisted `zapret-hosts-user*.txt` files);
- `init.d/*/custom.d/` and their regular-file contents.

Preservation must retain file contents and safe metadata. Symlinks and special files in preserved paths must fail closed rather than be followed.

### Migration from installations without a manifest

Absence of a manifest means ownership of existing files is unknown. The migration must not infer that an unknown file is upstream-owned and delete it.

Build the new tree from the validated new archive, then preserve explicit user data and unknown legacy files that are absent from the new archive. If an untracked legacy path collides with a path supplied by the new archive, the new archive owns that path; explicit user-data paths always take precedence. Unknown legacy files that do not collide are retained but remain untracked, so a later update cannot delete them automatically.

This is intentionally conservative: the first manifest establishes ownership only for the new archive and generated links. It does not retroactively claim ownership of the old installation.

### Subsequent updates

When a valid previous manifest exists:

1. Build the new tree from the new archive.
2. Preserve explicit user data.
3. Copy forward only legacy/untracked regular files and directories that are absent from the new archive.
4. Do not copy paths listed in the previous manifest unless they are also present in the new archive.
5. Generate architecture links and create the new manifest.
6. Validate the complete staged tree.

Thus a file present in the old manifest but absent from the new archive is omitted, while untracked files are retained. Never recursively delete a directory merely because it is absent from a manifest; only construct the new tree from the desired contents.

## Staging and validation

The download may remain under a temporary directory, but the extracted final staging tree must be a sibling of `/opt/zapret2` so it is guaranteed to be on the same filesystem as the active installation. Use a private, root-owned, non-symlink staging directory with a transaction-specific name.

Before touching the active installation:

- validate archive structure, path traversal, entry count, expanded size, file types, release version, and binary checksums;
- validate all required upstream files and the systemd unit paths;
- run `install_bin.sh` in the staged tree with a clean environment;
- verify that all three generated links exist, are executable, and resolve inside the staged tree;
- execute `nfqws2 --version`;
- run the known-safe `ip2net` probe with `0.0.0.0` input and require non-empty output;
- do not invent a `mdig` command-line probe until its supported invocation and exit semantics are confirmed;
- validate the manifest against the staged tree;
- validate the preserved configuration and hostlists without modifying them.

The update must also validate the trust boundary of `/opt`, the installation directory, staging directory, backup directory, and persistent transaction-state directory. They must be real directories with expected root ownership and safe permissions; no path component may be a symlink.

## Atomic activation and crash recovery

Do not use a two-step `mv active old; mv staged active` sequence: it creates a window in which `/opt/zapret2` is missing.

Use Linux `renameat2(..., RENAME_EXCHANGE)` to atomically exchange the active directory and the fully validated staged sibling. Both paths must be on the same filesystem. If the operation is unavailable or unsupported, fail closed and leave the active installation unchanged; do not silently fall back to a non-atomic replacement.

Persist a root-owned transaction journal outside `/opt/zapret2`, under `/var/lib/zapret2control/updater/`. The journal records a schema version, transaction UUID, release tag, staging and backup basenames, the old and new directory device/inode identities, and whether the service was active. Write it atomically with file and directory fsync before the exchange.

After exchange, the old installation is located at the former staging path. Rename it to a unique backup path in the same parent directory. Recovery must infer the exact completed step from the recorded directory identities, not from a phase string alone, because power loss can occur between a rename and the following journal update.

Recovery cases:

| Observed identities | Recovery |
|---|---|
| Active path is old; staging path is new | Exchange did not happen. Remove only the transaction's validated staging tree and clear the journal. |
| Active path is new; staging path is old | Exchange happened. Move the old tree to the recorded backup path, then finalize the transaction. |
| Active path is new; staging is absent; backup is old | Backup rename happened. Finalize the transaction. |
| Any identity is missing, duplicated, unsafe, or unexpected | Stop and preserve all paths; report manual recovery required. |

Never remove a path solely because its name resembles a staging or backup directory. Verify it against the journal UUID, root ownership, path containment, directory type, and recorded device/inode identity.

The existing service-restart semantics remain: if the service was active before the update, stop it only after staging is complete, verify it reached `inactive`, activate the new tree, then start it and verify `active`. A restart failure is reported as an unsuccessful partial update; the new tree and previous-version backup remain available. Do not automatically roll back a fully activated tree solely because the service failed to restart.

## Locking and failure behavior

The existing Blockcheck admission guard remains held throughout the complete update and recovery operation. The updater's own transaction lock/journal must prevent concurrent update attempts and reject malformed or unresolved transactions. A crash must not leave a lock whose mere existence permanently blocks recovery; process locks or safely recoverable transaction state are preferred over a persistent mkdir lock.

Any validation or staging failure occurs before service stop and must leave the active tree untouched. Any failure after service stop but before atomic exchange must attempt to restore the previous service state. Any failure after exchange must preserve both complete trees and the journal; never delete the only known-good copy.

## Regression test matrix

Tests must exercise the real helper against synthetic archives and an isolated filesystem:

- a file present in the previous manifest but absent from the new archive is removed;
- a new upstream file is installed;
- user config, user hostlists, and `custom.d` contents survive;
- unknown legacy files absent from the new archive survive first migration;
- a legacy installation without a manifest is never destructively pruned;
- a second update removes only paths owned by the previous manifest;
- generated symlinks are recorded and regenerated correctly;
- archive path traversal, symlinks, special files, oversized entries, missing checksums, and malformed manifests are rejected;
- invalid `nfqws2` and `ip2net` prevent activation; missing/non-executable `mdig` link prevents activation;
- staging on a different filesystem is not used;
- exchange unsupported or failed leaves the old installation and service state intact;
- simulated crash at each journal/exchange/backup boundary is recovered idempotently;
- unknown or tampered transaction paths are preserved and cause fail-closed recovery;
- service-state query errors and transitional states reject the update;
- failed restart retains the new installation, old backup, and a clear failure result.

The existing CI must not weaken archive, package, QML, or systemd test coverage. Real power-loss behavior still requires a separate VM-level test; deterministic filesystem and process-crash tests are necessary but not equivalent to physical power-loss testing.


## Implementation progress

The manifest primitives now validate the installation root and every traversed entry for real filesystem types, expected ownership, safe modes, hardlink constraints, and preserved-user-data structure.

A separate `updater_staging.py` module now builds a complete staging tree without changing the active installation. Its current contract is:

- the upstream tree is validated and its ownership manifest is built before copying;
- the destination must be a new absolute path under a privately owned, non-symlink parent on the same filesystem as the active installation;
- upstream-owned files and generated relative links are copied into a new private tree;
- explicit user data is copied from the active tree and takes precedence over archive paths;
- unknown legacy regular files and directories absent from the new archive are retained, including unknown children inside previously manifest-owned directories;
- paths owned by the previous manifest but absent from the new archive are not copied forward;
- untracked legacy symlinks fail closed and require manual review;
- the new manifest contains only paths from the validated upstream tree, not preserved legacy/user data;
- the completed tree and its manifest are verified before the function returns.

This module is deliberately **not connected to `update-locked`**. The existing overlay updater remains the production path until staging tests, atomic exchange, journaled recovery, service-state handling, and end-to-end transaction tests are complete. A staging failure leaves the active installation untouched; the caller must retain or inspect the failed private staging directory rather than treating it as activated.


### Atomic activation implementation progress

A separate `updater_activation.py` module now provides the next isolated transaction layer:

- secure, bounded, root-owned transaction journal with atomic creation and fsync;
- directory identity tracking by device and inode;
- Linux `renameat2(RENAME_EXCHANGE)` for the active/staged directory swap;
- Linux `renameat2(RENAME_NOREPLACE)` for moving the old tree to its backup without overwriting an existing path;
- no fallback to a two-step or non-atomic replacement;
- recovery for the pre-exchange, post-exchange/pre-backup, and post-backup/pre-journal-cleanup crash windows;
- fail-closed handling when paths do not match the journal's recorded identities.

Regression tests cover successful exchange, both principal recovery windows, unexpected inode replacement, existing backup paths, simulated unsupported exchange, and symlinked journals.

This activation module is also **not connected to `update-locked`**. Service stop/start orchestration, service-state persistence and recovery, full production-helper integration, and VM-level crash testing remain prerequisites.


### Service lifecycle implementation progress

The separate `updater_service.py` layer now wraps the activation primitives with systemd state checks:

- accepts only a definitive `active` or `inactive` pre-update state;
- stops an active service and verifies `inactive` before exchanging directories;
- starts the service after activation and verifies `active`;
- retains the transaction journal and old backup if restart fails;
- recovers the filesystem transaction first, then restores the previously recorded service state, and only then finalizes the journal;
- attempts to restore service state if stop verification or pre-activation work fails;
- supports an explicit no-service mode for isolated operations.

Mocked systemd tests cover normal active-service updates, restart failure followed by recovery, transitional-state rejection, and no-service operation.

The transaction layers are now wired into the privileged `update-locked` helper through `updater_cli.py`:

1. The helper acquires its existing update lock and invokes persistent-journal recovery before downloading a new release.
2. Existing archive path/type/size checks and upstream binary checksums remain in the shell helper.
3. The CLI runs upstream `install_bin.sh` in the extracted tree, builds the sibling staging tree, validates the manifest, generated links, runtime probes and systemd unit.
4. The service-aware activation layer stops and verifies an active service, performs the atomic directory exchange, retains the old tree as a unique sibling backup, restarts and verifies the service, then removes the journal.
5. If service restart fails, the new installation, previous-version backup and journal remain. The next update invocation attempts recovery and restores the previously recorded service state before downloading anything.

The previous overlay-copy implementation has been removed from the production update path. The updated shell E2E suite now exercises successful updates, active/inactive service behavior, restart failure, archive/checksum rejection and pre-activation runtime validation. These integration changes are not considered release-ready until the new CI, package, and systemd E2E workflows pass on the same final commit and the failure/recovery cases are reviewed.
