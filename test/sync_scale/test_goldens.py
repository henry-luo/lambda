"""Exact-golden sync sweep with ordinary deadlines, or focused existing-budget GC stress."""
from pathlib import Path
import os,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
TIER=os.environ.get('LAMBDA_EXEC_BACKEND','auto')
AREA=ROOT/'temp/sync-recovery-scale'/('gc' if '--gc' in sys.argv else 'goldens')/TIER
GC=['test/lambda/proc/sync_stream_cancel.ls','test/lambda/proc/sync_disk_stream.ls','test/lambda/proc/sync_stream_repository.ls',
    'test/lambda/proc/sync_cloud_audit.ls','test/lambda/proc/sync_retention_gc.ls']
def main():
    AREA.mkdir(parents=True,exist_ok=True)
    selected=GC if '--gc' in sys.argv else sorted(str(p.relative_to(ROOT)) for d in ['test/lambda','test/lambda/proc'] for p in (ROOT/d).glob('sync_*.ls'))
    budget=300 if '--gc' in sys.argv else 60
    env=dict(os.environ,TMPDIR=str(AREA))
    if '--gc' in sys.argv:env.update(LAMBDA_GC_FORCE_EVERY='1',LAMBDA_GC_POISON_FREED='1')
    for filename in selected:
        path=ROOT/filename
        command=('run ' if path.parent.name=='proc' else '')+filename
        r=subprocess.run([os.environ.get('LAMBDA_SYNC_EXE',str(ROOT/'lambda.exe')),'test-batch','--no-log','--timeout='+str(budget)],
            input=command+'\n',text=True,capture_output=True,env=env,cwd=ROOT,timeout=budget+30)
        (AREA/(path.stem+'.stdout')).write_text(r.stdout)
        (AREA/(path.stem+'.stderr')).write_text(r.stderr)
        found=re.fullmatch('\x01BATCH_START '+re.escape(filename)+r'\n(.*?)\x01BATCH_END (\d+)\n',r.stdout,re.S)
        assert r.returncode==0 and found and found[2]=='0',filename+': failed native batch'
        assert found[1].strip()==path.with_suffix('.txt').read_text().strip(),filename+': golden mismatch'
        print('PASS',filename,flush=True)
    print(len(selected),'exact sync goldens passed on',TIER,flush=True)
if __name__=='__main__':main()
