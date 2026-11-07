# Blockcheck systemd lifecycle

## Status

Architecture decision for the QML/Kirigami migration branch. This document describes the
target lifecycle; it does not claim that crash recovery is implemented.

## Problem

The current GUI controller verifies, stops, and restarts `zapret2.service` around a
privileged Blockcheck process. A GUI crash after the stop operation can therefore leave
the service inactive. Helper PID files, locks, and upstream signal handlers improve
process management but cannot recover the service after a GUI crash, worker SIGKILL,
kernel failure, or power loss.

## Decision

Move the privileged Blockcheck lifecycle into a dedicated systemd service. The GUI
becomes a client that starts, observes, and stops that unit; it no longer owns service
restoration.

Use two systemd units:

- `zapret2applet-blockcheck.service`: one-shot diagnostic job. Its main process runs
  the validated Blockcheck worker. Its `ExecStopPost=` invokes an idempotent recovery
  command after normal exit, failure, timeout, or forced termination while systemd is
  operational.
- `zapret2applet-blockcheck-recovery.service`: boot-time recovery job. It is enabled
  for `multi-user.target` and ordered `Before=zapret2.service`, so a pending recovery
  marker is processed before the zapret service is started in the normal boot
  transaction.

The recovery unit must be installed and enabled by the application package. It must
not start Blockcheck or alter zapret service state when no recovery marker exists.

## State and invariants

Persist state under `/var/lib/zapret2applet/blockcheck/`, owned by root, directory mode
`0700`, state file mode `0600`. The state is durable across reboot; `/run` is not
suitable for this purpose.

The state record contains only validated, versioned fields:

- schema version;
- unique operation ID;
- whether `zapret2.service` was active before this scan.

The record is written atomically (temporary file in the same directory, fsync, rename,
directory fsync) **before** stopping `zapret2.service`. A malformed, linked, wrongly
owned, or unexpectedly permissioned state file must fail closed: do not start the
service based on untrusted state and retain enough diagnostic information for an
administrator to investigate.

Required invariants:

1. If the service was inactive before Blockcheck, recovery must never start it.
2. If the service was active before Blockcheck, every exit path attempts to restore it.
3. Recovery is idempotent. Repeated `ExecStopPost=` and boot recovery calls are safe.
4. The recovery marker is removed only after systemd confirms the expected service
   state. On failure it remains for the next boot or an administrator retry.
5. The marker is created before the service stop request, so a crash at any point after
   that write is recoverable.
6. Only one Blockcheck operation may run at a time. Serialize start, cancel, and recovery
   using a root-owned lock in the persistent state directory.
7. GUI lifetime is irrelevant to worker and recovery lifetime.
8. Options cross the privilege boundary as validated argv values or a strictly parsed,
   bounded data file. Never use shell evaluation or source a generated shell file.

## Lifecycle

1. GUI validates the options and asks systemd to start the Blockcheck unit with a unique
   operation identifier.
2. The privileged worker validates options again, acquires the lifecycle lock, queries
   the actual active state of `zapret2.service`, and writes the durable recovery marker.
3. If the service was active, the worker stops it and verifies that it is inactive.
   If it was inactive, it leaves it untouched.
4. The worker executes the pinned upstream `blockcheck2.sh` with a clean environment.
5. On normal completion or cancellation, upstream cleanup runs and the unit exits.
   systemd invokes `ExecStopPost=` to run recovery.
6. Recovery validates the marker and current unit state. It starts `zapret2.service`
   only when the marker says it was active, verifies the resulting state, then removes
   the marker. If start or verification fails, it leaves the marker in place and exits
   unsuccessfully.
7. At boot, the recovery unit performs the same operation before `zapret2.service`
   enters the normal boot transaction.

Cancellation should request a normal unit stop, allowing upstream signal handlers to
remove temporary packet-filter rules. Do not use SIGKILL as the normal cancellation
mechanism. systemd's configured stop timeout remains a last-resort process bound; the
persistent marker protects service restoration if the worker is killed.

## GUI integration

Keep the existing Blockcheck options, log parsing, strategy extraction, and result model.
Replace direct service stop/start orchestration with a systemd job client:

- start the dedicated unit and wait asynchronously for its job result;
- observe unit state and journal output without blocking the GUI thread;
- request cancellation by stopping the dedicated unit;
- keep the busy state until systemd confirms the job is finished and recovery has been
  verified;
- surface a distinct recovery-pending/error state when the marker remains.

The GUI must not infer successful recovery from the worker exit code alone.

## Packaging and upgrade behavior

Install both unit files into the distribution's systemd unit directory. Reload systemd
after installation/removal. Enable the boot recovery unit on package installation and
disable it on package removal, without enabling or starting `zapret2.service`.

The helper's zapret installation/update paths must preserve their current behavior and
must not overwrite the application-owned Blockcheck units. The recovery unit must work
even when the GUI package is upgraded while a marker exists.

## Required tests

Use an isolated fake `systemctl`/unit environment; tests must never stop or start the
host's real `zapret2.service`.

- service inactive before scan: no start on normal exit, failure, or boot recovery;
- service active before scan: restore on normal exit and cancellation;
- worker SIGTERM: recovery runs after upstream cleanup;
- worker SIGKILL: systemd post-stop recovery runs;
- GUI exits while the systemd job continues: recovery still runs;
- power-loss simulation: leave marker, then boot recovery restores the service;
- recovery start failure: marker remains and the error is observable;
- malformed, symlinked, hard-linked, wrong-owner, and wrong-mode state files fail closed;
- concurrent start/cancel/recovery operations serialize correctly;
- repeated recovery is idempotent;
- package install/remove and systemd daemon-reload behavior are validated.

## Rollout boundary

Do not switch the production GUI to this flow until the worker unit, recovery unit, helper
commands, package integration, GUI job client, and regression tests are implemented and
validated. Keep the existing Widgets production application unchanged during this work.
