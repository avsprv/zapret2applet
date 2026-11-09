# Blockcheck systemd lifecycle

## Status

Architecture decision for the QML/Kirigami migration branch. The durable state store, strict options schema, runtime staging protocol, worker,
submission admission control, recovery entry point, helper commands, and systemd unit
templates are implemented. The unit templates and UUID-bound status helper are installed by CMake and
included in Debian/Fedora packaging metadata. The status helper reports queued,
running, ExecStopPost recovery, pending/failed recovery, completion, and stale job
identifiers while serializing reads against the lifecycle lock. It treats a removed
systemd RuntimeDirectory as an absent staging file, but fails closed on unsafe paths
or malformed state. The QML preview now uses an asynchronous job client for submit,
status polling, cancellation, bounded journal reads, strategy extraction, and recovery
state handling. A shared privileged admission guard now serializes service start/stop/
enable/disable and the complete upstream update transaction with Blockcheck submission
and recovery. Clean installation and updates both hold the same guard for their complete
transactions. It rejects pending markers, staged jobs, active worker/recovery units, and
unverifiable systemd state; a failed worker unit is tolerated only when no durable
recovery marker remains. Submit, UUID-bound cancellation, and recovery share a
dedicated queue lock; the worker does not hold it during diagnostics, and the QML
client preserves the original cancellation UUID even if it adopts a successor job.
Real systemd lifecycle E2E validation now passes on a disposable systemd VM. It covers normal recovery of a previously active service, preservation of an initially inactive service, marker retention and boot-recovery retry after a failed restore, and recovery after worker-main-process SIGKILL. A full machine reboot during an interrupted package upgrade is not covered by this E2E test. The installed production Widgets application continues to use its legacy Blockcheck path.

## Runtime and persistent storage paths

The current persistent Blockcheck state directory is
`/var/lib/zapret2control/blockcheck/`, owned by root with directory mode
`0700`; the recovery marker is a root-owned regular file with mode `0600`.
The staged options directory is `/run/zapret2control/blockcheck/`. It is
volatile: systemd creates the root-owned `/run/zapret2control/` RuntimeDirectory,
and the helper securely creates and validates its `blockcheck/` subdirectory.

Package upgrades migrate the durable state directory from the legacy
`/var/lib/zapret2applet/blockcheck/` location after stopping the old worker and
recovery units. The migration preserves the directory contents and fails closed
if both old and new state directories exist or either path is unsafe. The old
`/run/zapret2applet/` RuntimeDirectory is not copied: it contains volatile
staging data and is removed by systemd when the old worker unit is stopped.
Legacy path strings in package scripts are compatibility inputs only; they are
not active runtime locations.

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

- `zapret2control-blockcheck.service`: one-shot diagnostic job. Its main process runs
  the validated Blockcheck worker. Its `ExecStopPost=` invokes an idempotent recovery
  command after normal exit, failure, timeout, or forced termination while systemd is
  operational.
- `zapret2control-blockcheck-recovery.service`: one-shot boot recovery job, enabled
  for `multi-user.target`. It processes a pending marker and starts
  `zapret2.service` only when the marker records that the service was active before
  Blockcheck.

Do not add an `After=`/`Requires=` dependency from `zapret2.service` to the recovery
unit, and do not order recovery `Before=zapret2.service`. The recovery command itself
must synchronously request and verify the zapret service start. If zapret2 is enabled,
the normal boot transaction may already be starting it; `systemctl start` joins or
waits for that job. If zapret2 is disabled, the recovery command starts it because the
durable marker proves it was active before the interrupted diagnostic. Avoiding a
unit-ordering dependency prevents a boot-time deadlock in which recovery waits for
zapret2 while zapret2 waits for recovery.

The recovery unit is enabled as a boot fallback. It must not start Blockcheck or alter
zapret service state when no valid recovery marker exists.
## State and invariants

