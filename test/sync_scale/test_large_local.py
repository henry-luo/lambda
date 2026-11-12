"""Streamed local capture, external edit and exact restore; use a release executable for measurements."""
from pathlib import Path
import subprocess,os,shutil,json,signal
root=Path(__file__).resolve().parents[2];tier=os.environ.get('LAMBDA_EXEC_BACKEND','auto');area=root/'temp/sync-recovery-scale'/('large-'+tier)
if area.exists():shutil.rmtree(area)
(area/'tree').mkdir(parents=True)
block=bytes(i%251 for i in range(65536))
with (area/'tree/large.bin').open('wb') as f:
 for _ in range(256):f.write(block)
(area/'replacement-block.bin').write_bytes(bytes((i+17)%251 for i in range(65536)))
env=dict(os.environ,LAMBDA_SCALE_AREA=str(area),TMPDIR=str(area),LAMBDA_EXEC_BACKEND=tier)
with (area/'stdout').open('w') as out,(area/'stderr').open('w') as err:
 process=subprocess.Popen(['/usr/bin/time','-l',os.environ.get('LAMBDA_SYNC_EXE',str(root/'lambda.exe')),'run','--no-log','test/sync_scale/_large_file.ls'],env=env,stdout=out,stderr=err,start_new_session=True)
 try:code=process.wait(timeout=180)
 except subprocess.TimeoutExpired:
  os.killpg(process.pid,signal.SIGKILL);process.wait();raise
print('large',tier,'exit',code)
if (area/'report.json').exists():print((area/'report.json').read_text());print((area/'stderr').read_text()[-650:])
else:print((area/'stderr').read_text()[-1600:])
raise SystemExit(code)
