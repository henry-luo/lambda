#!/usr/bin/env python3
"""Same-binary interleaved L0 versus experimental MIR allocator measurements."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import time

from benchmark_provenance import sha256_file
from run_benchmarks import parse_timing
from run_compiler_metrics import parse_record
from run_paired_benchmarks import normalized_stdout

ROOT = Path(__file__).resolve().parents[2]


def run(binary, script, mode, directory, metrics=False, profile=False):
    env = dict(os.environ, LAMBDA_EXEC_BACKEND='jit', LAMBDA_MIR_OPT_LEVEL='0',
               LAMBDA_DISABLE_MIR_CACHE='1', LAMBDA_JS_LARGE_INTERP='0',
               LAMBDA_MIR_FAST_L0=mode, LAMBDA_COMPILER_TIMING='1' if metrics else '0')
    for key in ('LAMBDA_MIR_GEN_DEBUG', 'LAMBDA_MIR_FAST_PROFILE', 'LAMBDA_AST_NATIVE_REPORT'):
        env.pop(key, None)
    trace = directory / 'generator.jsonl'
    if profile:
        trace.unlink(missing_ok=True)
        env['LAMBDA_MIR_FAST_PROFILE'] = str(trace)
    cmd = [str(binary), 'run', str(script)]
    if metrics:
        cmd = ['/usr/bin/time', '-l', *cmd]
    start = time.perf_counter_ns()
    result = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True, timeout=180)
    wall = (time.perf_counter_ns() - start) / 1e6
    records = {}
    lines = []
    for line in result.stdout.splitlines():
        for marker in ('COMPILER_TIMING', 'MIR_VOLUME'):
            record = parse_record(line, marker)
            if record is not None:
                records[marker] = record
                break
        else:
            lines.append(line)
    output = normalized_stdout('\n'.join(lines))
    sample = {'self_ms': parse_timing(result.stdout, strict=True), 'wall_ms': wall,
              'stdout': output, 'stdout_sha256': hashlib.sha256(output.encode()).hexdigest(),
              'returncode': result.returncode, 'stderr': result.stderr, **records}
    # Closed-form kernels can finish within the script clock's resolution.
    if sample['self_ms'] is None and parse_timing(result.stdout) == 0:
        sample['self_ms'] = 0.0
        sample['timer_below_resolution'] = True
    if result.returncode or sample['self_ms'] is None or 'FAIL' in output:
        raise RuntimeError(f'{script} / {mode} failed: {sample}')
    if metrics:
        rss = re.search(r'^\s*(\d+)\s+maximum resident set size', result.stderr, re.M)
        if not rss or 'COMPILER_TIMING' not in records or 'MIR_VOLUME' not in records:
            raise RuntimeError(f'Missing compiler/memory metrics: {sample}')
        sample['process_peak_rss_bytes'] = int(rss[1])
    if profile:
        sample['generator'] = [json.loads(line) for line in trace.read_text().splitlines()]
        expected = {'off': 0, 'stack': 1, 'static': 2, 'compact': 3}[mode]
        if not sample['generator'] or any(r['level'] != 0 or r['fast'] != expected
                                          for r in sample['generator']):
            raise RuntimeError(f'Wrong generator path: {sample}')
    return sample


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / 'temp/mir_fast_l0/lambda-mir-fast.exe')
    parser.add_argument('--benches', default='fib,fibfp,sum,sumfp,tak,mbrot,fft,awfy/nbody,awfy/json,awfy/deltablue,awfy/richards')
    parser.add_argument('--modes', default='off,compact,static,stack')
    parser.add_argument('--repeat', type=int, default=5)
    parser.add_argument('--metrics-repeat', type=int, default=3)
    parser.add_argument('--output', type=Path, default=ROOT / 'temp/mir_fast_l0/results.json')
    args = parser.parse_args()
    modes = args.modes.split(',')
    if args.repeat < 1 or args.metrics_repeat < 1 or 'off' not in modes:
        parser.error('Positive repetitions and an off control are required')
    binary = args.exe.resolve()
    directory = args.output.resolve().parent
    directory.mkdir(parents=True, exist_ok=True)
    data = {'binary': str(binary), 'binary_sha256': sha256_file(binary),
            'git_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
            'platform': platform.platform(), 'repeat': args.repeat,
            'metrics_repeat': args.metrics_repeat, 'modes': modes,
            'patch_sha256': sha256_file(ROOT / 'patches/experiments/mir-fast-l0.patch'),
            'rows': [], 'complete': False}
    for entry in args.benches.split(','):
        suite, name = entry.split('/', 1) if '/' in entry else ('r7rs', entry)
        for variant, suffix in [('untyped', ''), ('typed', '2')]:
            script = Path('test/benchmark') / suite / f'{name}{suffix}.ls'
            row = {'suite': suite, 'name': name, 'variant': variant, 'script': str(script),
                   'source_sha256': sha256_file(ROOT / script), 'samples': [], 'metrics': [], 'profile': {}}
            expected = None
            for phase, count in [('warmup', 1), ('samples', args.repeat), ('metrics', args.metrics_repeat), ('profile', 1)]:
                for i in range(count):
                    order = modes[i % len(modes):] + modes[:i % len(modes)]
                    samples = {}
                    for mode in order:
                        sample = run(binary, script, mode, directory,
                                     metrics=phase == 'metrics', profile=phase == 'profile')
                        if expected is None:
                            expected = sample['stdout']
                        if sample['stdout'] != expected:
                            raise RuntimeError(f'Output mismatch: {script}, {mode}, {phase}')
                        samples[mode] = sample
                    if phase in ('warmup', 'profile'):
                        row[phase] = samples
                    else:
                        row[phase].append(samples)
            data['rows'].append(row)
            args.output.write_text(json.dumps(data, indent=2) + '\n')
            values = ', '.join(f"{m} self={statistics.median(s[m]['self_ms'] for s in row['samples']):.3f}ms"
                               for m in modes)
            print(f'{name}/{variant}: {values}', flush=True)
    data['binary_stable'] = data['binary_sha256'] == sha256_file(binary)
    data['complete'] = data['binary_stable']
    args.output.write_text(json.dumps(data, indent=2) + '\n')
    if not data['complete']:
        raise RuntimeError('Binary changed during measurement')


if __name__ == '__main__':
    main()
