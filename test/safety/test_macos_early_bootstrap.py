#!/usr/bin/env python3
"""Qualify early constructor policy separately from hardened publisher admission."""
import argparse
import errno
import hashlib
import json
from pathlib import Path
import platform
import shutil
import socket
import subprocess
import tempfile

from macos_fixture import ROOT, observe_bootstrap, require_qualification, run, stage_bundle
from test_macos_worker import exercise
from test_macos_supervisor import exercise as supervise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-build', action='store_true')
    options = parser.parse_args()
    if platform.system() != 'Darwin':
        raise SystemExit('macOS qualification only')
    output = ROOT / 'temp/app_safety_macos_early_bootstrap'; output.mkdir(parents=True, exist_ok=True)
    report = {'status': 'unqualified', 'cases': [], 'M2_complete': False,
              'publisher_qualification': 'identity setup skipped by user', 'default_rollout': False}
    def save():
        (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    save()
    if not options.no_build:
        with (output / 'build.log').open('w') as log:
            subprocess.run(['make', 'build-safety-early-worker'], cwd=ROOT, stdout=log,
                           stderr=subprocess.STDOUT, check=True, timeout=900)
    directory = Path(tempfile.mkdtemp(prefix='run-', dir=output))
    loader = ROOT / 'lambda-safety-bootstrap.exe'
    dependencies = run(['otool', '-L', str(loader)])
    report['loader_dependencies'] = dependencies
    names = [line.strip().split(' (')[0] for line in dependencies.splitlines()[1:] if line.strip()]
    require_qualification(names == ['/usr/lib/libSystem.B.dylib'], dependencies)
    runtime = ROOT / 'LambdaSafetyRuntime.dylib'
    symbols = run(['nm', '-j', str(runtime)])
    management = ('security_file_broker_', 'security_grant_store_', 'security_admission_',
                  'security_private_storage_', 'security_policy_')
    require_qualification(not any(name in symbols for name in management))
    require_qualification('_lambda_safety_worker_entry_v1' in run(['nm', '-gU', str(runtime)]))
    report['runtime_management_symbols_absent'] = True
    report['runtime_sha256'] = hashlib.sha256(runtime.read_bytes()).hexdigest()
    report['artifacts'] = str(directory.relative_to(ROOT)); save()
    private = directory / 'private.data'; private.write_bytes(b'host-private\n')
    for kind, definition in [('shared', '1'), ('private', '2')]:
        module = directory / (kind + '.dylib')
        run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread', '-dynamiclib',
             '-DSAFETY_CONSTRUCTOR_MODULE=' + definition, 'test/safety/constructor_module.c',
             'lib/process_sandbox.c', '-o', str(module)])
        for hardened in [False, True]:
            worker = stage_bundle(directory, kind + ('Hardened' if hardened else 'Control'),
                loader, '-', False, runtime_image=module, hardened_runtime=hardened)
            first, second = socket.socketpair()
            case, stdout = observe_bootstrap([str(worker), str(second.fileno()), '1', str(private)],
                directory, first, second, inherited=(second.fileno(),))
            case.update({'kind': kind, 'hardened_runtime': hardened,
                'loader_sha256': hashlib.sha256(worker.read_bytes()).hexdigest()})
            report['cases'].append(case); save()
            require_qualification(not case['broker_request_bytes'], case)
            if hardened:
                require_qualification(case['exit'] == 2 and not stdout, case)
            else:
                case['constructor'] = json.loads(stdout)
                value = case['constructor']
                require_qualification(case['exit'] == 0 and value['sandboxed'] and not value['available'] and
                    value['open_errno'] in {errno.EPERM, errno.EACCES}, case)
    # This control intentionally lacks hardened signing and is never admitted
    # by the production host; it proves the runtime/IPC composition only.
    worker = stage_bundle(directory, 'RuntimeCompositionControl', loader, '-', False,
        runtime_image=ROOT / 'LambdaSafetyRuntime.dylib', hardened_runtime=False)
    shutil.copy2(ROOT / 'test/test_security_channel_gtest.exe', directory / 'host-channel.exe')
    report['runtime_composition_controls'] = []
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0)); listener.listen(4)
        for mode in ['create', 'deny', 'approve', 'outside', 'protected', 'path-task', 'unsupported']:
            value = exercise(directory, worker, 'early-runtime-' + mode, listener.getsockname()[1],
                             True, mode, native_probe=False)
            report['runtime_composition_controls'].append(value); save()
    for label, mutation, hardened in [('hardened-handoff', None, True), ('tampered-runtime', 'runtime', True),
                                      ('unhardened-admission', None, False)]:
        value = supervise(directory, label, loader, runtime_image=runtime, mutation=mutation, hardened_runtime=hardened)
        report['cases'].append({'label': label, 'supervisor': value}); save()
        require_qualification(not value['host']['success'] and value['host']['requests'] == 0, value)
        if mutation or not hardened:
            require_qualification(not value['host']['verified'] and not value['host']['launched'], value)
        else:
            require_qualification(value['host']['verified'] and value['host']['launched'], value)
    require_qualification(private.read_bytes() == b'host-private\n')
    report['component_checks_passed'] = len(report['cases']) + len(report['runtime_composition_controls'])
    report['remaining'] = ['hardened same-publisher module loading and sealed-package admission',
                           'successful hardened runtime workflow and full M2 gates']
    save()
    print(json.dumps({'component_checks_passed': report['component_checks_passed'],
                      'status': report['status'], 'report': str(output / 'result.json'), 'M2_complete': False}))


if __name__ == '__main__':
    main()
