"""Coverage, invocation, failure and mutable-warmup checks for Julia ports."""

import os
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

import run_benchmarks as registry
import run_julia_benchmarks as julia


class JuliaRunnerTests(unittest.TestCase):
    def test_registered_port_coverage(self):
        entries = julia.benchmark_entries(True)
        self.assertEqual(77, len(entries))
        self.assertEqual(71, len(julia.benchmark_entries()))
        self.assertFalse([f"{b['suite']}/{b['name']}" for b in entries
                          if julia.port_source(b['suite'], b['name']) is None])

    def test_counts_come_from_node_sources(self):
        with patch.object(julia, "julia_executable", return_value="julia"):
            for name, *_ in registry.AWFY:
                outer, inner = registry.awfy_node_iterations(name)
                command = julia.build_command(julia.port_source("awfy", name))
                self.assertEqual([str(inner), str(outer)], command[-2:])
            for name, *_ in registry.JETSTREAM_LS:
                _, count = registry._detect_jetstream_run_function(registry.JETSTREAM_NODE[name])
                command = julia.build_command(julia.port_source("jetstream", name))
                self.assertEqual(str(count), command[-1])

    @unittest.skipUnless(julia.julia_executable(), "Julia is not installed")
    def test_jq_vm_semantics(self):
        env = os.environ.copy()
        env.update(julia.julia_environment())
        source = julia.JULIA_ROOT / "test_jq_vm.jl"
        proc = subprocess.run(julia.build_command(source), cwd=julia.PROJECT_ROOT,
                              env=env, capture_output=True, text=True, timeout=180)
        self.assertEqual(0, proc.returncode, proc.stdout + proc.stderr)

    def test_failures_are_not_timings(self):
        for returncode, output, status in [
            (1, "__TIMING__:1\n", "exit_1"),
            (0, "result: FAIL\n__TIMING__:1\n", "wrong_output"),
            (0, "result: PASS\n", "invalid_timing"),
            (0, "__TIMING__:1\n__TIMING__:2\n", "invalid_timing"),
            (0, "__TIMING__:1e999\n", "invalid_timing"),
        ]:
            with self.subTest(output=output):
                proc = subprocess.CompletedProcess([], returncode, output, "")
                self.assertEqual(status, julia.output_status(proc))

    def test_missing_toolchain_is_explicit(self):
        with patch.object(julia, "julia_executable", return_value=None):
            self.assertEqual((None, "toolchain_missing"), julia.shell_command("r7rs", "fib"))

    def test_jq_filters_and_inputs_are_hashed(self):
        with patch.object(julia, "julia_executable", return_value=None):
            metadata = julia.runtime_metadata()
        for name in ("mix.jq", "records.jq", "bf.jq", "tree.jq", "orders.json", "fib.bf"):
            path = Path("test/benchmark/text/jq") / name
            self.assertEqual(julia.sha256(julia.PROJECT_ROOT / path),
                             metadata["fixtures_sha256"][str(path)])

    def test_unified_runner_rejects_missing_execution_timer(self):
        results, row = {"r7rs": {"fib": {}}}, {}
        with patch.object(registry, "reference_port_command", return_value=("julia", "ok")), \
                patch.object(registry, "time_run_benchmark", return_value=(10, None, True, "wall_fallback", {})):
            registry.run_native_engine("julia", "r7rs", "fib", 1, 60, results, row)
        result = results["r7rs"]["fib"]
        self.assertIsNone(result["julia"])
        self.assertIsNone(result["julia_e2e"])
        self.assertEqual("invalid_timing", result["_status"]["julia"])

    @unittest.skipUnless(julia.julia_executable(), "Julia is not installed")
    def test_warmup_uses_fresh_mutable_inputs(self):
        directory = julia.PROJECT_ROOT / "temp" / "julia-runner-tests"
        directory.mkdir(parents=True, exist_ok=True)
        source = directory / "fresh_state.jl"
        common = str(julia.JULIA_ROOT / "common.jl").replace("\\", "\\\\").replace('"', '\\"')
        source.write_text(f'''include("{common}")
prepared = Ref(0)
worked = Ref(0)
prepare() = (prepared[] += 1; Ref(7))
function workload(io, state)
    @assert state[] == 7
    state[] += 1; worked[] += 1
    return state[]
end
run_benchmark(prepare, workload, value->value == 8, value->nothing)
println("PREPARED:", prepared[], " WORKED:", worked[])
''')
        for warmup in (0, 1):
            env = os.environ.copy()
            env.update(julia.julia_environment())
            env["JULIA_BENCH_WARMUP"] = str(warmup)
            proc = subprocess.run(julia.build_command(source), env=env,
                                  capture_output=True, text=True, timeout=60)
            self.assertEqual("ok", julia.output_status(proc), proc.stderr)
            self.assertIn(f"PREPARED:{warmup + 1} WORKED:{warmup + 1}", proc.stdout)


if __name__ == "__main__":
    unittest.main()
