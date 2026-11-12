"""Independent repositories and fresh function processes against a real S3 backend."""
from pathlib import Path
import importlib.util
import json
import os
import re
import secrets
import shutil
import sys
import subprocess
import time
from fault_proxy import start as fault_proxy

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('peer_gate',ROOT/'test/sync_peer/test_peer.py')
peer=importlib.util.module_from_spec(spec);spec.loader.exec_module(peer)
peer.AREA=ROOT/'temp/sync-cloud'/('acceptance-'+peer.TIER)
AREA=peer.AREA
CLI='lmd/package/sync/cloud_cli.ls'
STORAGE=os.environ.pop('LAMBDA_CLOUD_STORAGE',None)
if STORAGE is None:
    STORAGE=Path(os.environ['LAMBDA_CLOUD_TEST_STORAGE_FILE']).read_text()


def decode(data):
    def value(t):
        kind,payload=t
        if kind=='map':return {value(k):value(v) for k,v in payload}
        if kind=='array':return [value(x) for x in payload]
        if kind=='string':return payload
        if kind in ['bool','int','float','null']:return json.loads(payload)[0]
        if kind=='symbol':return payload[2:-2]
        return payload
    envelope=json.loads(data)
    return value(envelope[1])


def cloud(name,action,success=True,script=CLI,**fields):
    result=AREA/(str(peer.counter+1)+'-result.json')
    previous=os.environ.get('LAMBDA_CLOUD_RESULT')
    os.environ['LAMBDA_CLOUD_RESULT']=str(result)
    try:
        if action=='bootstrap':os.environ['LAMBDA_CLOUD_STORAGE']=STORAGE
        peer.finish(peer.launch(name,action,script=script,**fields),success)
    finally:
        os.environ.pop('LAMBDA_CLOUD_STORAGE',None)
        if previous is None:os.environ.pop('LAMBDA_CLOUD_RESULT',None)
        else:os.environ['LAMBDA_CLOUD_RESULT']=previous
    return decode(result.read_text()) if success else None


def function_server(port=0):
    ready=AREA/('function-ready-'+secrets.token_hex(4))
    log=(AREA/('function-'+secrets.token_hex(4)+'.log')).open('w')
    env=dict(os.environ,LAMBDA_CLOUD_STORAGE=STORAGE)
    process=subprocess.Popen([sys.executable,'deploy/sync_cloud/http_host.py','--port',str(port),'--ready',str(ready)],
                             cwd=ROOT,env=env,stdout=log,stderr=log)
    peer.children.append(process)
    deadline=time.monotonic()+30
    while not ready.exists():
        assert process.poll() is None, 'function host exited: '+log.name
        assert time.monotonic()<deadline,'function host not ready'
        time.sleep(0.05)
    log.close()
    return process,int(ready.read_text())


