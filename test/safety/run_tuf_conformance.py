#!/usr/bin/env python3
"""Pinned official qualification; no test exclusions or production IO overrides."""
from datetime import UTC, datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from macos_fixture import require_qualification

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    pin = json.loads((ROOT / 'test/safety/tuf/conformance.json').read_text())
    upstream = ROOT / 'temp/tuf-conformance-upstream'
    clock = ROOT / 'temp/libfaketime-upstream'
    python = ROOT / 'temp/tuf-conformance-env/bin/python'
    tools = ROOT / 'temp/tuf-conformance-tools'
    output = ROOT / 'temp/app_safety_tuf_conformance'; output.mkdir(exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix='run-', dir=output))
    report = {'status': 'unqualified', 'full_conformance': False, 'default_rollout': False,
              'delivery_available': False, 'run': str(directory.relative_to(ROOT))}
    def checkpoint():
        (directory / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
        (output / 'result.json').write_text((directory / 'result.json').read_text())
    checkpoint()
    for checkout, expected in [(upstream, pin['suite_commit']), (clock, pin['clock_commit'])]:
        actual = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], text=True).strip()
        dirty = subprocess.check_output(['git', '-C', str(checkout), 'status', '--porcelain'], text=True)
        require_qualification(actual == expected and not dirty, {'repository': str(checkout), 'commit': actual, 'dirty': dirty})
    binary = directory / 'lambda.exe'; shutil.copy2(ROOT / 'lambda.exe', binary)
    closure = ['security/tuf.ls', 'security/tuf/json.ls', 'security/tuf/metadata.ls',
               'security/tuf/delegation.ls', 'security/tuf/update.ls', 'security/tuf/path.ls', 'io/utf8.ls']
    source_hashes = {}
    for name in closure:
        source = ROOT / 'lmd/package' / name; target = directory / 'lmd/package' / name
        target.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(source, target)
        source_hashes[str(source.relative_to(ROOT))] = digest(target)
    step = directory / '_conformance_step.ls'; source = ROOT / 'test/safety/tuf/_conformance_step.ls'
    shutil.copy2(source, step); source_hashes[str(source.relative_to(ROOT))] = digest(step)
    client = ROOT / 'test/safety/tuf/conformance_client.py'; client_before = digest(client)
    temporary = directory / 'tmp'; temporary.mkdir()
    environment = dict(os.environ, PATH=str(tools / 'bin') + os.pathsep + os.environ.get('PATH', ''),
        TMPDIR=str(temporary), TZ='UTC', PYTHONDONTWRITEBYTECODE='1', FAKETIME_DONT_FAKE_MONOTONIC='1',
        LAMBDA_TUF_TEST_EXECUTABLE=str(binary), LAMBDA_TUF_TEST_HOME=str(directory / 'lmd'), LAMBDA_TUF_TEST_STEP=str(step))
    # Prove the real interposition clock works before running expiry tests. No
    # system time is changed, and no clock shim/constant substitutes for it.
    probe = subprocess.run([str(tools / 'bin/faketime'), '2030-01-01 00:00:00+00:00', str(python), '-c',
        'from datetime import datetime,UTC; assert datetime.now(UTC).year == 2030'], env=environment,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)
    require_qualification(probe.returncode == 0, probe.stderr.decode(errors='replace'))
    report.update(suite_commit=pin['suite_commit'], clock_commit=pin['clock_commit'], source_sha256=source_hashes,
                  lambda_sha256=digest(binary), client_sha256=client_before,
                  clock_sha256=digest(tools / 'lib/faketime/libfaketime.1.dylib'))
    checkpoint()
    results = directory / 'pytest.json'
    command = [str(python), '-m', 'pytest', str(upstream / 'tuf_conformance'), '--entrypoint', f'{python} {client}',
        '--basetemp', str(directory / 'pytest-temp'), '--json-report', '--json-report-file', str(results), '--tb=short', '-q']
    with (directory / 'pytest.log').open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=900)
    require_qualification(digest(client) == client_before, 'client changed during qualification')
    data = json.loads(results.read_bytes()); summary = data['summary']
    report.update(summary=summary, exit=result.returncode,
        failures=[row['nodeid'] for row in data['tests'] if row['outcome'] != 'passed'],
        completed_utc=datetime.now(UTC).isoformat(), pytest_report=str(results.relative_to(ROOT)),
        log=str((directory / 'pytest.log').relative_to(ROOT)))
    complete = result.returncode == 0 and summary.get('collected') == pin['collected_tests'] and summary.get('passed') == pin['collected_tests']
    report.update(full_conformance=complete, status='full_conformance_passed' if complete else 'conformance_incomplete')
    checkpoint(); print(json.dumps({'status': report['status'], 'summary': summary, 'report': str(output / 'result.json')}))
    return 0 if complete else 1


if __name__ == '__main__':
    raise SystemExit(main())
