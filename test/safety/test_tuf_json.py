#!/usr/bin/env python3
"""Deterministic differential/mutation corpus; no network or user settings."""
from datetime import UTC, datetime
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile

from generate_tuf_fixtures import canonical
from macos_fixture import require_qualification

ROOT = Path(__file__).resolve().parents[2]


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result: raise ValueError('duplicate key')
        result[key] = value
    return result


def unsupported(_):
    raise ValueError('non-integer number')


def oracle(raw):
    try:
        value = json.loads(raw.decode('utf8'), object_pairs_hook=unique_object,
                           parse_float=unsupported, parse_constant=unsupported)
        # Python's decoder accepts lone surrogates: strict wire UTF-8 does not.
        encoded = canonical(value).encode('utf8')
        return value, hashlib.sha256(encoded).hexdigest()
    except (ValueError, UnicodeError):
        return None, None


def corpus():
    rng = random.Random(0x545546)
    alphabet = ['a', 'z', 'é', '😀', '\x00', '\n', '\t', '"', '\\', '/', '中']
    def value(depth):
        options = [None, bool(rng.randrange(2)), rng.randrange(-10**40, 10**40),
                   ''.join(rng.choices(alphabet, k=rng.randrange(12)))]
        if depth:
            options += [[value(depth - 1) for _ in range(rng.randrange(5))],
                        {str(i) + ''.join(rng.choices(alphabet, k=3)): value(depth - 1)
                         for i in range(rng.randrange(5))}]
        return rng.choice(options)
    seeds = [json.dumps(value(3), ensure_ascii=False, separators=(',', ':')).encode() for _ in range(160)]
    rows = [*seeds, b' ' * 5000 + b'true', b'"' + b'x' * 10000 + b'"', b'9' * 5000,
            b'[' + b','.join([b'0'] * 1200) + b']',
            b'{"a":1,"\\u0061":2}', b'{"a\\u0000x":1,"a\\u0000y":2}',
            b'"\\ud800"', b'"\\udc00"', b'"\\ud800\\u0000"', b'"\\udbff\\udfff"',
            b'"\xc0\x80"', b'"\xed\xa0\x80"', b'"\xf4\x90\x80\x80"', b'"\xf0\x9f\x98"',
            b'1.0', b'1e10', b'NaN', b'Infinity', b'01', b'-01', b'+1', b'--1',
            b'[1,]', b'{"a":1,}', b'"\\u00zz"', b'"\x00"', b'[] garbage', b'\xef\xbb\xbf[]']
    for _ in range(500):
        raw = bytearray(rng.choice(seeds)); at = rng.randrange(len(raw) + 1)
        if rng.randrange(2): raw[at:at + 1] = bytes([rng.randrange(256)])
        else: raw[at:at + rng.randrange(1, 4)] = b''
        rows.append(bytes(raw))
    return rows


def main():
    # Oracle integers, like Lambda's version lexemes, must not hit Python's
    # decimal conversion guard in the explicitly bounded long-number corpus.
    sys.set_int_max_str_digits(10000)
    directory = ROOT / 'temp/app_safety_tuf_json'; directory.mkdir(exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix='run-', dir=directory))
    report = {'status': 'unqualified', 'seed': '0x545546', 'cases': [], 'tiers': {}}
    output = directory / 'result.json'
    def checkpoint():
        output.write_text(json.dumps(report, indent=2) + '\n')
        (run / 'result.json').write_text(output.read_text())
    checkpoint()
    rows = corpus(); executable = Path(os.environ.get('LAMBDA_TUF_TEST_EXECUTABLE', ROOT / 'lambda.exe')).resolve()
    report['executable_sha256'] = hashlib.sha256(executable.read_bytes()).hexdigest()
    report['cases'] = [{'id': i, 'sha256': hashlib.sha256(raw).hexdigest(), 'valid': oracle(raw)[1] is not None}
                       for i, raw in enumerate(rows)]
    for tier in ('interp', 'jit'):
        passed = 0
        for first in range(0, len(rows), 32):
            batch = rows[first:first + 32]
            (run / 'request.json').write_text(json.dumps([{'id': first + i, 'hex': raw.hex()} for i, raw in enumerate(batch)]))
            reply = run / 'reply.json'; reply.unlink(missing_ok=True)
            result = subprocess.run([str(executable), '--no-log', 'run', str(ROOT / 'test/safety/tuf/_json_probe.ls')],
                cwd=run, env=dict(os.environ, LAMBDA_HOME=str(ROOT / 'lmd'), LAMBDA_EXEC_BACKEND=tier),
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
            (run / f'{tier}-{first}.log').write_bytes(result.stdout + result.stderr)
            require_qualification(result.returncode == 0 and reply.exists(), {'tier': tier, 'first': first, 'stderr': result.stderr.decode(errors='replace')})
            actual = json.loads(reply.read_bytes()); require_qualification(len(actual) == len(batch))
            for offset, (raw, row) in enumerate(zip(batch, actual)):
                expected, digest = oracle(raw)
                require_qualification(isinstance(row, dict), {'id': first + offset, 'result': row})
                require_qualification(row['id'] == first + offset and row['valid'] == (digest is not None), row)
                if digest:
                    require_qualification(row['canonical_sha256'] == digest and json.loads(row['document']) == expected, row)
                passed += 1
            report['tiers'][tier] = passed; checkpoint()
    report.update(status='strict_json_differential_component_passed', total=len(rows),
                  completed_utc=datetime.now(UTC).isoformat(), report=str(run / 'result.json'))
    checkpoint(); print(json.dumps({'cases_per_tier': len(rows), 'tiers': report['tiers'], 'report': str(output)}))


if __name__ == '__main__':
    main()
