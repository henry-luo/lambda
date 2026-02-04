#!/usr/bin/env node

/**
 * Generate Real References from MathLive and KaTeX
 *
 * This creates authoritative reference files that Lambda should be compared against,
 * NOT Lambda's own output.
 * 
 * Usage:
 *   node generate_real_references.mjs [options]
 * 
 * Options:
 *   --test <name>     Process only test files matching <name> (e.g., "fracs_basic", "fracs")
 *   --help, -h        Show this help message
 * 
 * Examples:
 *   node generate_real_references.mjs                    # Regenerate all references
 *   node generate_real_references.mjs --test fracs_basic # Only fracs_basic.tex
 *   node generate_real_references.mjs --test fracs       # All tests starting with "fracs"
 */

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import * as mathlive from '../../mathlive/dist/mathlive-ssr.min.mjs';

// Try to load KaTeX if available
let katex = null;
try {
    katex = (await import('katex')).default;
} catch (e) {
    // KaTeX not installed - will skip KaTeX reference generation
}

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const FIXTURES_MATH_DIR = path.join(__dirname, 'fixtures', 'math');
const REFERENCE_DIR = path.join(__dirname, 'reference');

// Ensure reference directory exists
if (!fs.existsSync(REFERENCE_DIR)) {
    fs.mkdirSync(REFERENCE_DIR, { recursive: true });
}

/**
 * Parse command line arguments
 */
function parseArgs() {
    const args = process.argv.slice(2);
    const options = {
        testFilter: null,
        help: false
    };
    
    for (let i = 0; i < args.length; i++) {
        const arg = args[i];
        switch (arg) {
            case '--test':
                options.testFilter = args[++i];
                break;
            case '--help':
            case '-h':
                options.help = true;
                break;
        }
    }
    
    return options;
}

/**
 * Print help message
 */
function printHelp() {
    console.log(`
Generate Real References from MathLive and KaTeX

Usage: node generate_real_references.mjs [options]

Options:
  --test <name>     Process only test files matching <name>
                    (e.g., "fracs_basic" for exact match, "fracs" for prefix match)
  --help, -h        Show this help message

Examples:
  node generate_real_references.mjs                    # Regenerate all references
  node generate_real_references.mjs --test fracs_basic # Only fracs_basic.tex
  node generate_real_references.mjs --test fracs       # All tests starting with "fracs"
`);
}

/**
 * Parse a .tex file and extract math expressions
 * Supports: $...$, $$...$$, \[...\], \(...\)
 */
function parseTexFile(texPath) {
    const content = fs.readFileSync(texPath, 'utf-8');
    const expressions = [];
    let index = 0;

    // Remove comments (lines starting with %)
    const lines = content.split('\n');
    const cleanLines = lines.filter(line => !line.trim().startsWith('%'));
    const cleanContent = cleanLines.join('\n');

    // Pattern for display math: \[...\] or $$...$$
    const displayMathRegex = /\\\[([\s\S]*?)\\\]|\$\$([\s\S]*?)\$\$/g;
    let match;

    while ((match = displayMathRegex.exec(cleanContent)) !== null) {
        const latex = (match[1] || match[2]).trim();
        if (latex && !latex.includes('\\begin{document}') && !latex.includes('\\end{document}')) {
            expressions.push({
                index: index++,
                type: 'display',
                latex: latex
            });
        }
    }

    // Pattern for inline math: $...$  (but not $$)
    const inlineMathRegex = /(?<!\$)\$(?!\$)([\s\S]*?)(?<!\$)\$(?!\$)/g;
    while ((match = inlineMathRegex.exec(cleanContent)) !== null) {
        const latex = match[1].trim();
        if (latex && !latex.includes('\\begin{document}') && !latex.includes('\\end{document}')) {
            expressions.push({
                index: index++,
                type: 'inline',
                latex: latex
            });
        }
    }

    // Pattern for \(...\) inline math
    const inlineParenRegex = /\\\(([\s\S]*?)\\\)/g;
    while ((match = inlineParenRegex.exec(cleanContent)) !== null) {
        const latex = match[1].trim();
        if (latex) {
            expressions.push({
                index: index++,
                type: 'inline',
                latex: latex
            });
        }
    }

    return expressions;
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
    if (!katex) {
        return null; // KaTeX not available
    }
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
 * Process a single .tex test file
 */
function processTexFile(texPath) {
    const testName = path.basename(texPath, '.tex');
    console.log(`Processing: ${testName}`);

    let expressions;
    try {
        expressions = parseTexFile(texPath);
    } catch (e) {
        console.error(`  Failed to parse ${texPath}: ${e.message}`);
        return { generated: 0, errors: 0 };
    }

    if (expressions.length === 0) {
        console.log(`  No expressions found`);
        return { generated: 0, errors: 0 };
    }

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

    console.log(`  Generated ${generated} references (${expressions.length} expressions), ${errors} errors`);
    return { generated, errors };
}

/**
 * Recursively collect all .tex files from a directory
 */
function collectTexFiles(dir) {
    const files = [];
    if (!fs.existsSync(dir)) return files;
    
    const entries = fs.readdirSync(dir, { withFileTypes: true });
    for (const entry of entries) {
        const fullPath = path.join(dir, entry.name);
        if (entry.isDirectory()) {
            files.push(...collectTexFiles(fullPath));
        } else if (entry.name.endsWith('.tex')) {
            files.push(fullPath);
        }
    }
    return files;
}

/**
 * Main function
 */
async function main() {
    const options = parseArgs();
    
    if (options.help) {
        printHelp();
        return;
    }
    
    console.log('='.repeat(60));
    console.log('Generating MathLive and KaTeX References');
    if (!katex) {
        console.log('(KaTeX not available - generating MathLive references only)');
    }
    console.log('='.repeat(60));

    // Collect all .tex test files from fixtures/math
    let testFiles = collectTexFiles(FIXTURES_MATH_DIR);

    // Filter by test name if specified
    if (options.testFilter) {
        const filter = options.testFilter.replace(/\.tex$/, ''); // remove .tex if present
        testFiles = testFiles.filter(f => {
            const baseName = path.basename(f, '.tex');
            // exact match or prefix match
            return baseName === filter || baseName.startsWith(filter);
        });
        
        if (testFiles.length === 0) {
            console.error(`No test files found matching: ${filter}`);
            console.error(`Searched in: ${FIXTURES_MATH_DIR}`);
            return;
        }
        
        console.log(`Filter: "${filter}" matched ${testFiles.length} file(s)`);
    }

    console.log(`Found ${testFiles.length} test files`);
    console.log();

    let totalGenerated = 0;
    let totalErrors = 0;

    for (const testFile of testFiles) {
        const { generated, errors } = processTexFile(testFile);
        totalGenerated += generated;
        totalErrors += errors;
    }

    console.log();
    console.log('='.repeat(60));
    console.log(`Total: ${totalGenerated} references generated, ${totalErrors} errors`);
    console.log('='.repeat(60));
}

main().catch(console.error);
