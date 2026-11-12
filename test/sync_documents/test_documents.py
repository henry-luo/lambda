"""Headless editor clients use real authenticated peers and fresh HTTP function processes."""
from pathlib import Path
import json
import os
import re
import secrets
import shutil
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'test/sync_cloud'))
import test_cloud as cloud

peer = cloud.peer
AREA = ROOT / 'temp/sync-documents' / ('process-' + peer.TIER)
peer.AREA = cloud.AREA = AREA
CLI = 'lmd/package/sync/document_cli.ls'


def document(name, action, success=True, **fields):
    result = AREA / ('document-result-' + str(peer.counter + 1))
    previous = os.environ.get('LAMBDA_SYNC_RESULT')
    os.environ['LAMBDA_SYNC_RESULT'] = str(result)
    try:
        peer.finish(peer.launch(name, action, script=CLI, **fields), success)
    finally:
        if previous is None:
            os.environ.pop('LAMBDA_SYNC_RESULT', None)
        else:
            os.environ['LAMBDA_SYNC_RESULT'] = previous
    return cloud.decode(result.read_text()) if success else None


def direct(client, server, identities):
    job, port = peer.server(server)
    result = peer.run(client, 'sync', url='ws://127.0.0.1:' + str(port) + '/sync', peer=identities[server])
    peer.finish(job)
    return result


