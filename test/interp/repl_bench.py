#!/usr/bin/env python3
"""Per-line REPL latency vs history length, for corpus item C3.

The REPL re-parses, re-builds and (on the JIT tier) re-lowers the entire
accumulated history for every line — LR_01 Known Issue #8's O(n^2). This driver
feeds a synthetic history of a given length and times the marginal line, so the
compile share T0 removes is measurable directly.

All output goes under ./temp/ (CLAUDE rule 2).
"""
import argparse
import os
import statistics
import subprocess
import time

HISTORY_SIZES = (10, 100, 1000)


def build_history(lines: int) -> str:
    # Straight-line top-level bindings: the run-once workload the REPL actually
    # accumulates. Deterministic so runs are comparable.
    body = [f"let h{i} = {i % 91 + 1} + {i % 7}" for i in range(lines)]
    body.append("h0")
    return "\n".join(body) + "\n"


def time_run(path: str, tier: str, repeats: int) -> float:
    env = dict(os.environ)
    env["LAMBDA_TIER"] = tier
    samples = []
    for attempt in range(repeats + 1):
        start = time.perf_counter()
        subprocess.run(["./lambda.exe", path], env=env, capture_output=True)
        elapsed = (time.perf_counter() - start) * 1000.0
        if attempt:                       # discard one warm-up (U33 protocol)
            samples.append(elapsed)
    return statistics.median(samples)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--sizes", type=int, nargs="*", default=list(HISTORY_SIZES))
    ap.add_argument("--repeats", type=int, default=5)
    ap.add_argument("--out", default="temp/interp_repl_bench.tsv")
    args = ap.parse_args()

    os.makedirs("temp", exist_ok=True)
    rows = []
    for size in args.sizes:
        path = f"temp/interp_repl_history_{size}.ls"
        with open(path, "w") as f:
            f.write(build_history(size))
        jit_ms = time_run(path, "jit", args.repeats)
        interp_ms = time_run(path, "interp", args.repeats)
        rows.append((size, jit_ms, interp_ms))
        print(f"history={size:5d}  jit={jit_ms:8.2f} ms  interp={interp_ms:8.2f} ms  "
              f"ratio={jit_ms / interp_ms if interp_ms else 0:.2f}x")

    with open(args.out, "w") as f:
        f.write("# history_lines\tjit_ms\tinterp_ms\tratio\n")
        for size, jit_ms, interp_ms in rows:
            ratio = jit_ms / interp_ms if interp_ms else 0.0
            f.write(f"{size}\t{jit_ms:.3f}\t{interp_ms:.3f}\t{ratio:.3f}\n")
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
