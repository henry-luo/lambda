#!/usr/bin/env python3
"""Compare spelling loading and lookup with one pinned release host and identical inputs."""

import argparse
import json
import os
import platform
import shutil
import statistics
import subprocess
import tempfile
from pathlib import Path

from benchmark_process import run_measured
from spelling_vocab_poc import digest, encoded


MODULE = Path("lmd/package/doc/spelling/front_coded.ls")
SCRIPT = Path("test/lambda/spelling/_front_bench.ls")
RESOURCE = Path("temp/spelling_vocab_poc/front")


def source_hashes(home):
    return {str(p.relative_to(home)): digest(p.read_bytes())
            for p in sorted(home.rglob("*.ls")) if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path,
                        default=Path("temp/spelling_vocab_poc/lambda-front-release.exe"))
    parser.add_argument("--control-ref", default="0febb9623")
    parser.add_argument("--pairs", type=int, default=3)
    args = parser.parse_args()
    if args.pairs < 1:
        parser.error("pairs must be positive")
    repo = Path(__file__).resolve().parent.parent
    original_exe = args.executable.resolve()
    executable_bytes = original_exe.read_bytes()
    if b"VALIDATOR] validate_against_base_type" in executable_bytes:
        parser.error("performance measurements require a release executable")
    (repo / "temp").mkdir(exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="spelling-pointer-ab-", dir=repo / "temp"))
    exe = output / "lambda-release.exe"
    shutil.copy2(original_exe, exe)
    assert digest(exe.read_bytes()) == digest(executable_bytes)
    control = subprocess.check_output(["git", "show", f"{args.control_ref}:{MODULE}"], cwd=repo)
    candidate = (repo / MODULE).read_bytes()
    assert control != candidate, "control and candidate sources are identical"
    copies = [SCRIPT, SCRIPT.with_suffix(".txt"), Path("test/input/spelling/paragraph_100.txt"),
              RESOURCE / "manifest.json", RESOURCE / "en-US.dict", Path("log.conf")]
    provenance = {"executable_sha256": digest(executable_bytes),
                  "benchmark_utility_sha256": digest(Path(__file__).read_bytes()),
                  "inputs": {str(p): digest((repo / p).read_bytes()) for p in copies},
                  "packages": {}}
    for variant, module in [("control", control), ("candidate", candidate)]:
        workspace = output / variant
        shutil.copytree(repo / "lmd", workspace / "lmd", symlinks=True)
        (workspace / MODULE).write_bytes(module)
        for path in copies:
            target = workspace / path
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(repo / path, target)
        provenance["packages"][variant] = source_hashes(workspace / "lmd")
    control_packages = provenance["packages"]["control"]
    candidate_packages = provenance["packages"]["candidate"]
    assert control_packages.keys() == candidate_packages.keys()
    assert [p for p in control_packages if control_packages[p] != candidate_packages[p]] == [
        "package/doc/spelling/front_coded.ls"]
    (output / "provenance.json").write_bytes(encoded(provenance))
    report = {"output": str(output.relative_to(repo)), "control_ref": args.control_ref,
              "executable_sha256": provenance["executable_sha256"],
              "backend": "jit", "platform": platform.platform(), "cpu_count": os.cpu_count(),
              "scope": "read/from_bytes phases plus uncached 100-word checks; loading excluded from paragraph samples",
              "runs": []}
    print(f"Artifacts: {output.relative_to(repo)}", flush=True)
    expected = (repo / SCRIPT.with_suffix(".txt")).read_text().strip()
    result_path = output / "results.json"
    for pair in range(args.pairs):
        order = ["control", "candidate"] if pair % 2 == 0 else ["candidate", "control"]
        for variant in order:
            workspace = output / variant
            load_before = os.getloadavg()
            log_path = output / f"pair-{pair}-{variant}.log"
            with log_path.open("w") as log:
                process = run_measured([str(exe), "--no-log", "run", str(SCRIPT)], cwd=workspace,
                    env=dict(os.environ, LAMBDA_EXEC_BACKEND="jit", LAMBDA_HOME=str(workspace / "lmd")),
                    stdout=log, stderr=log, timeout=240)
            run = {"pair": pair, "variant": variant, "process": process,
                   "host_load_before": load_before, "host_load_after": os.getloadavg()}
            valid = (process["exit_code"] == 0 and not process["timed_out"] and
                     log_path.read_text().strip() == expected)
            if valid:
                data = json.loads((workspace / RESOURCE / "benchmark.json").read_text())
                run.update(data)
                run["median_ms"] = statistics.median(data["samples_ms"])
            run["golden_matches"] = valid
            report["runs"].append(run)
            result_path.write_text(json.dumps(report, indent=2) + "\n")
            if not valid:
                raise SystemExit(f"benchmark failed without retry: {log_path}")
            print(f"pair {pair} {variant}: {run['median_ms']:.3f} ms/paragraph; "
                  f"load {run['load_ms']:.3f} ms", flush=True)
    for variant in ["control", "candidate"]:
        workspace = output / variant
        assert source_hashes(workspace / "lmd") == provenance["packages"][variant]
        assert all(digest((workspace / p).read_bytes()) == provenance["inputs"][str(p)] for p in copies)
    assert digest(exe.read_bytes()) == provenance["executable_sha256"]
    control_runs = [r for r in report["runs"] if r["variant"] == "control"]
    candidate_runs = [r for r in report["runs"] if r["variant"] == "candidate"]
    assert all((r["words"], r["unknown"], r["checksum"]) ==
               (control_runs[0]["words"], control_runs[0]["unknown"], control_runs[0]["checksum"])
               for r in report["runs"])
    before = statistics.median(r["median_ms"] for r in control_runs)
    after = statistics.median(r["median_ms"] for r in candidate_runs)
    phases = {variant: {phase: statistics.median(r[field] for r in runs)
                       for phase, field in [("read", "read_ms"), ("from_bytes", "index_ms"),
                                            ("total_load", "load_ms")]}
              for variant, runs in [("control", control_runs), ("candidate", candidate_runs)]}
    report["summary"] = {
        "pairs": args.pairs, "control_median_ms": before, "candidate_median_ms": after,
        "speedup": before / after, "reduction_percent": (1 - after / before) * 100,
        "paired_speedups": [a["median_ms"] / b["median_ms"]
                            for a, b in zip(control_runs, candidate_runs)],
        "control_sample_range_ms": [min(x for r in control_runs for x in r["samples_ms"]),
                                    max(x for r in control_runs for x in r["samples_ms"])],
        "candidate_sample_range_ms": [min(x for r in candidate_runs for x in r["samples_ms"]),
                                      max(x for r in candidate_runs for x in r["samples_ms"])],
        "source_and_input_hashes_verified": True,
        "phase_medians_ms": phases,
        "load_speedup": phases["control"]["total_load"] / phases["candidate"]["total_load"],
    }
    result_path.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report["summary"], indent=2), flush=True)


if __name__ == "__main__":
    main()
