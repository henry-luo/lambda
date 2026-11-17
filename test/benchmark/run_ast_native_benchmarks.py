#!/usr/bin/env python3
"""Compare the admitted scalar AST-native experiment with same-binary MIR L0."""
import argparse
import hashlib
import json
import os
import platform
from pathlib import Path
import re
import statistics
import subprocess
import time

from run_paired_benchmarks import normalized_stdout
from run_benchmarks import parse_timing
from run_compiler_metrics import CODE_SIZE_RECORD, parse_record

ROOT = Path(__file__).resolve().parents[2]
ARCHIVED_IMPLEMENTATION = ('ast_native.cpp.txt', 'ast_native.hpp.txt', 'integration.patch')


def run(binary, script, tier, metrics, stem):
    env = os.environ.copy()
    env.update(LAMBDA_EXEC_BACKEND=tier, LAMBDA_MIR_OPT_LEVEL='0',
               LAMBDA_DISABLE_MIR_CACHE='1', LAMBDA_JS_LARGE_INTERP='0')
    report = stem.with_suffix('.jsonl')
    env.pop('LAMBDA_AST_NATIVE_REPORT', None)
    env.pop('LAMBDA_MIR_GEN_DEBUG', None)
    env['LAMBDA_COMPILER_TIMING'] = '1' if metrics else '0'
    if metrics and tier == 'ast-native':
        report.unlink(missing_ok=True)
        env['LAMBDA_AST_NATIVE_REPORT'] = str(report)
    command = [str(binary), 'run', str(script)]
    if metrics:
        command = ['/usr/bin/time', '-l'] + command
    start = time.perf_counter_ns()
    p = subprocess.run(command, env=env, cwd=ROOT, capture_output=True,
                       text=True, timeout=120)
    wall = (time.perf_counter_ns() - start) / 1e6
    records = {}; output = []
    for line in p.stdout.splitlines():
        for marker in ('COMPILER_TIMING', 'MIR_VOLUME'):
            record = parse_record(line, marker)
            if record is not None:
                records[marker] = record
                break
        else:
            output.append(line)
    stable = normalized_stdout('\n'.join(output))
    result = dict(returncode=p.returncode, self_ms=parse_timing(p.stdout, strict=True),
                  wall_ms=wall, stdout=stable,
                  stdout_sha256=hashlib.sha256(stable.encode()).hexdigest(),
                  stderr=p.stderr, compiler=records)
    rss = re.search(r'^\s*(\d+)\s+maximum resident set size', p.stderr, re.M)
    result['process_peak_rss_bytes'] = int(rss[1]) if rss else None
    if report.exists() and metrics and tier == 'ast-native':
        result['native'] = [json.loads(line) for line in report.read_text().splitlines()]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / 'lambda.exe')
    parser.add_argument('--pairs', type=int, default=3)
    parser.add_argument('--output', type=Path, default=ROOT / 'temp/arm64_baseline/results.json')
    args = parser.parse_args()
    if args.pairs < 1:
        parser.error('--pairs must be positive')
    binary = args.exe.resolve()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    data = {'git_commit': subprocess.check_output(['git','rev-parse','HEAD'], cwd=ROOT, text=True).strip(),
            'platform': platform.platform(),
            'source_hashes': {f'test/benchmark/ast_native/archive/{name}': hashlib.sha256(
                (ROOT / 'test/benchmark/ast_native/archive' / name).read_bytes()).hexdigest() for name in ARCHIVED_IMPLEMENTATION},
            'environment': {'LAMBDA_MIR_OPT_LEVEL':'0','LAMBDA_DISABLE_MIR_CACHE':'1','LAMBDA_JS_LARGE_INTERP':'0'},
            'binary': str(binary), 'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
            'pairs_requested': args.pairs, 'rows': [], 'complete': False}
    for name in ('fib', 'fibfp'):
        for variant, suffix in (('untyped', ''), ('typed', '2')):
            script = Path('test/benchmark/r7rs') / (name + suffix + '.ls')
            row = {'name': name, 'variant': variant, 'script': str(script),
                   'source_sha256': hashlib.sha256((ROOT / script).read_bytes()).hexdigest(), 'pairs': []}
            stem = args.output.parent / (name + suffix)
            for pair in range(-1, args.pairs):
                order = ['jit', 'ast-native'] if pair % 2 == 0 else ['ast-native', 'jit']
                sample = {tier: run(binary, script, tier, False, stem) for tier in order}
                sample['output_equal'] = sample['jit']['stdout'] == sample['ast-native']['stdout']
                if not sample['output_equal'] or any(sample[t]['returncode'] != 0 or
                        sample[t]['self_ms'] is None for t in order):
                    raise RuntimeError(f'Invalid pair for {script}: {sample}')
                if pair >= 0: row['pairs'].append(sample)
                else: row['warmup'] = sample
            row['metrics'] = {tier: run(binary, script, tier, True, stem) for tier in ('jit', 'ast-native')}
            for tier, sample in row['metrics'].items():
                if sample['returncode'] or sample['stdout'] != row['pairs'][0][tier]['stdout']:
                    raise RuntimeError(f'Metric output mismatch: {script} {tier}')
                if not sample['compiler'].get('COMPILER_TIMING') or not sample['process_peak_rss_bytes']:
                    raise RuntimeError(f'Missing compiler or memory metrics: {script} {tier}')
            native = row['metrics']['ast-native'].get('native', [])
            if not native or not sum(record['accepted'] for record in native) or not sum(
                    record['calls'] for record in native):
                raise RuntimeError(f'Kernel did not exercise native code: {script}')
            debug = ROOT / 'temp/mir_gen_debug.txt'
            debug.unlink(missing_ok=True)
            env = os.environ.copy()
            env.update(LAMBDA_EXEC_BACKEND='jit', LAMBDA_MIR_OPT_LEVEL='0',
                       LAMBDA_DISABLE_MIR_CACHE='1', LAMBDA_JS_LARGE_INTERP='0',
                       LAMBDA_MIR_GEN_DEBUG='0', LAMBDA_MIR_GEN_DEBUG_APPEND='1')
            p = subprocess.run([str(binary), 'run', str(script)], env=env, cwd=ROOT,
                               capture_output=True, text=True, timeout=120)
            if p.returncode or normalized_stdout(p.stdout) != row['pairs'][0]['jit']['stdout']:
                raise RuntimeError(f'Code-size output mismatch: {script}')
            sizes = [int(n) for n in CODE_SIZE_RECORD.findall(debug.read_text())]
            if not sizes:
                raise RuntimeError(f'Missing MIR code-size records: {script}')
            row['mir_native_code_bytes'] = sum(sizes)
            row['mir_functions'] = len(sizes)
            data['rows'].append(row)
            args.output.write_text(json.dumps(data, indent=2))
            print(f"{name}/{variant}: self ms MIR={statistics.median(p['jit']['self_ms'] for p in row['pairs']):.3f} AST={statistics.median(p['ast-native']['self_ms'] for p in row['pairs']):.3f}", flush=True)
    data['binary_stable'] = data['binary_sha256'] == hashlib.sha256(binary.read_bytes()).hexdigest()
    data['complete'] = data['binary_stable']
    args.output.write_text(json.dumps(data, indent=2))
    if not data['binary_stable']:
        raise RuntimeError('Executable changed during measurement')


if __name__ == '__main__':
    main()
