#!/usr/bin/env python3
"""Paired release comparison of pn and fn benchmark sources (S12.1.2v3)."""
import argparse
import hashlib
import json
import os
import platform
import random
from pathlib import Path
import statistics
import sys
import time

from run_benchmarks import (build_benchmark_list, check_release_build,
                            mir_script_variants, parse_timing)
from run_standard_benchmarks import check_ac_power

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'interp'))
from lambda_process import run_lambda_process

ROOT = Path(__file__).resolve().parents[2]
OUTPUT_LIMIT = 1024 * 1024
CONTROL_ENV = {name: None for name in (
    'LAMBDA_GC_FORCE_EVERY', 'LAMBDA_GC_FORCE_SEED', 'LAMBDA_GC_FORCE_ONE_IN',
    'LAMBDA_GC_POISON_FREED', 'LAMBDA_SATELLITE_SYNC',
    'LAMBDA_FUNC_JIT_THRESHOLD', 'LAMBDA_LOOP_JIT_THRESHOLD', 'LAMBDA_CONST_FOLD')}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def normalized_output(stdout):
    return '\n'.join(line for line in stdout.splitlines()
                     if '__TIMING__' not in line).strip()


def sample(binary, script, tier, timeout):
    start = time.perf_counter()
    result = run_lambda_process(str(binary), str(script), tier, timeout,
                                procedural=True, no_log=True, extra_env=CONTROL_ENV,
                                output_limit=OUTPUT_LIMIT)
    elapsed = (time.perf_counter() - start) * 1000
    timing = parse_timing(result.stdout, strict=True)
    ok = (result.return_code == 0 and not result.timed_out and not result.output_limited
          and timing is not None
          and 'FAIL' not in result.stdout and 'error[' not in result.stderr
          and '[ERR!]' not in result.stderr)
    return {'wall_ms': elapsed, 'exec_ms': timing, 'ok': ok,
            'status': result.status, 'output': normalized_output(result.stdout),
            'stderr': result.stderr[-3000:]}


def imports(path):
    # Benchmark imports use relative module spellings; the workload reads its
    # fixture data from the project root regardless of the source snapshot.
    import re
    targets = []
    for prefix, name in re.findall(r'^import\s+(\.|~~\.)(\w+)\s*$',
                                   path.read_text(), re.MULTILINE):
        targets.append((path.parent if prefix == '.' else path.parent.parent) /
                       (name + '.ls'))
    return targets


def dependencies(path, seen=None):
    seen = set() if seen is None else seen
    path = path.resolve()
    if path in seen or not path.exists():
        return seen
    seen.add(path)
    for target in imports(path):
        dependencies(target, seen)
    return seen


def comparisons(manifest, selected):
    changed = {(ROOT / row['path']).resolve() for row in manifest['migrated']}
    rows = []
    seen = set()
    covered = set()
    for benchmark in build_benchmark_list([], [], include_text=True):
        for variant, path in zip(('untyped', 'typed'), mir_script_variants(benchmark)):
            if path is None or path in seen or not Path(path).exists():
                continue
            seen.add(path)
            if selected and not any(name in path for name in selected):
                continue
            reached = dependencies(ROOT / path)
            if reached & changed:
                covered.update(reached)
                rows.append({'suite': benchmark['suite'], 'name': benchmark['name'],
                             'variant': variant, 'path': path,
                             'timeout': benchmark.get('timeout_s') or 120})
    # The standard registry deduplicates workloads across suites. Qualify the
    # changed copies and standalone timing probes too, rather than inferring
    # their performance from a different source file's canonical row.
    import re
    for path in sorted({r['path'] for r in manifest['migrated']}):
        script = ROOT / path
        if script.resolve() in covered or (selected and not any(s in path for s in selected)):
            continue
        if re.search(r'^\s*pn\s+main\s*\(', script.read_text(), re.MULTILINE):
            rows.append({'suite': script.parent.name, 'name': script.stem,
                         'variant': 'additional', 'path': path, 'timeout': 120})
            covered.update(dependencies(script))
    return rows


