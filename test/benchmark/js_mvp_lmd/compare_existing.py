#!/usr/bin/env python3
"""Compare runtime-reported execution times; never substitute process wall time."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_benchmarks import parse_timing
from run_paired_benchmarks import normalized_stdout

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--runs', type=int, default=5)
parser.add_argument('--output', default='temp/mvp_lmd_self_timing_mir/comparison.json')
parser.add_argument('--mvp-control', type=Path, help='Archived release executable for paired MVP comparison')
parser.add_argument('--control-record', type=Path, help='Prior benchmark record proving the control binary hash')
parser.add_argument('--integer-items', action='store_true', help='Also measure dense numeric Items and indirect calls')
args = parser.parse_args()
assert args.runs > 0
binary = Path('lambda.exe')
assert Path('.lambda_release_build').exists(), 'build release before timing'
assert Path('.lambda_release_build').stat().st_mtime >= binary.stat().st_mtime, \
    'release stamp must postdate the executable'
output = Path(args.output)
output.parent.mkdir(parents=True, exist_ok=True)
assert output.resolve().is_relative_to(Path('temp').resolve()), 'artifacts belong under ./temp/'
engines = ['mvp_lmd', 'lambda_js', 'lambda_untyped', 'node']
environment = os.environ.copy()
for key in ('LAMBDA_GC_FORCE_EVERY', 'LAMBDA_GC_POISON_FREED',
            'JS_OPT_TRACE', 'JS_MIR_DUMP', 'JS_TRANSPILE_TIMING', 'LAMBDA_COMPILER_TIMING'):
    environment.pop(key, None)
environment.pop('JS_EXECUTION_BACKEND', None)
backend_flags = {'JS_EXEC_BACKEND': 'mir', 'LAMBDA_EXEC_BACKEND': 'jit',
                 'JS_MIR_INTERP': '0', 'LAMBDA_JS_LARGE_INTERP': '0'}
environment.update(backend_flags)

def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

control = None
if args.mvp_control:
    assert args.control_record, '--mvp-control requires its release benchmark --control-record'
    previous = json.loads(args.control_record.read_text())
    assert sha256(args.mvp_control) == previous['metadata']['binary_sha256'], 'control binary differs from its record'
    control = {'binary': str(args.mvp_control.resolve()), 'sha256': sha256(args.mvp_control),
               'record': str(args.control_record), 'record_sha256': sha256(args.control_record)}
    engines.insert(1, 'mvp_lmd_before')

artifact = {'metadata': {
    'started_at': datetime.datetime.now().astimezone().isoformat(),
    'platform': platform.platform(), 'runs': args.runs, 'warmups': 1,
    'binary': str(binary.resolve()), 'binary_sha256': sha256(binary),
    'runner_sha256': sha256(__file__),
    'mvp_control': control,
    'node_version': subprocess.check_output(['node', '--version'], text=True).strip(),
    'backend_flags': backend_flags,
    'backend_validation': 'Each Lambda lane must emit a fresh finalized MIR artifact '
        'during its untimed warmup. AST/AUTO and MIR interpreter selection are disabled.',
    'timing': 'self_reported_execution_ms_from___TIMING___only',
    'boundaries': 'MVP measures its generated program entry; other engines use '
        'the original benchmark timers. Startup, parsing, compilation, printing '
        'and teardown are excluded. Node tiering during execution is included.',
    'order': 'one warmup per engine, then rotating engine order each round',
    'lambda_scope': 'Unannotated canonical ports; normal Lambda numeric semantics. '
        'Collatz uses shr where JS divides by two. These are port comparisons.',
}, 'rows': []}
workloads = [('r7rs/fib2', 'r7rs/fib'), ('r7rs/fibfp2', 'r7rs/fibfp'),
             ('r7rs/sum2', 'r7rs/sum'), ('r7rs/tak2', 'r7rs/tak'),
             ('larceny/diviter', 'larceny/diviter'),
             ('larceny/divrec', 'larceny/divrec'), ('kostya/collatz', 'kostya/collatz')]
if args.integer_items:
    workloads.extend((name, name) for name in
        ('js_mvp_lmd/integer_dense', 'js_mvp_lmd/integer_indirect'))
for js_name, ls_name in workloads:
    js_path = Path('test/benchmark') / (js_name + '.js')
    ls_path = Path('test/benchmark') / (ls_name + '.ls')
    source = js_path.read_text()
    before, main = source.split('\nfunction main()', 1)
    workload = main.split('const __t0 = performance.now();', 1)[1].split(
        'const __t1 = performance.now();', 1)[0]
    variable, expected = re.search(r'if \((\w+) === ([0-9.eE+-]+)\)', main).groups()
    # Preserve the JS kernel and main workload; replace only host timing/output.
    js = before + '\nfunction main() {' + workload + '\nreturn ' + variable + ';}\n'
    stem = output.parent / js_name.replace('/', '_')
    mvp_source = Path(str(stem) + '.js')
    mvp_source.write_text(js + 'main()\n')
    commands = {
        'mvp_lmd': ['./lambda.exe', 'js', '--no-log', '--runtime=mvp-lmd', '--timing', '-p', mvp_source.read_text()],
        'lambda_js': ['./lambda.exe', 'js', '--no-log', str(js_path)],
        'lambda_untyped': ['./lambda.exe', 'run', str(ls_path), '--no-log'],
        'node': ['node', str(js_path)],
    }
    if control:
        commands['mvp_lmd_before'] = [str(args.mvp_control.resolve()), *commands['mvp_lmd'][1:]]
    row = {'id': js_name, 'expected': expected, 'sources': [
        {'path': str(path), 'sha256': sha256(path)}
        for path in (js_path, ls_path, mvp_source)], 'samples': [], 'backend_validation': {}}
    artifact['rows'].append(row)
    for round_index in range(args.runs + 1):
        order = engines[round_index % len(engines):] + engines[:round_index % len(engines)]
        for engine in order:
            command = commands[engine]
            run_environment = environment
            mir_path = None
            if not round_index and engine != 'node':
                # Validate lowering outside timed samples; --no-log suppresses MIR artifacts.
                command = [argument for argument in command if argument != '--no-log']
                mir_path = Path(str(stem) + '_' + engine + '.mir')
                mir_path.unlink(missing_ok=True)
                run_environment = dict(environment, LAMBDA_MIR_DUMP_PATH=str(mir_path),
                                       LAMBDA_MIR_LOG_FRAME_SLOTS='1')
            started = time.perf_counter()
            result = subprocess.run(command, capture_output=True, text=True,
                                    timeout=120, env=run_environment)
            wall_ms = (time.perf_counter() - started) * 1000
            execution_ms = parse_timing(result.stdout)
            observable = normalized_stdout(result.stdout)
            oracle_equal = float(observable) == float(expected) if engine.startswith('mvp_lmd') \
                else ': PASS' in observable and 'FAIL' not in observable
            assert result.returncode == 0 and oracle_equal and execution_ms is not None \
                and execution_ms > 0, \
                f'{js_name}/{engine}: {result.returncode}: {result.stdout} {result.stderr}'
            if mir_path:
                assert mir_path.exists() and mir_path.stat().st_size > 0, \
                    f'{js_name}/{engine}: no finalized MIR artifact; backend validation failed'
                row['backend_validation'][engine] = {'path': str(mir_path),
                    'sha256': sha256(mir_path), 'bytes': mir_path.stat().st_size}
                frames = [line for line in Path('log.txt').read_text(errors='replace').splitlines()
                          if 'mir-function: function=' in line]
                frame_path = Path(str(stem) + '_' + engine + '.frames.log')
                frame_path.write_text('\n'.join(frames) + '\n')
                row['backend_validation'][engine]['frames'] = {
                    'path': str(frame_path), 'sha256': sha256(frame_path)}
            if round_index:
                row['samples'].append({'engine': engine, 'round': round_index,
                    'exec_ms': execution_ms, 'wall_ms': wall_ms,
                    'stdout': result.stdout, 'oracle_equal': True})
            print(js_name, 'warmup' if not round_index else round_index,
                  engine, f'{execution_ms:.6f} ms execution', flush=True)
        output.write_text(json.dumps(artifact, indent=2) + '\n')
    row['medians_ms'] = {engine: statistics.median(
        sample['exec_ms'] for sample in row['samples'] if sample['engine'] == engine)
        for engine in engines}
    output.write_text(json.dumps(artifact, indent=2) + '\n')

artifact['metadata']['finished_at'] = datetime.datetime.now().astimezone().isoformat()
artifact['metadata']['binary_unchanged'] = sha256(binary) == artifact['metadata']['binary_sha256']
if control:
    artifact['metadata']['control_unchanged'] = sha256(args.mvp_control) == control['sha256']
artifact['metadata']['sources_unchanged'] = all(sha256(source['path']) == source['sha256']
    for row in artifact['rows'] for source in row['sources'])
output.write_text(json.dumps(artifact, indent=2) + '\n')
