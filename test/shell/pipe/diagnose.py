#!/usr/bin/env python3
"""Measure Lambda's final writer and tier on the verified large mixed input."""

import csv
from pathlib import Path
import random
import runpy
import statistics


bench = runpy.run_path(str(Path(__file__).with_name("benchmark.py")))
work = bench["WORK"]
env = bench["ENV"]
launch = bench["launch"]
bench["create_fixture"]("large", bench["SIZES"]["large"])

variants = {
    "cat/auto": ("mixed", "auto"),
    "stdout/auto": ("mixed_terminal", "auto"),
    "cat/jit": ("mixed", "jit"),
}
reference, _ = launch("Bash", "external", "large", capture=True)
for (task, tier) in variants.values():
    env["LAMBDA_EXEC_BACKEND"] = tier
    result, elapsed = launch("Lambda", task, "large", capture=True)
    assert result == reference, (task, tier, len(result), len(reference))
    print(f"validated {task} under {tier} in {elapsed:.1f} ms", flush=True)

rng = random.Random(735)
order = list(variants)
timings = {name: [] for name in order}
with (work / "timings-diagnostic.csv").open("w", encoding="utf-8", newline="") as output:
    writer = csv.writer(output)
    writer.writerow(("trial", "variant", "ms"))
    for trial in range(1, 4):
        rng.shuffle(order)
        for name in order:
            task, tier = variants[name]
            env["LAMBDA_EXEC_BACKEND"] = tier
            _, elapsed = launch("Lambda", task, "large")
            timings[name].append(elapsed)
            writer.writerow((trial, name, f"{elapsed:.6f}"))
        output.flush()
        print(f"timed diagnostic trial {trial}/3", flush=True)
for name, samples in timings.items():
    print(f"{name}: median {statistics.median(samples):.3f} ms "
          f"(range {min(samples):.3f}–{max(samples):.3f}, n={len(samples)})")