def main():
    if AREA.exists():shutil.rmtree(AREA)
    AREA.mkdir(parents=True)
    ids={}
    for name in ['a','b','c']:
        (AREA/name/'agent').mkdir(parents=True,mode=0o700)
        (AREA/name/'tree').mkdir()
        peer.keys[name]=secrets.token_hex(32)
        ids[name]=peer.identifier(peer.run(name,'device'))
        peer.run(name,'export_identity',output=str(AREA/(name+'.device')))
    space='cloud-space-'+secrets.token_hex(8)
    peer.run('a','init',root=str(AREA/'a/tree'),space=space,label='Alice')
    for name in ['b','c']:
        peer.run('a','invite',recipient=str(AREA/(name+'.device')),output=str(AREA/(name+'.invitation')))
        peer.run(name,'enroll',root=str(AREA/name/'tree'),label=name,invitation=str(AREA/(name+'.invitation')),expected=ids['a'])
    host,port=function_server()
    config={'url':'http://127.0.0.1:'+str(port)+'/sync','authority':ids['a']}
    cloud('a','bootstrap',members=[str(AREA/(x+'.device')) for x in ['b','c']])
    (AREA/'a/tree/folder').mkdir()
    (AREA/'a/tree/folder/note.txt').write_text('first cloud version')
    (AREA/'a/tree/zero').write_bytes(b'')
    binary=bytes(range(256))*4
    (AREA/'a/tree/payload.bin').write_bytes(binary)
    cloud('a','snapshot',script='test/sync_cloud/_repository.ls',label='first')
    first=cloud('a','sync',cloud=config)
    peer.check(first['cloud_saved'], 'content-first cloud publication acknowledged')
    second=cloud('b','sync',cloud=config)
    peer.check(second['cloud_saved'] and (AREA/'b/tree/folder/note.txt').read_text()=='first cloud version', 'disjoint client processes restore through stateless HTTP')
    peer.check((AREA/'b/tree/zero').read_bytes()==b'' and (AREA/'b/tree/payload.bin').read_bytes()==binary,'binary and empty file recovery')
    idle=cloud('a','sync',cloud=config)
    peer.check(idle['cloud']['uploaded']==0 and idle['cloud']['unchanged'], 'idle sync has no new publication or object uploads')
    (AREA/'b/tree/folder/note.txt').write_text('offline edit')
    (AREA/'a/tree/folder').rename(AREA/'a/tree/moved')
    cloud('a','sync',cloud=config)
    cloud('b','sync',cloud=config)
    cloud('a','sync',cloud=config)
    peer.check((AREA/'a/tree/moved/note.txt').read_text()=='offline edit' and (AREA/'b/tree/moved/note.txt').read_text()=='offline edit','offline rename and edit merge via cloud')
    fresh=cloud('c','sync',cloud=config)
    peer.check(fresh['cloud_saved'] and (AREA/'c/tree/moved/note.txt').read_text()=='offline edit','fresh enrolled device restores with original peers offline')
    verified=cloud('c','verify',script='test/sync_cloud/_repository.ls')
    (AREA/'historical-restore').mkdir()
    cloud('c','restore',script='test/sync_cloud/_repository.ls',destination=str(AREA/'historical-restore'))
    peer.check(verified['verified'] and len(verified['snapshots'])==1 and
               (AREA/'historical-restore/folder/note.txt').read_text()=='first cloud version',
               'cloud-only named snapshot restore retains older saved bytes')
    history=cloud('c','history',cloud=config,actor=ids['a'])
    peer.check(len(history['versions'])>=2 and history['next'] is None,'per-device immutable publication history survives head replacement')
    host.terminate();host.wait(timeout=10)
    host,again=function_server(port)
    peer.check(again==port and cloud('c','sync',cloud=config)['cloud_saved'],'HTTP host restart preserves all cloud state')
    marker=AREA/'dropped-response'
    proxy=fault_proxy(config['url'],decode,marker)
    proxied={**config,'url':'http://127.0.0.1:'+str(proxy.server_port)+'/sync'}
    try:
        (AREA/'a/tree/after-drop.txt').write_text('survives uncertain response')
        cloud('a','sync',success=False,cloud=proxied)
        assert marker.exists(),'response was not dropped after commit'
        recovered=cloud('a','sync',cloud=proxied)
        peer.check(recovered['cloud_saved'] and recovered['cloud']['unchanged'],
                   'lost commit response recovers by reading the existing publication')
    finally:
        proxy.shutdown();proxy.server_close()
    # No listener is running: try direct, then fall back in this same round.
    fallback=cloud('b','sync',cloud=config,direct={'url':'ws://127.0.0.1:1/sync','peer':ids['a']})
    peer.check(not fallback['peer_connected'] and fallback['cloud_checked'] and fallback['cloud_saved'],'unreachable peer falls back to periodic HTTP path')
    server,port=peer.server('a')
    direct=cloud('b','sync',cloud=config,direct={'url':'ws://127.0.0.1:'+str(port)+'/sync','peer':ids['a']})
    peer.finish(server)
    peer.check(direct['peer_connected'] and direct['cloud_saved'],'return to peer exchange preserves cloud coverage')
    bad=cloud('a','adversary',script='test/sync_cloud/_adversary.ls',cloud=config)
    peer.check(bad['rejected'] and bad['missing'],'S3 rejects tampered checksum and no publication receipt is issued')
    combined_gate(config,ids)
    polled=cloud('c','watch',cloud=config,rounds=2,interval_ms=1000,cloud_every=1)
    peer.check(polled['connected'] and polled['cloud_saved'],'periodic polling completes multiple rounds in one native client')
    (AREA/'a/tree/pending.txt').write_text('survives a client killed before publication')
    pending_ready=AREA/'pending-upload-ready'
    interrupted=peer.launch('a','interrupt',script='test/sync_cloud/_interrupt.ls',cloud=config,ready=str(pending_ready))
    deadline=time.monotonic()+90
    while not pending_ready.exists():
        assert interrupted[0].poll() is None,'interrupted client exited before its upload marker'
        assert time.monotonic()<deadline,'immutable uploads did not finish'
        time.sleep(0.05)
    captured=decode(pending_ready.read_text())
    interrupted[0].kill();interrupted[0].wait(timeout=10)
    interrupted[1].close();interrupted[2].close()
    cloud('c','sync',cloud=config)
    assert not (AREA/'c/tree/pending.txt').exists(),'unpublished objects became a discoverable revision'
    assert not cloud('a','status')['cloud_saved'],'unpublished work was marked cloud-saved'
    resumed=cloud('a','sync',cloud=config)
    cloud('c','sync',cloud=config)
    peer.check(resumed['frontier']==captured['frontier'] and resumed['cloud_saved'] and
               (AREA/'c/tree/pending.txt').read_text()=='survives a client killed before publication',
               'client killed before head publication resumes original operation identities')
    cloud('a','membership',cloud=config,recipient=str(AREA/'b.device'),active=False)
    cloud('b','sync',success=False,cloud=config)
    peer.check(True,'revoked device cannot request further cloud access')
    print('Cloud acceptance complete',flush=True)


def combined_gate(config,ids):
    (AREA/'b/tree/managed-peer.txt').write_text('received by the combined owner')
    ready=AREA/'combined-ready'
    combined=peer.launch('a','run',script=CLI,cloud=config,port=0,bind='127.0.0.1',interval_ms=60000,
                         rounds=4,ready=str(ready))
    deadline=time.monotonic()+30
    while not ready.exists():
        assert combined[0].poll() is None,'combined agent exited'
        assert time.monotonic()<deadline,'combined listener not ready'
        time.sleep(0.05)
    peer.run('b','sync',url='ws://127.0.0.1:'+ready.read_text()+'/sync',peer=ids['a'])
    output=peer.finish(combined)
    peer.check('sessions:1' in output.replace(' ','') and
               (AREA/'a/tree/managed-peer.txt').read_text()=='received by the combined owner',
               'one agent owns periodic cloud work and incoming peer sessions')



if __name__=='__main__':
    try:main()
    finally:
        for process in peer.children:
            if process.poll() is None:process.kill();process.wait()
