"""Coverage, invocation, timing and runtime checks for Java/Erlang ports."""
import json
import importlib.util
import os
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

import native_benchmark_runner as native
import run_benchmarks as registry
import verify_julia_suite as micro_verifier

spec = importlib.util.spec_from_file_location('native_codegen_checks', native.BASE / 'native_ports/generate.py')
codegen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(codegen)

def erlang_backend(readonly=()):
    spec=importlib.util.spec_from_file_location('erlang_codegen_checks',native.BASE/'native_ports/erlang_codegen.py')
    module=importlib.util.module_from_spec(spec)
    with patch.dict('sys.modules',{'generate':codegen}):spec.loader.exec_module(module)
    return module.Erlang(readonly)


class NativeRunnerTests(unittest.TestCase):
    def test_scalar_constants_require_no_competing_bindings(self):
        s=lambda name:{'s':name}
        constant=lambda name,value:codegen.node('const',codegen.node('=',s(name),value))
        body=codegen.node('block',constant('stable',7),constant('mutable',8),
            codegen.node('=',s('mutable'),9),constant('parameter',10),
            codegen.node('function',codegen.node('call',s('f'),s('parameter')),s('parameter')),
            constant('container',codegen.node('vect',1,2)))
        self.assertEqual({'stable'},codegen.readonly_scalar_names([body]))

    def test_scalar_constants_are_read_once_and_respect_locals(self):
        s=lambda name:{'s':name}
        body=codegen.node('block',codegen.node('while',True,
            codegen.node('call',s('+'),s('stable'),s('stable'))),s('stable'))
        for backend,scope,lookup in ((codegen.Java({'stable'}),'e','get(e,"stable")'),
                                     (erlang_backend({'stable'}),'E','pr:getv(E,<<"stable"/utf8>>)')):
            backend.makefn('f',[],body,scope)
            self.assertEqual(1,backend.functions[-1].count(lookup))
            backend.makefn('f',[s('stable')],body,scope)
            self.assertNotIn(lookup,backend.functions[-1])
            writes=codegen.node('block',codegen.node('=',s('stable'),8),s('stable'))
            self.assertEqual([],backend.constant_reads(writes,codegen.assigned_names(writes)))

    def test_union_type_members_survive_generation(self):
        symbol=lambda name:{'s':name}
        union=codegen.node('curly',symbol('Union'),symbol('Int'),
            codegen.node('curly',symbol('Union'),symbol('String'),symbol('Bool')))
        self.assertEqual('Int|String|Bool',codegen.type_name(union))
        expression=codegen.node('call',symbol('isa'),symbol('value'),union)
        for backend in (codegen.Java(),erlang_backend()):
            self.assertIn('Int|String|Bool',backend.expr(expression))
            self.assertIn('Int|String|Bool',backend.expr(union))

    def test_native_aliases_preserve_mutable_bindings(self):
        symbol = lambda name: {'s': name}
        body = codegen.node('block', codegen.node('=', symbol('stable'), 7),
            codegen.node('=', symbol('counter'), 0),
            codegen.node('while', True, codegen.node('+=', symbol('counter'), 1)),
            codegen.node('return', symbol('counter')))
        writes = codegen.assigned_names(body)
        self.assertEqual(1, writes['stable'])
        self.assertEqual(2, writes['counter'])
        prefix, _ = codegen.immutable_prefix(body)
        self.assertEqual([('stable', 7)], prefix)

    def test_native_aliases_retain_final_expression(self):
        prefix, remaining = codegen.immutable_prefix(codegen.node('block', codegen.node('=', {'s': 'answer'}, 7)))
        self.assertEqual([], prefix)
        self.assertEqual('=', remaining['a'][0]['h'])

    def test_mutating_a_field_does_not_rebind_its_owner(self):
        body = codegen.node('=', codegen.node('.', {'s': 'object'}, {'q': {'s': 'value'}}), 8)
        self.assertEqual(0, codegen.assigned_names(body)['object'])

    def test_registered_coverage(self):
        entries = native.contract.benchmark_entries(True)
        self.assertEqual(77, len(entries))
        self.assertEqual(71, len(native.contract.benchmark_entries()))
        manifest = json.loads((native.BASE / 'native_ports/manifest.json').read_text())
        self.assertEqual({f"{e['suite']}/{e['name']}" for e in entries}, set(manifest['entries']))
        for language in ('java', 'erlang'):
            self.assertFalse([e for e in entries if native.port_source(language, e['suite'], e['name']) is None])

    def test_node_workload_counts(self):
        for language in ('java', 'erlang'):
            with patch.object(native, 'build', return_value=Path('temp/native-test-cache')), \
                    patch.object(native, 'executable', return_value=language):
                for name, *_ in registry.AWFY:
                    outer, inner = registry.awfy_node_iterations(name)
                    self.assertEqual([str(inner), str(outer)], native.build_command(language, 'awfy', name)[-2:])
                for name, *_ in registry.JETSTREAM_LS:
                    _, count = registry._detect_jetstream_run_function(registry.JETSTREAM_NODE[name])
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
            for path in ('native_benchmark_runner.py', 'native_ports/parse_julia.jl', f'run_{language}_benchmarks.py'):
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
        source.write_text('''class RuntimeChecks extends PortRuntime {
public static void main(String[] args) {
    check(isWindows("Windows 11") && !isWindows("Darwin") && !isWindows("Linux"));
    Env root = new Env(null); bind(root, "ARGS", arr()); load(root, "text/jq_vm.jl");
    check(isa(root,7L,"Int|String") && isa(root,"seven","Int|String") && !isa(root,arr(),"Int|String"));
    Env jq = (Env)get(root, "JqVM");
    Object parsed=call(root,field(jq,"jparse"),new Object[]{"{\\"n\\":7}",new Atom("json")});
    check(equal(parsed,java.util.Map.of("n",7L)));
    Object program = call(jq, get(jq,"compile_program"), new Object[]{"def outer($x): def inner(f): f + $x; inner(. * 2); 3 | outer(7)"});
    check(!truth(field(program,"failed")));
    Object vm = call(jq,get(jq,"vm_run"),new Object[]{program,null});
    check(equal(field(vm,"outputs"),arr(13L)));
    int[] prepared={0}, worked={0};
    runBenchmark(root,new Object[]{
        new Fn(root,(e,a)->{prepared[0]++;return arr(7L);},new String[]{},0,0),
        new Fn(root,(e,a)->{check(equal(index(a[1],new Object[]{1L}),7L));putIndex(a[1],new Object[]{1L},8L);worked[0]++;return 8L;},new String[]{"Any","Any"},2,2),
        new Fn(root,(e,a)->equal(a[0],8L),new String[]{"Any"},1,1),
        new Fn(root,(e,a)->null,new String[]{"Any"},1,1)});
    int expected=Integer.parseInt(System.getenv("NATIVE_BENCH_WARMUP"))+1;
    check(prepared[0]==expected && worked[0]==expected);
    check(TextSearch.naive(new int[]{1,2,1,2,3},new int[]{1,2,3},0)==2);
    check(TextSearch.kmp(new int[]{1,2,1,2,3},new int[]{1,2,3},0)==2);
    check(TextSearch.boyerMoore(new int[]{1,2,1,2,3},new int[]{1,2,3},0)==2);
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
main()->pr:check(pr:is_windows({win32,nt}) andalso not pr:is_windows({unix,darwin}) andalso not pr:is_windows({unix,linux})),
    Root=pr:init(),Before=get(next_id),Roots=get(roots),Temps=get(temps),
    pr:check(pr:eval_same(Root,fun(Scope)->pr:setv(Scope,<<"local">>,7)end)=:=7),
    pr:check(get(next_id)=:=Before andalso pr:getv(Root,<<"local">>)=:=7),
    Returned=pr:eval_same(Root,fun(_)->pr:arr([13])end),pr:collect(),pr:check(pr:elements(Returned)=:=[13]),
    try pr:eval_same(Root,fun(_)->throw(audit_exception)end)catch throw:audit_exception->ok end,
    pr:check(get(roots)=:=Roots andalso length(get(temps))=:=length(Temps)),
    Kw={kw,#{<<"flag">>=>true}},Spread={spread,pr:arr([2,3])},
    pr:check(pr:normalize([Kw,1,Spread])=:=[1,2,3,Kw]),pr:check(pr:normalize([1,2])=:=[1,2]),
    pr:load(Root,<<"text/jq_vm.jl">>),Jq=pr:getv(Root,<<"JqVM">>),
    pr:check(pr:isa(Root,7,<<"Int|String">>) andalso pr:isa(Root,<<"seven">>,<<"Int|String">>) andalso not pr:isa(Root,pr:arr([]),<<"Int|String">>)),
    Parsed=pr:call(Root,pr:field(Jq,<<"jparse">>),[<<"{\\"n\\":7}">>,{atom,<<"json">>}]),pr:check(pr:dict_get(Parsed,<<"n">>,nil)=:=7),
    Program=pr:call(Jq,pr:getv(Jq,<<"compile_program">>),[<<"def outer($x): def inner(f): f + $x; inner(. * 2); 3 | outer(7)">>]),
    pr:check(not pr:truth(pr:field(Program,<<"failed">>))),
    Vm=pr:call(Jq,pr:getv(Jq,<<"vm_run">>),[Program,nil]),pr:check(pr:equal(pr:field(Vm,<<"outputs">>),pr:arr([13]))),
    put(prepared,0),put(worked,0),pr:run_benchmark(Root,[
        {fn,Root,fun(_,[])->put(prepared,get(prepared)+1),pr:arr([7])end,[],0,0},
        {fn,Root,fun(_,[_,State])->pr:check(pr:index(State,[1])=:=7),pr:putindex(State,[1],8),put(worked,get(worked)+1),8 end,[<<"Any">>,<<"Any">>],2,2},
        {fn,Root,fun(_,[V])->V=:=8 end,[<<"Any">>],1,1},
        {fn,Root,fun(_,_)->nil end,[<<"Any">>],1,1}]),
    Expected=list_to_integer(os:getenv("NATIVE_BENCH_WARMUP"))+1,pr:check(get(prepared)=:=Expected andalso get(worked)=:=Expected),
    pr:check(text_search:naive(<<1,2,1,2,3>>,<<1,2,3>>,0)=:=2),
    pr:check(text_search:kmp(<<1,2,1,2,3>>,<<1,2,3>>,0)=:=2),
    pr:check(text_search:boyer_moore(<<1,2,1,2,3>>,<<1,2,3>>,0)=:=2),
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
