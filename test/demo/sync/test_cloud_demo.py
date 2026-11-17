"""Demo LAN/cloud integration with an isolated S3-compatible store."""
import base64
import hashlib
import importlib.util
import json
import os
from pathlib import Path
from urllib.parse import urlsplit
import secrets
import shutil
import time

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('demo_gate', ROOT / 'test/demo/sync/test_sync.py')
demo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(demo)
peer = demo.peer
AREA = ROOT / 'temp/sync-demo-cloud' / ('process-' + peer.TIER)
demo.AREA = peer.AREA = AREA
demo.USER = 'demo-cloud-' + secrets.token_hex(8)
STORAGE = json.loads(Path(os.environ['LAMBDA_CLOUD_TEST_STORAGE_FILE']).read_text())
FAULT = None
if os.environ.get('LAMBDA_SYNC_TEST_DROP') == '1':
    from s3_fault_proxy import FaultProxy
    endpoint = urlsplit(STORAGE['endpoint'])
    assert endpoint.hostname == '127.0.0.1' and endpoint.scheme == 'http'
    FAULT = FaultProxy((endpoint.hostname, endpoint.port))
    STORAGE['endpoint'] = 'http://127.0.0.1:' + str(FAULT.server_address[1])


def wait_for(predicate, label, jobs, seconds=300):
    deadline = time.monotonic() + seconds
    while not predicate():
        for job in jobs:
            assert job[0].poll() is None, label + ': agent exited; see ' + job[2].name
        assert time.monotonic() < deadline, label + ': timed out'
        time.sleep(.1)


def saved(name):
    status = demo.read(AREA / (name + '.events'))
    return status.get('type') == 'status' and status.get('status', {}).get('cloud_saved') is True


def start(name):
    return demo.launch(name, 'run', options=dict(retry_ms=1000, interval_ms=100, cloud_ms=1000))


def digest(path):
    return hashlib.sha256(path.read_bytes()).digest()


def main():
    if AREA.exists(): shutil.rmtree(AREA)
    AREA.mkdir(parents=True, mode=0o700)
    for name in ['a', 'b']:
        (AREA / name / 'tree').mkdir(parents=True)
        peer.keys[name] = secrets.token_hex(32)
    (AREA / 'test-keys.json').write_text(json.dumps(peer.keys))
    os.chmod(AREA / 'test-keys.json', 0o600)
    host = demo.launch('a', 'host')
    wait_for(lambda: demo.read(AREA / 'a.events').get('type') == 'pairing', 'pairing', [host], 60)
    code = demo.read(AREA / 'a.events')['code']
    peer.finish(demo.launch('b', 'join', code=code, lifetime_ms=30000))
    peer.finish(host)
    for name in ['a', 'b']:
        peer.finish(demo.launch(name, 'configure_cloud', storage=STORAGE))
    cases = {
        'notes/readme.txt': 'Unicode: λ 測試 😀\nline two\n'.encode(),
        'notes/config.json': b'{"hello":[1,true,null],"value":"text"}\n',
        'notes/data.xml': b'<root><item>one &amp; two</item></root>\n',
        'notes/document.md': b'# Heading\n\n**Bold** and `code`.\n',
        'images/pixel.png': base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jR5sAAAAASUVORK5CYII='),
        'empty': b'',
        'data/all-bytes.bin': bytes(range(256))*64,
    }
    if FAULT is not None:
        cases.update({
            'images/pixel.gif': base64.b64decode('R0lGODlhAQABAIAAAAAAAP///ywAAAAAAQABAAACAUwAOw=='),
            'images/icon.svg': b'<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"><path d="M0 0h1v1z"/></svg>',
            'notes/utf16.txt': 'Hello λ and 世界'.encode('utf-16'),
        })
    reports = []
    # The fault run isolates retry behavior; the normal run measures larger volumes.
    sizes = [131072] if FAULT is not None else [32768, 1048576, 8388608]
    for size in sizes:
        files = dict(cases)
        files['data/payload.bin'] = os.urandom(size)
        if size == 8388608:
            files.update({f'many/file-{i:03}.txt': ('entry-%d' % i).encode() for i in range(100)})
        for name, data in files.items():
            path = AREA / 'a/tree' / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        for name in ['a', 'b']:
            (AREA / (name + '.events')).unlink(missing_ok=True)
        started = time.monotonic()
        if FAULT is not None and not FAULT.dropped.is_set(): FAULT.armed.set()
        sender = start('a')
        if FAULT is not None and not FAULT.dropped.is_set():
            wait_for(FAULT.dropped.is_set, 'interrupted object upload', [sender], 120)
            assert not saved('a'), 'partial upload was marked cloud saved'
            print('PASS connection dropped during object upload; retry pending',flush=True)
        wait_for(lambda: saved('a'), 'cloud publication', [sender], 900)
        published = time.monotonic()-started
        demo.stop(sender)
        # The original device stays offline throughout the restore.
        receiver = start('b')
        wait_for(lambda: all((AREA/'b/tree'/name).is_file() and digest(AREA/'b/tree'/name) == hashlib.sha256(data).digest() for name,data in files.items()),
                 'cloud restore', [receiver], 900)
        restored = time.monotonic()-started-published
        demo.stop(receiver)
        reports.append(dict(payload_bytes=size, total_bytes=sum(map(len,files.values())), file_count=len(files),
                            publish_seconds=round(published,3), restore_seconds=round(restored,3)))
        print('PASS', json.dumps(reports[-1]), flush=True)
    (AREA/'timings.json').write_text(json.dumps(reports,indent=2)+'\n')
    print('Demo cloud backup and offline restore passed',flush=True)


if __name__ == '__main__':
    try: main()
    finally:
        for child in peer.children:
            if child.poll() is None: child.kill()
            child.wait()
        if FAULT is not None: FAULT.stop()
