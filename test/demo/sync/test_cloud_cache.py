"""Cold blobs sharing a cloud pack survive eviction, maintenance and hydration."""
import importlib.util
import json
import os
from pathlib import Path
import secrets
import shutil

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('cloud_gate', ROOT / 'test/demo/sync/test_cloud_demo.py')
cloud = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cloud)
demo = cloud.demo
peer = demo.peer
AREA = ROOT / 'temp/sync-demo-cache' / ('process-' + peer.TIER)
demo.AREA = peer.AREA = AREA
demo.USER = 'demo-cache-' + secrets.token_hex(8)


def main():
    if AREA.exists(): shutil.rmtree(AREA)
    (AREA / 'a/tree').mkdir(parents=True)
    peer.keys['a'] = secrets.token_hex(32)
    peer.finish(demo.launch('a', 'create'))
    peer.finish(demo.launch('a', 'configure_cloud', storage=cloud.STORAGE))
    for name in ['first', 'second']:
        (AREA / 'a/tree' / name).write_bytes((name + ' old version').encode())
    job = demo.launch('a', 'cloud_backup')
    peer.finish(job)
    assert Path(job[1].name).read_text().strip() == 'true'
    for name in ['first', 'second']:
        (AREA / 'a/tree' / name).write_bytes((name + ' new version').encode())
    job = demo.launch('a', 'cache_cycle')
    peer.finish(job)
    assert json.loads(Path(job[1].name).read_text()) == [True] * 6
    for name in ['first', 'second']:
        assert (AREA / 'a/tree' / name).read_bytes() == (name + ' new version').encode()
    print('PASS shared-pack eviction, maintenance and cold-blob hydration', flush=True)


if __name__ == '__main__':
    try: main()
    finally:
        for child in peer.children:
            if child.poll() is None: child.kill()
            child.wait()
