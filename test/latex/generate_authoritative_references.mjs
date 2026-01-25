#!/usr/bin/env node

/**
 * Generate Authoritative References
 *
 * 1. AST: MathML from MathLive (semantic representation)
 * 2. HTML: Already generated from MathLive/KaTeX
 * 3. DVI: From pdfTeX/LaTeX compilation
 */

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { execSync, spawn } from 'child_process';
import * as mathlive from 'mathlive';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const MATH_DIR = path.join(__dirname, 'math');
const MATH_AST_DIR = path.join(__dirname, 'math-ast');
const REFERENCE_DIR = path.join(__dirname, 'reference');
const TEMP_DIR = path.join(__dirname, '..', '..', 'temp', 'latex_refs');

// Ensure directories exist
if (!fs.existsSync(REFERENCE_DIR)) {
    fs.mkdirSync(REFERENCE_DIR, { recursive: true });
}
if (!fs.existsSync(TEMP_DIR)) {
    fs.mkdirSync(TEMP_DIR, { recursive: true });
}

/**
 * Generate MathML from MathLive as AST reference
 */
function generateMathML(latex) {
    try {
        const mathml = mathlive.convertLatexToMathMl(latex);
        // Parse MathML to JSON-like structure for comparison
        return {
            format: 'mathml',
            latex: latex,
            mathml: mathml,
            // Also create a normalized structure for comparison
            structure: parseMathMLToStructure(mathml)
        };
    } catch (e) {
        console.error(`  MathML error for "${latex}": ${e.message}`);
        return null;
    }
}

/**
 * Parse MathML string to a simplified structure for comparison
 */
function parseMathMLToStructure(mathml) {
    // Simple regex-based parser for MathML structure
    const structure = {
        type: 'root',
        children: []
    };

    // Extract element hierarchy
    const tagPattern = /<(\w+)([^>]*)>(.*?)<\/\1>|<(\w+)([^>]*)\/>/gs;
    let match;

    function parseElement(content) {
        const elements = [];
        const simpleTagPattern = /<(m\w+)(?:\s[^>]*)?>([^<]*)<\/\1>/g;
        let m;
        while ((m = simpleTagPattern.exec(content)) !== null) {
            elements.push({
                tag: m[1],
                content: m[2] || null
            });
        }
        return elements;
    }

    structure.children = parseElement(mathml);
    return structure;
}

/**
 * Generate DVI from pdfTeX
 */
function generateDVI(latex, outputPath) {
    const tempTexFile = path.join(TEMP_DIR, 'temp_formula.tex');
    const tempDviFile = path.join(TEMP_DIR, 'temp_formula.dvi');

    // Create minimal LaTeX document
    const texContent = `\\documentclass{article}
\\usepackage{amsmath}
\\usepackage{amssymb}
\\pagestyle{empty}
\\begin{document}
\\[
${latex}
\\]
\\end{document}
`;

    try {
        fs.writeFileSync(tempTexFile, texContent);

        // Run latex to generate DVI
        execSync(`latex -interaction=nonstopmode -output-directory="${TEMP_DIR}" "${tempTexFile}"`, {
            cwd: TEMP_DIR,
            stdio: 'pipe',
            timeout: 10000
        });

        if (fs.existsSync(tempDviFile)) {
            fs.copyFileSync(tempDviFile, outputPath);
            return true;
        }
        return false;
    } catch (e) {
        // LaTeX compilation failed - common for complex formulas
        return false;
    } finally {
        // Cleanup temp files
        try {
            const extensions = ['.tex', '.dvi', '.aux', '.log'];
            for (const ext of extensions) {
                const file = path.join(TEMP_DIR, 'temp_formula' + ext);
                if (fs.existsSync(file)) fs.unlinkSync(file);
            }
        } catch (e) { /* ignore cleanup errors */ }
    }
}

/**
 * Process a single JSON test file
 */
function processTestFile(testPath, options = {}) {
    const testName = path.basename(testPath, '.json');
    console.log(`Processing: ${testName}`);

    let testData;
    try {
        testData = JSON.parse(fs.readFileSync(testPath, 'utf-8'));
    } catch (e) {
        console.error(`  Failed to read ${testPath}: ${e.message}`);
        return { mathml: 0, dvi: 0, errors: 0 };
    }

    const expressions = testData.expressions || [];
    let mathmlCount = 0;
    let dviCount = 0;
    let errors = 0;

    for (const expr of expressions) {
        const latex = expr.latex;
        const index = expr.index;
        const baseName = `${testName}_${index}`;

        // Generate MathML AST reference
        if (options.mathml !== false) {
            const mathmlData = generateMathML(latex);
            if (mathmlData) {
                const mathmlFile = path.join(REFERENCE_DIR, `${baseName}.mathml.json`);
                fs.writeFileSync(mathmlFile, JSON.stringify(mathmlData, null, 2));
                mathmlCount++;
            } else {
                errors++;
            }
        }

        // Generate pdfTeX DVI reference
        if (options.dvi !== false) {
            const dviFile = path.join(REFERENCE_DIR, `${baseName}.pdftex.dvi`);
            if (generateDVI(latex, dviFile)) {
                dviCount++;
            } else {
                // DVI generation failed - not an error, just unsupported
            }
        }
    }

    console.log(`  MathML: ${mathmlCount}, DVI: ${dviCount}, Errors: ${errors}`);
    return { mathml: mathmlCount, dvi: dviCount, errors };
}

/**
 * Main function
 */
async function main() {
    const args = process.argv.slice(2);
    const options = {
        mathml: !args.includes('--no-mathml'),
        dvi: !args.includes('--no-dvi'),
        mathmlOnly: args.includes('--mathml-only'),
        dviOnly: args.includes('--dvi-only')
    };

    if (options.mathmlOnly) {
        options.dvi = false;
    }
    if (options.dviOnly) {
        options.mathml = false;
    }

    console.log('='.repeat(60));
    console.log('Generating Authoritative References');
    console.log('='.repeat(60));
    console.log(`  MathML (AST): ${options.mathml ? 'Yes' : 'No'}`);
    console.log(`  pdfTeX DVI:   ${options.dvi ? 'Yes' : 'No'}`);
    console.log();

    // Collect all JSON test files
    const testFiles = [];

    if (fs.existsSync(MATH_DIR)) {
        const mathFiles = fs.readdirSync(MATH_DIR)
            .filter(f => f.endsWith('.json'))
            .map(f => path.join(MATH_DIR, f));
        testFiles.push(...mathFiles);
    }

    if (fs.existsSync(MATH_AST_DIR)) {
        const astFiles = fs.readdirSync(MATH_AST_DIR)
            .filter(f => f.endsWith('.json'))
            .map(f => path.join(MATH_AST_DIR, f));
        testFiles.push(...astFiles);
    }

    console.log(`Found ${testFiles.length} test files`);
    console.log();

    let totalMathML = 0;
    let totalDVI = 0;
    let totalErrors = 0;

    for (const testFile of testFiles) {
        const result = processTestFile(testFile, options);
        totalMathML += result.mathml;
        totalDVI += result.dvi;
        totalErrors += result.errors;
    }

    console.log();
    console.log('='.repeat(60));
    console.log(`Total: ${totalMathML} MathML, ${totalDVI} DVI references generated`);
    if (totalErrors > 0) {
        console.log(`Errors: ${totalErrors}`);
    }
    console.log('='.repeat(60));
}

main().catch(console.error);
