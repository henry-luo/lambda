#!/usr/bin/env node
/**
 * Convert Yoga flex test fixtures to Lambda test format
 *
 * Usage: node utils/convert_yoga_fixtures.js <yoga_fixtures_dir> <output_dir>
 *
 * Example:
 *   node utils/convert_yoga_fixtures.js ../yoga/gentest/fixtures test/layout/data/flex
 */

const fs = require('fs');
const path = require('path');

// Parse command line arguments
const args = process.argv.slice(2);
if (args.length < 2) {
    console.log('Usage: node convert_yoga_fixtures.js <yoga_fixtures_dir> <output_dir>');
    console.log('Example: node convert_yoga_fixtures.js ../yoga/gentest/fixtures test/layout/data/flex');
    process.exit(1);
}

const yogaFixturesDir = args[0];
const outputDir = args[1];

// Ensure output directory exists
if (!fs.existsSync(outputDir)) {
    fs.mkdirSync(outputDir, { recursive: true });
}

// HTML template for Lambda tests
function createLambdaTestHtml(testId, testContent, features, description) {
    return `<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>Yoga Test: ${testId}</title>
    <style>
        /* Yoga default styles - all divs are flex containers */
        body {
            margin: 0;
            padding: 0;
            font: 10px/1 Arial, sans-serif;
        }
        div {
            box-sizing: border-box;
            position: relative;
            display: flex;
            flex-direction: column;
            align-items: stretch;
            align-content: flex-start;
            justify-content: flex-start;
            flex-shrink: 0;
            border: 0 solid black;
            margin: 0;
            padding: 0;
        }
    </style>
</head>
<body>
${testContent}
</body>
</html>
`;
}

// Extract test ID and features from the div
function extractFeatures(content) {
    const features = new Set();

    // Detect flex properties
    if (/flex-grow/i.test(content)) features.add('flex-grow');
    if (/flex-shrink/i.test(content)) features.add('flex-shrink');
    if (/flex-basis/i.test(content)) features.add('flex-basis');
    if (/flex-wrap/i.test(content)) features.add('flex-wrap');
    if (/flex-direction/i.test(content)) features.add('flex-direction');
    if (/align-items/i.test(content)) features.add('align-items');
    if (/align-content/i.test(content)) features.add('align-content');
    if (/align-self/i.test(content)) features.add('align-self');
    if (/justify-content/i.test(content)) features.add('justify-content');
    if (/gap|row-gap|column-gap/i.test(content)) features.add('gap');
    if (/min-width|min-height/i.test(content)) features.add('min-max');
    if (/max-width|max-height/i.test(content)) features.add('min-max');
    if (/%/.test(content)) features.add('percentage');

    return Array.from(features);
}

// Parse a single Yoga fixture file and extract individual test cases
function parseYogaFixture(content, fixtureBaseName) {
    const tests = [];

    // Match each top-level div with an id attribute
    const divRegex = /<div\s+id="([^"]+)"[^>]*>[\s\S]*?<\/div>\s*(?=<div\s+id=|$)/g;

    let match;
    while ((match = divRegex.exec(content)) !== null) {
        const fullMatch = match[0];
        const testId = match[1];

        // Clean up the content - remove the outer position:absolute that Yoga adds
        let testContent = fullMatch;

        tests.push({
            id: testId,
            content: testContent,
            features: extractFeatures(fullMatch),
            source: fixtureBaseName
        });
    }

    return tests;
}

// Process fixture content more carefully to handle nested divs
function parseYogaFixtureCareful(content, fixtureBaseName) {
    const tests = [];
    const lines = content.split('\n');

    let currentTest = null;
    let depth = 0;
    let buffer = [];

    for (const line of lines) {
        // Check for start of a new top-level test div
        const startMatch = line.match(/^<div\s+id="([^"]+)"/);
        if (startMatch && depth === 0) {
            currentTest = startMatch[1];
            depth = 1;
            buffer = [line];

            // Check if it's a self-closing or single-line div
            if (line.includes('</div>')) {
                const openCount = (line.match(/<div/g) || []).length;
                const closeCount = (line.match(/<\/div>/g) || []).length;
                depth = openCount - closeCount;

                if (depth === 0) {
                    tests.push({
                        id: currentTest,
                        content: buffer.join('\n'),
                        features: extractFeatures(buffer.join('\n')),
                        source: fixtureBaseName
                    });
                    currentTest = null;
                    buffer = [];
                }
            }
            continue;
        }

        if (currentTest) {
            buffer.push(line);

            // Count div opens and closes
            const openCount = (line.match(/<div/g) || []).length;
            const closeCount = (line.match(/<\/div>/g) || []).length;
            depth += openCount - closeCount;

            if (depth === 0) {
                tests.push({
                    id: currentTest,
                    content: buffer.join('\n'),
                    features: extractFeatures(buffer.join('\n')),
                    source: fixtureBaseName
                });
                currentTest = null;
                buffer = [];
            }
        }
    }

    return tests;
}

// Convert test ID to filename
function toFilename(testId, source) {
    // Convert camelCase/snake_case to kebab-case and prefix with source
    const prefix = source.replace(/^YG/, '').replace(/Test$/, '').toLowerCase();
    return `yoga_${prefix}_${testId}`.toLowerCase().replace(/_/g, '-');
}

// Main conversion function
function convertYogaFixtures() {
    // Get list of fixture files
    const fixtureFiles = fs.readdirSync(yogaFixturesDir)
        .filter(f => f.endsWith('.html'))
        .filter(f => {
            // Filter to flex-related tests
            const name = f.toLowerCase();
            return name.includes('flex') ||
                   name.includes('align') ||
                   name.includes('justify') ||
                   name.includes('gap') ||
                   name.includes('wrap');
        });

    console.log(`Found ${fixtureFiles.length} flex-related fixture files`);

    let totalTests = 0;

    for (const fixtureFile of fixtureFiles) {
        const fixtureBaseName = path.basename(fixtureFile, '.html');
        const fixturePath = path.join(yogaFixturesDir, fixtureFile);
        const content = fs.readFileSync(fixturePath, 'utf8');

        const tests = parseYogaFixtureCareful(content, fixtureBaseName);
        console.log(`  ${fixtureFile}: ${tests.length} test cases`);

        for (const test of tests) {
            const filename = toFilename(test.id, test.source);
            const outputPath = path.join(outputDir, `${filename}.html`);

            const htmlContent = createLambdaTestHtml(
                test.id,
                test.content,
                test.features,
                `Converted from Yoga ${test.source}`
            );

            fs.writeFileSync(outputPath, htmlContent);
            totalTests++;
        }
    }

    console.log(`\nConverted ${totalTests} test cases to ${outputDir}`);
    console.log('\nNext steps:');
    console.log('  1. Run: make capture-layout suite=flex');
    console.log('  2. Run: make layout suite=flex');
}

// Run the converter
convertYogaFixtures();
