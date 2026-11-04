#!/usr/bin/env python3
"""Adversarial offline Lambda package decoder; owns all bytes, bundles and IO."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import tempfile
import time
import warnings
import zipfile

from macos_fixture import require_qualification, stage_bundle

ROOT = Path(__file__).resolve().parents[2]
FIXED, MEMBER, BODY = 4173, 16 * 1024 * 1024, 64 * 1024 * 1024
INIT, RESPONSE, READY, ENTRY, END, ACK = 8, 5, 7, 9, 10, 11


def zip_bytes(rows, method=zipfile.ZIP_STORED):
    output = io.BytesIO()
    with warnings.catch_warnings(), zipfile.ZipFile(output, 'w', compression=method) as archive:
        warnings.simplefilter('ignore', UserWarning)
        for filename, content, mode in rows:
            info = zipfile.ZipInfo(filename, (2026, 1, 1, 0, 0, 0))
            info.create_system = 3; info.external_attr = mode << 16
            info.compress_type = method; archive.writestr(info, content)
    return output.getvalue()


def single(filename, content=b'data', mode=0o100644):
    return zip_bytes([(filename, content, mode)])


def receive_exact(channel, length):
    parts = []
    while length:
        block = channel.recv(min(length, 65536))
        if not block: raise EOFError('worker closed the channel')
        parts.append(block); length -= len(block)
    return b''.join(parts)


def receive(channel, sequence):
    header = receive_exact(channel, 36)
    magic, version, kind, session, serial, length = struct.unpack('<8sIIQQI', header)
    require_qualification((magic, version, session, serial) == (b'LMTUFIP1', 1, 29, sequence), header.hex())
    cap = FIXED + MEMBER if kind == ENTRY else FIXED
    require_qualification(kind in (READY, ENTRY, END) and length <= cap, {'kind': kind, 'length': length})
    body = receive_exact(channel, length)
    require_qualification(len(body) >= FIXED, len(body))
    status, directory, limit = struct.unpack_from('<IIQ', body)
    path = body[16:4112].split(b'\0', 1)[0].decode('utf8')
    count, size = struct.unpack_from('<II', body, 4133)
    require_qualification(count == 0 and size == len(body) - FIXED, {'count': count, 'size': size})
    return kind, {'status': status, 'directory': directory, 'limit': limit, 'path': path,
                  'digest': body[4141:4173].hex(), 'bytes': body[FIXED:]}


def send(channel, kind, sequence, content=b'', status=0, limit=0):
    body = bytearray(FIXED)
    struct.pack_into('<IIQ', body, 0, status, 0, limit)
    struct.pack_into('<II', body, 4133, 0, len(content))
    channel.sendall(struct.pack('<8sIIQQI', b'LMTUFIP1', 1, kind, 29, sequence, FIXED + len(content)))
    channel.sendall(body)
    if content: channel.sendall(content)


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--no-build', action='store_true')
    args = parser.parse_args()
    base = ROOT / 'temp/app_safety_package_stage'; base.mkdir(exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix='run-', dir=base))
    environment = dict(os.environ, TMPDIR=str(ROOT / 'temp/app-safety-build-tmp'), PYTHONDONTWRITEBYTECODE='1')
    if not args.no_build:
        with (directory / 'build.log').open('w') as log:
            for command in (['make', 'generate-premake'], ['premake5', 'gmake', '--file=premake5.mac.lua'],
                            ['make', '-C', 'build/premake', 'config=release_native', 'lambda-safety-bootstrap', '-j4'],
                            ['make', 'build-update-verifier']):
                subprocess.run(command, cwd=ROOT, env=environment,
                               stdout=log, stderr=subprocess.STDOUT, check=True, timeout=900)
    pinned = directory / 'bin'; pinned.mkdir()
    for filename in ['lambda-safety-bootstrap.exe', 'LambdaUpdateRuntime.dylib']:
        shutil.copy2(ROOT / filename, pinned / filename)
    worker = stage_bundle(directory, 'PackageStageControl', pinned / 'lambda-safety-bootstrap.exe', '-', False,
        runtime_image=pinned / 'LambdaUpdateRuntime.dylib', hardened_runtime=False)
    report = {'status': 'unqualified', 'cases': [], 'delivery_available': False, 'default_rollout': False,
              'fixture_bundle': 'unhardened_ad_hoc_composition_control',
              'protected_publication': False, 'remote_connections': False,
              'artifacts': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in pinned.iterdir()},
              'launched_fixture_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in
                  (worker, worker.parent.parent / 'Frameworks/LambdaSafetyRuntime.dylib')}}
    def checkpoint():
        (directory / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
        (base / 'result.json').write_text((directory / 'result.json').read_text())
    checkpoint()
    def check(label, data, expected=None, *, fail_before_entries=False, operation='normal'):
        first, second = socket.socketpair(); child = None; rows = []; terminal = None
        start = time.monotonic(); caught = None
        (directory / f'archive-{len(report["cases"])}.bin').write_bytes(data)
        try:
            child = subprocess.Popen([str(worker), str(second.fileno()), '29', 'lambda-package-stage-v1'],
                cwd=directory, env={'PATH': '/usr/bin:/bin', 'LANG': 'en_US.UTF-8'},
                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                pass_fds=(second.fileno(),), close_fds=True, start_new_session=True)
            second.close(); first.settimeout(15)
            kind, ready = receive(first, 1); require_qualification(kind == READY, ready)
            if operation == 'unexpected': send(first, RESPONSE, 1, status=1)
            elif operation == 'oversize':
                first.sendall(struct.pack('<8sIIQQI', b'LMTUFIP1', 1, INIT, 29, 1, FIXED + BODY + 1))
            else: send(first, INIT, 1, data, limit=10000)
            sequence = 2
            while True:
                kind, row = receive(first, sequence)
                if kind == END: terminal = row; break
                require_qualification(kind == ENTRY and row['limit'] == 0, row)
                rows.append(row)
                if operation == 'close_after_entry': first.close(); break
                send(first, ACK, sequence - 1 if operation == 'replay_ack' else sequence,
                     content=b'x' if operation == 'ack_payload' else b'', status=2 if operation == 'reject_ack' else 1)
                sequence += 1
        except (EOFError, ConnectionResetError, BrokenPipeError, OSError) as error:
            caught = type(error).__name__
        finally:
            first.close(); second.close()
            if child:
                try: out, err = child.communicate(timeout=5)
                except subprocess.TimeoutExpired:
                    child.kill(); out, err = child.communicate(timeout=5)
                    raise AssertionError({'case': label, 'error': 'worker did not exit within budget'})
                (directory / (str(len(report['cases'])) + '.log')).write_bytes(out + err)
        complete = terminal is not None and child.returncode == 0
        row = {'name': label, 'passed': False, 'complete': complete, 'exit': child.returncode,
               'entries': len(rows), 'milliseconds': round((time.monotonic() - start) * 1000), 'transport_end': caught,
               'archive_sha256': hashlib.sha256(data).hexdigest()}
        report['cases'].append(row); checkpoint()
        require_qualification(complete == (expected is not None), row)
        if expected is not None:
            actual = {item['path']: (item['directory'] == 1, item['status'] == 1, item['bytes']) for item in rows}
            require_qualification(len(actual) == len(rows) and actual == expected, {'case': label, 'actual_paths': list(actual)})
            require_qualification(terminal['status'] == len(rows) and terminal['limit'] == sum(len(item['bytes']) for item in rows)
                and terminal['digest'] == hashlib.sha256(data).hexdigest(), row)
        else:
            require_qualification(child.returncode == 3 and terminal is None, row)
            if fail_before_entries: require_qualification(not rows, row)
        row['passed'] = True; checkpoint()
    sample = (ROOT / 'test/safety/stage/package.zip').read_bytes()
    expected = {'data': (True, False, b''), 'main.ls': (False, True, b'"hello"\n'),
                'data/message.txt': (False, False, b'package\n'), 'data/empty': (False, False, b'')}
    check('stored package exact bytes and executable intent', sample, expected)
    check('implicit directories and binary bytes', zip_bytes([('a/b', b'\x00\xff', 0o100644)], zipfile.ZIP_DEFLATED),
          {'a': (True, False, b''), 'a/b': (False, False, b'\x00\xff')})
    check('empty archive', zip_bytes([]), {})
    check('literal percent and spaces are native names', single('data/100% ready.txt'),
          {'data': (True, False, b''), 'data/100% ready.txt': (False, False, b'data')})
    check('maximum member boundary', zip_bytes([('large', b'x' * MEMBER, 0o100644)], zipfile.ZIP_DEFLATED),
          {'large': (False, False, b'x' * MEMBER)})
    check('maximum expanded-byte boundary', zip_bytes([(str(i), b'x' * MEMBER, 0o100644) for i in range(4)], zipfile.ZIP_DEFLATED),
          {str(i): (False, False, b'x' * MEMBER) for i in range(4)})
    check('maximum entry-count boundary', zip_bytes([(str(i), b'', 0o100644) for i in range(4096)]),
          {str(i): (False, False, b'') for i in range(4096)})
    for filename in ['../escape', '/absolute', 'C:/drive', 'dir\\escape', 'x:stream', 'NUL.txt', 'CON',
                     'com9.dat', 'lpt1', 'CONIN$', 'conout$.txt', 'a.', 'a ', 'con .txt', '.lambda/grants', 'x/.lambda-stage-secret',
                     'é.txt', 'e\u0301.txt', '\x01file', 'name?query', 'name*glob', 'a' * 256,
                     '/'.join(['d'] * 33), 'd/' * 512 + 'x']:
        check('reject path ' + repr(filename[:45]), single(filename), fail_before_entries=True)
    for label, data in [
        ('case-fold collision', zip_bytes([('Data/a', b'a', 0o100644), ('data/b', b'b', 0o100644)])),
        ('duplicate entry', zip_bytes([('same', b'a', 0o100644), ('same', b'b', 0o100644)])),
        ('file directory conflict', zip_bytes([('a', b'a', 0o100644), ('a/b', b'b', 0o100644)])),
        ('symlink', single('link', b'../../outside', 0o120777)),
        ('fifo', single('pipe', b'', 0o010644)),
        ('setuid', single('tool', b'x', 0o104755)),
        ('sticky directory', zip_bytes([('dir/', b'', 0o041755)])),
        ('member expansion bomb', zip_bytes([('huge', b'x' * (MEMBER + 1), 0o100644)], zipfile.ZIP_DEFLATED)),
        ('aggregate expansion bomb', zip_bytes([(str(i), b'x' * MEMBER, 0o100644) for i in range(5)], zipfile.ZIP_DEFLATED)),
        ('entry count overflow', zip_bytes([(str(i), b'', 0o100644) for i in range(4097)])),
        ('truncated archive', sample[:-5]),
        ('wrong format', b'not an archive'),
    ]:
        check(label, data, fail_before_entries=True)
    bad = bytearray(single('file')); bad[30 + len('file')] ^= 1
    check('member CRC corruption', bytes(bad), fail_before_entries=True)
    bad = bytearray(single('file')); central = bad.index(b'PK\x01\x02'); struct.pack_into('<H', bad, 6, 1); struct.pack_into('<H', bad, central + 8, 1)
    check('encrypted flag', bytes(bad), fail_before_entries=True)
    check('wrong operation before allocation', sample, operation='unexpected', fail_before_entries=True)
    check('oversized initial frame before allocation', sample, operation='oversize', fail_before_entries=True)
    check('denied acknowledgement stops decoder', sample, operation='reject_ack')
    check('closed channel stops decoder', sample, operation='close_after_entry')
    check('acknowledgement payload is rejected', sample, operation='ack_payload')
    check('replayed acknowledgement is rejected', sample, operation='replay_ack')
    bad = bytearray(zip_bytes([('good', b'ok', 0o100644), ('bad', b'bad', 0o100644)]))
    second = bad.index(b'PK\x03\x04', 4); bad[second + 30 + len('bad')] ^= 1
    check('late CRC failure never completes partial image', bytes(bad))
    # Script bytes are inert data, never package-provided install hooks.
    canary = directory / 'not-executed'; canary.write_bytes(b'unchanged\n')
    code = b'pn main() { output("changed", "not-executed")^ }\n'
    check('package script is not executed', single('install.ls', code), {'install.ls': (False, False, code)})
    require_qualification(canary.read_bytes() == b'unchanged\n')
    report['status'] = 'confined_decoder_component_passed_publication_and_deployment_unqualified'
    checkpoint(); print(json.dumps({'passed': len(report['cases']), 'report': str(base / 'result.json')}))


if __name__ == '__main__':
    main()
