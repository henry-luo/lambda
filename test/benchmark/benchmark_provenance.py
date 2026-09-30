"""Shared source and toolchain identity for Lambda/C2MIR benchmark campaigns."""

import hashlib
import os
import subprocess


PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def command_output(command):
    try:
        result = subprocess.run(command, capture_output=True, text=True, check=False)
    except OSError:
        return None
    output = (result.stdout or result.stderr or "").strip()
    return output or None


def hashed_file_corpus(paths, root):
    """Record a stable path-and-content digest for a complete source set."""
    files = []
    for path in sorted(paths):
        files.append({
            "path": os.path.relpath(path, root).replace(os.sep, "/"),
            "sha256": sha256_file(path),
        })
    digest = hashlib.sha256()
    for item in files:
        digest.update(item["path"].encode("utf-8"))
        digest.update(b"\0")
        digest.update(item["sha256"].encode("ascii"))
        digest.update(b"\n")
    return {"sha256": digest.hexdigest(), "file_count": len(files), "files": files}


def c2mir_source_corpus():
    """Hash the C ports, shared headers, timer, and runner used for comparison."""
    root = os.path.join(PROJECT_ROOT, "test", "benchmark")
    paths = []
    for directory, subdirs, filenames in os.walk(root):
        subdirs.sort()
        for filename in sorted(filenames):
            path = os.path.join(directory, filename)
            relative = os.path.relpath(path, root).replace(os.sep, "/")
            if not os.path.isfile(path) or not (
                ("/c2mir/" in "/" + relative and filename.endswith((".c", ".h")))
                or relative in ("c2mir/bench_timer_main.c",
                                "run_c2mir_benchmarks.py", "benchmark_provenance.py")
            ):
                continue
            paths.append(path)
    return hashed_file_corpus(paths, root)
