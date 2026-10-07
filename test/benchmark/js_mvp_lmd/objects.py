#!/usr/bin/env python3
"""Release screen for MVP objects and Map; identical sources, checked outputs."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import statistics
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_benchmarks import parse_timing
from run_paired_benchmarks import normalized_stdout

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--runs', type=int, default=5)
parser.add_argument('--output', default='temp/mvp_lmd_objects/comparison.json')
args = parser.parse_args()
assert args.runs > 0
binary = Path('lambda.exe')
assert Path('.lambda_release_build').stat().st_mtime >= binary.stat().st_mtime, 'build release before timing'
output = Path(args.output)
assert output.resolve().is_relative_to(Path('temp').resolve()), 'artifacts belong under ./temp/'
output.parent.mkdir(parents=True, exist_ok=True)
sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
archive = output.parent / 'lambda-map-phase.exe'
shutil.copy2(binary, archive)
production = list(Path('lambda/js/mvp-lmd').glob('*.[ch]*')) + [Path(path) for path in (
    'lambda/core/name_pool.cpp', 'lambda/input/input.cpp', 'lambda/js/js_runtime_value.cpp', 'lambda/js/js_runtime.cpp',
    'lambda/lambda.h', 'lambda/lambda.hpp',
    'lambda/runtime/gc/gc_heap.c', 'lambda/runtime/lambda-data-runtime.cpp',
    'lambda/runtime/lambda-eval.cpp', 'lambda/runtime/lambda-mem.cpp', 'lib/utf.c', 'lib/utf.h')]
source_records = {str(path): {'sha256': sha(path), 'lines': len(path.read_text().splitlines())}
    for path in sorted(production)}
env = os.environ.copy()
for key in ('LAMBDA_GC_FORCE_EVERY', 'LAMBDA_GC_FORCE_ONE_IN', 'LAMBDA_GC_FORCE_SEED',
            'LAMBDA_GC_POISON_FREED', 'JS_EXECUTION_BACKEND', 'LAMBDA_MIR_DUMP_PATH',
            'LAMBDA_MIR_LOG_FRAME_SLOTS', 'JS_MIR_DUMP', 'JS_TRANSPILE_TIMING', 'LAMBDA_COMPILER_TIMING'):
    env.pop(key, None)
flags = {'JS_EXEC_BACKEND': 'mir', 'JS_MIR_INTERP': '0', 'LAMBDA_JS_LARGE_INTERP': '0'}
env.update(flags)
record = {'metadata': {'started_at': datetime.datetime.now().astimezone().isoformat(),
    'platform': platform.platform(), 'binary_sha256': sha(binary), 'runner_sha256': sha(__file__),
    'binary_archive': str(archive), 'production_sources': source_records,
    'node_version': subprocess.check_output(['node', '--version'], text=True).strip(),
    'runs': args.runs, 'warmups': 1, 'backend_flags': flags,
    'timing': 'self_reported_execution_ms; startup, compilation, output and teardown excluded',
    'boundary': 'MVP times its program entry; full JS and Node time one work() call. Node tiering is included.',
    'order': 'one untimed process per engine, then rotating engine order; every sample is a fresh process',
    'mir_definition': 'finalized pre-JIT instruction counts include cold/error paths; frame slots are emitter plans, not executed operation counts',
    'claim': 'cross-engine screen, not a causal before/after optimization result'}, 'rows': []}
oracles = {'object_fields': 125000750000, 'object_growth': 1990000,
    'object_retype': 4999950000, 'object_delete': 200030000,
    'map_lookup': 120000000, 'map_iteration': 200000000}
for name, expected in oracles.items():
    path = Path(__file__).parent / (name + '.js')
    source = path.read_text()
    kernel = source.rstrip().removesuffix('work()')
    timed = kernel + '\nlet start=performance.now();let result=work();let elapsed=performance.now()-start;console.log(result);console.log("__TIMING__:"+elapsed);\n'
    commands = {'mvp_lmd': ['./lambda.exe', 'js', '--no-log', '--runtime=mvp-lmd', '--timing', '-p', source],
        'lambda_js': ['./lambda.exe', 'js', '--no-log', '-e', timed], 'node': ['node', '-e', timed]}
    engines = list(commands)
    row = {'kernel': name, 'source': str(path), 'source_sha256': sha(path), 'expected': expected,
        'samples': [], 'mir': {}}
    record['rows'].append(row)
    for round_index in range(args.runs + 1):
        order = engines[round_index % 3:] + engines[:round_index % 3]
        for engine in order:
            run_env = env.copy()
            command = commands[engine]
            mir = output.parent / (name + '_' + engine + '.mir')
            frames = output.parent / (name + '_' + engine + '_frames.log')
            if round_index == 0 and engine != 'node':
                mir.unlink(missing_ok=True)
                frames.unlink(missing_ok=True)
                run_env['LAMBDA_MIR_DUMP_PATH'] = str(mir)
                run_env['LAMBDA_MIR_LOG_FRAME_SLOTS'] = '1'
                run_env['LAMBDA_LOG_FILE'] = str(frames)
                command = [arg for arg in command if arg != '--no-log']
            start = time.perf_counter()
            run = subprocess.run(command, capture_output=True, text=True, env=run_env, timeout=120)
            wall = (time.perf_counter() - start) * 1000
            elapsed = parse_timing(run.stdout)
            observed = normalized_stdout(run.stdout).strip()
            assert run.returncode == 0 and observed == str(expected) and elapsed is not None, \
                f'{name}/{engine}: {run.returncode}: {run.stdout} {run.stderr}'
            if round_index == 0 and engine != 'node':
                assert mir.exists() and mir.stat().st_size > 0, 'missing finalized MIR'
                lines = mir.read_text().splitlines()
                operations = []
                inside = False
                for line in lines:
                    if ':\tfunc\t' in line: inside = True
                    elif line.strip() == 'endfunc': inside = False
                    elif inside and line.startswith('\t') and line.strip() and line.strip().split()[0] != 'local':
                        operations.append(line.strip().split()[0])
                row['mir'][engine] = {'path': str(mir), 'sha256': sha(mir), 'bytes': mir.stat().st_size,
                    'instructions': len(operations), 'bodies': sum(':\tfunc\t' in line for line in lines),
                    'calls': operations.count('call'),
                    'branches': sum((op.startswith('b') and op not in ('bstart', 'bend')) or op == 'jmp' for op in operations),
                    'numeric_conversions': sum(op in ('i2d', 'ui2d', 'd2i', 'd2ui') for op in operations),
                    'imports': [line.split('\timport\t', 1)[1].strip() for line in lines if '\timport\t' in line]}
                row['mir'][engine]['frames'] = [{'function': match[1],
                    **{key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', match[2])}}
                    for match in re.finditer(r'mir-function: function=(\S+) ([^\n]+)', frames.read_text())]
                assert row['mir'][engine]['frames'], 'missing frame ownership metrics'
            if round_index:
                row['samples'].append({'engine': engine, 'round': round_index, 'exec_ms': elapsed, 'wall_ms': wall})
            print(name, 'warmup' if not round_index else round_index, engine, elapsed, flush=True)
        output.write_text(json.dumps(record, indent=2) + '\n')
    row['medians_ms'] = {engine: statistics.median(s['exec_ms'] for s in row['samples'] if s['engine'] == engine)
        for engine in engines}
    if sys.platform == 'darwin':
        row['process_peak_rss_bytes'] = {}
        memory_code = ('import resource,subprocess,sys;'
            'run=subprocess.run(sys.argv[1:],capture_output=True,text=True);'
            'sys.stdout.write(run.stdout);sys.stderr.write(run.stderr);'
            'print("__RSS_BYTES__:"+str(resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss));'
            'sys.exit(run.returncode)')
        for engine in engines:
            memory = output.parent / (name + '_' + engine + '_memory.txt')
            run = subprocess.run([sys.executable, '-c', memory_code, *commands[engine]],
                capture_output=True, text=True, env=env, timeout=120)
            memory.write_text(run.stdout + run.stderr)
            rss = re.search(r'^__RSS_BYTES__:(\d+)$', run.stdout, re.MULTILINE)
            observed = re.sub(r'^__RSS_BYTES__:\d+\n?', '', run.stdout, flags=re.MULTILINE)
            assert run.returncode == 0 and normalized_stdout(observed).strip() == str(expected), memory.read_text()
            assert rss and int(rss[1]) > 0, 'missing process memory observation'
            row['process_peak_rss_bytes'][engine] = int(rss[1])
        record['metadata']['memory_boundary'] = 'one separate process peak RSS observation, including startup, compilation and teardown; not live object bytes'
record['metadata']['finished_at'] = datetime.datetime.now().astimezone().isoformat()
record['metadata']['binary_unchanged'] = sha(binary) == record['metadata']['binary_sha256']
record['metadata']['sources_unchanged'] = all(sha(row['source']) == row['source_sha256'] for row in record['rows'])
record['metadata']['production_unchanged'] = all(sha(path) == saved['sha256'] for path, saved in source_records.items())
assert record['metadata']['binary_unchanged'] and record['metadata']['sources_unchanged'] and record['metadata']['production_unchanged']
output.write_text(json.dumps(record, indent=2) + '\n')
