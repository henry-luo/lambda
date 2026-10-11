#!/usr/bin/env python3
"""Compare the Lambda-script CSL processor with pinned citeproc-js; no runtime dependency."""
import argparse
import hashlib
import html
import json
import os
from pathlib import Path
import re
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'temp/citeproc/oracle'
PACKAGE = ROOT / 'lmd/package/latex/citeproc'
REFERENCE_COMMIT = 'cc9153c45293af878de08cafddbefe6ea150c380'
REFERENCE_SHA256 = 'db98d3341d39eb7a9f166231b6c51118ed499f79821daa3e41f1ea8d7f8d0cbb'
SUITE_COMMIT = '6eefc5b07c6969ab8999e48542acbcc131cba864'
SUITE_SHA256 = '5658b6b9dbacc0167f91c817e12186b51302036fde512722609ea0452a6617c1'

NODE_RUNNER = r'''
const fs = require('fs'), vm = require('vm');
const sandbox = {console}; vm.createContext(sandbox);
vm.runInContext(fs.readFileSync(process.argv[2], 'utf8'), sandbox);
const CSL = sandbox.CSL;
const fixtures = JSON.parse(fs.readFileSync(process.argv[3], 'utf8'));
const packageDir = process.argv[4];
const results = fixtures.map(fixture => {
  const byId = Object.fromEntries(fixture.references.map(item => [String(item.id), item]));
  const engine = new CSL.Engine({retrieveItem: id => byId[String(id)], retrieveLocale: lang => {
    const path = packageDir + '/locales/locales-' + lang + '.xml';
    return fs.readFileSync(fs.existsSync(path) ? path : packageDir + '/locales/locales-en-US.xml', 'utf8');
  }}, fixture.style);
  engine.setOutputFormat('text');
  const cited = fixture.requests.flatMap(request => request.items.map(item => String(item.id)));
  const uncited = (fixture.nocite || []).includes('*') ? Object.keys(byId) : (fixture.nocite || []);
  engine.updateItems([...new Set(cited)]);
  engine.updateUncitedItems(uncited);
  const previous = [], rendered = [];
  fixture.requests.forEach((request, i) => {
    const citation = {citationID: request.id, citationItems: request.items,
      properties: {noteIndex: request.note_index || 0}};
    const changes = engine.processCitationCluster(citation, previous, [])[1];
    changes.forEach(change => rendered[change[0]] = change[1]);
    previous.push([request.id, request.note_index || 0]);
  });
  const bibliography = engine.makeBibliography();
  return {id: fixture.id, citations: rendered,
    bibliography: bibliography ? bibliography[1].map(text => text.replace(/\n$/, '')) : []};
});
fs.writeFileSync(process.argv[5], JSON.stringify(results, null, 2));
'''


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fixtures():
    cases = []
    for filename in ['core.json', 'batch.json']:
        for case in json.loads((ROOT / 'test/lambda/latex/fixtures/citeproc' / filename).read_text()):
            case.setdefault('requests', [{'id': 'one', 'items': [{'id': 'item'}]}])
            cases.append(case)
    journal = dict(id='doe', type='article-journal', title='Test CSL title',
        author=[dict(family='Doe', given='John'), dict(family='Roe', given='Jane')],
        issued={'date-parts': [[2020]]}, volume='12', issue='3', page='1–9', DOI='10.123/test')
    journal['container-title'] = 'Science'
    for style in ['ieee', 'apa', 'chicago-author-date', 'chicago-fullnote-bibliography']:
        cases.append(dict(id='bundled-' + style, style=(PACKAGE / 'styles' / (style + '.csl')).read_text(),
            references=[journal], requests=[dict(id='one', items=[dict(id='doe')], note_index=1)]))
    return cases


