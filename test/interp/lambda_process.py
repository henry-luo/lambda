"""Isolated Lambda process execution shared by tier and fuzz test runners.

Each invocation starts a process group so an engine timeout can close renderer
or helper descendants without leaving their output pipes held open.
"""

from dataclasses import dataclass
import os
import signal
import subprocess
import threading
from typing import Mapping, Optional, Sequence


@dataclass
class LambdaProcessResult:
    argv: Sequence[str]
    stdout: str
    stderr: str
    return_code: Optional[int]
    timed_out: bool
    launch_error: str = ""
    output_limited: bool = False

    @property
    def status(self) -> str:
        if self.launch_error:
            return "launcher-error"
        if self.timed_out:
            return "timeout"
        if self.output_limited:
            return "output-limit"
        if self.return_code == 0:
            return "ok"
        return f"exit{self.return_code}"


def run_process(argv: Sequence[str], timeout: float,
                extra_env: Optional[Mapping[str, Optional[str]]] = None,
                output_limit: Optional[int] = None) -> LambdaProcessResult:
    """Runs an arbitrary test command in an isolated process group."""
    env = dict(os.environ)
    if extra_env:
        for name, value in extra_env.items():
            if value is None:
                env.pop(name, None)
            else:
                env[name] = value
    command = list(argv)
    if output_limit is not None and output_limit < 1:
        raise ValueError("output_limit must be positive")

    proc = None
    try:
        proc = subprocess.Popen(
            command,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            errors="replace",
            start_new_session=os.name != "nt",
        )
        if output_limit is not None:
            stdout, stderr, timed_out, limited = _communicate_bounded(proc, timeout, output_limit)
            return LambdaProcessResult(command, stdout, stderr,
                                       None if timed_out else proc.returncode, timed_out,
                                       output_limited=limited)
        stdout, stderr = proc.communicate(timeout=timeout)
        return LambdaProcessResult(command, stdout, stderr, proc.returncode, False)
    except subprocess.TimeoutExpired as error:
        if proc is None:
            return LambdaProcessResult(command, "", "launcher timeout", None, True)
        _terminate_process_group(proc)
        stdout = error.stdout if isinstance(error.stdout, str) else ""
        stderr = error.stderr if isinstance(error.stderr, str) else ""
        return LambdaProcessResult(command, stdout, stderr, None, True)
    except OSError as error:
        return LambdaProcessResult(command, "", "", None, False, str(error))


def run_lambda_process(executable: str, script: str, tier: Optional[str],
                       timeout: float, procedural: bool = False,
                       dry_run: bool = False,
                       no_log: bool = False,
                       extra_env: Optional[Mapping[str, str]] = None,
                       output_limit: Optional[int] = None) -> LambdaProcessResult:
    """Runs one Lambda script in an isolated process group.

    A missing tier deliberately means AUTO. Callers that need eager MIR Direct
    must pass ``jit`` rather than relying on the default (D8.1.1v10).
    """
    env: dict[str, Optional[str]] = dict(extra_env or {})
    if tier is None:
        env["LAMBDA_EXEC_BACKEND"] = None
    else:
        env["LAMBDA_EXEC_BACKEND"] = tier
    argv = [executable]
    if procedural:
        argv.append("run")
    if dry_run:
        argv.append("--dry-run")
    if no_log:
        argv.append("--no-log")
    argv.append(script)
    return run_process(argv, timeout, env, output_limit)


def _communicate_bounded(proc, timeout, limit):
    """Drain both pipes with a fixed per-stream cap, including during timeout cleanup."""
    output = [[], []]
    limited = threading.Event()

    def drain(index, stream):
        remaining = limit
        try:
            while True:
                chunk = stream.read(4096)
                if not chunk:
                    break
                retained = chunk[:remaining]
                output[index].append(retained)
                remaining -= len(retained)
                if len(retained) < len(chunk):
                    limited.set()
                    _terminate_process_group(proc, close_pipes=False)
                    break
        finally:
            stream.close()

    readers = [threading.Thread(target=drain, args=(index, stream), daemon=True)
               for index, stream in enumerate((proc.stdout, proc.stderr))]
    for reader in readers:
        reader.start()
    timed_out = threading.Event()

    def expire():
        if proc.returncode is None:
            timed_out.set()
            _terminate_process_group(proc, close_pipes=False)

    # timed Popen.wait polls on POSIX and quantizes short wall measurements;
    # a watchdog preserves the blocking wait's exit timestamp.
    watchdog = threading.Timer(timeout, expire)
    watchdog.daemon = True
    watchdog.start()
    try:
        proc.wait()
    finally:
        watchdog.cancel()
    for reader in readers:
        reader.join(timeout=5)
    return ''.join(output[0]), ''.join(output[1]), timed_out.is_set(), limited.is_set()


def _terminate_process_group(proc: subprocess.Popen[str], close_pipes: bool = True) -> None:
    """Terminates descendants before closing inherited output pipes."""
    if os.name == "nt":
        proc.kill()
    else:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except (ProcessLookupError, PermissionError):
            try:
                proc.kill()
            except (ProcessLookupError, PermissionError):
                pass
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            pass
    if close_pipes and proc.stdout:
        proc.stdout.close()
    if close_pipes and proc.stderr:
        proc.stderr.close()
