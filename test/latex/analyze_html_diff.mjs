#!/usr/bin/env node
/**
 * Analyze HTML differences between Lambda and MathLive output
 */
import fs from 'fs';
import path from 'path';
import { JSDOM } from 'jsdom';
import { fileURLToPath } from 'url';
import { spawn } from 'child_process';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const PROJECT_ROOT = path.join(__dirname, '..', '..');

// Extract text content in order
function extractTexts(html) {
    const dom = new JSDOM(`<body>${html}</body>`);
    const body = dom.window.document.body;
    const texts = [];
    const walk = (node) => {
        if (node.nodeType === 3 && node.textContent.trim()) {
            texts.push(node.textContent.trim());
        }
        for (const child of node.childNodes) walk(child);
    };
    walk(body);
    return texts;
}

// Extract class structure
function extractClasses(html) {
    const dom = new JSDOM(`<body>${html}</body>`);
    const all = dom.window.document.body.querySelectorAll('*');
    const classes = new Set();
    for (const el of all) {
        for (const c of el.classList) {
            classes.add(c);
        }
    }
    return [...classes].sort();
}

// Count elements
function countElements(html) {
    const dom = new JSDOM(`<body>${html}</body>`);
    return dom.window.document.body.querySelectorAll('*').length;
}

// Get simplified tree structure
function getTreeStructure(html, depth = 0, maxDepth = 4) {
    const dom = new JSDOM(`<body>${html}</body>`);
    const body = dom.window.document.body;
    const lines = [];
    
    function walk(node, indent) {
        if (indent > maxDepth) return;
        if (node.nodeType === 3) {
            const text = node.textContent.trim();
            if (text && text !== '​') { // skip zero-width space
                lines.push('  '.repeat(indent) + `"${text}"`);
            }
        } else if (node.nodeType === 1) {
            const classes = [...node.classList].filter(c => 
                c.startsWith('ML__') || c === 'base' || c === 'frac' || c === 'sqrt'
            ).join(' ');
            const tag = node.tagName.toLowerCase();
            lines.push('  '.repeat(indent) + `<${tag}${classes ? ' class="' + classes + '"' : ''}>`);
            for (const child of node.childNodes) {
                walk(child, indent + 1);
            }
        }
    }
    
    walk(body, 0);
    return lines.join('\n');
}

// Run Lambda to generate HTML
async function runLambda(latex) {
    return new Promise((resolve) => {
        const lambdaExe = path.join(PROJECT_ROOT, 'lambda.exe');
        const tempDir = path.join(PROJECT_ROOT, 'temp', 'analysis');
        
        if (!fs.existsSync(tempDir)) {
            fs.mkdirSync(tempDir, { recursive: true });
        }
        
        const htmlFile = path.join(tempDir, 'output.html');
        
        const child = spawn(lambdaExe, ['math', latex, '--output-html', htmlFile], {
            cwd: PROJECT_ROOT
        });
        
        child.on('close', (code) => {
            if (code === 0 && fs.existsSync(htmlFile)) {
                resolve(fs.readFileSync(htmlFile, 'utf8'));
            } else {
                resolve(null);
            }
        });
    });
}

// Main analysis
async function main() {
    const testCases = [
        { name: 'fracs_basic_3', latex: '\\frac{a^n}{b^n}' },
        { name: 'fracs_basic_0', latex: '\\frac{a}{b}' },
        { name: 'nested_test2_0', latex: '\\left[\\left(\\left\\{\\left|\\frac{a}{b}\\right|\\right\\}\\right)\\right]' },
    ];
    
    for (const test of testCases) {
        console.log('='.repeat(60));
        console.log(`Test: ${test.name}`);
        console.log(`LaTeX: ${test.latex}`);
        console.log('='.repeat(60));
        
        // Load MathLive reference
        const refFile = path.join(__dirname, 'reference', `${test.name}.mathlive.html`);
        if (!fs.existsSync(refFile)) {
            console.log(`  Reference not found: ${refFile}`);
            continue;
        }
        const mathliveHtml = fs.readFileSync(refFile, 'utf8');
        
        // Generate Lambda output
        const lambdaHtml = await runLambda(test.latex);
        if (!lambdaHtml) {
            console.log('  Lambda failed to generate HTML');
            continue;
        }
        
        console.log('\n📊 Comparison:');
        console.log(`  MathLive text: ${JSON.stringify(extractTexts(mathliveHtml))}`);
        console.log(`  Lambda text:   ${JSON.stringify(extractTexts(lambdaHtml))}`);
        console.log(`  MathLive elements: ${countElements(mathliveHtml)}`);
        console.log(`  Lambda elements:   ${countElements(lambdaHtml)}`);
        
        console.log('\n📋 MathLive Classes:');
        console.log(`  ${extractClasses(mathliveHtml).join(', ')}`);
        console.log('\n📋 Lambda Classes:');
        console.log(`  ${extractClasses(lambdaHtml).join(', ')}`);
        
        // Show class differences
        const mlClasses = new Set(extractClasses(mathliveHtml));
        const lambdaClasses = new Set(extractClasses(lambdaHtml));
        const onlyInML = [...mlClasses].filter(c => !lambdaClasses.has(c));
        const onlyInLambda = [...lambdaClasses].filter(c => !mlClasses.has(c));
        
        if (onlyInML.length > 0) {
            console.log('\n⚠️ Classes only in MathLive:', onlyInML.join(', '));
        }
        if (onlyInLambda.length > 0) {
            console.log('\n⚠️ Classes only in Lambda:', onlyInLambda.join(', '));
        }
        
        console.log('\n');
    }
}

main().catch(console.error);
