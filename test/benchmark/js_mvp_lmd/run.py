#!/usr/bin/env python3
"""Release-only end-to-end timings; identical kernels and checked outputs."""
import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('--runs', type=int, default=5)
parser.add_argument('--output', default='temp/mvp_lmd_benchmark.json')
args = parser.parse_args()
environment = os.environ.copy()
backend_flags = {'JS_EXEC_BACKEND': 'mir', 'LAMBDA_EXEC_BACKEND': 'jit',
                 'JS_MIR_INTERP': '0', 'LAMBDA_JS_LARGE_INTERP': '0'}
environment.update(backend_flags)
assert Path('.lambda_release_build').exists(), 'run make release before timing'
assert Path('.lambda_release_build').stat().st_mtime >= Path('lambda.exe').stat().st_mtime, \
    'release stamp must postdate the executable; rebuild release before timing'
rows = []
expected = {'numeric': 1249999987500000, 'dense_array': 781246875000,
            'calls': 676500, 'strings': 10000000}
for name, oracle in expected.items():
    source = (Path(__file__).parent / (name + '.js')).read_text()
    # Full JS and Node print through their normal host console; MVP prints its script result.
    printed = source.rstrip().removesuffix('work()') + 'console.log(work())'
    commands = {
        'mvp_lmd': ['./lambda.exe', 'js', '--no-log', '--runtime=mvp-lmd', '-p', source],
        'lambda_js': ['./lambda.exe', 'js', '--no-log', '-e', printed],
        'node': ['node', '-e', printed],
    }
    row = {'kernel': name, 'expected': oracle, 'engines': {}}
    for engine, command in commands.items():
        times = []
        for repeat in range(args.runs + 1):
            start = time.perf_counter()
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, env=environment)
            elapsed = time.perf_counter() - start
            if result.returncode or float(result.stdout.strip()) != oracle:
                raise RuntimeError(f'{name}/{engine}: {result.returncode}: {result.stdout} {result.stderr}')
            if repeat: times.append(elapsed * 1000)
        row['engines'][engine] = {'median_ms': statistics.median(times), 'samples_ms': times}
    rows.append(row)
    print(json.dumps(row), flush=True)
Path(args.output).write_text(json.dumps({'kind': 'end_to_end_including_startup_and_compile',
    'runs': args.runs, 'backend_flags': backend_flags, 'rows': rows}, indent=2) + '\n')
