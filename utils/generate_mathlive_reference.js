#!/usr/bin/env node
/**
 * Generate MathLive reference HTML files for LaTeX math comparison tests.
 * 
 * This script reads LaTeX test files, extracts math formulas, and generates
 * reference HTML using MathLive's convertLatexToMarkup() function.
 * 
 * Usage:
 *   node utils/generate_mathlive_reference.js [--all | --file <name>]
 * 
 * Options:
 *   --all         Generate references for all test files
 *   --file <name> Generate reference for a specific test file (without .tex extension)
 */

const fs = require('fs');
const path = require('path');

// Import MathLive's SSR function
let convertLatexToMarkup;

async function loadMathLive() {
    try {
        // Try ESM import first
        const mathlive = await import('../mathlive/dist/mathlive-ssr.min.mjs');
        convertLatexToMarkup = mathlive.convertLatexToMarkup;
        return true;
    } catch (e) {
        console.error('Error: Could not load MathLive:', e.message);
        console.error('Make sure MathLive is built: cd mathlive && npm run build');
        return false;
    }
}

const LATEX_DIR = path.join(__dirname, '..', 'test', 'latex');
const REFERENCE_DIR = path.join(LATEX_DIR, 'reference', 'mathlive');

// Regex patterns to extract math from LaTeX files
const DISPLAY_MATH_PATTERNS = [
    /\$\$([\s\S]*?)\$\$/g,           // $$ ... $$
    /\\\[([\s\S]*?)\\\]/g,           // \[ ... \]
    /\\begin\{equation\*?\}([\s\S]*?)\\end\{equation\*?\}/g,
    /\\begin\{align\*?\}([\s\S]*?)\\end\{align\*?\}/g,
    /\\begin\{gather\*?\}([\s\S]*?)\\end\{gather\*?\}/g,
];

const INLINE_MATH_PATTERNS = [
    /(?<!\$)\$(?!\$)(.*?)\$/g,       // $ ... $ (not $$)
    /\\\((.*?)\\\)/g,                // \( ... \)
];

/**
 * Extract all math formulas from a LaTeX file.
 */
function extractMathFromLatex(content) {
    const formulas = [];
    
    // Extract display math
    for (const pattern of DISPLAY_MATH_PATTERNS) {
        let match;
        const regex = new RegExp(pattern.source, pattern.flags);
        while ((match = regex.exec(content)) !== null) {
            formulas.push({
                latex: match[1].trim(),
                mode: 'math',  // display mode
                original: match[0]
            });
        }
    }
    
    // Extract inline math
    for (const pattern of INLINE_MATH_PATTERNS) {
        let match;
        const regex = new RegExp(pattern.source, pattern.flags);
        while ((match = regex.exec(content)) !== null) {
            formulas.push({
                latex: match[1].trim(),
                mode: 'inline-math',
                original: match[0]
            });
        }
    }
    
    return formulas;
}

/**
 * Generate MathLive HTML for a formula.
 */
function generateMathLiveHtml(latex, mode) {
    try {
        return convertLatexToMarkup(latex, { defaultMode: mode });
    } catch (error) {
        console.warn(`  Warning: MathLive error for "${latex.substring(0, 50)}...": ${error.message}`);
        return null;
    }
}

/**
 * Process a single LaTeX test file and generate reference HTML.
 */
function processTestFile(testName) {
    const latexPath = path.join(LATEX_DIR, `${testName}.tex`);
    
    if (!fs.existsSync(latexPath)) {
        console.error(`Error: Test file not found: ${latexPath}`);
        return false;
    }
    
    console.log(`Processing: ${testName}`);
    
    const content = fs.readFileSync(latexPath, 'utf8');
    const formulas = extractMathFromLatex(content);
    
    if (formulas.length === 0) {
        console.log(`  No math formulas found in ${testName}`);
        return true;
    }
    
    console.log(`  Found ${formulas.length} formula(s)`);
    
    // Generate reference HTML for each formula
    const references = [];
    for (let i = 0; i < formulas.length; i++) {
        const formula = formulas[i];
        const html = generateMathLiveHtml(formula.latex, formula.mode);
        
        if (html) {
            references.push({
                index: i,
                latex: formula.latex,
                mode: formula.mode,
                html: html
            });
        }
    }
    
    if (references.length === 0) {
        console.log(`  No valid references generated for ${testName}`);
        return true;
    }
    
    // Write JSON reference file
    const outputPath = path.join(REFERENCE_DIR, `${testName}.json`);
    const output = {
        source: `${testName}.tex`,
        generated: new Date().toISOString(),
        generator: 'MathLive',
        formulas: references
    };
    
    fs.writeFileSync(outputPath, JSON.stringify(output, null, 2));
    console.log(`  Generated: ${outputPath} (${references.length} formulas)`);
    
    return true;
}

/**
 * Get all test files in the latex directory.
 */
function getAllTestFiles() {
    const files = fs.readdirSync(LATEX_DIR);
    return files
        .filter(f => f.startsWith('test_') && f.endsWith('.tex'))
        .map(f => f.replace('.tex', ''));
}

/**
 * Main entry point.
 */
async function main() {
    // Load MathLive
    if (!await loadMathLive()) {
        process.exit(1);
    }
    
    const args = process.argv.slice(2);
    
    // Ensure reference directory exists
    if (!fs.existsSync(REFERENCE_DIR)) {
        fs.mkdirSync(REFERENCE_DIR, { recursive: true });
    }
    
    let testFiles = [];
    
    if (args.includes('--all')) {
        testFiles = getAllTestFiles();
    } else if (args.includes('--file')) {
        const fileIndex = args.indexOf('--file');
        if (fileIndex + 1 < args.length) {
            testFiles = [args[fileIndex + 1]];
        } else {
            console.error('Error: --file requires a test name argument');
            process.exit(1);
        }
    } else {
        // Default: process baseline test files
        testFiles = [
            'test_simple_math',
            'test_fraction',
            'test_greek',
            'test_sqrt',
            'test_subscript_superscript',
            'test_delimiters',
            'test_sum_integral',
            'test_complex_formula',
        ];
    }
    
    console.log(`\nGenerating MathLive reference HTML files...\n`);
    console.log(`Reference directory: ${REFERENCE_DIR}\n`);
    
    let success = 0;
    let failed = 0;
    
    for (const testFile of testFiles) {
        if (processTestFile(testFile)) {
            success++;
        } else {
            failed++;
        }
    }
    
    console.log(`\nDone: ${success} succeeded, ${failed} failed`);
}

main();
