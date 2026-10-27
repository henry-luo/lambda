#!/usr/bin/env python3
"""CLI DOCX exports and publication failures; run from the checkout root."""
from pathlib import Path
import json, os, subprocess
from docx import Document
folder=Path('temp/docx-export')
folder.mkdir(parents=True, exist_ok=True)
cases=[]
def run(name,source,output,*options,ok=True):
    command=['./lambda.exe','convert',source,'-t','docx','-o',str(output),*options]
    r=subprocess.run(command,capture_output=True,text=True,env={**os.environ,'LAMBDA_EXEC_BACKEND':'auto'},timeout=120)
    (folder/(name+'.log')).write_text(r.stdout+r.stderr)
    assert (r.returncode==0)==ok,(name,r.returncode,(r.stdout+r.stderr)[-500:])
    cases.append({'name':name,'exit':r.returncode})
run('cli-markdown','test/demo/word/tests/import.md',folder/'cli.docx')
assert Document(folder/'cli.docx').paragraphs[0].text=='Imported heading'
run('cli-plain','test/input/docx/export/plain.txt',folder/'cli-plain.docx')
assert Document(folder/'cli-plain.docx').paragraphs[0].text=='Literal <xml> & Unicode 世界'
run('cli-html','test/input/docx/export/features.html',folder/'cli-features.docx')
assert len(Document(folder/'cli-features.docx').inline_shapes)==2
protected=folder/'cli-protected.docx'; protected.write_bytes(b'previous CLI content')
run('cli-strict','test/input/docx/export/losses.html',protected,ok=False)
assert protected.read_bytes()==b'previous CLI content'
run('cli-allow','test/input/docx/export/losses.html',folder/'cli-losses.docx','--loss','allow')
assert any('Remote image' in p.text for p in Document(folder/'cli-losses.docx').paragraphs)
run('cli-unwritable','test/input/docx/export/plain.txt',folder,ok=False)
report=json.dumps({'result':'PASS','cases':cases},indent=2)
(folder/'cli-result.json').write_text(report+'\n')
print(report)
