"""Run focused goldens with the existing 300-second every-allocation GC budget.

Ordinary discovery keeps its 60-second deadline. --full additionally attempts the
large workflow cases; the admission case exceeds this budget on the recorded
native host. Every selected item requires normal exit, markers and exact golden.
"""
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
AREA = ROOT / 'temp/sync-documents' / ('stress-' + os.environ.get('LAMBDA_EXEC_BACKEND', 'auto'))
FIXTURES = ['test/lambda/sync_document_formats.ls',
            'test/lambda/proc/sync_cloud_repository.ls',
            'test/lambda/proc/sync_document_gc.ls',
            'test/lambda/proc/sync_local_document.ls',
            'test/lambda/proc/sync_peer_exchange.ls']
FULL = ['test/lambda/proc/sync_document_admission.ls',
        'test/lambda/proc/sync_document_races.ls',
        'test/lambda/proc/sync_document_repository.ls']


def main():
    AREA.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, LAMBDA_GC_FORCE_EVERY='1', LAMBDA_GC_POISON_FREED='1')
    selected = FIXTURES + (FULL if '--full' in sys.argv else [])
    for filename in selected:
        path = ROOT / filename
        command = ('run ' if path.parent.name == 'proc' else '') + filename
        result = subprocess.run([os.environ.get('LAMBDA_SYNC_EXE', str(ROOT / 'lambda.exe')),
                                 'test-batch', '--no-log', '--timeout=300'],
                                input=command + '\n', text=True, capture_output=True,
                                env=env, cwd=ROOT, timeout=330)
        (AREA / (path.stem + '.stdout')).write_text(result.stdout)
        (AREA / (path.stem + '.stderr')).write_text(result.stderr)
        match = re.fullmatch('\x01BATCH_START ' + re.escape(filename) + r'\n(.*?)\x01BATCH_END (\d+)\n',
                             result.stdout, re.S)
        assert result.returncode == 0 and match and match[2] == '0', filename + ': incomplete/failed native batch'
        assert match[1].strip() == path.with_suffix('.txt').read_text().strip(), filename + ': golden mismatch'
        print('PASS ' + filename, flush=True)
    print(str(len(selected)) + '/' + str(len(selected)) + ' forced-GC/poison goldens passed', flush=True)


if __name__ == '__main__':
    main()
