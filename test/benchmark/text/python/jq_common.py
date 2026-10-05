"""Shared driver for the jq_* text benchmarks on the vendored purejq 0.3.1.

purejq is a pure-Python jq (no C extension); see vendor/VENDOR.md. Each
workload runs the same jq/<name>.jq filter as every other column.
"""

import json
import os
import sys
import threading
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "vendor"))
import purejq  # noqa: E402

JQ_DIR = "test/benchmark/text/jq"


def _run(name, input_kind, input_path, expected, result):
    with open(f"{JQ_DIR}/{name[3:]}.jq", encoding="utf-8") as stream:
        program = purejq.compile(stream.read())
    value = None
    if input_kind == "json":
        with open(input_path, encoding="utf-8") as stream:
            value = json.load(stream)
    elif input_kind == "raw":
        with open(input_path, encoding="utf-8") as stream:
            value = stream.read()
    # compiling the filter and loading the input stay outside the timed region
    started = time.perf_counter_ns()
    outputs = program.all(value)
    elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000
    if outputs != [expected]:
        raise RuntimeError(f"{name}: unexpected output {outputs!r}")
    result.append((outputs[0], elapsed_ms))


def run_jq_benchmark(name, input_kind, input_path, expected):
    # purejq evaluates until/recurse by Python recursion, so jq_bf needs a
    # deeper recursion limit and a thread with a large stack (runtime
    # configuration only; purejq itself is unmodified)
    sys.setrecursionlimit(2_000_000)
    threading.stack_size(512 * 1024 * 1024)
    result = []
    errors = []

    def target():
        try:
            _run(name, input_kind, input_path, expected, result)
        except BaseException as error:  # re-raised on the main thread
            errors.append(error)

    worker = threading.Thread(target=target)
    worker.start()
    worker.join()
    if errors:
        raise errors[0]
    checksum, elapsed_ms = result[0]
    print(f"{name}: CHECKSUM:{checksum}")
    print(f"__TIMING__:{elapsed_ms:.3f}")
