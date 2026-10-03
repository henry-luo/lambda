#!/usr/bin/env python3
"""Run native Julia benchmark ports, optionally comparing their output with Node.

Each workload gets its own Julia process. The Julia harness warms a fresh copy
before its execution timer; process time includes startup, compilation and warmup.
"""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import time

PROJECT_ROOT = Path(__file__).resolve().parents[2]
JULIA_ROOT = PROJECT_ROOT / "test" / "benchmark" / "julia"
TIMING_RE = re.compile(r"^__TIMING__:([\d.]+(?:[eE][+-]?\d+)?)$", re.MULTILINE)


def julia_executable():
    configured = os.environ.get("JULIA_EXE")
    return shutil.which(configured or "julia")


def port_source(suite, name):
    path = JULIA_ROOT / suite / f"{name}.jl"
    return path if path.is_file() else None


def julia_environment():
    # keep Julia's depot and temporary artifacts inside the repository.
    depot = PROJECT_ROOT / "temp" / "julia-depot"
    depot.mkdir(parents=True, exist_ok=True)
    return {"JULIA_DEPOT_PATH": str(depot), "TMPDIR": str(PROJECT_ROOT / "temp"),
            "JULIA_NUM_THREADS": "1", "OPENBLAS_NUM_THREADS": "1",
            "JULIA_BENCH_WARMUP": str(warmup_runs())}


def warmup_runs():
    count = int(os.environ.get("JULIA_BENCH_WARMUP", "1"))
    if count not in (0, 1):
        raise ValueError("JULIA_BENCH_WARMUP must be 0 or 1")
    return count


def build_command(source):
    command = [julia_executable(), "--startup-file=no", "--history-file=no", "--threads=1", str(source)]
    if source.parent.name == "awfy":
        import run_benchmarks as registry
        outer, inner = registry.awfy_node_iterations(source.stem)
        command += [str(inner), str(outer)]
    elif source.parent.name == "jetstream":
        import run_benchmarks as registry
        _, count = registry._detect_jetstream_run_function(registry.JETSTREAM_NODE[source.stem])
        command.append(str(count))
    return command


def shell_command(suite, name):
    source = port_source(suite, name)
    if source is None:
        return None, "missing_port"
    if not julia_executable():
        return None, "toolchain_missing"
    env = " ".join(f"{key}={shlex.quote(value)}" for key, value in julia_environment().items())
    return "env " + env + " " + shlex.join(build_command(source)), "ok"


def normalized_output(stdout):
    return TIMING_RE.sub("", stdout).strip()


def output_status(proc):
    if proc.returncode:
        return f"exit_{proc.returncode}"
    if re.search(r"\bFAIL\b", proc.stdout):
        return "wrong_output"
    matches = TIMING_RE.findall(proc.stdout)
    if len(matches) != 1 or not math.isfinite(float(matches[0])):
        return "invalid_timing"
    return "ok"


def benchmark_entries(all_entries=False):
    import run_benchmarks as registry
    if not all_entries:
        return registry.build_benchmark_list(None, None)
    entries = []
    for suite, rows in registry.STANDARD_SUITES + [("text", registry.TEXT)]:
        for name, category, ls, js, py in rows:
            entries.append({"suite": suite, "name": name, "category": category,
                            "js_path": js, "is_jetstream": False})
    for name, category, ls in registry.JETSTREAM_LS:
        entries.append({"suite": "jetstream", "name": name, "category": category,
                        "is_jetstream": True, "ref_js": registry.JETSTREAM_NODE[name]})
    return entries


