#!/usr/bin/env node
// Analyze differences between Lambda and MathLive outputs

const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');
const { JSDOM } = require('jsdom');

const refDir = 'test/latex/reference';

// Tests to analyze
const tests = [
    { name: 'fracs_basic', expr: '\\frac{a}{b}', idx: 0 },
    { name: 'nested_test2', expr: '\\frac{\\sqrt{x}}{y}', idx: 0 },
    { name: 'accents_000', expr: '\\vec{x}', idx: 0 },
    { name: 'radicals_001', expr: '\\sqrt[3]{x}', idx: 0 },
];

function normalizeHtml(html) {
    // Remove style attributes for structural comparison
    return html.replace(/\s*style="[^"]*"/g, '')
               .replace(/\s+/g, ' ')
               .trim();
}

function extractStructure(html) {
    const dom = new JSDOM(html);
    const result = [];
    
    function walk(node, depth) {
        if (node.nodeType === 1) { // Element
            const classes = node.className ? node.className.split(' ').filter(c => c) : [];
            result.push({ depth, tag: node.tagName.toLowerCase(), classes });
            for (const child of node.childNodes) {
                walk(child, depth + 1);
            }
        } else if (node.nodeType === 3 && node.textContent.trim()) {
            result.push({ depth, text: node.textContent.trim() });
        }
    }
    
    walk(dom.window.document.body, 0);
    return result;
}

for (const test of tests) {
    console.log(`\n${'='.repeat(60)}`);
    console.log(`TEST: ${test.name} - "${test.expr}"`);
    console.log('='.repeat(60));
    
    // Generate Lambda output
    try {
        execSync(`./lambda.exe math '${test.expr}' --output-html /tmp/lambda.html --output-ast /tmp/lambda.json 2>/dev/null`);
    } catch (e) {
        console.log('Error generating Lambda output');
        continue;
    }
    
    const lambdaHtml = fs.readFileSync('/tmp/lambda.html', 'utf-8');
    const lambdaAst = fs.readFileSync('/tmp/lambda.json', 'utf-8');
    
    // Read MathLive reference
    const mlHtmlPath = path.join(refDir, `${test.name}_${test.idx}.mathlive.html`);
    const mlJsonPath = path.join(refDir, `${test.name}_${test.idx}.mathlive.json`);
    
    if (!fs.existsSync(mlHtmlPath)) {
        console.log(`No MathLive HTML reference found: ${mlHtmlPath}`);
        continue;
    }
    
    const mlHtml = fs.readFileSync(mlHtmlPath, 'utf-8');
    
    // Compare structure
    const lambdaStruct = extractStructure(lambdaHtml);
    const mlStruct = extractStructure(mlHtml);
    
    console.log('\n--- Lambda Structure (first 20 nodes) ---');
    lambdaStruct.slice(0, 20).forEach((n, i) => {
        const indent = '  '.repeat(n.depth);
        if (n.text) {
            console.log(`${i}: ${indent}TEXT: "${n.text}"`);
        } else {
            console.log(`${i}: ${indent}<${n.tag}> classes: [${n.classes.join(', ')}]`);
        }
    });
    
    console.log('\n--- MathLive Structure (first 20 nodes) ---');
    mlStruct.slice(0, 20).forEach((n, i) => {
        const indent = '  '.repeat(n.depth);
        if (n.text) {
            console.log(`${i}: ${indent}TEXT: "${n.text}"`);
        } else {
            console.log(`${i}: ${indent}<${n.tag}> classes: [${n.classes.join(', ')}]`);
        }
    });
    
    // Find structural differences
    console.log('\n--- Key Differences ---');
    const lambdaClasses = new Set(lambdaStruct.filter(n => n.classes).flatMap(n => n.classes));
    const mlClasses = new Set(mlStruct.filter(n => n.classes).flatMap(n => n.classes));
    
    const onlyLambda = [...lambdaClasses].filter(c => !mlClasses.has(c));
    const onlyML = [...mlClasses].filter(c => !lambdaClasses.has(c));
    
    if (onlyLambda.length) console.log(`Classes only in Lambda: ${onlyLambda.join(', ')}`);
    if (onlyML.length) console.log(`Classes only in MathLive: ${onlyML.join(', ')}`);
    
    console.log(`\nLambda nodes: ${lambdaStruct.length}, MathLive nodes: ${mlStruct.length}`);
}
