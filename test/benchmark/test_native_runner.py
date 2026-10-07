"""Coverage, invocation, timing and runtime checks for Java/Erlang ports."""
import json
import os
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

import native_benchmark_runner as native
import run_benchmarks as registry
import verify_julia_suite as micro_verifier

class NativeRunnerTests(unittest.TestCase):
    def test_registered_coverage(self):
        entries = native.contract.benchmark_entries(True)
        self.assertEqual(77, len(entries))
        self.assertEqual(71, len(native.contract.benchmark_entries()))
        manifest = json.loads((native.BASE / 'native_ports/manifest.json').read_text())
        self.assertEqual({f"{e['suite']}/{e['name']}" for e in entries}, set(manifest['entries']))
        for language in ('java', 'erlang'):
            self.assertFalse([e for e in entries if native.port_source(language, e['suite'], e['name']) is None])

    def test_node_workload_counts(self):
        manifest = json.loads((native.BASE / 'native_ports/manifest.json').read_text())['entries']
        for language in ('java', 'erlang'):
            with patch.object(native, 'build', return_value=Path('temp/native-test-cache')), \
                    patch.object(native, 'executable', return_value=language):
                for name, *_ in registry.AWFY:
                    outer, inner = registry.awfy_node_iterations(name)
                    self.assertEqual((inner, outer), (manifest['awfy/' + name]['inner_iterations'], manifest['awfy/' + name]['outer_iterations']))
                    self.assertEqual([str(inner), str(outer)], native.build_command(language, 'awfy', name)[-2:])
                for name, *_ in registry.JETSTREAM_LS:
                    _, count = registry._detect_jetstream_run_function(registry.JETSTREAM_NODE[name])
                    self.assertEqual(count, manifest['jetstream/' + name]['repeats'])
                    self.assertEqual(str(count), native.build_command(language, 'jetstream', name)[-1])

    def test_independent_micro_verifier_uses_native_ports(self):
        for language in ('java', 'erlang'):
            with patch.object(native, 'build_command', return_value=['native-micro']) as command:
                invocation, env = micro_verifier.port_command(language, 'parse_integers')
            command.assert_called_once_with(language, 'julia', 'parse_integers')
            self.assertEqual(['native-micro'], invocation)
            self.assertEqual(str(native.ROOT / 'temp'), env['TMPDIR'])

    def test_missing_port_and_toolchain(self):
        for language in ('java', 'erlang'):
            self.assertEqual((None, 'missing_port'), native.shell_command(language, 'unknown', 'unknown'))
            with patch.object(native, 'executable', return_value=None):
                self.assertEqual((None, 'toolchain_missing'), native.shell_command(language, 'r7rs', 'fib'))

    def test_compilation_failure_is_explicit(self):
        with patch.object(native, 'build', side_effect=RuntimeError('compile failure')):
            for language in ('java', 'erlang'):
                self.assertEqual((None, 'build_failed'), native.shell_command(language, 'r7rs', 'fib'))

    def test_unified_runner_requires_execution_timer(self):
        for language in ('java', 'erlang'):
            results = {'r7rs': {'fib': {}}}
            with patch.object(registry, 'reference_port_command', return_value=(language, 'ok')), \
                    patch.object(registry, 'time_run_benchmark', return_value=(10, None, True, 'wall_fallback', {})):
                registry.run_native_engine(language, 'r7rs', 'fib', 1, 60, results, {})
            row = results['r7rs']['fib']
            self.assertIsNone(row[language])
            self.assertIsNone(row[language + '_e2e'])
            self.assertEqual('invalid_timing', row['_status'][language])

    def test_failure_output_is_not_timing(self):
        for code, stdout, expected in ((1, '__TIMING__:1\n', 'exit_1'),
                (0, 'result: FAIL\n__TIMING__:1\n', 'wrong_output'),
                (0, 'result: PASS\n', 'invalid_timing'),
                (0, '__TIMING__:1\n__TIMING__:2\n', 'invalid_timing'),
                (0, '__TIMING__:1e999\n', 'invalid_timing')):
            self.assertEqual(expected, native.contract.output_status(subprocess.CompletedProcess([], code, stdout, '')))

    def test_metadata_hashes_sources_and_inputs(self):
        for language in ('java', 'erlang'):
            with patch.object(native, 'executable', return_value=None):
                metadata = native.runtime_metadata(language)
            for name in ('mix.jq', 'records.jq', 'bf.jq', 'tree.jq', 'orders.json', 'fib.bf'):
                path = Path('test/benchmark/text/jq') / name
                self.assertEqual(native.contract.sha256(native.ROOT / path), metadata['fixtures_sha256'][str(path)])
            for path in ('native_benchmark_runner.py', 'native_ports/manifest.json', f'run_{language}_benchmarks.py'):
                self.assertIn('test/benchmark/' + path, metadata['sources_sha256'])

    def test_environment_validation(self):
        with patch.dict(os.environ, {'NATIVE_BENCH_WARMUP': '2'}):
            with self.assertRaises(ValueError):
                native.environment()

    @unittest.skipUnless(native.executable('java') and native.executable('java', True), 'Java is not installed')
    def test_java_runtime(self):
        directory = native.ROOT / 'temp/native-runner-tests'
        directory.mkdir(parents=True, exist_ok=True)
        source = directory / 'RuntimeChecks.java'
        source.write_text('''class RuntimeChecks {
static void jq(String filter, String expected) {
    var values=JqVM.run(JqCompiler.program(filter),null);
    NativeBench.check(values.size()==1 && JqValues.json(values.get(0)).equals(expected));
}
public static void main(String[] args) {
    jq("def outer($x): def inner(f): f + $x; inner(. * 2); 3 | outer(7)","13");
    jq("[range(10)] | add","45");
    jq("[1,[2,3]] as $old | ($old | (.. | scalars) |= . + 1) as $new | [$old,$new]","[[1,[2,3]],[2,[3,4]]]");
    jq("[try error(7) catch . + 1, (null // false // 9)]","[8,9]");
    int[] prepared={0},worked={0};
    NativeBench.prepared(()->{prepared[0]++;return new int[]{7};},
        state->{NativeBench.check(state[0]==7);state[0]=8;worked[0]++;return 8;},
        value->NativeBench.check(value==8),value->{});
    int expected=Integer.parseInt(System.getenv("NATIVE_BENCH_WARMUP"))+1;
    NativeBench.check(prepared[0]==expected && worked[0]==expected);
    NativeBench.check(TextSearch.naive(new int[]{1,2,1,2,3},new int[]{1,2,3},0)==2);
    NativeBench.check(TextSearch.kmp(new int[]{1,2,1,2,3},new int[]{1,2,3},0)==2);
    NativeBench.check(TextSearch.boyerMoore(new int[]{1,2,1,2,3},new int[]{1,2,3},0)==2);
    System.out.println("runtime: PASS");
}}
''')
        build = native.build('java')
        subprocess.run([native.executable('java', True), '-cp', str(build), '-d', str(directory), str(source)],
                       cwd=native.ROOT, check=True, capture_output=True, text=True)
        for warmup in (0, 1):
            env = os.environ.copy(); env.update(native.environment()); env['NATIVE_BENCH_WARMUP'] = str(warmup)
            proc = subprocess.run([native.executable('java'), '-Xss16m', '-cp', str(build) + os.pathsep + str(directory), 'RuntimeChecks'],
                                  cwd=native.ROOT, env=env, capture_output=True, text=True, timeout=60)
            self.assertEqual('ok', native.contract.output_status(proc), proc.stdout + proc.stderr)

    @unittest.skipUnless(native.executable('erlang') and native.executable('erlang', True), 'Erlang is not installed')
    def test_erlang_runtime(self):
        directory = native.ROOT / 'temp/native-runner-tests'
        directory.mkdir(parents=True, exist_ok=True)
        source = directory / 'runtime_checks.erl'
        source.write_text('''-module(runtime_checks).
-export([main/0]).
jq(Filter,Expected)->[V]=jq_vm:run(jq_compile:program(Filter),null),native_bench:check(jq_values:json(V)=:=Expected).
main()->
    jq(<<"def outer($x): def inner(f): f + $x; inner(. * 2); 3 | outer(7)">>,<<"13">>),
    jq(<<"[range(10)] | add">>,<<"45">>),
    jq(<<"[1,[2,3]] as $old | ($old | (.. | scalars) |= . + 1) as $new | [$old,$new]">>,<<"[[1,[2,3]],[2,[3,4]]]">>),
    jq(<<"[try error(7) catch . + 1, (null // false // 9)]">>,<<"[8,9]">>),
    put(prepared,0),put(worked,0),native_bench:prepared(
        fun()->put(prepared,get(prepared)+1),array:from_list([7])end,
        fun(State)->native_bench:check(array:get(0,State)=:=7),put(worked,get(worked)+1),8 end,
        fun(V)->native_bench:check(V=:=8)end,fun(_)->ok end,fun(_)->ok end),
    Expected=list_to_integer(os:getenv("NATIVE_BENCH_WARMUP"))+1,native_bench:check(get(prepared)=:=Expected andalso get(worked)=:=Expected),
    native_bench:check(text_search:naive(<<1,2,1,2,3>>,<<1,2,3>>,0)=:=2),
    native_bench:check(text_search:kmp(<<1,2,1,2,3>>,<<1,2,3>>,0)=:=2),
    native_bench:check(text_search:boyer_moore(<<1,2,1,2,3>>,<<1,2,3>>,0)=:=2),
    io:format("runtime: PASS~n"),halt(0).
''')
        build = native.build('erlang')
        subprocess.run([native.executable('erlang', True), '-o', str(directory), str(source)],
                       cwd=native.ROOT, check=True, capture_output=True, text=True)
        for warmup in (0, 1):
            env = os.environ.copy(); env.update(native.environment()); env['NATIVE_BENCH_WARMUP'] = str(warmup)
            proc = subprocess.run([native.executable('erlang'), '+S', '1:1', '+A', '1', '-noshell', '-pa', str(build), str(directory), '-s', 'runtime_checks', 'main'],
                                  cwd=native.ROOT, env=env, capture_output=True, text=True, timeout=60)
            self.assertEqual('ok', native.contract.output_status(proc), proc.stdout + proc.stderr)


if __name__ == '__main__':
    unittest.main()
