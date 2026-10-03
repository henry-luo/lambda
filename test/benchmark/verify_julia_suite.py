#!/usr/bin/env python3
"""Verify the Julia microbenchmark contract and every selected language port."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time

import run_benchmarks as registry
import run_c2mir_benchmarks as c2mir
import run_go_benchmarks as go
import run_julia_benchmarks as julia

ROOT = Path(registry.PROJECT_ROOT).resolve()
SUITE = ROOT / "test/benchmark/julia"
NAMES = [entry[0] for entry in registry.JULIA_MICRO]


def checked_output(result, name, expected):
    """Require a complete result and exactly one finite positive workload timer."""
    lines = result.stdout.strip().splitlines()
    oracle_line = name + ": PASS " + " ".join(map(str, expected))
    if result.returncode != 0 or len(lines) != 2 or lines[0] != oracle_line:
        return None
    if not re.fullmatch(r"__TIMING__:[\d.]+(?:e[+-]?\d+)?", lines[1]):
        return None
    timer = registry.parse_timing(result.stdout)
    return timer if timer is not None and math.isfinite(timer) and timer > 0 else None


def contract_oracle():
    """Use independent conversion, summation and trace identities, not port kernels."""
    expected = {}
    seed, checksum, size = 42, 0, 0
    for i in range(100000):
        seed = seed * 16807 % 2147483647
        value = 0 if i % 8 == 0 else -seed if i % 8 == 1 else seed
        text = str(value)
        size += len(text)
        checksum = (checksum * 31 + int(text) + 2147483647) % 1000000007
    expected["parse_integers"] = [checksum, size, seed, 0]
    values = [math.fsum(1.0 / (k * k) for k in range(1, 10001 + r)) for r in range(500)]
    expected["iteration_pi_sum"] = [math.floor(values[0] * 1e12), math.floor(values[-1] * 1e12),
                                     math.floor(math.fsum(v * (i + 1) for i, v in enumerate(values)) * 1e6),
                                     sum(10000 + r for r in range(500))]
    text = "".join(f"{i} {i + 1}\n" for i in range(1, 100001))
    digest = 0
    for byte in text.encode("ascii"):
        digest = (digest * 31 + byte) % 1000000007
    expected["formatted_output"] = [len(text), digest, math.ceil(100000 / 256), 100000]
    seed, digest, v, w = 42, 0, [], []
    for _ in range(1000):
        blocks = []
        for _ in range(100):
            seed = seed * 16807 % 2147483647
            blocks.append(seed / 2147483647.0 * 2.0 - 1.0)
        a, b, c, d = [blocks[k:k + 25] for k in range(0, 100, 25)]
        p = [a[r*5:r*5+5] + b[r*5:r*5+5] + c[r*5:r*5+5] + d[r*5:r*5+5] for r in range(5)]
        q = [a[r*5:r*5+5] + b[r*5:r*5+5] for r in range(5)] + [c[r*5:r*5+5] + d[r*5:r*5+5] for r in range(5)]
        traces = []
        for matrix in (p, q):
            columns = list(zip(*matrix))
            g = [[sum(x*y for x, y in zip(ci, cj)) for cj in columns] for ci in columns]
            second = [[sum(x*y for x, y in zip(gi, gj)) for gj in g] for gi in g]
            # For symmetric G, tr(G^4) is the squared Frobenius norm of G^2.
            traces.append(sum(x*x for row in second for x in row))
        v.append(traces[0])
        w.append(traces[1])
        for value in traces:
            digest = (digest * 31 + math.floor(value * 1000)) % 1000000007

    def cv(values):
        mean = math.fsum(values) / len(values)
        return math.sqrt(math.fsum((x - mean)**2 for x in values) / (len(values) - 1)) / mean

    expected["matrix_statistics"] = [math.floor(cv(v) * 1e9), math.floor(cv(w) * 1e9), digest, seed]
    return expected


def port_command(engine, name):
    if engine in ("mir", "mir_typed", "interp", "interp_typed"):
        typed = engine.endswith("_typed")
        path = SUITE / f"{name}{'2' if typed else ''}.ls"
        env = {"LAMBDA_EXEC_BACKEND": "interp" if engine.startswith("interp") else "jit"}
        return [registry.LAMBDA_EXE, "run", str(path)], env
    if engine in ("nodejs", "lambdajs", "quickjs", "mvpjs"):
        path = str(SUITE / f"{name}.js")
        if engine == "nodejs":
            return [registry.NODE_EXE, path], {}
        if engine == "quickjs":
            return [registry.QJS_EXE, "--stack-size", str(registry.QJS_STACK_SIZE), "--std", "-m",
                    registry.make_qjs_wrapper(path)], {}
        path = registry.expand_benchmark_js(path)
        if engine == "mvpjs":
            return [registry.LAMBDA_EXE, "js", "--runtime=mvp", path], {}
        return [registry.LAMBDA_EXE, "js", path], {"JS_EXEC_BACKEND": "mir"}
    if engine == "python":
        return [sys.executable, str(SUITE / "python" / f"{name}.py")], {}
    if engine == "julia":
        return julia.build_command(julia.port_source("julia", name)), julia.julia_environment()
    if engine == "c2mir":
        error = c2mir.ensure_c2m()
        if error:
            raise RuntimeError(error)
        return c2mir.build_command(c2mir.port_source("julia", name)), {}
    if engine == "go":
        compiler = go.go_executable()
        if not compiler:
            raise RuntimeError("Go toolchain is missing")
        go.DEFAULT_BUILD_DIR.mkdir(parents=True, exist_ok=True)
        executable, error = go.build_binary(compiler, "julia", name, go.DEFAULT_BUILD_DIR)
        if error:
            raise RuntimeError(error)
        return [str(executable)], {}
    raise ValueError(engine)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engines", default="mir,mir_typed,nodejs,lambdajs,quickjs,python,julia,c2mir,go")
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--output", type=Path, default=ROOT / "temp/julia_suite_validation.json")
    args = parser.parse_args()
    engines = args.engines.split(",")
    if set(engines) & {"mir", "mir_typed", "interp", "interp_typed", "lambdajs", "mvpjs"}:
        registry.check_release_build()
    registry.require_pinned_node_version(engines, "time")
    expected = contract_oracle()
    assert expected == json.loads((SUITE / "expected.json").read_text()), "contract oracle changed"
    records = []
    for engine in engines:
        for name in NAMES:
            record = {"engine": engine, "name": name, "status": "failed"}
            try:
                command, extra_env = port_command(engine, name)
                env = os.environ.copy()
                env.update(extra_env)
                started = time.perf_counter_ns()
                result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True,
                                        text=True, timeout=args.timeout)
                record.update(command=command, process_ms=(time.perf_counter_ns()-started)/1e6,
                              returncode=result.returncode, stderr=result.stderr,
                              stdout_sha256=hashlib.sha256(result.stdout.encode()).hexdigest())
                timer = checked_output(result, name, expected[name])
                if timer is not None:
                    record.update(status="ok", result=expected[name], execution_ms=timer)
                else:
                    record["stdout"] = result.stdout
            except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                record["error"] = str(error)
            records.append(record)
            print(f"{engine}/{name}: {record['status']}", flush=True)
    sources = [p for p in SUITE.rglob("*") if p.suffix in (".ls", ".js", ".py", ".jl", ".c", ".h", ".json")]
    sources += list((ROOT / "test/benchmark/go/cmd/julia").rglob("*.go"))
    sources.append(ROOT / "test/benchmark/go/internal/bench/julia_micro.go")
    payload = {"warmup_runs": 1, "expected": expected, "records": records,
               "sources_sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2) + "\n")
    passed = sum(record["status"] == "ok" for record in records)
    print(f"{passed}/{len(records)} ports verified; {args.output}")
    return 0 if passed == len(records) else 1


if __name__ == "__main__":
    sys.exit(main())
