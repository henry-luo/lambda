#!/usr/bin/env python3
"""RFC URI codec differential checks across Lambda and the native host gate."""
import hashlib
import json
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
import tempfile
from urllib.parse import quote, unquote_to_bytes

from macos_fixture import require_qualification

ROOT = Path(__file__).resolve().parents[2]


def printable(text, ceiling):
    return 0 < len(text.encode('utf8')) < ceiling and all(ord(c) >= 32 and not 127 <= ord(c) <= 159 for c in text)


def uri_model(text, iri):
    if not printable(text, 4096) or any(part in ('', '.', '..') for part in text.split('/')):
        return None
    encoded = []
    try:
        for part in text.split('/'):
            if re.search(r'%(?![0-9A-F]{2})', part) or any(c in part for c in '\\@?#'):
                return None
            raw = unquote_to_bytes(part); decoded = raw.decode('utf8')
            if not printable(decoded, 4096): return None
            canonical = quote(raw, safe='-._~')
            # stdlib round-tripping rejects escaped unreserved bytes while
            # retaining escaped separators inside their original URI component.
            before = quote(part, safe='-._~%') if iri else part
            if canonical != before: return None
            encoded.append(canonical)
        return '/'.join(encoded)
    except UnicodeError:
        return None


def corpus():
    rng = random.Random(0x555249)
    alphabet = list('aZ09._~-/ %:@?#\\') + ['é', 'e\u0301', '名', '😀', '\n', '\x7f', '\x85', '\xa0']
    rows = ['', '../x', '/x', 'a//b', 'a/../b', 'nonexistent target', '../名#?role',
            '1...%2F%E5%90%8D%23%3Frole.json', 'literal%25name.pkg', 'a%252Fz',
            '%C0%AF', '%ED%A0%80', '%F4%90%80%80', '%FF', '%C2%80', '%00', '%0A',
            '%2E', '%41', '%7E', '%2f', '%', '%2', '%GG', 'é', 'e\u0301', 'a' * 128, 'a' * 129]
    for _ in range(300):
        text = ''.join(rng.choices(alphabet, k=rng.randrange(1, 22))); rows.append(text)
        rows.append(quote(text, safe='-._~'))
    for _ in range(100): rows.append('a' + ''.join('%%%02X' % rng.randrange(256) for _ in range(rng.randrange(1, 5))) + '.pkg')
    return rows


def main():
    output = ROOT / 'temp/app_safety_tuf_paths'; output.mkdir(exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix='run-', dir=output))
    report = {'status': 'unqualified', 'seed': '0x555249', 'tiers': {}, 'native': 0}
    def checkpoint():
        (directory / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
        (output / 'result.json').write_text((directory / 'result.json').read_text())
    checkpoint()
    native = directory / 'native.exe'; shutil.copy2(ROOT / 'test/test_security_update_pull_gtest.exe', native)
    executable = Path(os.environ.get('LAMBDA_TUF_TEST_EXECUTABLE', ROOT / 'lambda.exe')).resolve()
    report['native_sha256'] = hashlib.sha256(native.read_bytes()).hexdigest()
    report['lambda_sha256'] = hashlib.sha256(executable.read_bytes()).hexdigest()
    rows = corpus(); expected = []
    for text in rows:
        component = quote(text, safe='-._~') if printable(text, 4096) else None
        target = uri_model(text, True)
        wanted = {'role': printable(text, 129), 'wire': uri_model(text, False) is not None, 'target': target is not None}
        result = subprocess.run([str(native), '--uri-profile', text], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
        require_qualification(result.returncode == 0 and json.loads(result.stdout) == wanted,
                              {'text': text, 'expected': wanted, 'native': result.stdout.decode(errors='replace')})
        expected.append(dict(wanted, component=component, encoded_target=target)); report['native'] += 1
    checkpoint()
    for tier in ('interp', 'jit'):
        passed = 0
        for first in range(0, len(rows), 48):
            (directory / 'request.json').write_text(json.dumps([{'id': first + i, 'text': text} for i, text in enumerate(rows[first:first + 48])]))
            reply = directory / 'reply.json'; reply.unlink(missing_ok=True)
            result = subprocess.run([str(executable), '--no-log', 'run', str(ROOT / 'test/safety/tuf/_path_probe.ls')], cwd=directory,
                env=dict(os.environ, LAMBDA_HOME=str(ROOT / 'lmd'), LAMBDA_EXEC_BACKEND=tier),
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
            (directory / f'{tier}-{first}.log').write_bytes(result.stdout + result.stderr)
            require_qualification(result.returncode == 0 and reply.exists(), {'tier': tier, 'first': first, 'stderr': result.stderr.decode(errors='replace')})
            actual = json.loads(reply.read_bytes()); require_qualification(len(actual) == len(rows[first:first + 48]))
            for i, row in enumerate(actual):
                require_qualification(isinstance(row, dict) and row.get('id') == first + i and
                    all(row.get(key) == value for key, value in expected[first + i].items()),
                    {'text': rows[first + i], 'expected': expected[first + i], 'actual': row})
                passed += 1
            report['tiers'][tier] = passed; checkpoint()
    report.update(status='uri_policy_differential_component_passed', total=len(rows))
    checkpoint(); print(json.dumps({'inputs': len(rows), 'tiers': report['tiers'], 'native': report['native'], 'report': str(output / 'result.json')}))


if __name__ == '__main__':
    main()
