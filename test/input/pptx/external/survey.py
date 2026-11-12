import hashlib, json, os, pathlib, re, subprocess, sys, time, zipfile
from xml.etree import ElementTree as ET

ROOT=pathlib.Path('test/input/pptx/external')
TEMP=pathlib.Path('temp/pptx-external')
TEMP.mkdir(parents=True, exist_ok=True)
P='http://schemas.openxmlformats.org/presentationml/2006/main'
R='http://schemas.openxmlformats.org/officeDocument/2006/relationships'
A='http://schemas.openxmlformats.org/drawingml/2006/main'

def source_slides(path):
    with zipfile.ZipFile(path) as z:
        presentation=ET.fromstring(z.read('ppt/presentation.xml'))
        rels=ET.fromstring(z.read('ppt/_rels/presentation.xml.rels'))
        targets={r.get('Id'):r.get('Target') for r in rels}
        texts=[]
        for slide_id in presentation.findall(f'./{{{P}}}sldIdLst/{{{P}}}sldId'):
            target=targets[slide_id.get(f'{{{R}}}id')]
            part=target.lstrip('/') if target.startswith('/') else 'ppt/'+target
            root=ET.fromstring(z.read(part))
            texts.append([t.text for t in root.iter(f'{{{A}}}t') if t.text and t.text.strip()])
        return texts

def compact(value): return ''.join(value.split())
def package(record):
    source=source_slides(ROOT/record['file']); env=os.environ.copy()
    env['LAMBDA_PPTX_SURVEY_PATH']=str(ROOT/record['file'])
    prefix=TEMP/record['file'].removesuffix('.pptx')
    try:
        result=subprocess.run(['./lambda.exe','run',str(ROOT/'_probe.ls')],env=env,capture_output=True,text=True,timeout=120)
    except subprocess.TimeoutExpired:
        return {'status':'timeout','source_slides':len(source)}
    prefix.with_suffix('.package.stderr').write_text(result.stderr)
    prefix.with_suffix('.package.stdout').write_text(result.stdout)
    try: parsed=json.loads(result.stdout)
    except ValueError: return {'status':'invalid output','exit_code':result.returncode,'source_slides':len(source),'output':result.stdout[:500]}
    if 'error' in parsed: return {'status':'error','exit_code':result.returncode,'source_slides':len(source),'error':parsed['error']}
    output=compact(parsed['text']); missing=[{'slide':i+1,'text':t} for i,slide in enumerate(source) for t in slide if compact(t) not in output]
    return {'status':'ok' if result.returncode==0 and parsed['slides']==len(source) and not missing else 'incorrect slide count, missing text, or exit',
        'source_slides':len(source),'imported_slides':parsed['slides'],'source_text_runs':sum(map(len,source)),
        'missing_text_runs':missing,'diagnostics':parsed['diagnostics']}

def ui(record):
    source=source_slides(ROOT/record['file']); count=len(source); name=record['file'].removesuffix('.pptx')
    events=[{'type':'advance_time','ms':100,'steps':3},
        {'type':'click','target':{'selector':'#open'}},
        {'type':'type','target':{'selector':'#file-path'},'text':str(ROOT/record['file']),'clear':True},
        {'type':'click','target':{'selector':'#file-confirm'}},
        {'type':'advance_time','ms':100,'steps':10},
        {'type':'assert_attribute','target':{'selector':'#slides-editor'},'attribute':'data-dialog','equals':''},
        {'type':'assert_attribute','target':{'selector':'#slides-editor'},'attribute':'data-error','equals':''},
        {'type':'assert_attribute','target':{'selector':'#slides-editor'},'attribute':'data-slides','equals':str(count)}]
    first=next((t.strip() for t in source[0] if len(t.strip())>=4),None)
    if first: events.append({'type':'assert_text','target':{'selector':'#slide-render'},'contains':first})
    if count>1:
        events += [{'type':'click','target':{'selector':f'#thumbnails [data-slide="{count-1}"]'}},
            {'type':'assert_attribute','target':{'selector':'#slides-editor'},'attribute':'data-index','equals':str(count-1)}]
        last=next((t.strip() for t in source[-1] if len(t.strip())>=4),None)
        if last: events.append({'type':'assert_text','target':{'selector':'#slide-render'},'contains':last})
    # Sample actual content in addition to checking import and navigation.
    if name=='cc0-1mb':
        events += [{'type':'assert_script','js':"document.querySelector('#slide-render img') && document.querySelector('#slide-render img').getAttribute('src').startsWith('data:image/')"},
            {'type':'assert_value','target':{'selector':'#speaker-notes'},'equals':'Labore consectetur quaerat ipsum voluptate ea maiores repudiandae omnis recusandae vitae reiciendis sapiente laudantium dolor officia.'}]
    elif name=='cc0-with-chart':
        events.append({'type':'assert_script','js':"!!document.querySelector('#slide-render svg')"})
    elif name=='cc0-with-table':
        events.append({'type':'assert_text','target':{'selector':'#slide-render table'},'contains':'Fatima'})
    events.append({'type':'render','file':str(TEMP/(name+'.png'))})
    eventfile=TEMP/(name+'.events.json'); eventfile.write_text(json.dumps({'name':'PPTX survey '+name,'html':'test/demo/slides/slides.ls','viewport':{'width':1440,'height':900},'events':events}))
    try:
        result=subprocess.run(['./lambda.exe','view','test/demo/slides/slides.ls','--headless','--event-file',str(eventfile)],capture_output=True,text=True,timeout=150)
    except subprocess.TimeoutExpired: return {'status':'timeout'}
    (TEMP/(name+'.ui.stdout')).write_text(result.stdout)
    (TEMP/(name+'.ui.stderr')).write_text(result.stderr)
    combined=result.stdout+'\n'+result.stderr
    summaries=re.findall(r'Assertions:\s*(\d+) passed,\s*(\d+) failed',combined)
    passed,failed=map(int,summaries[-1]) if summaries else (0,-1)
    ok=result.returncode==0 and failed==0 and 'Result: PASS' in combined
    return {'status':'ok' if ok else 'failed','exit_code':result.returncode,'assertions_passed':passed,
        'assertions_failed':failed,'first_text_asserted':first,'last_slide_checked':count>1}


manifest=json.loads((ROOT/'manifest.json').read_text())
for record in manifest['files']:
    actual=hashlib.sha256((ROOT/record['file']).read_bytes()).hexdigest()
    if actual != record['sha256']:
        raise SystemExit('Fixture checksum mismatch: '+record['file'])
modes=sys.argv[1:] or ['package','ui']
if any(mode not in ('package','ui') for mode in modes):
    raise SystemExit('Usage: python3 test/input/pptx/external/survey.py [package] [ui]')
failed=False
for mode in modes:
    output=TEMP/(mode+'-results.json'); results=[]
    for i,record in enumerate(manifest['files']):
        start=time.monotonic(); value=package(record) if mode=='package' else ui(record)
        value['file']=record['file']; results.append(value); output.write_text(json.dumps(results,indent=2,ensure_ascii=False)+'\n')
        print(f"{mode} {i+1:2d}/20 {record['file']}: {value['status']} ({time.monotonic()-start:.1f}s)",flush=True)
        failed |= value['status']!='ok'
if failed: raise SystemExit(1)
