"""Drive one release viewer and report process memory at each completed round."""

from collections import deque
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading


DOCUMENTS = {
    "comprehensive_test.tex": "article.latex-document",
    "math_intensive_test.tex": "article svg.lambda-math",
    "advanced_math_test.md": "#rendered-preview svg.lambda-math",
    "go-package.dot": ".lambda-graph",
    "seq_diagram.d2": ".lambda-sequence svg",
    "architecture.mmd": ".lambda-graph",
    "local.html": "iframe#html-preview",
}
MARKER = "VIEWER_MEMORY_ROUND "


def memory_sampler(process):
    if sys.platform == "darwin":
        # Match sys/resource.h's rusage_info_v2 including its trailing counters.
        class Usage(ctypes.Structure):
            _fields_ = [("uuid", ctypes.c_uint8 * 16), ("prefix", ctypes.c_uint64 * 7),
                        ("footprint", ctypes.c_uint64), ("tail", ctypes.c_uint64 * 10)]

        query = ctypes.CDLL("/usr/lib/libproc.dylib", use_errno=True).proc_pid_rusage
        query.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.POINTER(Usage)]
        usage = Usage()

        def sample():
            if query(process.pid, 2, ctypes.byref(usage)) != 0:
                raise OSError(ctypes.get_errno(), "proc_pid_rusage failed")
            return usage.footprint

        return sample

    if sys.platform.startswith("linux"):
        status = Path(f"/proc/{process.pid}/status")

        def sample():
            for line in status.read_text().splitlines():
                if line.startswith("VmRSS:"):
                    return int(line.split()[1]) * 1024
            raise RuntimeError(f"VmRSS missing from {status}")

        return sample

    if sys.platform == "win32":
        from ctypes import wintypes

        class Counters(ctypes.Structure):
            _fields_ = [("size", wintypes.DWORD), ("faults", wintypes.DWORD),
                        ("counters", ctypes.c_size_t * 8), ("private_bytes", ctypes.c_size_t)]

        query = ctypes.WinDLL("psapi", use_last_error=True).GetProcessMemoryInfo
        query.argtypes = [wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD]
        query.restype = wintypes.BOOL
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        kernel.CloseHandle.restype = wintypes.BOOL
        usage = Counters()
        usage.size = ctypes.sizeof(usage)

        def sample():
            query_limited_information = 0x1000
            handle = kernel.OpenProcess(query_limited_information, False, process.pid)
            if not handle:
                raise ctypes.WinError(ctypes.get_last_error())
            try:
                if not query(handle, ctypes.byref(usage), usage.size):
                    raise ctypes.WinError(ctypes.get_last_error())
                return usage.private_bytes
            finally:
                kernel.CloseHandle(handle)

        return sample

    raise RuntimeError(f"No viewer process memory sampler for {sys.platform}")


def build_replay(work, rounds):
    documents = work / "documents"
    documents.mkdir()
    events = []
    for filename in DOCUMENTS:
        if filename == "local.html":
            (documents / filename).write_text(
                "<!doctype html><meta charset=utf-8><h1>Local viewer fixture</h1>", encoding="utf-8")
        else:
            shutil.copyfile(Path("test/input") / filename, documents / filename)

    for round_number in range(1, rounds + 1):
        for filename, preview in DOCUMENTS.items():
            events.extend([
                {"type": "click", "target": {"selector": f".tree-entry[data-tree-path$='/{filename}'] .tree-label"}},
                {"type": "assert_text", "target": {"selector": ".document-name"}, "equals": filename},
                {"type": "assert_count", "target": {"selector": preview}, "min": 1},
            ])
        # Sample retained memory at the same page and GC boundary every time.
        events.extend([
            {"type": "collect_gc", "count": 2},
            {"type": "assert_script", "js": f"console.log('{MARKER}{round_number}'); true"},
            {"type": "wait", "ms": 250},
        ])

    replay = work / "replay.json"
    replay.write_text(json.dumps({"viewport": {"width": 1200, "height": 900}, "events": events}), encoding="utf-8")
    return documents, replay


def run(executable, work, rounds):
    documents, replay = build_replay(work, rounds)
    result_file = work / "result.json"
    env = dict(os.environ, LAMBDA_AUDIO_OUTPUT="offline")
    env.setdefault("LAMBDA_HOME", str(Path("lmd").resolve()))
    args = [executable, "view", documents, "--headless", "--no-log", "--event-file", replay,
            "--event-result", result_file]
    tail = deque(maxlen=40)
    with subprocess.Popen(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, encoding="utf-8", errors="replace") as viewer:
        deadline = threading.Timer(150, viewer.kill)
        deadline.start()
        try:
            sample = memory_sampler(viewer)
            for line in viewer.stdout:
                tail.append(line)
                if line.startswith(MARKER):
                    round_number = int(line[len(MARKER):])
                    print(f"VIEWER_MEMORY_SAMPLE {round_number} {sample()}", flush=True)
            if viewer.wait() != 0:
                raise RuntimeError(f"Viewer exited with code {viewer.returncode}")
            result = json.loads(result_file.read_text(encoding="utf-8"))
            expected = {"passed": rounds * (len(DOCUMENTS) * 2 + 2), "failed": 0}
            if result["result"] != "PASS" or result["assertions"] != expected:
                raise RuntimeError(f"Viewer replay incomplete: {result}")
        except (OSError, ValueError, RuntimeError):
            sys.stderr.writelines(tail)
            raise
        finally:
            deadline.cancel()
            if viewer.poll() is None:
                viewer.kill()


if __name__ == "__main__":
    Path("temp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="viewer-navigation-", dir="temp") as directory:
        run(sys.argv[1], Path(directory), int(sys.argv[2]))