Persist state under `/var/lib/zapret2control/blockcheck/`, owned by root, directory mode
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
6. Only one Blockcheck operation may run at a time. Serialize submit, cancel, and
   recovery with a dedicated root-owned `queue.lock` in the persistent state directory.
   Acquire locks in the order queue lock -> lifecycle lock. The worker holds the lifecycle
   lock for the scan but does not hold the queue lock while running upstream code.
   Cancellation validates its immutable operation UUID against the recovery marker or
   staged request and submits `systemctl stop --no-block` before releasing the queue lock.
   Recovery also holds the queue lock while verifying service state and removing the marker,
   so a delayed cancellation cannot overlap a replacement submission. If the matching
   request is still staged, cancellation waits for the lifecycle lock after queuing the
   stop and removes only that UUID's staged options; this covers a worker canceled before
   it consumes the request.
7. GUI lifetime is irrelevant to worker and recovery lifetime.
8. Options cross the privilege boundary as validated argv values or a strictly parsed,
   bounded data file. Never use shell evaluation or source a generated shell file.

## Lifecycle

1. GUI validates the options and invokes a narrow privileged submit command through
   the existing Polkit boundary. The submit command validates all options again, writes
   a bounded options record atomically into a root-owned `/run/zapret2control/blockcheck/`
   directory, then queues `systemctl start --no-block zapret2control-blockcheck.service`.
   It must reject a second submission while the unit or a recovery operation is active.
2. The static worker unit invokes a dedicated helper worker command. The worker securely
   opens and validates the staged options record while holding the persistent lifecycle
   lock, queries the actual active state of `zapret2.service`, and writes the durable
   recovery marker with the same operation ID. Only after the marker is durable does it
   consume the staged options record. It must not accept arbitrary shell fragments or
   source the options record.
3. If the service was active, the worker stops it and verifies that it is inactive.
   If it was inactive, it leaves it untouched.
4. The worker securely opens the installed upstream `blockcheck2.sh`, checks its type,
   owner, and write permissions, then executes it with a clean allowlisted environment.
5. On normal completion or cancellation, upstream cleanup runs and the unit exits.
   systemd invokes `ExecStopPost=` to run recovery. The unit uses a bounded
   `TimeoutStopSec=` and normal SIGTERM delivery; SIGKILL is only systemd's final
   timeout behavior, not the GUI's cancellation mechanism.
6. Recovery validates the marker and current unit state. It starts `zapret2.service`
   only when the marker says it was active, verifies the resulting state, then removes
   the marker. If start or verification fails, it leaves the marker in place and exits
   unsuccessfully.
7. At boot, the recovery unit performs the same operation as an independent
   `multi-user.target` job. No ordering dependency with `zapret2.service` is introduced;
   the recovery command itself starts and verifies the service when the marker requires it.

Recovery acquires the queue and lifecycle locks in nonblocking mode. If either lock is
busy, it exits unsuccessfully without removing the marker; the GUI retry path or the
next boot recovery attempt can retry after the competing operation finishes. The worker unit's `TimeoutStopSec=180` bounds the `ExecStopPost=` recovery
window. The independent boot recovery unit also sets `TimeoutStartSec=180`; this
covers the helper's bounded `systemctl start` (timeout 120 seconds), state verification
(timeout 30 seconds), and process overhead. Both systemd entry points therefore have
a budget larger than the helper's maximum bounded service operation.


8. Service management, clean installation, and upstream updates enter the same persistent
   lifecycle lock through `blockcheck_admission.py`. The lock is held across the complete
   `systemctl start/stop/enable/disable zapret2.service` action and across the full
   update transaction, including stop, file replacement, rollback, and service restore.
   The clean-install transaction is also guarded from its initial validation through
   archive verification, installation, and systemd reload. Admission rejects a pending recovery marker, staged options, a busy lifecycle lock,
   a non-idle recovery unit, or any untrusted systemd query. The worker unit may remain
   in systemd's `failed` state after a scan failure only when the durable recovery
   marker is absent; this allows normal service administration after verified recovery.
   The internal `update-locked` helper entry point is accepted only from the guarded
   admission process.

