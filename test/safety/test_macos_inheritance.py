#!/usr/bin/env python3
"""Measure Mach capabilities at the actual suspended-worker launch boundary."""
import argparse
import errno
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import tempfile

from macos_fixture import require_qualification, ROOT, run, stage_bundle


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-build', action='store_true')
    options = parser.parse_args()
    if platform.system() != 'Darwin':
        raise SystemExit('macOS qualification only')
    output = ROOT / 'temp/app_safety_macos_inheritance'
    output.mkdir(parents=True, exist_ok=True)
    report = {'status': 'unqualified', 'platform': platform.platform(), 'cases': [], 'M2_complete': False}
    def save():
        (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    save()
    if not options.no_build:
        with (output / 'build.log').open('w') as log:
            subprocess.run(['make', '-C', 'build/premake', 'config=release_native',
                'lambda-safety-supervisor-probe', '-j4'], cwd=ROOT, stdout=log,
                stderr=subprocess.STDOUT, check=True, timeout=900)
    directory = Path(tempfile.mkdtemp(prefix='run-', dir=output))
    probe = ROOT / 'lambda-safety-supervisor-probe.exe'
    worker = stage_bundle(directory, 'MachInheritanceWorker', probe, '-', False,
                          identifier='org.lambda-script.safety-worker')
    report['artifacts'] = str(directory.relative_to(ROOT))
    report['parent_sha256'] = hashlib.sha256(probe.read_bytes()).hexdigest()
    report['worker_sha256'] = hashlib.sha256(worker.read_bytes()).hexdigest()
    for command, sandboxed in [('mach-control', False), ('mach-control-confined', True)]:
        control = run([str(probe), command, str(worker)])
        rows = [json.loads(line) for line in control.splitlines()]
        require_qualification(len(rows) == 2, rows)
        report['cases'].append({'label': command, 'child': rows[0], 'parent': rows[1]}); save()
        require_qualification(rows[0]['before_registered'] == rows[0]['registered'] == rows[0]['sent'] == 3, rows)
        require_qualification(rows[0]['before_exceptions'] == rows[0]['exceptions'] == 1, rows)
        require_qualification(rows[0]['before_bootstrap'] == rows[0]['bootstrap'] == 1, rows)
        require_qualification(rows[0]['sandboxed'] == sandboxed and rows[0]['memory_map_status'] == 0, rows)
        require_qualification(rows[0]['memory_written'] and rows[1]['memory_changed'], rows)
        require_qualification(rows[1]['messages'] == 3, rows)
        require_qualification((rows[0]['trace_errno'] in {errno.EPERM, errno.EACCES}) if sandboxed else rows[0]['trace_errno'] == 0, rows)
        require_qualification((rows[0]['parent_name_status'] != 0) == sandboxed, rows)
        require_qualification((rows[0]['self_control_status'] != 0) == sandboxed, rows)
    entry = directory / 'mach-ports.ls'; entry.write_text('null\n')
    entry.with_suffix('.txt').write_text('null\n')
    value = json.loads(run([str(probe), 'mach-monitor', str(worker.parents[2]),
        report['worker_sha256'], str(entry), str(directory)]))
    report['cases'].append({'label': 'confined-launch', 'result': value}); save()
    require_qualification(value['success'] and value['exit'] == value['stop'] == value['messages'] == 0, value)
    state = value['mach']
    for key in ['registered', 'exceptions', 'bootstrap']:
        require_qualification(state['before_' + key] == state[key] == 0, (key, value))
    require_qualification(state['sent'] == 0 and state['registered_status'] == 0, value)
    require_qualification(not state['memory_written'] and not value['memory_changed'], value)
    require_qualification(state['trace_errno'] in {errno.EPERM, errno.EACCES} and state['parent_name_status'] != 0, value)
    require_qualification(state['self_control_status'] != 0, value)
    report['user_capabilities_denied'] = True
    report['ptrace_denials_passed'] = True
    report['self_task_control_entry_denied'] = True
    report['task_control_positive_control_available'] = report['cases'][0]['child']['parent_control_status'] == 0
    report['remaining'] = ['TASK_ACCESS_PORT remains live; OS task/debug qualification is incomplete']
    save()
    require_qualification(state['before_access'] == state['access'] == 0, value)
    require_qualification(report['task_control_positive_control_available'], 'task-control positive control is unavailable')
    require_qualification(state['parent_control_status'] != 0, value)
    report['remaining'] = []
    report['status'] = 'passed'; report['passed'] = len(report['cases']); save()
    print(json.dumps({'passed': report['passed'], 'report': str(output / 'result.json'), 'M2_complete': False}))


if __name__ == '__main__':
    main()
