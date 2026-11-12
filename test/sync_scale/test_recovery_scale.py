"""Real S3, separate devices and restartable owner audits; no production fault switches."""
from pathlib import Path
import hashlib, os, secrets, shutil, sys
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'test/sync_cloud'))
import test_cloud as cloud
peer=cloud.peer
AREA=ROOT/'temp/sync-recovery-scale'/('acceptance-'+peer.TIER)
peer.AREA=cloud.AREA=AREA
CONTROL='test/sync_retention/_control.ls'
ADMIN='test/sync_scale/_admin.ls'

def admin(action,**fields):
    return cloud.cloud('a',action,script=ADMIN,**fields)
def raw(name,action,config,**body):
    return cloud.cloud(name,action,script=CONTROL,cloud=config,body=body)
def audit(config):
    cloud.cloud('a','audit_begin',cloud=config)
    for _ in range(128):
        row=cloud.cloud('a','audit_scan',cloud=config,budget=32)
        if row['audit']['complete']:break
    else:raise AssertionError('inventory did not finish')
    for _ in range(128):
        row=cloud.cloud('a','audit_step',cloud=config,budget=8)
        if row['complete']:break
    else:raise AssertionError('verification did not finish')
    return row

def cut(config,audited=False):
    plan=cloud.cloud('a','audit_prepare' if audited else 'maintenance_prepare',cloud=config)
    for _ in range(128):
        row=cloud.cloud('a','maintenance_step',cloud=config,budget=16)
        if row.get('committed'):break
        assert not row.get('replan'),row
    else:raise AssertionError('cut did not finish')
    for _ in range(128):
        pruned=cloud.cloud('a','maintenance_prune',cloud=config,id=row['id'],budget=64)
        if pruned['complete']:break
    else:raise AssertionError('prune did not finish')
    return plan,row

def main():
    if AREA.exists():shutil.rmtree(AREA)
    AREA.mkdir(parents=True)
    os.environ['LAMBDA_SCALE_STORAGE']=cloud.STORAGE
    ids={}
    for name in ['a','b','c']:
        (AREA/name/'agent').mkdir(parents=True,mode=0o700)
        (AREA/name/'tree').mkdir()
        peer.keys[name]=secrets.token_hex(32)
        ids[name]=peer.identifier(peer.run(name,'device'))
        peer.run(name,'export_identity',output=str(AREA/(name+'.device')))
    peer.run('a','init',root=str(AREA/'a/tree'),space='scale-'+secrets.token_hex(8),label='owner')
    for name in ['b','c']:
        peer.run('a','invite',recipient=str(AREA/(name+'.device')),output=str(AREA/(name+'.invitation')))
        peer.run(name,'enroll',root=str(AREA/name/'tree'),label=name,invitation=str(AREA/(name+'.invitation')),expected=ids['a'])
    block=bytes(i%251 for i in range(65536))
    large=AREA/'a/tree/large.bin'
    with large.open('wb') as f:
        for _ in range(256):f.write(block)
    digest=hashlib.sha256(large.read_bytes()).digest()
    (AREA/'a/tree/note').write_text('saved')
    # qualify streaming P2P independently of the cloud recovery path.
    peer.sync('b','a',ids['a'])
    peer.check(hashlib.sha256((AREA/'b/tree/large.bin').read_bytes()).digest()==digest,'16 MiB peer install is exact')
    (AREA/'b/tree/offline').write_text('pending offline work')
    host,port=cloud.function_server()
    config=dict(url='http://127.0.0.1:'+str(port)+'/sync',authority=ids['a'])
    cloud.cloud('a','bootstrap',members=[str(AREA/(n+'.device')) for n in ['b','c']],storage_protocol=os.environ.get('LAMBDA_SCALE_STORAGE_PROTOCOL'))
    cloud.cloud('a','sync',cloud=config)
    remote=raw('a','pull',config)
    peer.check(remote['protocol']==(1 if os.environ.get('LAMBDA_SCALE_STORAGE_PROTOCOL')=='catalog-1' else 2),'bootstrap selects the intended legacy or default paged protocol')
    chosen=remote['catalog_backup']
    # discovery is available with the pointer absent and does not guess a latest backup.
    admin('erase',key='catalog')
    cloud.cloud('a','sync',False,cloud=config)
    discovered=cloud.cloud('a','backup_discover',cloud=config,limit=16)
    peer.check(any(e['id']==chosen and e['valid'] for e in discovered['entries']),'lost catalog backups discovered and authenticated')
    cloud.cloud('a','catalog_recover',cloud=config,snapshot=chosen)
    cloud.cloud('a','maintenance_prepare',False,cloud=config)
    cloud.cloud('b','audit_begin',False,cloud=config)
    cloud.cloud('a','audit_begin',cloud=config)
    while not cloud.cloud('a','audit_scan',cloud=config,budget=32)['audit']['complete']:pass
    key=admin('enumerate',prefix='objects/')['entries'][0]['key']
    stash=str(AREA/'ciphertext-stash')
    admin('corrupt',key=key,stash=stash)
    while True:
        checked=cloud.cloud('a','audit_step',cloud=config)
        if checked['complete']:break
    peer.check(checked['bad']>0 and checked['attestation'] is None,'observed ETag mutation blocks audit promotion')
    cloud.cloud('a','audit_prepare',False,cloud=config)
    admin('restore',key=key,stash=stash)
    cloud.cloud('a','audit_abort',cloud=config)
    report=audit(config)
    peer.check(report['bad']==0 and report['attestation'] is not None,'resumed fresh audit verifies ciphertext and retained graphs')
    first,committed=cut(config,True)
    peer.check(raw('a','pull',config)['maintenance_allowed'],'audited cut enables bounded cleanup')
    cloud.cloud('a','audit_abort',cloud=config) # clears completed local audit record.
    for _ in range(3):
        plan,row=cut(config)
        peer.check(plan['copy_objects']==0 and row['copied']==0,'maintenance reuses every retained ciphertext object')
    options=dict(enabled=True,interval_ms=1000,budget=1)
    scheduled=cloud.cloud('a','maintenance_tick',cloud=config,options=options)
    while scheduled['phase']!='copy':
        scheduled=cloud.cloud('a','maintenance_tick',cloud=config,options=options)
    cloud.cloud('a','maintenance_tick',cloud=config,options=options)
    cloud.cloud('a','maintenance_abort',cloud=config)
    replanned=cloud.cloud('a','maintenance_tick',cloud=config,options=options)
    peer.check(replanned['phase']=='replan','scheduled aborted cut returns to selection')
    for _ in range(256):
        scheduled=cloud.cloud('a','maintenance_tick',cloud=config,options=dict(options,budget=64))
        if scheduled['phase']=='complete':break
    else:raise AssertionError('replanned scheduler did not finish')
    cloud.cloud('b','sync',cloud=config)
    cloud.cloud('a','sync',cloud=config)
    peer.check((AREA/'a/tree/offline').read_text()=='pending offline work','offline device returns after repeated cuts without losing work')
    cloud.cloud('c','sync',cloud=config)
    peer.check(hashlib.sha256((AREA/'c/tree/large.bin').read_bytes()).digest()==digest,'fresh cloud-only device restores 16 MiB exactly')
    verified=cloud.cloud('c','verify',script='test/sync_cloud/_repository.ls')
    peer.check(verified['verified'],'recovered large-file repository graph verifies')
    print('recovery-scale acceptance complete',flush=True)

if __name__=='__main__':
    try:main()
    finally:
        for p in peer.children:
            if p.poll() is None:p.kill();p.wait()