Cancellation should request a normal unit stop, allowing upstream signal handlers to
remove temporary packet-filter rules. Do not use SIGKILL as the normal cancellation
mechanism. systemd's configured stop timeout remains a last-resort process bound; the
persistent marker protects service restoration if the worker is killed.

## GUI integration

Keep the existing Blockcheck options, log parsing, strategy extraction, and result model.
Replace direct service stop/start orchestration with a systemd job client:

- submit the validated options and queue the dedicated unit with
  `systemctl start --no-block`; do not wait for the complete scan in the submit
  process;
- observe unit state with asynchronous polling and read the unit journal through a
  narrowly allowlisted helper operation; do not block the GUI thread;
- request cancellation with the current operation UUID; the helper checks that UUID
  against the active recovery marker or staged request while holding `queue.lock`, and
  ignores stale requests without stopping a replacement job;
- keep the busy state until systemd confirms the job is finished and recovery has been
  verified;
- surface a distinct recovery-pending/error state when the marker remains, and offer a retry
  through the fixed recovery helper using the same operation ID.
- read the fixed Blockcheck unit journal through
  `blockcheck-job-logs {50|100|200|500} START_EPOCH`; helper output is capped at 1 MiB and the
  GUI parses strategy records from the journal rather than relying on the worker's
  process stream. Journal reads are filtered from the submitted operation start time
  to avoid mixing results from earlier invocations of the static systemd unit;
- keep service and update operations disabled while a recovery marker remains after
  a recovery failure.

The GUI must not infer successful recovery from the worker exit code alone.

## Packaging and upgrade behavior

Install both unit files into the distribution's systemd unit directory. Reload systemd
after installation/removal. Enable the boot recovery unit on package installation and
disable it on package removal, without enabling or starting `zapret2.service`. The
worker unit is static and must not be enabled at boot. The helper submit/cancel/status
commands are restricted to the fixed Blockcheck unit name; no caller-supplied unit name
is accepted.

The helper's zapret clean-install/update paths must preserve their current behavior,
must hold the shared Blockcheck admission lock for the full transaction, and must not
overwrite the application-owned Blockcheck units. The recovery unit must work
even when the GUI package is upgraded while a marker exists.

## Required tests

Use an isolated fake `systemctl`/unit environment; tests must never stop or start the
host's real `zapret2.service`.

- service inactive before scan: no start on normal exit, failure, or boot recovery;
- service active before scan: restore on normal exit and cancellation;
- worker SIGTERM: recovery runs after upstream cleanup;
- worker SIGKILL: systemd post-stop recovery runs;
- GUI exits while the systemd job continues: recovery still runs;
- power-loss simulation: leave marker, then boot recovery restores the service. The current VM E2E validates marker retention after a failed restore and explicitly starts the boot recovery unit in the same VM; it does not reboot the VM or simulate an actual power loss;
- recovery start failure: marker remains and the error is observable;
- malformed, symlinked, hard-linked, wrong-owner, and wrong-mode state files fail closed;
- concurrent submit/cancel/recovery operations serialize through `queue.lock` without
  holding it during the upstream diagnostic;
- delayed cancellation of operation A cannot stop a staged or active operation B, including
  when the worker publishes its recovery marker while cancellation is inspecting state;
- QML cancellation passes one immutable UUID and does not retarget a newly adopted job;
- concurrent service-management/update operations serialize correctly with the lifecycle lock;
- a staged request or durable recovery marker blocks service-management and update admission;
- failed worker state without a recovery marker does not permanently block service operations;
- direct invocation of the internal update or clean-install entry point without the admission guard is rejected;
- a pending recovery marker or staged Blockcheck request blocks clean installation;
- repeated recovery is idempotent;
- package install/remove and systemd daemon-reload behavior are validated.

## Rollout boundary

Do not switch the production GUI to this flow until the worker unit, recovery unit, helper
commands, package integration, GUI job client, and regression tests are implemented and
validated. Keep the existing Widgets production application unchanged during this work.
