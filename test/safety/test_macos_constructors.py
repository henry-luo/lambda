#!/usr/bin/env python3
"""Full-worker constructor acquisitions must be confined before native startup."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import socket
import subprocess
import tempfile

from macos_fixture import ROOT, observe_bootstrap, require_qualification, stage_bundle


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-build', action='store_true')
    options = parser.parse_args()
    if platform.system() != 'Darwin':
        raise SystemExit('macOS qualification only')
    output = ROOT / 'temp/app_safety_macos_constructors'; output.mkdir(parents=True, exist_ok=True)
    report = {'status': 'unqualified', 'platform': platform.platform(), 'cases': [], 'M2_complete': False}
    def save():
        (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    save()
    if not options.no_build:
        with (output / 'build.log').open('w') as log:
            subprocess.run(['make', '-C', 'build_safety_probe_worker/premake', 'config=release_native',
                'lambda-safety-probe-worker', '-j4'], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=900)
    directory = Path(tempfile.mkdtemp(prefix='run-', dir=output))
    probe = stage_bundle(directory, 'ConstructorWorker', ROOT / 'lambda-safety-probe-worker.exe', '-', False)
    report['artifacts'] = str(directory.relative_to(ROOT))
    report['worker_sha256'] = hashlib.sha256(probe.read_bytes()).hexdigest()
    private = directory / 'private.data'; private.write_bytes(b'host-private\n')
    entry = directory / 'entry.ls'; entry.write_text('null\n'); entry.with_suffix('.txt').write_text('null\n')
    for mode in ['constructor-shared', 'constructor-private']:
        first, second = socket.socketpair()
        case, stdout = observe_bootstrap([str(probe), str(second.fileno()), '1', str(entry), str(private),
            str(private), str(directory / 'raw-new'), '9', mode], directory, first, second,
            inherited=(second.fileno(),))
        case['label'] = mode
        case['native'] = json.loads(stdout); report['cases'].append(case); save()
        require_qualification(case['native']['unexpected_fds'] == 0, case)
    require_qualification(private.read_bytes() == b'host-private\n')
    for case in report['cases']:
        native = case['native']['constructor']
        require_qualification(native['sandboxed'] and native['open_errno'] != 0 and not native['available'], case)
    report['status'] = 'passed'; report['passed'] = len(report['cases']); save()
    print(json.dumps({'passed': report['passed'], 'report': str(output / 'result.json'), 'M2_complete': False}))


if __name__ == '__main__':
    main()