def node_command(entry):
    import run_benchmarks as registry
    if entry["is_jetstream"]:
        oracle = (registry.NAVIER_STOKES_DENSITY_ORACLE_V1
                  if entry["name"] == "navier_stokes" else None)
        source = registry.make_jetstream_node_wrapper(entry["name"], entry["ref_js"], oracle)
    else:
        source = entry["js_path"]
    return [registry.NODE_EXE, source]


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def runtime_metadata():
    executable = julia_executable()
    sources = {str(path.relative_to(PROJECT_ROOT)): sha256(path)
               for path in sorted(JULIA_ROOT.rglob("*.jl"))}
    fixtures = {str(path.relative_to(PROJECT_ROOT)): sha256(path)
                for path in sorted((PROJECT_ROOT / "test/benchmark/text").glob("*.json"))}
    fasta = PROJECT_ROOT / "test/benchmark/beng/input/fasta_1000.txt"
    fixtures[str(fasta.relative_to(PROJECT_ROOT))] = sha256(fasta)
    contract = JULIA_ROOT / "expected.json"
    fixtures[str(contract.relative_to(PROJECT_ROOT))] = sha256(contract)
    return {"executable": executable,
            "version": subprocess.check_output([executable, "--version"], text=True).strip()
                       if executable else None,
            "executable_sha256": sha256(executable) if executable else None,
            "flags": ["--startup-file=no", "--history-file=no", "--threads=1"],
            "warmup_runs": warmup_runs(),
            "suite_warmup_runs": {"julia": 1},
            "warmup": "fresh state for warmup and measurement",
            "timing": "execution excludes startup, warmup and post-work verification; process includes all",
            "threads": 1, "sources_sha256": sources, "fixtures_sha256": fixtures}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", action="append", help="select a suite; may be repeated")
    parser.add_argument("--bench", action="append", help="select a benchmark name; may be repeated")
    parser.add_argument("--all", action="store_true", help="include the six noncanonical duplicate entries")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--verify-node", action="store_true", help="require matching Node result output")
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--warmup", type=int, choices=(0, 1), default=None,
                        help="full warmup runs before timing (default: 1); julia microbenchmarks always use 1 in every language")
    parser.add_argument("--output", type=Path, help="write validation results to this JSON file")
    args = parser.parse_args()
    if args.warmup is not None:
        os.environ["JULIA_BENCH_WARMUP"] = str(args.warmup)
    entries = [b for b in benchmark_entries(args.all)
               if (not args.suite or b["suite"] in args.suite)
               and (not args.bench or b["name"] in args.bench)]
    if not entries:
        parser.error("no matching benchmark entries")
    if args.list:
        for b in entries:
            print(f"{b['suite']}/{b['name']}" + (" (missing port)" if port_source(b["suite"], b["name"]) is None else ""))
        return 0
    executable = julia_executable()
    if not executable:
        parser.error("Julia not found; install Julia or set JULIA_EXE to its executable")
    env = os.environ.copy()
    env.update(julia_environment())
    version = subprocess.check_output([executable, "--version"], text=True).strip()
    print(version, flush=True)
    if args.verify_node:
        import run_benchmarks as registry
        registry.require_pinned_node_version(["nodejs"], "time")
    records = []
    for entry in entries:
        suite, name = entry["suite"], entry["name"]
        record = {"suite": suite, "name": name}
        source = port_source(suite, name)
        if source is None:
            record["status"] = "missing_port"
        else:
            started = time.perf_counter_ns()
            try:
                proc = subprocess.run(build_command(source), cwd=PROJECT_ROOT, env=env,
                                      capture_output=True, text=True, timeout=args.timeout)
                record.update(status=output_status(proc), wall_ms=(time.perf_counter_ns() - started) / 1e6,
                              stdout=proc.stdout, stderr=proc.stderr,
                              source=str(source.relative_to(PROJECT_ROOT)), source_sha256=sha256(source))
                if record["status"] == "ok":
                    record["exec_ms"] = float(TIMING_RE.search(proc.stdout).group(1))
                    if args.verify_node:
                        node = subprocess.run(node_command(entry), cwd=PROJECT_ROOT, capture_output=True,
                                              text=True, timeout=args.timeout)
                        record.update(node_stdout=node.stdout, node_stderr=node.stderr,
                                      node_source_sha256=sha256(entry.get("ref_js") or entry["js_path"]))
                        if output_status(node) != "ok":
                            record["status"] = "node_reference_failed"
                        elif normalized_output(proc.stdout) != normalized_output(node.stdout):
                            record["status"] = "node_output_mismatch"
            except subprocess.TimeoutExpired:
                record["status"] = "timeout"
        records.append(record)
        print(f"{suite}/{name:<18} {record['status']}", flush=True)
        if record["status"] != "ok":
            for key in ("stdout", "stderr", "node_stdout", "node_stderr"):
                if record.get(key):
                    print(f"  {key}: {record[key].strip()[:4000]}", flush=True)
    passed = sum(r["status"] == "ok" for r in records)
    print(f"{passed}/{len(records)} Julia benchmarks passed", flush=True)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps({"julia": runtime_metadata(),
                                          "verify_node": args.verify_node, "records": records}, indent=2) + "\n")
    return 0 if passed == len(records) else 1


if __name__ == "__main__":
    sys.exit(main())
