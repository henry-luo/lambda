#!/usr/bin/env python3
"""Measure closure-phase workloads with frozen sources and self-reported timings."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'test/benchmark'))
from run_benchmarks import build_benchmark_list, check_release_build, mir_script_variants, parse_timing
from run_paired_benchmarks import normalized_stdout


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def adapt(row):
    """Replace host timing/output only; retain the complete bundle and its own oracle."""
    if row['suite'] == 'awfy':
        path = Path(row['js_path'].replace('2.js', '2_bundle.js'))
        body, harness = path.read_text().split('// --- timing harness ---')
        constructor = re.search(r'const bench = new (\w+)\(\)', harness)[1]
        inner = re.search(r'bench.innerBenchmarkLoop\((\d+)\)', harness)[1]
        setup = f'const bench = new {constructor}();\n'
        result = f'bench.innerBenchmarkLoop({inner}) ? 1 : 0'
        contract = {'constructor': constructor, 'inner_iterations': int(inner)}
    else:
        path = Path(row['js_path'] or row['ref_js'])
        body, setup, result = path.read_text(), 'new Benchmark().runIteration();\n', '1'
        contract = {'entry': 'Benchmark.runIteration', 'oracle': 'native SHA1 digest assertion in every run()'}
    return path, body + '\n' + setup, result, contract


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate', default='lambda.exe')
    parser.add_argument('--out', default='temp/mvp_closures')
    parser.add_argument('--runs', type=int, default=15)
    args = parser.parse_args()
    os.chdir(ROOT)
    out = Path(args.out).resolve()
    assert out.is_relative_to(ROOT / 'temp') and args.runs > 0
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'candidate.exe'
    shutil.copy2(args.candidate, binary)
    check_release_build(exe_path=str(binary), log_path=str(out / 'release.log'))
    flags = dict(JS_EXEC_BACKEND='mir', JS_EXECUTION_BACKEND='mir', JS_MIR_INTERP='0',
                 LAMBDA_JS_LARGE_INTERP='0', LAMBDA_EXEC_BACKEND='jit', LAMBDA_TIER='jit')
    env = {key: value for key, value in os.environ.items() if not key.startswith('LAMBDA_GC_')
           and key not in ('LAMBDA_MIR_DUMP_PATH', 'JS_MIR_DUMP', 'LAMBDA_MIR_LOG_FRAME_SLOTS',
                           'LAMBDA_LOG_FILE', 'JS_OPT_TRACE', 'JS_TRANSPILE_TIMING', 'LAMBDA_COMPILER_TIMING')}
    env.update(flags)
    record = {'metadata': {'started': datetime.datetime.now().astimezone().isoformat(),
        'binary': str(binary), 'binary_sha256': sha(binary), 'runner_sha256': sha(__file__),
        'node_version': subprocess.check_output(['node', '--version'], text=True).strip(),
        'node_sha256': sha(shutil.which('node')), 'runs': args.runs, 'warmups': 1, 'flags': flags,
        'timing': 'Fresh processes in rotating lane order. One discarded process per lane. Self-reported execution only; '
                  'MVP and Node include class creation/setup and oracle; Lambda uses the canonical port timer. '
                  'Node lazy compilation/tiering during entry is included. Initial MVP compilation is excluded.',
        'inventory_sha256': sha('test/benchmark/js_mvp_lmd_manifest_v1.json')}, 'rows': []}
    targets = {'awfy/' + name for name in ('bounce', 'storage', 'nbody', 'richards', 'cd')} | {'jetstream/crypto_sha1'}
    rows = [row for row in build_benchmark_list(None, None, include_text=True)
            if row['suite'] + '/' + row['name'] in targets]
    assert len(rows) == len(targets)
    def save():
        (out / 'comparison.json').write_text(json.dumps(record, indent=2) + '\n')
    for spec in rows:
        identifier = spec['suite'] + '/' + spec['name']
        source, body, result, contract = adapt(spec)
        stem = identifier.replace('/', '_')
        js = out / (stem + '.js'); js.write_text(body + result + ';\n')
        node = out / (stem + '.node.js')
        node.write_text('function workload(){\n' + body + 'return ' + result + ';\n}\n'
            'const start=performance.now();const result=workload();const elapsed=performance.now()-start;\n'
            'console.log(result);console.log("__TIMING__:"+elapsed);\n')
        untyped, _ = mir_script_variants(spec)
        ls = out / (stem + '.ls'); shutil.copy2(untyped, ls)
        commands = {'mvp': [str(binary), 'js', '--runtime=mvp-lmd', '--no-log', '--timing', '-p', js.read_text()],
                    'node': ['node', str(node)], 'lambda_untyped': [str(binary), 'run', '--no-log', str(ls)]}
        golden = Path(untyped).with_suffix('.txt')
        # JetStream ports validate internally and do not all ship golden output files.
        expected_lambda = normalized_stdout(golden.read_text()) if golden.exists() else \
            re.search(r'print\("([^"\n]+: PASS)\\n"\)', ls.read_text())[1]
        row = {'id': identifier, 'contract': contract, 'expected_lambda': expected_lambda,
            'lambda_variant': 'canonical .ls; explicit numeric annotations retained' if
                re.search(r':\s*(?:int|float|u32)\b', ls.read_text()) else 'untyped canonical .ls',
            'sources': [{'path': str(path), 'sha256': sha(path)} for path in
                        [source, js, node, Path(untyped), ls] + ([golden] if golden.exists() else [])],
            'commands': commands, 'backend': {}, 'samples': []}
        record['rows'].append(row)
        for iteration in range(args.runs + 1):
            lanes = list(commands); offset = iteration % len(lanes); lanes = lanes[offset:] + lanes[:offset]
            for lane in lanes:
                command = commands[lane][:]; environment = env.copy()
                mir = out / (stem + '_' + lane + '.mir')
                if not iteration and lane != 'node':
                    command.remove('--no-log'); mir.unlink(missing_ok=True)
                    environment['LAMBDA_MIR_DUMP_PATH'] = str(mir)
                proc = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=120)
                output = normalized_stdout(proc.stdout); ms = parse_timing(proc.stdout)
                log = out / (stem + '_' + lane + '_' + str(iteration))
                log.with_suffix('.stdout').write_text(proc.stdout); log.with_suffix('.stderr').write_text(proc.stderr)
                expected = expected_lambda if lane == 'lambda_untyped' else '1'
                if proc.returncode or output != expected or ms is None or ms <= 0:
                    row['failure'] = dict(lane=lane, iteration=iteration, code=proc.returncode,
                        expected=expected, output=output, stderr=proc.stderr); save(); raise RuntimeError(row['failure'])
                if iteration: row['samples'].append(dict(lane=lane, round=iteration, ms=ms, output_matches=True))
                elif lane != 'node':
                    assert mir.exists() and mir.stat().st_size > 0
                    row['backend'][lane] = {'path': str(mir), 'sha256': sha(mir)}
            save()
        row['median_ms'] = {lane: statistics.median(sample['ms'] for sample in row['samples'] if sample['lane'] == lane)
                            for lane in commands}
        save(); print(identifier, row['median_ms'], flush=True)
    record['metadata']['finished'] = datetime.datetime.now().astimezone().isoformat()
    assert sha(binary) == record['metadata']['binary_sha256']
    assert all(sha(s['path']) == s['sha256'] for row in record['rows'] for s in row['sources'])
    save()


if __name__ == '__main__':
    main()
