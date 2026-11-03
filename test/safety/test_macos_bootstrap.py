#!/usr/bin/env python3
"""Require the real signed worker to reject leaked handles before broker IO."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import socket
import subprocess
import tempfile

from macos_fixture import ROOT, observe_bootstrap, require_qualification, stage_bundle
from test_macos_worker import build


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-build', action='store_true')
    options = parser.parse_args()
    if platform.system() != 'Darwin':
        raise SystemExit('macOS qualification only')
    output = ROOT / 'temp/app_safety_macos_bootstrap'
    output.mkdir(parents=True, exist_ok=True)
    report = {'status': 'unqualified', 'platform': platform.platform(), 'cases': [], 'M2_complete': False}
    def save():
        (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    save()
    if not options.no_build:
        build(output)
    directory = Path(tempfile.mkdtemp(prefix='run-', dir=output))
    worker = stage_bundle(directory, 'BootstrapWorker', ROOT / 'lambda-safety-worker.exe', '-', False)
    probe = stage_bundle(directory, 'BootstrapThreadWorker', ROOT / 'lambda-safety-probe-worker.exe', '-', False)
    report['artifacts'] = str(directory.relative_to(ROOT))
    report['worker_sha256'] = hashlib.sha256(worker.read_bytes()).hexdigest()
    report['thread_probe_sha256'] = hashlib.sha256(probe.read_bytes()).hexdigest()
    entry = directory / 'entry.ls'; entry.write_text('null\n')
    entry.with_suffix('.txt').write_text('null\n')
    private = directory / 'private.data'; private.write_bytes(b'host-private\n')
    fifo = directory / 'named-pipe'; os.mkfifo(fifo, 0o600)
    for label in ('allowed-handles', 'leaked-file', 'high-file', 'stdin-file', 'stdout-file', 'stderr-file',
                  'stdout-fifo', 'stdout-read-pipe', 'threaded'):
        first, second = socket.socketpair()
        high = -1; fifo_fd = -1; fifo_reader = -1; read_pipe = None
        with private.open('r+b') as held:
            try:
                if label == 'high-file':
                    high = fcntl.fcntl(held.fileno(), fcntl.F_DUPFD, 256)
                if label == 'stdout-fifo':
                    fifo_reader = os.open(fifo, os.O_RDONLY | os.O_NONBLOCK)
                    fifo_fd = os.open(fifo, os.O_WRONLY | os.O_NONBLOCK)
                if label == 'stdout-read-pipe':
                    read_pipe = os.pipe()
                leak = held.fileno() if label == 'leaked-file' else high
                descriptors = (second.fileno(), leak) if leak >= 0 else (second.fileno(),)
                args = [str(worker), str(second.fileno()), '1', str(entry)]
                if label == 'threaded':
                    args[0] = str(probe)
                    args += [str(private), str(private), str(directory / 'raw-new'), '9', 'threaded']
                case, stdout = observe_bootstrap(args, directory, first, second, inherited=descriptors,
                    stdin=held if label == 'stdin-file' else subprocess.DEVNULL,
                    stdout=held if label == 'stdout-file' else fifo_fd if fifo_fd >= 0 else
                        read_pipe[0] if read_pipe else subprocess.PIPE,
                    stderr=held if label == 'stderr-file' else subprocess.PIPE)
                case['label'] = label
                report['cases'].append(case); save()
                if label == 'allowed-handles':
                    require_qualification(bool(case['broker_request_bytes']), case)
                else:
                    require_qualification(not case['broker_request_bytes'] and case['exit'] == 2, case)
                    if label == 'threaded':
                        case['native'] = json.loads(stdout)
                        require_qualification(case['native']['thread_started'] and not case['native']['unexpected_fds'], case)
                    else:
                        require_qualification(not stdout, case)
            finally:
                if high >= 0:
                    os.close(high)
                if fifo_fd >= 0:
                    os.close(fifo_fd)
                if fifo_reader >= 0:
                    os.close(fifo_reader)
                if read_pipe:
                    for descriptor in read_pipe:
                        os.close(descriptor)
                first.close(); second.close()
    require_qualification(private.read_bytes() == b'host-private\n' and not (directory / 'raw-new').exists())
    report['status'] = 'passed'; report['passed'] = len(report['cases']); save()
    print(json.dumps({'passed': report['passed'], 'report': str(output / 'result.json'), 'M2_complete': False}))


if __name__ == '__main__':
    main()
