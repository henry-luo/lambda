"""A newly paired device joins existing cloud control without replacing prior history."""
import importlib.util
import json
import os
from pathlib import Path
import secrets
import shutil
import time

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('demo_cloud', ROOT / 'test/demo/sync/test_cloud_demo.py')
cloud = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cloud)
demo = cloud.demo
peer = demo.peer
AREA = ROOT / 'temp/sync-demo-membership' / ('process-' + peer.TIER)
demo.AREA = peer.AREA = AREA
demo.USER = 'demo-members-' + secrets.token_hex(8)
storage = json.loads(Path(os.environ['LAMBDA_CLOUD_TEST_STORAGE_FILE']).read_text())


def wait_for(predicate, label, jobs, seconds=120):
    deadline = time.monotonic()+seconds
    while not predicate():
        assert all(job[0].poll() is None for job in jobs), label + ': agent exited'
        assert time.monotonic() < deadline, label + ': timeout'
        time.sleep(.1)


def saved(name):
    v = demo.read(AREA/(name+'.events'))
    return v.get('type') == 'status' and v.get('status',{}).get('cloud_saved') is True


def main():
    if AREA.exists(): shutil.rmtree(AREA)
    AREA.mkdir(parents=True, mode=0o700)
    for name in ['a','b']:
        (AREA/name/'tree').mkdir(parents=True)
        peer.keys[name] = secrets.token_hex(32)
    if os.environ.get('LAMBDA_TEST_LEGACY') == '1':
        (AREA/'a/tree/note').write_text('before new device')
        peer.finish(demo.launch('a','legacy_fixture'))
        old_files = {p: p.read_bytes() for p in (AREA/'a/profile').rglob('device.sqlite')}
        assert len(old_files) == 1
        peer.finish(demo.launch('a','migrate'))
        assert all(p.read_bytes() == data for p, data in old_files.items())
        assert all(p.with_name('repo.sqlite').exists() for p in old_files)
    else:
        peer.finish(demo.launch('a','create'))
    peer.finish(demo.launch('a','configure_cloud',storage=storage))
    (AREA/'a/tree/note').write_text('before new device')
    owner = demo.launch('a','run',options=dict(interval_ms=100,cloud_ms=1000))
    wait_for(lambda:saved('a'),'initial backup',[owner])
    demo.stop(owner)
    host = demo.launch('a','host')
    wait_for(lambda:demo.read(AREA/'a.events').get('type') == 'pairing','pairing',[host])
    code = demo.read(AREA/'a.events')['code']
    peer.finish(demo.launch('b','join',code=code,lifetime_ms=30000))
    peer.finish(host)
    peer.finish(demo.launch('b','configure_cloud',storage=storage))
    (AREA/'b.events').unlink(missing_ok=True)
    member = demo.launch('b','run',options=dict(interval_ms=100,cloud_ms=1000))
    wait_for(lambda:demo.read(AREA/'b.events').get('status',{}).get('error_code') is not None,
             'new device awaits cloud admission',[member])
    assert not saved('b') and not (AREA/'b/tree/note').exists()
    (AREA/'a.events').unlink(missing_ok=True)
    owner = demo.launch('a','run',options=dict(interval_ms=100,cloud_ms=1000))
    wait_for(lambda:saved('a'),'owner updates cloud membership',[owner])
    demo.stop(owner)
    wait_for(lambda:(AREA/'b/tree/note').is_file() and (AREA/'b/tree/note').read_text() == 'before new device',
             'newly paired device restores with owner offline',[member])
    demo.stop(member)
    print('PASS paired device admitted to existing cloud control and restores while owner offline',flush=True)


if __name__ == '__main__':
    try: main()
    finally:
        for child in peer.children:
            if child.poll() is None: child.kill()
            child.wait()
