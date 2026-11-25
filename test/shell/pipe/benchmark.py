#!/usr/bin/env python3
"""Compare fresh CLI processes with identical generated inputs and checked outputs."""

import argparse
import csv
import hashlib
import os
from pathlib import Path
import random
import statistics
import subprocess
import time


ROOT = Path(__file__).resolve().parents[3]
WORK = ROOT / "temp" / "shell-pipeline" / "benchmark"
LAMBDA_EXE = Path(os.environ.get("LAMBDA_SHELL_BENCH_EXE", WORK / "lambda.exe")).resolve()
COMMANDS = {
    "Bash": ("/bin/bash", "--noprofile", "--norc"),
    "Lambda": (str(LAMBDA_EXE), "run", "--no-log"),
    "Nushell": (str(WORK / "nu-0.117.0-aarch64-apple-darwin" / "nu"), "--no-config-file"),
}
SUFFIX = {"Bash": "sh", "Lambda": "ls", "Nushell": "nu"}
WORKLOADS = Path(__file__).resolve().with_name("workloads")
TASKS = ("startup", "external", "sort_count", "mixed")

ENV = {
    key: value for key, value in os.environ.items()
    if not key.startswith("LAMBDA_") and key not in ("BASH_ENV", "ENV")
}
ENV.update(PATH="/usr/bin:/bin", LC_ALL="C", LAMBDA_HOME=str(ROOT / "lmd"),
           LAMBDA_EXEC_BACKEND="auto", TMPDIR=str(WORK))
SIZES = {"tiny": 3, "large": 1_048_576}


def create_fixture(size, rows):
    folder = WORK / size
    folder.mkdir(parents=True, exist_ok=True)
    target = folder / "input.csv"
    if target.exists() and target.stat().st_size == rows * 100:
        return folder
    with target.open("wb") as output:
        for first in range(0, rows, 8192):
            chunk = bytearray()
            for i in range(first, min(first + 8192, rows)):
                kind = "ERROR" if i % 4 < 2 else "DEBUG"
                service = (i * 73 + i // 2048 * 97) % 2048
                prefix = f"{kind},svc{service:04d},{i:07d},"
                message = f"client={i % 101:03d} duration={i % 2000:04d} action=read status=retry record queued"
                chunk.extend(f"{prefix + message:<99}\n".encode("ascii"))
            output.write(chunk)
    assert target.stat().st_size == rows * 100
    return folder


def launch(engine, task, size, capture=False):
    command = (*COMMANDS[engine], str(WORKLOADS / f"{task}.{SUFFIX[engine]}"))
    start = time.perf_counter_ns()
    result = subprocess.run(command, cwd=WORK / size, env=ENV, stdin=subprocess.DEVNULL,
                            stdout=subprocess.PIPE if capture else subprocess.DEVNULL,
                            stderr=subprocess.PIPE, timeout=180)
    elapsed_ms = (time.perf_counter_ns() - start) / 1_000_000
    if result.returncode:
        raise RuntimeError(f"{command} {size} exited {result.returncode}\n{result.stderr.decode(errors='replace')}")
    return result.stdout, elapsed_ms


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tiny-only", action="store_true")
    parser.add_argument("--validate-only", action="store_true")
    parser.add_argument("--tiny-trials", type=int, default=31)
    parser.add_argument("--large-trials", type=int, default=7)
    args = parser.parse_args()
    for size, count in SIZES.items():
        if args.tiny_only and size == "large":
            continue
        create_fixture(size, count)
        tasks = [task for task in TASKS if task != "startup" or size == "tiny"]
        workload = [(task, engine) for task in tasks for engine in COMMANDS]
        outputs = {}
        for task, engine in workload:
            output, elapsed = launch(engine, task, size, capture=True)
            if size == "tiny" and engine == "Lambda":
                expected = (WORKLOADS / f"{task}.txt").read_bytes()
                assert output == expected, (task, output, expected)
            if task != "startup":
                reference_task = "external" if task == "mixed" else task
                reference = outputs.setdefault(reference_task, output)
                if output != reference:
                    raise ValueError(
                        f"{size} {task} {engine} differs: {len(output)} bytes vs {len(reference)} "
                        f"({output[:120]!r} vs {reference[:120]!r})"
                    )
            print(f"validated {size} {task} {engine}: {len(output)} bytes, "
                  f"sha256={hashlib.sha256(output).hexdigest()} (validation {elapsed:.1f} ms)", flush=True)
        if args.validate_only:
            continue
        rng = random.Random(6191)
        trials = args.tiny_trials if size == "tiny" else args.large_trials
        timings = {row: [] for row in workload}
        with (WORK / f"timings-{size}.csv").open("w", newline="", encoding="utf-8") as results:
            csv_writer = csv.writer(results)
            csv_writer.writerow(("trial", "size", "task", "engine", "ms"))
            for index in range(trials):
                rng.shuffle(workload)
                for task, engine in workload:
                    _, elapsed = launch(engine, task, size)
                    timings[task, engine].append(elapsed)
                    csv_writer.writerow((index + 1, size, task, engine, f"{elapsed:.6f}"))
                results.flush()
                if size == "large":
                    print(f"timed {size} trial {index + 1}/{trials}", flush=True)
        for (task, engine), samples in timings.items():
            print(f"{size} {task} {engine}: median {statistics.median(samples):.3f} ms "
                  f"(range {min(samples):.3f}–{max(samples):.3f}, n={trials})", flush=True)


if __name__ == "__main__":
    main()
