"""Isolated Lambda process execution shared by tier and fuzz test runners.

Each invocation starts a process group so an engine timeout can close renderer
or helper descendants without leaving their output pipes held open.
"""

from dataclasses import dataclass
import os
import signal
import subprocess
from typing import Mapping, Optional, Sequence


@dataclass
class LambdaProcessResult:
    argv: Sequence[str]
    stdout: str
    stderr: str
    return_code: Optional[int]
    timed_out: bool
    launch_error: str = ""

    @property
    def status(self) -> str:
        if self.launch_error:
            return "launcher-error"
        if self.timed_out:
            return "timeout"
        if self.return_code == 0:
            return "ok"
        return f"exit{self.return_code}"


def run_process(argv: Sequence[str], timeout: float,
                extra_env: Optional[Mapping[str, Optional[str]]] = None) -> LambdaProcessResult:
    """Runs an arbitrary test command in an isolated process group."""
    env = dict(os.environ)
    if extra_env:
        for name, value in extra_env.items():
            if value is None:
                env.pop(name, None)
            else:
                env[name] = value
    command = list(argv)

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
                       extra_env: Optional[Mapping[str, str]] = None) -> LambdaProcessResult:
    """Runs one Lambda script in an isolated process group.

    A missing tier deliberately means AUTO. Callers that need eager MIR Direct
    must pass ``jit`` rather than relying on the default (D8.1.1v10).
    """
    env: dict[str, Optional[str]] = dict(extra_env or {})
    if tier is None:
        env["LAMBDA_TIER"] = None
    else:
        env["LAMBDA_TIER"] = tier
    argv = [executable]
    if procedural:
        argv.append("run")
    if dry_run:
        argv.append("--dry-run")
    if no_log:
        argv.append("--no-log")
    argv.append(script)
    return run_process(argv, timeout, env)


def _terminate_process_group(proc: subprocess.Popen[str]) -> None:
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
    if proc.stdout:
        proc.stdout.close()
    if proc.stderr:
        proc.stderr.close()
