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


DOCUMENTS = (
    ("comprehensive_test.tex", "article.latex-document", r"\documentclass"),
    ("math_intensive_test.tex", "article svg.lambda-math", r"\documentclass"),
    ("advanced_math_test.md", "#rendered-preview svg.lambda-math", "Advanced Math Expression Test"),
    ("go-package.dot", ".lambda-graph", "digraph"),
    ("seq_diagram.d2", ".lambda-sequence svg", "sequence_diagram"),
    ("architecture.mmd", ".lambda-graph", "graph"),
    ("local.html", "iframe#html-preview", "Local viewer fixture"),
    ("docx/minimal.docx", "#docx-preview .docx-document p", "Hello, DOCX!"),
    ("pptx/features.pptx", "#pptx-preview .slide-stage p", "<title>Feature showcase</title>"),
    ("xlsx/basic.xlsx", "#xlsx-preview [data-cell='A1']", None),
    ("math_intensive_test.pdf", "#pdf-preview .pdf-page svg", None),
    ("ebook/minimal.epub", "#epub-preview .ebook-reader iframe", "<package"),
    ("test.rtf", "#rtf-preview p", r"\rtf"),
    ("test/fixtures/edit/sample.csv", "#csv-preview tbody td", "Kuala Lumpur"),
)
EDIT_TEXT = "ReplayEdit"
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


def target_event(kind, selector, **fields):
    return {"type": kind, "target": {"selector": selector}, **fields}


def click(selector):
    return target_event("click", selector)


def check_text(selector, **fields):
    return target_event("assert_text", selector, **fields)


def check_count(selector, **fields):
    return target_event("assert_count", selector, **fields)


def source_events(filename, source):
    if source is None:
        return []
    events = [
        click(".tab-source"),
        check_count("#edit-surface[aria-readonly='true']", count=1),
        check_text("#source-preview", contains=source),
    ]
    if filename.endswith(".md"):
        events.extend([
            check_text(".tok-heading", contains=source),
            target_event("scroll", "#edit-surface", dx=0, dy=-28),
            check_count(".tok-strong", min=1),
            check_text("#source-preview", contains="Scrollable sample", not_contains=source),
        ])
    elif filename.endswith((".html", ".docx", ".pptx", ".epub")):
        events.append(check_count(".tok-tag", min=1))
        if filename.endswith(".pptx"):
            events.extend([
                target_event("scroll", "#edit-surface", dx=-28, dy=0),
                {"type": "assert_script",
                 "js": "parseFloat(document.querySelector('.src-hthumb')?.style.left) > 0"},
            ])
    return [*events, click(".tab-view")]


def text_edit_events(selector, path):
    return [
        click(".tab-edit"),
        click(selector),
        {"type": "key_press", "key": "End"},
        {"type": "type", "text": EDIT_TEXT},
        check_text("#edit-surface", contains=EDIT_TEXT),
        click(".edit-btn-undo"),
        check_text("#edit-surface", not_contains=EDIT_TEXT),
        click(".edit-btn-redo"),
        check_text("#edit-surface", contains=EDIT_TEXT),
        click(".viewer-edit-host .tab-view"),
        check_count(".edit-dialog-navigate", count=1),
        click(".edit-dialog-discard"),
        *([{"type": "assert_file", "path": str(path), "not_contains": EDIT_TEXT}]
          if path.suffix != ".docx" else [check_text("#docx-preview p", equals="Hello, DOCX!")]),
    ]


def document_events(path, round_number):
    filename = path.name
    if filename == "advanced_math_test.md":
        return text_edit_events("#edit-surface h1", path)
    if filename == "comprehensive_test.tex":
        return text_edit_events("#edit-surface .edit-doc > div:first-child", path)
    if filename == "minimal.docx":
        return text_edit_events("#edit-surface p", path)
    if filename == "local.html":
        return [
            {"type": "switch_frame", "selector": "#html-preview"},
            check_text("h1", equals="Local viewer fixture"),
            {"type": "switch_frame"},
        ]
    if filename == "basic.xlsx":
        return [
            click(".xlsx-tab[data-sheet='1']"),
            check_text("[data-cell='A1']", equals="Second worksheet"),
        ]
    if filename == "minimal.epub":
        return [
            click("#epub-preview [data-ebook-command=next]"),
            {"type": "wait", "ms": 200},
            {"type": "switch_frame", "selector": "#epub-preview iframe"},
            check_text("h1", equals="Next chapter"),
            {"type": "switch_frame"},
        ]
    if filename == "sample.csv":
        # Alternate two values so saving exercises the disk path without growing the file.
        value = "Alison" if round_number % 2 else "Alice"
        return [
            click(".tab-edit"),
            click("#grid-cell-1-0"),
            {"type": "key_combo", "key": "a", "mods_str": "super"},
            {"type": "type", "text": value},
            {"type": "key_combo", "key": "s", "mods_str": "super"},
            {"type": "assert_file", "path": str(path), "contains": value},
            click(".viewer-edit-host .tab-view"),
            check_text("#csv-preview tbody tr:first-child td:first-child", equals=value),
        ]
    return []


def build_replay(work, rounds):
    documents = work / "documents"
    documents.mkdir()
    events = []
    for fixture, _, _ in DOCUMENTS:
        path = documents / Path(fixture).name
        if fixture == "local.html":
            path.write_text(
                "<!doctype html><meta charset=utf-8><h1>Local viewer fixture</h1>", encoding="utf-8")
        else:
            source = Path(fixture) if fixture.startswith("test/") else Path("test/input") / fixture
            shutil.copyfile(source, path)
        if path.suffix == ".md":
            with path.open("a", encoding="utf-8") as markdown:
                markdown.write("".join(f"\n## Scrollable sample {i:03d}\n\n**bold {i:03d}**\n"
                                       for i in range(80)))

    for round_number in range(1, rounds + 1):
        for fixture, preview, source in DOCUMENTS:
            filename = Path(fixture).name
            events.extend([
                click(f".tree-entry[data-tree-path$='/{filename}'] .tree-label"),
                check_text(".document-name", equals=filename),
                check_count(preview, min=1),
                *source_events(filename, source),
                *document_events(documents / filename, round_number),
            ])
            if source is not None:
                events.append(check_count(preview, min=1))
        # Sample retained memory at the same page and GC boundary every time.
        events.extend([
            {"type": "collect_gc", "count": 2},
            {"type": "assert_script", "js": f"console.log('{MARKER}{round_number}'); true"},
            {"type": "wait", "ms": 250},
        ])

    replay = work / "replay.json"
    replay.write_text(json.dumps({"viewport": {"width": 1200, "height": 900}, "events": events}), encoding="utf-8")
    assertions = sum(event["type"].startswith("assert_") or event["type"] == "collect_gc"
                     for event in events)
    return documents, replay, assertions


def run(executable, work, rounds):
    documents, replay, assertions = build_replay(work, rounds)
    result_file = work / "result.json"
    env = dict(os.environ, LAMBDA_AUDIO_OUTPUT="offline")
    env.setdefault("LAMBDA_HOME", str(Path("lmd").resolve()))
    args = [executable, "view", documents, "--headless", "--no-log", "--event-file", replay,
            "--event-result", result_file]
    tail = deque(maxlen=40)
    with subprocess.Popen(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, encoding="utf-8", errors="replace") as viewer:
        deadline = threading.Timer(900, viewer.kill)
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
            expected = {"passed": assertions, "failed": 0}
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
