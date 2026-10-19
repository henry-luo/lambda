#!/usr/bin/env node
'use strict';

const assert = require('assert/strict');
const fs = require('fs').promises;
const path = require('path');
const { createHash } = require('crypto');
const { referenceControlIsValid } = require('./layout/reference_controls');

async function runReferenceControlTests({ RadiantLayoutTester, compactBrowserReference }) {
    const root = path.resolve(__dirname, '..');
    const directory = await fs.mkdtemp(path.join(root, 'temp/layout-reference-contracts-'));
    const html = path.join(directory, 'sample.html');
    const source = '<!doctype html><div>A&#x2060;<span>B</span></div>';
    await fs.writeFile(html, source);
    const tester = new RadiantLayoutTester({ projectRoot: root, json: true, emitJson: false });
    tester.referenceDir = directory;
    const raw = path.join(directory, 'sample.json');
    const standard = path.join(directory, `sample.standards.${process.platform}.json`);
    const compact = standard.replace(/\.json$/, '.min.json');
    const reference = {
        reference_control: {
            stylesheet: 'div { white-space: nowrap }',
            specification: 'https://www.w3.org/TR/css-text-3/#line-break-details',
            reason: 'CSS Text 3 §5.5 prohibits the WJ boundary break.',
            fixture_sha256: createHash('sha256').update(source).digest('hex')
        },
        layout_tree: { nodeType: 'element', tag: 'html', layout: { x: 0, y: 0, width: 100, height: 100 }, children: [] }
    };
    await fs.writeFile(raw, JSON.stringify({ ...reference, reference_control: undefined }));
    await fs.writeFile(standard, JSON.stringify(reference));
    await fs.writeFile(compact, JSON.stringify(compactBrowserReference(reference)));

    const selected = await tester.resolveBrowserReference('sample', 'contracts', html);
    assert.equal(selected.path, compact);
    const loaded = await tester.loadBrowserReference('sample', 'contracts', html, selected.path);
    assert.deepEqual(loaded.reference_control, reference.reference_control);

    const textControl = { ...reference.reference_control,
        specification: 'https://www.w3.org/TR/css-text-3/#white-space-processing',
        reason: 'DOM carriage returns have U+0020 semantics.',
        text_codepoints: [{ from: 13, to: 32 }] };
    delete textControl.stylesheet;
    assert.equal(referenceControlIsValid(textControl), true);
    for (const text_codepoints of [[null], [], [{ from: 13, to: 13 }], [{ from: 13, to: 0xD800 }],
            [{ from: -1, to: 32 }], [{ from: 13, to: 32 }, { from: 13, to: 10 }]]) {
        assert.equal(referenceControlIsValid({ ...textControl, text_codepoints }), false);
    }
    const textReference = { ...reference, reference_control: textControl };
    await fs.writeFile(compact, JSON.stringify(compactBrowserReference(textReference)));
    assert.deepEqual((await tester.loadBrowserReference('sample', 'contracts', html,
        compact)).reference_control, textControl);
    await fs.writeFile(html, source + '<span>changed</span>');
    await assert.rejects(tester.loadBrowserReference('sample', 'contracts', html,
        compact), /Stale standards reference/);
    await fs.writeFile(html, source);
    await fs.writeFile(compact, JSON.stringify(compactBrowserReference(reference)));

    // Source edits invalidate the control even when the old browser capture still exists.
    await fs.writeFile(html, source.replace('&#x2060;', ' '));
    await assert.rejects(tester.loadBrowserReference('sample', 'contracts', html), /Stale standards reference/);
    await fs.writeFile(html, source);

    const missing = compactBrowserReference(reference);
    delete missing.reference_control;
    await fs.writeFile(compact, JSON.stringify(missing));
    await assert.rejects(tester.loadBrowserReference('sample', 'contracts', html), /missing its source or control provenance/);
    await fs.writeFile(compact, JSON.stringify(compactBrowserReference(reference)));

    const generic = path.join(directory, 'sample.standards.json');
    await fs.writeFile(generic, JSON.stringify(reference));
    assert.equal((await tester.resolveBrowserReference('sample', 'contracts', html)).sourcePath, standard);
    await fs.unlink(standard);
    await fs.unlink(compact);
    tester._compactReferenceValidity.clear();
    assert.equal((await tester.resolveBrowserReference('sample', 'contracts', html)).sourcePath, generic);
    await fs.unlink(generic);
    assert.equal((await tester.resolveBrowserReference('sample', 'contracts', html)).sourcePath, raw);
    assert.equal((await tester.loadBrowserReference('sample', 'contracts', html)).reference_control, undefined);

    const expected = reference.layout_tree.layout;
    const wrong = { ...expected, height: 200 };
    assert.equal(tester.compareLayout(expected, expected).some(difference => difference.exceedsTolerance), false);
    assert.equal(tester.compareLayout(wrong, expected).some(difference => difference.exceedsTolerance), true);
    console.log('Layout reference controls: provenance, source invalidation, precedence, legacy fallback and strict geometry PASS');
}

if (require.main === module) {
    runReferenceControlTests({
        ...require('./layout/test_radiant_layout'),
        ...require('./layout/comparison_schema')
    }).catch(error => { console.error(error); process.exitCode = 1; });
}

module.exports = { runReferenceControlTests };
