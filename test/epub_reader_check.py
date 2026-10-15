#!/usr/bin/env python3
"""Check EPUB reader persistence across moves and reject an altered saved passage."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parent.parent

def workflow():
    (ROOT / 'temp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='epub-state-', dir=ROOT / 'temp') as name:
        work = Path(name)
        book = ROOT / 'test/input/ebook/minimal.epub'
        identity = hashlib.sha256(book.read_bytes()).hexdigest()
        env = {**os.environ, 'LAMBDA_EBOOK_STATE_DIR': str(work / 'state')}
        def click(command):
            return {'type':'click','target':{'selector':f'[data-ebook-command={command}]'}}
        def check(label, path, events):
            script = work / f'{label}.json'
            result = work / f'{label}-result.json'
            script.write_text(json.dumps({'name':label,'viewport':{'width':1100,'height':800},'events':events}))
            process = subprocess.run([str(ROOT / 'lambda.exe'),'view',str(path),'--headless',
                '--event-file',str(script),'--event-result',str(result)],cwd=ROOT,env=env,
                capture_output=True,text=True,timeout=90)
            log = ROOT / 'temp' / f'epub-state-{label}.log'
            log.write_text(process.stdout+process.stderr)
            assert process.returncode == 0, f'{label} failed; see {log}'
            assert json.loads(result.read_text())['assertions']['failed'] == 0
        wait = {'type':'wait','ms':200}
        status = lambda text: {'type':'assert_text','target':{'selector':'.ebook-status'},'equals':text}
        check('save',book,[click('next'),wait,click('bookmark'),click('settings'),wait,click('larger'),wait,
            click('theme'),wait,click('mode'),wait,status('2 / 2')])
        state = work / 'state' / f'{identity}.json'
        saved = json.loads(state.read_text())
        assert saved['publication_fingerprint'] == identity
        assert saved['location']['spine_occurrence'] == 1
        assert len(saved['bookmarks']) == 1
        assert saved['preferences']['font_size'] == 22
        assert saved['preferences']['theme'] == 'sepia'
        assert saved['preferences']['mode'] == 'paged'
        moved = work / 'renamed.epub'
        shutil.copyfile(book,moved)
        check('resume',moved,[status('2 / 2'),
            {'type':'assert_count','target':{'selector':'.ebook-bookmarks button'},'count':1},
            {'type':'assert_text','target':{'selector':'.ebook-page-status'},'contains':'Page 1 / '},
            {'type':'switch_frame','selector':'iframe'},
            {'type':'assert_style','target':{'selector':'body'},'property':'font-size','equals':'22px'}])
        saved['location']['quote'] = 'This is a different passage'
        state.write_text(json.dumps(saved))
        check('invalid-position',book,[status('1 / 2'),
            {'type':'assert_text','target':{'selector':'.ebook-notices'},'contains':'could not be verified'}])
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path)
    options = parser.parse_args()
    result = {'name': 'EPUB save, moved-book resume and passage validation', 'pass': True}
    try:
        workflow()
    except (AssertionError, OSError, ValueError, subprocess.SubprocessError) as error:
        result.update({'pass': False, 'details': str(error)})
    report = {'summary': {'passed': int(result['pass']), 'failed': int(not result['pass']), 'total': 1},
              'results': [result]}
    if options.report:
        options.report.parent.mkdir(parents=True, exist_ok=True)
        options.report.write_text(json.dumps(report, indent=2) + '\n')
    print('EPUB reader state: ' + ('PASS' if result['pass'] else 'FAIL: ' + result['details']))
    return 0 if result['pass'] else 1

if __name__ == '__main__':
    sys.exit(main())
