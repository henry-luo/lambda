#!/usr/bin/env node

/**
 * Generate Real References from MathLive and KaTeX
 *
 * This creates authoritative reference files that Lambda should be compared against,
 * NOT Lambda's own output.
 */

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import * as mathlive from 'mathlive';
import katex from 'katex';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const MATH_DIR = path.join(__dirname, 'math');
const MATH_AST_DIR = path.join(__dirname, 'math-ast');
const REFERENCE_DIR = path.join(__dirname, 'reference');

// Ensure reference directory exists
if (!fs.existsSync(REFERENCE_DIR)) {
    fs.mkdirSync(REFERENCE_DIR, { recursive: true });
}

/**
 * Generate MathLive HTML for a LaTeX expression
 */
function generateMathLiveHTML(latex) {
    try {
        return mathlive.convertLatexToMarkup(latex, { defaultMode: 'math' });
    } catch (e) {
        console.error(`  MathLive error for "${latex}": ${e.message}`);
        return null;
    }
}

/**
 * Generate KaTeX HTML for a LaTeX expression
 */
function generateKaTeXHTML(latex, displayMode = true) {
    try {
        return katex.renderToString(latex, {
            displayMode,
            throwOnError: false,
            strict: false
        });
    } catch (e) {
        console.error(`  KaTeX error for "${latex}": ${e.message}`);
        return null;
    }
}

/**
 * Process a single JSON test file
 */
function processTestFile(testPath) {
    const testName = path.basename(testPath, '.json');
    console.log(`Processing: ${testName}`);

    let testData;
    try {
        testData = JSON.parse(fs.readFileSync(testPath, 'utf-8'));
    } catch (e) {
        console.error(`  Failed to read ${testPath}: ${e.message}`);
        return { generated: 0, errors: 0 };
    }

    const expressions = testData.expressions || [];
    let generated = 0;
    let errors = 0;

    for (const expr of expressions) {
        const latex = expr.latex;
        const index = expr.index;
        const baseName = `${testName}_${index}`;

        // Generate MathLive HTML
        const mathliveHTML = generateMathLiveHTML(latex);
        if (mathliveHTML) {
            const mathliveFile = path.join(REFERENCE_DIR, `${baseName}.mathlive.html`);
            fs.writeFileSync(mathliveFile, mathliveHTML);
            generated++;
        } else {
            errors++;
        }

        // Generate KaTeX HTML
        const displayMode = expr.type !== 'inline';
        const katexHTML = generateKaTeXHTML(latex, displayMode);
        if (katexHTML) {
            const katexFile = path.join(REFERENCE_DIR, `${baseName}.katex.html`);
            fs.writeFileSync(katexFile, katexHTML);
            generated++;
        } else {
            errors++;
        }
    }

    console.log(`  Generated ${generated} references, ${errors} errors`);
    return { generated, errors };
}

/**
 * Main function
 */
async function main() {
    console.log('='.repeat(60));
    console.log('Generating MathLive and KaTeX References');
    console.log('='.repeat(60));

    // Collect all JSON test files
    const testFiles = [];

    // From math/ directory
    if (fs.existsSync(MATH_DIR)) {
        const mathFiles = fs.readdirSync(MATH_DIR)
            .filter(f => f.endsWith('.json'))
            .map(f => path.join(MATH_DIR, f));
        testFiles.push(...mathFiles);
    }

    // From math-ast/ directory
    if (fs.existsSync(MATH_AST_DIR)) {
        const astFiles = fs.readdirSync(MATH_AST_DIR)
            .filter(f => f.endsWith('.json'))
            .map(f => path.join(MATH_AST_DIR, f));
        testFiles.push(...astFiles);
    }

    console.log(`Found ${testFiles.length} test files`);
    console.log();

    let totalGenerated = 0;
    let totalErrors = 0;

    for (const testFile of testFiles) {
        const { generated, errors } = processTestFile(testFile);
        totalGenerated += generated;
        totalErrors += errors;
    }

    console.log();
    console.log('='.repeat(60));
    console.log(`Total: ${totalGenerated} references generated, ${totalErrors} errors`);
    console.log('='.repeat(60));
}

main().catch(console.error);