def upstream_cases(directory):
    # An exploratory slice, selected by input shape, never by current pass/fail status.
    cases, excluded = [], []
    paths = sorted((directory / 'processor-tests/machines').glob('*.json'))
    checksum = hashlib.sha256()
    for path in paths:
        checksum.update(path.name.encode() + b'\0' + path.read_bytes() + b'\0')
    if checksum.hexdigest() != SUITE_SHA256:
        raise SystemExit('Official CSL fixture contents differ from the pinned suite')
    for path in paths:
        value = json.loads(path.read_text())
        reason = None
        if path.stem.split('_')[0] not in ['affix', 'group', 'condition', 'label', 'name', 'nameattr', 'macro', 'number', 'date', 'textcase']:
            reason = 'category outside the exploratory evaluator slice'
        elif len(value['input']) != 1 or value['mode'] != 'citation' or value['citations'] or value['citation_items'] or value['abbreviations']:
            reason = 'requires bibliography, citation-sequence, multi-item, or abbreviation adapter'
        item = value['input'][0]
        if 'id' not in item or 'type' not in item:
            reason = reason or 'requires incomplete-item normalization'
        language = re.search(r'default-locale="([^"]+)"', value['csl'])
        if language and language[1] not in ['en', 'en-US', 'de', 'de-DE', 'fr', 'fr-FR']:
            reason = reason or 'requires an unbundled locale'
        if reason:
            excluded.append(dict(id=path.stem, reason=reason))
            continue
        cases.append(dict(id=path.stem, style=value['csl'], references=value['input'],
            requests=[dict(id='one', items=[dict(id=str(item['id']))])],
            expected=[html.unescape(re.sub('<[^>]*>', '', value['result'])).strip()]))
    return cases, excluded


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, help='Existing pinned citeproc.js; otherwise download into ./temp/')
    parser.add_argument('--upstream-suite', type=Path, help='Explore the pinned official CSL test-suite checkout instead of the acceptance fixtures')
    args = parser.parse_args()
    WORK.mkdir(parents=True, exist_ok=True)
    for entry in json.loads((PACKAGE / 'resources.json').read_text())['files']:
        if digest(PACKAGE / entry['path']) != entry['sha256']:
            raise SystemExit('Bundled CSL resource hash mismatch: ' + entry['path'])
    cases, excluded = upstream_cases(args.upstream_suite.resolve()) if args.upstream_suite else (fixtures(), [])
    source, actual, expected = [WORK / name for name in ['inputs.json', 'lambda.json', 'reference.json']]
    source.write_text(json.dumps(cases, ensure_ascii=False))
    run = subprocess.run([str(ROOT / 'lambda.exe'), '--no-log', 'run', 'utils/citeproc_oracle.ls'],
        cwd=ROOT, env={**os.environ, 'CSL_ORACLE_INPUT': str(source), 'CSL_ORACLE_OUTPUT': str(actual)},
        capture_output=True, text=True)
    if run.returncode:
        raise SystemExit(run.stdout + run.stderr)
    if args.upstream_suite:
        oracle = [dict(id=case['id'], citations=case['expected']) for case in cases]
        expected.write_text(json.dumps(oracle, indent=2, ensure_ascii=False))
    else:
        reference = args.reference.resolve() if args.reference else WORK / 'citeproc.js'
        if not reference.exists():
            url = f'https://raw.githubusercontent.com/Juris-M/citeproc-js/{REFERENCE_COMMIT}/citeproc.js'
            with urllib.request.urlopen(url, timeout=60) as response:
                reference.write_bytes(response.read())
        if digest(reference) != REFERENCE_SHA256:
            raise SystemExit('citeproc-js hash differs from the pinned reference')
        runner = WORK / 'reference.cjs'
        runner.write_text(NODE_RUNNER)
        subprocess.run(['node', str(runner), str(reference), str(source), str(PACKAGE), str(expected)], check=True)
        oracle = json.loads(expected.read_text())
    results = json.loads(actual.read_text())
    failures = []
    for got, wanted in zip(results, oracle, strict=True):
        keys = ['citations'] if args.upstream_suite else ['citations', 'bibliography']
        if any(got[key] != wanted[key] for key in keys):
            failures.append(dict(id=got['id'], actual=got, expected=wanted))
    report = dict(total=len(cases), passed=len(cases)-len(failures), failed=len(failures), failures=failures,
        excluded=excluded, suite_total=len(cases)+len(excluded),
        reference_commit=REFERENCE_COMMIT if not args.upstream_suite else SUITE_COMMIT)
    (WORK / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False))
    print(f"CSL comparison: {report['passed']}/{report['total']} passed; {report['failed']} failed")
    if excluded:
        print(f"Exploratory slice: {len(cases)}/{report['suite_total']} fixtures admitted; {len(excluded)} excluded with reasons in report")
    for failure in failures:
        print('  ' + failure['id'])
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
