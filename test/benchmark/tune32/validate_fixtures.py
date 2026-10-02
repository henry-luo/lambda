"""Replay Tune32 semantic goldens across Lambda tiers and existing GC modes."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

binary, output = sys.argv[1:3]
scripts = sys.argv[3:]
assert scripts, 'supply fixture paths after binary and output JSON'
modes = [
    ('jit', {'LAMBDA_TIER': 'jit'}, []),
    ('interp', {'LAMBDA_TIER': 'interp'}, []),
    ('auto', {'LAMBDA_TIER': 'auto'}, []),
    ('gc_every', {'LAMBDA_TIER': 'jit', 'LAMBDA_GC_FORCE_EVERY': '1',
                  'LAMBDA_GC_POISON_FREED': '1'}, []),
    ('gc_random', {'LAMBDA_TIER': 'jit', 'LAMBDA_GC_FORCE_SEED': '1592594996',
                   'LAMBDA_GC_FORCE_ONE_IN': '3', 'LAMBDA_GC_POISON_FREED': '1'}, []),
    ('mir_interp_gc', {'LAMBDA_TIER': 'jit', 'LAMBDA_GC_FORCE_EVERY': '1',
                       'LAMBDA_GC_POISON_FREED': '1'}, ['--mir-interp']),
]
rows = []
for script in scripts:
    source = Path(script)
    expected_path = source.with_suffix('.txt')
    expected = expected_path.read_text().strip()
    for mode, variables, flags in modes:
        env = os.environ.copy()
        for name in ('LAMBDA_GC_FORCE_EVERY', 'LAMBDA_GC_FORCE_SEED',
                     'LAMBDA_GC_FORCE_ONE_IN', 'LAMBDA_GC_POISON_FREED'):
            env.pop(name, None)
        env.update(variables)
        result = subprocess.run([binary, 'run', *flags, '--no-log', script],
                                env=env, capture_output=True, text=True, timeout=120)
        row = {'script': script, 'mode': mode, 'env': variables,
               'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
               'expected_sha256': hashlib.sha256(expected_path.read_bytes()).hexdigest(),
               'exit': result.returncode, 'matches_expected': result.stdout.strip() == expected,
               'stdout_sha256': hashlib.sha256(result.stdout.encode()).hexdigest()}
        rows.append(row)
        if result.returncode or not row['matches_expected']:
            print(row, result.stdout, result.stderr, flush=True)
    print(script, 'done', flush=True)
report = {'binary': binary, 'binary_sha256': hashlib.sha256(Path(binary).read_bytes()).hexdigest(),
          'rows': rows, 'passed': all(x['matches_expected'] and x['exit'] == 0 for x in rows)}
Path(output).write_text(json.dumps(report, indent=2) + '\n')
assert report['passed'], 'fixture replay failed; inspect the report and console output'