def summarize(pairs, mode, tolerance, absolute_ms, warmups=()):
    metric = 'exec_ms' if mode == 'jit' else 'wall_ms'
    ratios = []
    for pair in [*warmups, *pairs]:
        before, after = pair['before'], pair['after']
        if not before['ok'] or not after['ok'] or before['output'] != after['output']:
            return {'status': 'correctness_failure'}
    for pair in pairs:
        before, after = pair['before'], pair['after']
        a, b = before[metric], after[metric]
        if a is None or b is None or a <= 0 or b <= 0:
            return {'status': 'invalid_timing'}
        ratios.append(b / a)
    if not ratios:
        return {'status': 'not_measured'}
    before = statistics.median(p['before'][metric] for p in pairs)
    after = statistics.median(p['after'][metric] for p in pairs)
    ratio = statistics.median(ratios)
    delta = statistics.median(p['after'][metric] - p['before'][metric] for p in pairs)
    # Resample paired ratios rather than mixing measurements from different
    # source versions; alternating pair order limits temperature/startup drift.
    rng = random.Random(7321)
    bootstrap = sorted(statistics.median(rng.choices(ratios, k=len(ratios)))
                       for _ in range(4000))
    lower, upper = bootstrap[100], bootstrap[3899]
    exceeds = ratio > 1 + tolerance and delta > absolute_ms
    status = 'regression' if exceeds and lower > 1 + tolerance else 'needs_more_runs' if exceeds else 'pass'
    return {'status': status, 'metric': metric, 'before_median_ms': before,
            'after_median_ms': after, 'median_paired_ratio': ratio,
            'median_paired_delta_ms': delta,
            'paired_median_bootstrap_ci95': [lower, upper], 'samples': len(pairs)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--binary', type=Path, default=Path('./lambda.exe'))
    parser.add_argument('--before-binary', type=Path,
                        help='optional original release binary; defaults to --binary')
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--runs', type=int, default=5)
    parser.add_argument('--warmups', type=int, default=1)
    parser.add_argument('--modes', default='jit,auto')
    parser.add_argument('--filter', action='append', default=[])
    parser.add_argument('--tolerance', type=float, default=0.05)
    parser.add_argument('--absolute-ms', type=float, default=0.1)
    parser.add_argument('--timeout', type=float)
    parser.add_argument('--resume', action='store_true',
                        help='extend non-passing rows to --runs paired samples')
    args = parser.parse_args()
    if args.runs < 1 or args.warmups < 0:
        parser.error('runs must be positive and warmups nonnegative')
    if not set(args.modes.split(',')) <= {'jit', 'auto'}:
        parser.error('modes must contain jit and/or auto')
    os.chdir(ROOT)
    args.binary = args.binary.resolve()
    args.before_binary = (args.before_binary or args.binary).resolve()
    args.before = args.before.resolve()
    check_release_build(exe_path=str(args.binary))
    if args.before_binary != args.binary:
        check_release_build(exe_path=str(args.before_binary))
    check_ac_power()
    manifest = json.loads(args.manifest.read_text())
    report = {'controlled_environment': CONTROL_ENV, 'binary': str(args.binary), 'binary_sha256': digest(args.binary),
              'before_binary': str(args.before_binary),
              'before_binary_sha256': digest(args.before_binary),
              'platform': platform.platform(), 'cpu_count': os.cpu_count(),
              'load_at_start': os.getloadavg(),
              'started_at_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
              'before_commit': manifest.get('before_commit'),
              'manifest_sha256': digest(args.manifest),
              'output_limit_chars': OUTPUT_LIMIT,
              'process_runner_sha256': digest(ROOT / 'test/interp/lambda_process.py'),
              'sources': [dict(path=path, before_sha256=digest(args.before / path),
                               after_sha256=digest(ROOT / path))
                          for path in sorted({r['path'] for r in manifest['migrated']})],
              'before_root': str(args.before), 'runs': args.runs,
              'warmups': args.warmups, 'tolerance': args.tolerance,
              'absolute_ms': args.absolute_ms, 'rows': []}
    if args.resume:
        previous = json.loads(args.output.read_text())
        for key in ('binary_sha256', 'before_binary_sha256', 'before_root',
                    'controlled_environment', 'manifest_sha256', 'sources',
                    'output_limit_chars', 'process_runner_sha256',
                    'tolerance', 'absolute_ms'):
            if previous.get(key) != report[key]:
                parser.error('cannot resume comparison with different ' + key)
        report = previous
        report['runs'] = args.runs
    args.output.parent.mkdir(parents=True, exist_ok=True)
    def save():
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    for case in comparisons(manifest, args.filter):
        before = args.before / case['path']
        after = ROOT / case['path']
        for mode in args.modes.split(','):
            row = next((r for r in report['rows'] if r['path'] == case['path']
                        and r['mode'] == mode), None)
            if row:
                if row['before_sha256'] != digest(before) or row['after_sha256'] != digest(after):
                    parser.error('cannot resume comparison with changed source: ' + case['path'])
                if row['summary']['status'] == 'pass':
                    continue
            else:
                row = dict(case, mode=mode, before_sha256=digest(before),
                           after_sha256=digest(after), pairs=[], warmups=[])
                report['rows'].append(row)
            timeout = args.timeout or case['timeout']
            row['load_before'] = os.getloadavg()
            for _ in range(len(row['warmups']), args.warmups):
                row['warmups'].append({'before': sample(args.before_binary, before, mode, timeout),
                                       'after': sample(args.binary, after, mode, timeout)})
            warmups_ok = all(p['before']['ok'] and p['after']['ok']
                             and p['before']['output'] == p['after']['output']
                             for p in row['warmups'])
            for repeat in range(len(row['pairs']), args.runs if warmups_ok else 0):
                pair = {}
                order = ('before', 'after') if repeat % 2 == 0 else ('after', 'before')
                for side in order:
                    pair[side] = sample(args.before_binary if side == 'before' else args.binary,
                                        before if side == 'before' else after,
                                        mode, timeout)
                row['pairs'].append(pair)
                if not pair['before']['ok'] or not pair['after']['ok'] or pair['before']['output'] != pair['after']['output']:
                    break
            row['summary'] = summarize(row['pairs'], mode, args.tolerance, args.absolute_ms,
                                       row['warmups'])
            row['load_after'] = os.getloadavg()
            save()
            print(case['path'], mode, json.dumps(row['summary']), flush=True)
    statuses = [row['summary']['status'] for row in report['rows']]
    report['passed'] = bool(statuses) and all(status == 'pass' for status in statuses)
    report['completed_at_utc'] = time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
    save()
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
