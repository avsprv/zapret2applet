"""systemd-owned Blockcheck worker; recovery is deliberately delegated to ExecStopPost."""
from __future__ import annotations

import os
import stat
import subprocess
from pathlib import Path

from blockcheck_manager import BlockcheckOptions
from blockcheck_staging import StagedOptionsStore
from blockcheck_state import StateError, StateStore

SERVICE = "zapret2.service"
DEFAULT_UPSTREAM_SCRIPT = "/opt/zapret2/blockcheck2.sh"


class WorkerError(StateError):
    """Raised when the Blockcheck worker cannot safely execute its operation."""


class BlockcheckWorker:
    def __init__(
        self,
        state_store: StateStore,
        staged_store: StagedOptionsStore,
        upstream_script: str | os.PathLike[str] = DEFAULT_UPSTREAM_SCRIPT,
    ) -> None:
        self.state_store = state_store
        self.staged_store = staged_store
        self.upstream_script = Path(upstream_script)

    def _open_upstream_script(self) -> int:
        try:
            fd = os.open(
                self.upstream_script,
                os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC,
            )
        except OSError as exc:
            raise WorkerError(f"cannot safely open upstream Blockcheck script: {exc}") from exc
        info = os.fstat(fd)
        if (
            not stat.S_ISREG(info.st_mode)
            or info.st_uid != self.state_store.owner
            or stat.S_IMODE(info.st_mode) & 0o022
        ):
            os.close(fd)
            raise WorkerError(
                "upstream Blockcheck script must be a regular file owned by the worker "
                "user and not writable by group or others"
            )
        return fd

    def _stop_zapret(self) -> None:
        try:
            result = subprocess.run(
                [self.state_store.systemctl, "stop", SERVICE],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
                check=False,
                timeout=120,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise WorkerError(f"cannot stop {SERVICE}: {exc}") from exc
        if result.returncode != 0:
            detail = result.stderr.decode("utf-8", "replace").strip()
            raise WorkerError(f"systemctl stop failed ({result.returncode}): {detail}")
        if self.state_store._service_is_active():
            raise WorkerError(f"{SERVICE} remained active after stop request")

    def run(self, operation_id: str | None = None) -> int:
        """Run one UUID-bound request while holding the persistent lifecycle lock.

        A durable recovery marker is created before stopping zapret2. It is not
        removed here: systemd ExecStopPost owns recovery after this process exits.
        """
        script_fd = self._open_upstream_script()
        try:
            with self.state_store.locked(create=True):
                if operation_id is None:
                    operation_id, options = self.staged_store.read_pending_locked()
                else:
                    options = self.staged_store.read_locked(operation_id)
                marker = self.state_store.prepare_locked(operation_id)
                self.staged_store.consume_locked(operation_id)

                if marker["service_was_active"]:
                    self._stop_zapret()

                try:
                    result = subprocess.run(
                        ["/bin/sh", f"/proc/self/fd/{script_fd}"],
                        stdin=subprocess.DEVNULL,
                        env=options.upstream_environment(),
                        pass_fds=(script_fd,),
                        check=False,
                    )
                except OSError as exc:
                    raise WorkerError(f"cannot execute upstream Blockcheck: {exc}") from exc
                return result.returncode
        finally:
            os.close(script_fd)

def main() -> int:
    import sys
    state = StateStore("/var/lib/zapret2control/blockcheck")
    staged = StagedOptionsStore(state, "/run/zapret2control/blockcheck")
    try:
        return BlockcheckWorker(state, staged).run()
    except StateError as exc:
        print(f"zapret2control blockcheck worker: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
