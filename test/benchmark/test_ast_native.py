#!/usr/bin/env python3
"""Historical ARM64 experiment checks; requires its archived executable."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, default=ROOT / 'lambda.exe')
    args = p.parse_args()
    binary = args.exe.resolve()
    fixture = ROOT / 'test/benchmark/ast_native/archive/ast_native_scalar.ls'
    expected = fixture.with_suffix('.txt').read_text()
    (ROOT / 'temp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ast-native-check-', dir=ROOT / 'temp') as directory:
        report = Path(directory) / 'native.jsonl'
        for mode, gc in [('interp', False), ('jit', False), ('ast-native', False), ('ast-native', True)]:
            report.unlink(missing_ok=True)
            env = dict(os.environ, LAMBDA_EXEC_BACKEND=mode, LAMBDA_DISABLE_MIR_CACHE='1',
                       LAMBDA_AST_NATIVE_REPORT=str(report))
            if gc:
                env.update(LAMBDA_GC_FORCE_EVERY='1', LAMBDA_GC_POISON_FREED='1')
            result = subprocess.run([str(binary), str(fixture)], cwd=ROOT, env=env,
                                    capture_output=True, text=True, timeout=60)
            if result.returncode or result.stdout != expected:
                raise RuntimeError(f'{mode} forced_gc={gc}: {result.returncode}\n{result.stdout}\n{result.stderr}')
            if mode == 'ast-native':
                records = [json.loads(line) for line in report.read_text().splitlines()]
                if sum(x['accepted'] for x in records) != 4 or sum(x['calls'] for x in records) < 350:
                    raise RuntimeError(f'Fixture did not exercise its native bodies: {records}')
            elif report.exists():
                raise RuntimeError(f'{mode} unexpectedly created native code')
            print(f'{mode}, forced_gc={gc}: output and native admission passed')
        fallback = ROOT / 'test/benchmark/ast_native/archive/ast_native_fallback.ls'
        report.unlink(missing_ok=True)
        env = dict(os.environ, LAMBDA_EXEC_BACKEND='ast-native', LAMBDA_DISABLE_MIR_CACHE='1',
                   LAMBDA_AST_NATIVE_REPORT=str(report))
        result = subprocess.run([str(binary), str(fallback)], cwd=ROOT, env=env,
                                capture_output=True, text=True, timeout=60)
        records = [json.loads(line) for line in report.read_text().splitlines()]
        if result.returncode or result.stdout != fallback.with_suffix('.txt').read_text() or any(
                record['accepted'] or record['calls'] for record in records):
            raise RuntimeError(f'Unsupported-body admission failed: {result} {records}')
        print('procedures, error channels, loops and tail calls: T0 admission/parity passed')



if __name__ == '__main__':
    main()
