#!/usr/bin/env node
'use strict';

const fs = require('fs').promises;
const path = require('path');
const { extractLayoutFromFile, closeSharedBrowser } = require('../test/layout/extract_browser_references');
const { RadiantLayoutTester } = require('../test/layout/test_radiant_layout');
const { compactBrowserReference, compactFixtureCandidates } = require('../test/layout/comparison_schema');

async function main() {
    const manifestPath = process.argv[2];
    if (!manifestPath) throw new Error('Usage: node utils/capture_layout_reference_controls.js <manifest.json>');
    const root = path.resolve(__dirname, '..');
    const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
    if (!manifest.reference_control || !manifest.category ||
            !Array.isArray(manifest.fixtures) || !manifest.fixtures.length) {
        throw new Error('A reference-control manifest requires its control, category and fixtures');
    }
    const tester = new RadiantLayoutTester({ projectRoot: root, json: true, emitJson: false });
    try {
        for (const fixture of manifest.fixtures) {
            const html = path.join(root, 'test/layout/data', manifest.category, fixture);
            await extractLayoutFromFile(html, true, process.platform, manifest.category, {
                referenceControl: manifest.reference_control
            });
            // Regenerate both forms together so a previous compact capture cannot win.
            const selected = await tester.resolveBrowserReference(
                tester.getTestNameFromPath(html, manifest.category), manifest.category, html,
                { preferCompact: false });
            const reference = await tester.loadBrowserReference(
                fixture, manifest.category, html, selected.path);
            if (!reference.reference_control) throw new Error(`Missing standards control for ${fixture}`);
            await fs.writeFile(compactFixtureCandidates(selected.path)[0],
                JSON.stringify(compactBrowserReference(reference)));
        }
    } finally {
        await closeSharedBrowser();
    }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
