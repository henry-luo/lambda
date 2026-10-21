"""Measure one child without inheriting earlier children's resource high-water marks."""

import math
import os
import signal
import subprocess
import sys
import time


def run_measured(command, *, cwd=None, env=None, stdout=None, stderr=None, timeout=120):
    if timeout <= 0 or not math.isfinite(timeout):
        raise ValueError("timeout must be finite and positive")
    started = time.monotonic()
    process = subprocess.Popen(command, cwd=cwd, env=env, stdout=stdout, stderr=stderr,
                               start_new_session=os.name == "posix")
    usage = None
    timed_out = False
    wait4 = getattr(os, "wait4", None)

    def kill_owned_group():
        try:
            if os.name == "posix":
                os.killpg(process.pid, signal.SIGKILL)
            else:
                process.kill()
        except ProcessLookupError:
            pass

    try:
        if callable(wait4):
            while True:
                pid, status, child_usage = wait4(process.pid, os.WNOHANG)
                if pid:
                    process.returncode = os.waitstatus_to_exitcode(status)
                    usage = child_usage
                    break
                remaining = timeout - (time.monotonic() - started)
                if remaining <= 0:
                    timed_out = True
                    kill_owned_group()
                    _, status, usage = wait4(process.pid, 0)
                    process.returncode = os.waitstatus_to_exitcode(status)
                    break
                # file-backed output avoids pipe deadlock; limit exit-observation bias to 1 ms.
                time.sleep(min(.001, remaining))
        else:
            try:
                process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                kill_owned_group()
                process.wait()
    except BaseException:
        kill_owned_group()
        process.wait()
        raise

    return dict(pid=process.pid, exit_code=process.returncode, timed_out=timed_out,
                wall_seconds=time.monotonic() - started,
                peak_rss_bytes=None if usage is None else
                    usage.ru_maxrss * (1 if sys.platform == "darwin" else 1024),
                user_seconds=None if usage is None else usage.ru_utime,
                system_seconds=None if usage is None else usage.ru_stime,
                resource_accounting="wait4" if usage is not None else "unavailable")