def main():
    if AREA.exists():
        shutil.rmtree(AREA)
    AREA.mkdir(parents=True)
    identities = {}
    for name in ['a', 'b', 'c']:
        (AREA / name / 'agent').mkdir(parents=True, mode=0o700)
        (AREA / name / 'tree').mkdir()
        peer.keys[name] = secrets.token_hex(32)
        identities[name] = peer.identifier(peer.run(name, 'device'))
        peer.run(name, 'export_identity', output=str(AREA / (name + '.device')))
    peer.run('a', 'init', root=str(AREA / 'a/tree'), space='documents-' + secrets.token_hex(8), label='Alice')
    for name in ['b', 'c']:
        peer.run('a', 'invite', recipient=str(AREA / (name + '.device')), output=str(AREA / (name + '.invitation')))
        peer.run(name, 'enroll', root=str(AREA / name / 'tree'), label=name,
                 invitation=str(AREA / (name + '.invitation')), expected=identities['a'])
    note = AREA / 'a/tree/note.txt'
    note.write_text('ab')
    (AREA / 'a/tree/data.json').write_text('{"left":0,"right":0}')
    (AREA / 'a/tree/assets.mark').write_text('{label:"attachments"}')
    attachment = bytes(range(256)) * 16
    (AREA / 'attachment.bin').write_bytes(attachment)
    direct('a', 'b', identities)
    text = document('a', 'open', path='note.txt')['id']
    mark = document('a', 'open', path='data.json', profile='mark', format='json')['id']
    assets = document('a', 'open', path='assets.mark', profile='mark', format='mark')['id']
    direct('b', 'a', identities)
    document('b', 'open', path='note.txt')
    document('b', 'open', path='data.json', profile='mark', format='json')
    document('b', 'open', path='assets.mark', profile='mark', format='mark')
    command = dict(kind='text', start=1, count=0, value='A')
    document('a', 'edit', document=text, commands=[command], edit_id='first-edit')
    repeated = document('a', 'edit', document=text, commands=[command], edit_id='first-edit')
    peer.check(repeated['value'] == 'aAb', 'editor request ID prevents duplicate edits across native restarts')
    document('b', 'edit', document=text, commands=[dict(kind='text', start=2, count=0, value='B')])
    document('a', 'edit', document=mark, commands=[dict(kind='set', key='left', value=1)])
    document('b', 'edit', document=mark, commands=[dict(kind='set', key='right', value=2)])
    document('a', 'attach', document=assets, root=str(AREA), path='attachment.bin', key='asset')
    reopened = document('a', 'status', document=text)
    peer.check(reopened['value'] == 'aAb' and reopened['dirty'] and note.read_text() == 'ab',
               'restarted editor recovers locally committed unsaved edits without exporting')
    synced = direct('b', 'a', identities)
    a = document('a', 'status', document=text)
    b = document('b', 'status', document=text)
    tree = document('a', 'status', document=mark)
    peer.check(a['value'] == b['value'] == 'aAbB' and tree['value']['left'] == 1 and tree['value']['right'] == 2,
               'independent offline Text and Mark clients converge over authenticated peers')
    peer.check(re.search(r'document_packets:\s*[1-9]', synced) and re.search(r'document_roots:\s*0', synced),
               'ordinary edits transfer CRDT deltas without full document roots')
    document('a', 'edit', document=mark, commands=[dict(kind='set', key='left', value=2)])
    document('b', 'edit', document=mark, commands=[dict(kind='set', key='left', value=3)])
    direct('b', 'a', identities)
    conflict = document('a', 'status', document=mark)
    assert conflict['projection_error'] and len(conflict['editing_conflicts'][0]['alternatives']) == 2
    document('a', 'edit', document=mark, commands=[dict(kind='set', key='left', value=4)])
    direct('b', 'a', identities)
    peer.check(document('b', 'status', document=mark)['value'] == dict(left=4, right=2),
               'editor exposes concurrent Mark alternatives and explicit resolution converges')
    document('a', 'edit', document=text, commands=[dict(kind='text', start=0, count=0, value='D')])
    document('a', 'compact', document=text)
    document('b', 'edit', document=text, commands=[dict(kind='text', start=4, count=0, value='C')])
    direct('b', 'a', identities)
    compacted = document('a', 'status', document=text)
    peer.check(compacted['value'] == document('b', 'status', document=text)['value'] == 'DaAbBC',
               'offline edits merge through compact checkpoint recovery')
    saved = document('a', 'save', document=text)
    document('a', 'save', document=mark)
    document('a', 'save', document=assets)
    document('b', 'edit', document=text, commands=[dict(kind='text', start=0, count=0, value='U')])
    direct('b', 'a', identities)
    later = document('b', 'status', document=text)
    peer.check(later['dirty'] and later['value'] == 'UDaAbBC' and
               (AREA / 'b/tree/note.txt').read_text() == 'DaAbBC' and not saved['dirty'],
               'replicated save advances disk base while concurrent live edits remain unsaved')
    (AREA / 'b/tree/note.txt').write_text('DaAbBC!')
    external = document('b', 'refresh', document=text)
    peer.check(external['value'] == 'UDaAbBC!' and external['dirty'] and external['notices'],
               'external file write merges with unsaved editor edits and produces notice')
    # The cloud path has no peer listener and uses a new function process per request.
    server, port = cloud.function_server()
    config = dict(url='http://127.0.0.1:' + str(port) + '/sync', authority=identities['a'])
    cloud.cloud('a', 'bootstrap', members=[str(AREA / (name + '.device')) for name in ['b', 'c']])
    first = cloud.cloud('b', 'sync', cloud=config,
                        direct=dict(url='ws://127.0.0.1:1/sync', peer=identities['a']))
    restored = cloud.cloud('c', 'sync', cloud=config)
    recovered = document('c', 'status', document=text)
    document('c', 'read_attachment', document=assets, key='asset', root=str(AREA), path='restored.bin')
    peer.check(not first['peer_connected'] and first['cloud_saved'] and restored['cloud_saved'] and
               recovered['value'] == external['value'] and recovered['coverage']['cloud'],
               'periodic HTTP fallback restores unsaved CRDT state with original peers offline')
    peer.check((AREA / 'c/tree/note.txt').read_text() == 'DaAbBC!' and recovered['dirty'],
               'fresh-device recovery keeps saved disk bytes distinct from unsaved live state')
    peer.check((AREA / 'restored.bin').read_bytes() == attachment,
               'cloud-acknowledged document attachment restores byte for byte')
    document('c', 'edit', document=text, commands=[dict(kind='text', start=0, count=0, value='R')])
    pending = document('c', 'status', document=text)
    top_pending = cloud.cloud('c', 'status', cloud=config)
    peer.check(not pending['coverage']['cloud'] and pending['coverage']['local'] and not top_pending['cloud_saved'],
               'new live edit invalidates document cloud coverage without changing saved file')
    cloud.cloud('c', 'watch', cloud=config, rounds=2, interval_ms=1000, cloud_every=1)
    cloud.cloud('a', 'sync', cloud=config)
    peer.check(document('a', 'status', document=text)['value'] == 'RUDaAbBC!',
               'polling rounds relay live edits back into an existing editor session')
    versions = document('c', 'versions', document=text)
    peer.check(any(version['raw'] == 'ab' for version in versions) and
               any(version['raw'] == 'DaAbBC' for version in versions),
               'saved document versions remain restorable after compaction and cloud recovery')
    # A has just published its retained older repository objects; first catch up that inventory.
    cloud.cloud('c', 'sync', cloud=config)
    idle = cloud.cloud('c', 'sync', cloud=config)
    peer.check(idle['cloud']['unchanged'] and idle['cloud']['uploaded'] == 0,
               'idle document sync creates no redundant publication or object uploads')
    marker = AREA / 'dropped-document-response'
    proxy = cloud.fault_proxy(config['url'], cloud.decode, marker)
    proxied = dict(config, url='http://127.0.0.1:' + str(proxy.server_port) + '/sync')
    try:
        document('c', 'edit', document=text, commands=[dict(kind='text', start=0, count=0, value='Z')])
        cloud.cloud('c', 'sync', success=False, cloud=proxied)
        assert marker.exists(), 'document commit response was not dropped'
        resumed = cloud.cloud('c', 'sync', cloud=proxied)
        peer.check(resumed['cloud_saved'] and resumed['cloud']['unchanged'] and
                   document('c', 'status', document=text)['coverage']['cloud'],
                   'lost doc-only cloud commit response recovers durable coverage idempotently')
    finally:
        proxy.shutdown()
        proxy.server_close()
    ready = AREA / 'installed-save-ready'
    interrupted = peer.launch('c', 'interrupt', script='test/sync_documents/_interrupt.ls',
                              document=text, ready=str(ready))
    deadline = time.monotonic() + 60
    while not ready.exists():
        assert interrupted[0].poll() is None, 'save worker exited before its durable installation marker'
        assert time.monotonic() < deadline, 'save installation timed out'
        time.sleep(0.05)
    interrupted[0].kill()
    interrupted[0].wait(timeout=10)
    interrupted[1].close()
    interrupted[2].close()
    pending_save = document('c', 'status', document=text)
    completed = document('c', 'commit_save', document=text)
    peer.check(pending_save['prepared'] and not completed['prepared'] and not completed['dirty'] and
               (AREA / 'c/tree/note.txt').read_text() == completed['value'],
               'client killed after disk installation finalizes the original durable save intent')
    cloud.cloud('c', 'sync', cloud=config)
    cloud.cloud('a', 'sync', cloud=config)
    direct('c', 'a', identities)
    peer.check(document('a', 'status', document=text)['value'] == completed['value'],
               'return from polling to direct document exchange preserves the same editing state')
    print('Document acceptance complete', flush=True)


if __name__ == '__main__':
    try:
        main()
    finally:
        for process in peer.children:
            if process.poll() is None:
                process.kill()
                process.wait()
