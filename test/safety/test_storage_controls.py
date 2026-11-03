#!/usr/bin/env python3
"""Trusted host storage controls in isolated profiles; no user data removal."""
import argparse
import errno
import hashlib
import json
import os
import select
from pathlib import Path
import subprocess
import tempfile

from macos_fixture import ROOT, require_qualification


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--no-build', action='store_true')
    options = parser.parse_args()
    output = ROOT / 'temp/app_safety_storage_controls'
    output.mkdir(parents=True, exist_ok=True)
    rows = []
    report = {'status': 'unqualified', 'commands': rows, 'real_settings_opened': False}

    def checkpoint():
        (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')

    checkpoint()
    if not options.no_build:
        with (output / 'build.log').open('w') as log:
            for command in (['make', 'build-safety-host'],
                            ['make', '-C', 'build/premake', 'config=release_native',
                             'lambda-safety-host-fixture', 'test_security_admission_gtest', '-j4']):
                subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=900, check=True)
    directory = Path(tempfile.mkdtemp(prefix='run-', dir=output))
    fixture = ROOT / 'lambda-safety-host-fixture.exe'

    def seed(name, pending=False):
        home = directory / name
        home.mkdir(mode=0o700)
        command = [str(ROOT / 'test/test_security_admission_gtest.exe'), '--seed-private-fixture', str(home)]
        if pending:
            command.append('--pending-reset')
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=20, check=True)
        return home, json.loads(result.stdout)

    def invoke(home, *args, code=0):
        result = subprocess.run([str(fixture), '--fixture-storage', str(home), *args], cwd=ROOT,
            env=dict(os.environ, HOME='/untrusted', LAMBDA_HOME='/untrusted'), capture_output=True, text=True, timeout=20)
        rows.append({'command': list(args), 'exit': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr})
        checkpoint()
        require_qualification(result.returncode == code, rows[-1])
        text = result.stdout or result.stderr
        return json.loads(text) if text.startswith('{') else None

    def grants(home):
        return {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                for p in (home / '.lambda/setting').iterdir() if p.is_file()}

    home, initial = seed('ordinary')
    principal = initial['principal']
    before = grants(home)
    status = invoke(home, 'status', 'script', principal)
    require_qualification(not status['live_sessions'] and not status['reset_pending'])
    require_qualification([u['bytes'] for u in status['usage']] == [2, 5, 0])
    for args in [('reset-data', 'script', principal),
                 ('reset-data', 'script', principal, '--yes'),
                 ('status', 'script', 'g' * 64), ('status', 'script', principal, 'extra'),
                 ('status', 'unknown', principal)]:
        invoke(home, *args, code=2)
    # The store keys by principal digest and rejects a kind mismatch in the
    # existing record, before any private-storage path can be selected.
    require_qualification(invoke(home, 'status', 'app', principal, code=2)['system_error'] == errno.EACCES)
    status = invoke(home, 'status', 'script', principal)
    require_qualification(status['usage'][0]['bytes'] == 2 and not status['reset_pending'])
    status = invoke(home, 'reset-data', 'script', principal, '--confirm-data-loss')
    require_qualification(not status['reset_pending'] and status['usage'][0]['entries'] == 0)
    require_qualification(status['usage'][1]['bytes'] == 5 and grants(home) == before)

    home, pending = seed('interrupted', True)
    before = grants(home)
    status = invoke(home, 'status', 'script', principal)
    require_qualification(status['reset_pending'] and status['reset_id'] == pending['reset_id'])
    require_qualification(status['usage'][0]['entries'] == 3)
    require_qualification(invoke(home, 'reset-data', 'script', principal, '--confirm-data-loss', code=2)['system_error'] == errno.EALREADY)
    invoke(home, 'resume-reset', 'script', principal, pending['reset_id'], code=2)
    require_qualification(invoke(home, 'resume-reset', 'script', principal, '0' * 32,
        '--confirm-data-loss', code=2)['system_error'] == errno.EACCES)
    status = invoke(home, 'resume-reset', 'script', principal, pending['reset_id'], '--confirm-data-loss')
    require_qualification(not status['reset_pending'] and status['usage'][0]['entries'] == 0)
    require_qualification(status['usage'][1]['bytes'] == 5 and grants(home) == before)
    require_qualification(invoke(home, 'resume-reset', 'script', principal, pending['reset_id'],
        '--confirm-data-loss', code=2)['system_error'] == errno.ENOENT)

    home, _ = seed('quotas')
    before = grants(home)
    status = invoke(home, 'status', 'script', principal)
    require_qualification(status['quota_generation'] == 0 and not status['custom_quota'])
    status = invoke(home, 'quota', 'script', principal, '0', 'data', '1', '1')
    require_qualification(status['quota_generation'] == 1 and status['custom_quota'])
    require_qualification(status['usage'][0]['over_limit'] and status['usage'][0]['bytes'] == 2)
    require_qualification(status['usage'][0]['max_bytes'] == 1 and status['usage'][0]['max_entries'] == 1)
    require_qualification({k: v for k, v in grants(home).items() if k.endswith('.grant')} ==
                          {k: v for k, v in before.items() if k.endswith('.grant')})
    saved = grants(home)
    require_qualification(invoke(home, 'quota-defaults', 'script', principal, '0', code=2)['system_error'] == errno.ESTALE)
    for args in [('quota', 'script', principal, '1', 'unknown', '4', '4'),
                 ('quota', 'script', principal, '1', 'temp', '4', '2'),
                 ('quota', 'script', principal, '1', 'data', '0', '1'),
                 ('quota', 'script', principal, '1', 'data', '4', '4097'),
                 ('quota', 'script', principal, '1', 'data', '18446744073709551616', '1'),
                 ('quota', 'script', principal, '1', 'data', '-1', '1'),
                 ('quota', 'script', principal, '+1', 'data', '4', '1'),
                 ('quota-defaults', 'script', principal, ' 1')]:
        invoke(home, *args, code=2)
        require_qualification(grants(home) == saved)
    status = invoke(home, 'reset-data', 'script', principal, '--confirm-data-loss')
    require_qualification(status['quota_generation'] == 1 and status['custom_quota'] and grants(home) == saved)
    status = invoke(home, 'quota-defaults', 'script', principal, '1')
    require_qualification(status['quota_generation'] == 2 and not status['custom_quota'])
    require_qualification(status['usage'][0]['max_bytes'] == 64 * 1024 * 1024 and status['usage'][1]['bytes'] == 5)
    require_qualification(invoke(home, 'quota', 'script', principal, '1', 'data', '8', '1', code=2)['system_error'] == errno.ESTALE)

    invoke(home, 'quota', 'script', principal, '2', 'data', '8', '4096')
    data = next((home / '.lambda/app-data').glob('s-*')) / 'data'
    for name in [*(f'file-{i}' for i in range(4096)), '.lambda-stage-' + '0' * 32]:
        descriptor = os.open(data / name, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        os.close(descriptor)
    status = invoke(home, 'status', 'script', principal)
    require_qualification(status['usage'][0]['entries'] == 4097 and status['usage'][0]['over_limit'])
    saved = grants(home)
    status = invoke(home, 'reset-data', 'script', principal, '--confirm-data-loss')
    require_qualification(status['usage'][0]['entries'] == 0 and not status['reset_pending'] and grants(home) == saved)

    home = directory / 'live-quota'
    home.mkdir(mode=0o700)
    child = subprocess.Popen([str(ROOT / 'test/test_security_admission_gtest.exe'), '--seed-private-fixture',
                              str(home), '--hold-storage'], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        require_qualification(select.select([child.stdout], [], [], 10)[0], 'live fixture did not become ready')
        ready = json.loads(child.stdout.readline())
        require_qualification(ready['principal'] == principal)
        require_qualification(invoke(home, 'quota', 'script', principal, '0', 'data', '4', '1', code=2)['system_error'] == errno.EBUSY)
        require_qualification(invoke(home, 'status', 'script', principal)['quota_generation'] == 0)
        child.kill(); child.communicate(timeout=5)
        status = invoke(home, 'quota', 'script', principal, '0', 'data', '4', '1')
        require_qualification(status['quota_generation'] == 1 and not status['live_sessions'] and status['abandoned_sessions'] == 1)
    finally:
        if child.poll() is None:
            child.kill(); child.communicate(timeout=5)
    for args in [('storage', 'status', 'script', principal), ('--fixture-storage', str(home), 'status', 'script', principal)]:
        result = subprocess.run([str(ROOT / 'lambda-safety-host.exe'), *args], cwd=ROOT, capture_output=True, timeout=10)
        require_qualification(result.returncode == 2)
        require_qualification(b'publisher signature required' in result.stderr if args[0] == 'storage' else b'Usage:' in result.stderr)
    report.update(status='passed', passed=len(rows) + 2, production_signature_gate=True,
        artifacts={name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in
            ['lambda-safety-host.exe', 'lambda-safety-host-fixture.exe', 'test/test_security_admission_gtest.exe']})
    (directory / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    checkpoint()
    print(json.dumps({'passed': report['passed'], 'report': str(output / 'result.json')}))


if __name__ == '__main__':
    main()
