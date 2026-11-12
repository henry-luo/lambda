"""Independent native clients qualify retention, fenced S3 generations and offline return."""
from pathlib import Path
import os
import secrets
import shutil
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'test/sync_documents'))
import test_documents as docs

cloud, peer = docs.cloud, docs.peer
AREA = ROOT / 'temp/sync-retention' / ('process-' + peer.TIER)
peer.AREA = cloud.AREA = docs.AREA = AREA
CLI = 'lmd/package/sync/maintenance_cli.ls'
proxies = []


def maintain(name, action, success=True, **fields):
    result = AREA / ('maintenance-result-' + str(peer.counter + 1))
    old = os.environ.get('LAMBDA_SYNC_RESULT')
    os.environ['LAMBDA_SYNC_RESULT'] = str(result)
    try:
        peer.finish(peer.launch(name, action, script=CLI, **fields), success)
    finally:
        if old is None:
            os.environ.pop('LAMBDA_SYNC_RESULT', None)
        else:
            os.environ['LAMBDA_SYNC_RESULT'] = old
    return cloud.decode(result.read_text()) if success else None


def binary_hex(value):
    # The typed scalar codec uses Mark's canonical binary literal.
    return value.removeprefix("[b'\\x").removesuffix("']").lower()


def main():
    if AREA.exists():
        shutil.rmtree(AREA)
    AREA.mkdir(parents=True)
    identities = {}
    for name in ['a', 'b', 'c', 'd']:
        (AREA / name / 'agent').mkdir(parents=True, mode=0o700)
        (AREA / name / 'tree').mkdir()
        peer.keys[name] = secrets.token_hex(32)
        identities[name] = peer.identifier(peer.run(name, 'device'))
        peer.run(name, 'export_identity', output=str(AREA / (name + '.device')))
    peer.run('a', 'init', root=str(AREA / 'a/tree'), space='retention-' + secrets.token_hex(8), label='Alice')
    for name in ['b', 'c', 'd']:
        peer.run('a', 'invite', recipient=str(AREA / (name + '.device')), output=str(AREA / (name + '.invitation')))
        peer.run(name, 'enroll', root=str(AREA / name / 'tree'), label=name,
                 invitation=str(AREA / (name + '.invitation')), expected=identities['a'])
    note = AREA / 'a/tree/note'
    note.write_text('a')
    document = docs.document('a', 'open', path='note')['id']
    docs.direct('b', 'a', identities)
    docs.document('b', 'open', path='note')
    docs.document('b', 'edit', document=document, commands=[dict(kind='text', start=1, count=0, value='L')])
    versions = {}
    for value in ['b', 'c', 'd']:
        docs.document('a', 'edit', document=document,
                      commands=[dict(kind='text', start=len(note.read_text()), count=0, value=value)])
        saved = docs.document('a', 'save', document=document)
        versions[value] = binary_hex(saved['saved'])
        if value == 'b':
            protected_snapshot = binary_hex(maintain('a', 'snapshot', label='protected-b')[0])
    archive = AREA / 'a/tree/archive.bin'
    archive.write_bytes(b'A' * 8192)
    maintain('a', 'scan')
    archive.write_bytes(b'B' * 8192)
    maintain('a', 'scan')
    file_headers = maintain('a', 'headers')['files']
    archive_groups = {h['group'] for h in file_headers if h['revision']['content']['size'] == 8192}
    function_host, port = cloud.function_server()
    config = dict(url='http://127.0.0.1:' + str(port) + '/sync', authority=identities['a'])
    cloud.cloud('a', 'bootstrap', members=[str(AREA / (name + '.device')) for name in ['b', 'c', 'd']])
    cloud.cloud('a', 'sync', cloud=config)
    old_publication = cloud.cloud('a', 'history', cloud=config, actor=identities['a'])['versions'][0]
    policy = dict(version=1, mode='buckets', timezone='America/New_York', keep_all_ms=0,
                  rules=[dict(unit='day', count=None)], arrival_grace_ms=0, delete_grace_ms=0, clock_skew_ms=300000)
    permanent = dict(policy, mode='forever', rules=[])
    maintain('a', 'configure', policy=policy, overrides=[dict(id=g, policy=permanent) for g in archive_groups])
    timestamp = int(time.time() * 1000) + 172800000
    plan = maintain('a', 'plan', at=timestamp)
    peer.check(versions['c'] in [binary_hex(v) for v in plan['versions']] and
               versions['b'] not in [binary_hex(v) for v in plan['versions']],
               'daily selection expires an intermediate save and protects a named snapshot')
    maintain('a', 'forget', id=binary_hex(plan['id']), at=timestamp)
    listed = docs.document('a', 'versions', document=document, cursor=0, limit=2)
    tail = docs.document('a', 'versions', document=document, cursor=listed['next'], limit=2,
                         root=binary_hex(listed['root']))
    peer.check(len(listed['versions']) == 2 and tail['next'] is None,
               'bounded saved-history pages retain stable root identity')
    marker = AREA / 'maintenance-committed-response-dropped'
    proxy = cloud.fault_proxy(config['url'], cloud.decode, marker, kind='maint_commit')
    proxies.append(proxy)
    job_config = dict(config, url='http://127.0.0.1:' + str(proxy.server_address[1]) + '/sync')
    job = cloud.cloud('a', 'maintenance_prepare', cloud=job_config)
    partial = cloud.cloud('a', 'maintenance_step', cloud=job_config, budget=1)
    peer.check(not partial['committed'] and partial['next'] == 1,
               'replacement object copying records progress across native client exits')
    while job['objects'] - partial['next'] > 128:
        partial = cloud.cloud('a', 'maintenance_step', cloud=job_config, budget=128)
    cloud.cloud('a', 'maintenance_step', success=False, cloud=job_config, budget=128)
    peer.check(marker.exists(), 'lost maintenance-cut response leaves a recoverable durable publication')
    partial = cloud.cloud('a', 'maintenance_step', cloud=job_config, budget=128)
    cloud.peer.check(partial['epoch'] == 1, 'verified replacement checkpoint advances storage epoch')
    pruning = cloud.cloud('a', 'maintenance_prune', cloud=config, id=job['id'], budget=1)
    while not pruning['complete']:
        pruning = cloud.cloud('a', 'maintenance_prune', cloud=config, id=job['id'], budget=64)
    peer.check(pruning['total'] > 0, 'old physical generations are deleted in resumable bounded steps')
    cloud.cloud('a', 'read', success=False, script='test/sync_retention/_control.ls', cloud=config,
                key=old_publication['bundle']['key'])
    peer.check(True, 'retired publication bundle is physically unavailable after pruning')
    cloud.cloud('c', 'sync', cloud=config)
    recovered = docs.document('c', 'open', path='note')
    peer.check(recovered['value'] == 'abcd', 'fresh device recovers current data after physical pruning')
    restored = docs.document('c', 'restore_version', document=document, version=versions['b'])
    peer.check(restored['value'] == 'ab', 'retained intermediate snapshot endpoint restores exactly from cloud')
    docs.document('c', 'restore_version', success=False, document=document, version=versions['c'])
    peer.check(True, 'retired version remains distinguishable from corruption and is not resurrected')
    eviction = cloud.cloud('a', 'cache_plan', cloud=config)
    peer.check(len(eviction['objects']) > 0, 'cache policy selects retained cold chunks independently of history expiry')
    evicted = cloud.cloud('a', 'cache_step', cloud=config, id=eviction['id'], budget=128)
    check = maintain('a', 'verify')
    peer.check(evicted['complete'] and check['verified'] and not check['data_complete'],
               'verified cloud copies permit local eviction while metadata check reports deferred data')
    docs.direct('d', 'a', identities)
    cold_peer = maintain('d', 'verify')
    peer.check(not cold_peer['data_complete'] and (AREA / 'd/tree/archive.bin').read_bytes() == b'B' * 8192,
               'offline peer receives current files and explicit deferred-history availability')
    hydration = cloud.cloud('d', 'cache_hydrate', cloud=config, budget=128)
    peer.check(hydration['remaining'] == 0 and maintain('d', 'verify')['data_complete'],
               'another authorized device authenticates and hydrates deferred history chunks')
    owner_hydration = cloud.cloud('a', 'cache_hydrate', cloud=config, budget=128)
    peer.check(owner_hydration['remaining'] == 0 and maintain('a', 'verify')['data_complete'],
               'cache owner rehydrates before releasing its durable remote reservation')
    cloud.cloud('b', 'sync', cloud=config)
    returned = docs.document('b', 'status', document=document)
    peer.check('L' in returned['value'] and returned['dirty'] and (AREA / 'b/tree/note').read_text() == 'abcd',
               'offline unsaved edits merge through pruned history and remain unsaved')
    docs.direct('b', 'a', identities)
    peer.check(docs.document('a', 'status', document=document)['value'] == returned['value'],
               'peer exchange propagates retirement and preserves late offline edits')
    local_plan = maintain('a', 'plan_prune')
    peer.check(len(local_plan['objects']) > 0, 'local cleanup discovers obsolete encodings independently of saved history')
    ready = AREA / 'prune-interrupted-after-removal'
    paused = peer.launch('a', 'interrupt', script='test/sync_retention/_interrupt.ls',
                         id=binary_hex(local_plan['id']), ready=str(ready))
    deadline = time.monotonic() + 30
    while not ready.exists():
        assert paused[0].poll() is None, 'prune helper exited before the crash point'
        assert time.monotonic() < deadline, 'prune helper did not reach its crash point'
        time.sleep(0.05)
    maintain('a', 'pin', success=False, ids=[], enabled=True)
    peer.check(paused[0].poll() is None, 'pending physical deletion holds the SQLite writer fence against another writer')
    paused[0].kill()
    paused[0].wait(timeout=10)
    paused[1].close()
    paused[2].close()
    resumed = maintain('a', 'prune_step', id=binary_hex(local_plan['id']), budget=128)
    while not resumed['complete']:
        resumed = maintain('a', 'prune_step', id=binary_hex(local_plan['id']), budget=128)
    peer.check(maintain('a', 'verify')['verified'], 'kill between deletion and SQL commit resumes without losing retained data')
    maintain('a', 'release_snapshot', snapshot=protected_snapshot)
    scheduled = cloud.cloud('a', 'maintenance_tick', cloud=config, at=timestamp,
                            options=dict(enabled=True, interval_ms=1000, budget=128))
    for _ in range(80):
        if scheduled['phase'] == 'complete':
            break
        scheduled = cloud.cloud('a', 'maintenance_tick', cloud=config, at=timestamp,
                                options=dict(enabled=True, interval_ms=1000, budget=128))
    assert scheduled['phase'] == 'complete', scheduled
    peer.check(True, 'scheduled bounded invocations thin history and finish cloud/local maintenance')
    idle = cloud.cloud('a', 'maintenance_tick', cloud=config, at=timestamp,
                       options=dict(enabled=True, interval_ms=1000, budget=128))
    peer.check(idle['phase'] == 'idle', 'an idle scheduled tick waits until its next configured deadline')
    cloud.cloud('d', 'sync', cloud=config)
    docs.document('d', 'restore_version', success=False, document=document, version=versions['b'])
    peer.check(True, 'snapshot retirement propagates and old replicas cannot revive its expired saved version')
    print('Retention process acceptance complete', flush=True)


if __name__ == '__main__':
    try:
        main()
    finally:
        for proxy in proxies:
            proxy.shutdown()
            proxy.server_close()
        for child in peer.children:
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=10)
                except Exception:
                    child.kill()
                    child.wait()
