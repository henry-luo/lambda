#!/usr/bin/env node

/**
 * LaTeX Math Test Runner
 *
 * Multi-layered semantic comparison framework for LaTeX math typesetting.
 * Compares Lambda's output against references at three abstraction levels:
 *
 * 1. AST Layer (50% weight): Structural/semantic correctness
 * 2. HTML Layer (40% weight): Visual representation correctness
 * 3. DVI Layer (10% weight): Precise typographic correctness
 *
 * Usage:
 *   node test_math_comparison.js [options]
 *
 * Options:
 *   --suite <name>        Test suite: 'baseline', 'extended', or 'all' (default)
 *   --test <file>         Run specific test file
 *   --group <prefix>      Run tests with given prefix (e.g., 'fracs', 'scripts')
 *   --compare <layer>     Run specific comparison: 'ast', 'html', 'dvi', or 'all'
 *   --tolerance <px>      Position tolerance for DVI comparison (default: 0.5)
 *   --verbose, -v         Show detailed comparison output
 *   --json                Output results as JSON
 *   --threshold <pct>     Minimum pass threshold (default: 80)
 *   --help, -h            Show help
 */

import fs from 'fs';
import path from 'path';
import { execSync } from 'child_process';
import { fileURLToPath } from 'url';

// ES module __dirname equivalent
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Import comparators
import { compareAST } from './comparators/ast_comparator.js';
import { compareHTML } from './comparators/html_comparator.js';
import { compareDVI, validateDVI } from './comparators/dvi_comparator.js';
import { compareASTToMathML } from './comparators/mathml_comparator.js';
import { compareASTToMathLive } from './comparators/mathlive_ast_comparator.js';
import {
    lambda_to_mathlive_classes,
    mathlive_expected_error,
    render_mathlive_markup,
    render_lambda_math
} from '../lambda/mathlive/lambda_math_renderer.mjs';

// Configuration
const CONFIG = {
    weights: {
        ast: 0.50,   // 50% - semantic structure
        html: 0.49,  // 49% - visual representation
        dvi: 0.01    // 1% - precise typographics
    },
    defaultThreshold: 80,
    dviTolerance: 0.5
};

// Directories
const TEST_DIR = __dirname;
const FIXTURES_MATH_DIR = path.join(TEST_DIR, 'fixtures', 'math');
const BASELINE_DIR = path.join(TEST_DIR, 'baseline');
const REFERENCE_DIR = path.join(TEST_DIR, 'reference');
const PROJECT_ROOT = path.join(TEST_DIR, '..', '..');

// Feature group prefixes (all tex files start with group_ prefix)
const FEATURE_GROUPS = [
    'accents',    // Accents, decorations, over/under
    'arrays',     // Arrays, matrices, environments
    'bigops',     // Big operators (sum, int, prod)
    'boxes',      // Boxes, rules, dimensions
    'delims',     // Delimiters, left/right, sizing
    'fonts',      // Font styles, mode shifts
    'fracs',      // Fractions, binomials, choose
    'greek',      // Greek letters
    'misc',       // Miscellaneous
    'negation',   // Negation, not
    'nested',     // Nested/complex structures
    'operators',  // Operators, relations
    'radicals',   // Square roots, nth roots
    'scripts',    // Subscripts, superscripts
    'spacing',    // Spacing, phantoms
    'styles',     // Display styles, sizing
    'subjects',   // Real-world subject tests
    'symbols'     // Ordinary symbols, arrows
];

/**
 * Parse command line arguments
 */
function parseArgs() {
    const args = process.argv.slice(2);
    const options = {
        suite: 'all',
        test: null,
        group: null,
        // The script-level math API exposes the tree-sitter AST, not the
        // retired CLI's MathLive-shaped AST. HTML is the stable package API.
        compare: 'html',
        tolerance: CONFIG.dviTolerance,
        verbose: false,
        json: false,
        threshold: CONFIG.defaultThreshold
    };

    for (let i = 0; i < args.length; i++) {
        const arg = args[i];
        const next = args[i + 1];

        switch (arg) {
            case '--suite':
                options.suite = next;
                i++;
                break;
            case '--test':
                options.test = next;
                i++;
                break;
            case '--group':
                options.group = next;
                i++;
                break;
            case '--compare':
                options.compare = next;
                i++;
                break;
            case '--tolerance':
                options.tolerance = parseFloat(next);
                i++;
                break;
            case '--verbose':
            case '-v':
                options.verbose = true;
                break;
            case '--json':
                options.json = true;
                break;
            case '--threshold':
                options.threshold = parseFloat(next);
                i++;
                break;
            case '--help':
            case '-h':
                printHelp();
                process.exit(0);
        }
    }

    return options;
}

/**
 * Print help message
 */
function printHelp() {
    console.log(`
LaTeX Math Test Runner

Usage: node test_math_comparison.js [options]

Options:
  --suite <name>        Test suite: 'baseline', 'extended', or 'all' (default)
  --test <file>         Run specific test file
  --group <prefix>      Run tests with given prefix (e.g., 'fracs', 'scripts')
  --compare <layer>     Run specific comparison: 'ast', 'html', 'dvi', or 'all'
  --tolerance <pt>      Position tolerance for DVI comparison (default: 0.5)
  --verbose, -v         Show detailed comparison output
  --json                Output results as JSON
  --threshold <pct>     Minimum pass threshold (default: 80)
  --help, -h            Show help

Examples:
  node test_math_comparison.js                          # Run all tests
  node test_math_comparison.js --suite baseline         # Run baseline tests only
  node test_math_comparison.js --group fracs            # Run fraction tests
  node test_math_comparison.js --test fracs_basic -v    # Run specific test with details
  node test_math_comparison.js --compare ast            # AST comparison only
`);
}

/**
 * Collect test files based on options
 */
function collectTestFiles(options) {
    const tests = [];

    // Single test mode
    if (options.test) {
        const testPath = findTestFile(options.test);
        if (testPath) {
            tests.push(testPath);
        } else {
            console.error(`Test not found: ${options.test}`);
        }
        return tests;
    }

    // Baseline tests
    if (options.suite === 'baseline' || options.suite === 'all') {
        if (fs.existsSync(BASELINE_DIR)) {
            const baselineTests = fs.readdirSync(BASELINE_DIR)
                .filter(f => f.endsWith('.tex'))
                .map(f => ({ file: f, path: path.join(BASELINE_DIR, f), suite: 'baseline' }));
            tests.push(...baselineTests);
        }
    }

    // Extended tests (from fixtures/math directory - tex files)
    if (options.suite === 'extended' || options.suite === 'all') {
        // Recursively collect all .tex files from fixtures/math
        const collectTexFiles = (dir, prefix = '') => {
            const results = [];
            if (!fs.existsSync(dir)) return results;

            const entries = fs.readdirSync(dir, { withFileTypes: true });
            for (const entry of entries) {
                const fullPath = path.join(dir, entry.name);
                if (entry.isDirectory()) {
                    // Recurse into subdirectories
                    const subPrefix = prefix ? `${prefix}/${entry.name}` : entry.name;
                    results.push(...collectTexFiles(fullPath, subPrefix));
                } else if (entry.name.endsWith('.tex')) {
                    const relPath = prefix ? `${prefix}/${entry.name}` : entry.name;
                    results.push({
                        file: entry.name,
                        path: fullPath,
                        relPath: relPath,
                        suite: 'extended'
                    });
                }
            }
            return results;
        };

        let texTests = collectTexFiles(FIXTURES_MATH_DIR);

        // Filter by group if specified
        if (options.group) {
            texTests = texTests.filter(t =>
                t.file.startsWith(options.group + '_') ||
                t.file.startsWith(options.group + '.') ||
                t.relPath.includes('/' + options.group + '/') ||
                t.relPath.startsWith(options.group + '/')
            );
        }

        tests.push(...texTests);
    }

    return tests;
}

/**
 * Find a test file by name
 */
function findTestFile(name) {
    // Check baseline directory for .tex files
    const texPath = path.join(BASELINE_DIR, name.endsWith('.tex') ? name : `${name}.tex`);
    if (fs.existsSync(texPath)) {
        return { file: path.basename(texPath), path: texPath, suite: 'baseline' };
    }

    // Check fixtures/math directory for .tex files
    const fixturesTexPath = path.join(FIXTURES_MATH_DIR, name.endsWith('.tex') ? name : `${name}.tex`);
    if (fs.existsSync(fixturesTexPath)) {
        return { file: path.basename(fixturesTexPath), path: fixturesTexPath, suite: 'extended' };
    }

    // Check fixtures/math/mathlive subdirectory
    const mathlivePath = path.join(FIXTURES_MATH_DIR, 'mathlive', name.endsWith('.tex') ? name : `${name}.tex`);
    if (fs.existsSync(mathlivePath)) {
        return { file: path.basename(mathlivePath), path: mathlivePath, relPath: `mathlive/${path.basename(mathlivePath)}`, suite: 'extended' };
    }

    return null;
}

/**
 * Load test data from a JSON file (legacy format)
 */
function loadTestDataJson(testPath) {
    try {
        const content = fs.readFileSync(testPath, 'utf-8');
        return JSON.parse(content);
    } catch (error) {
        return { error: error.message };
    }
}

/**
 * Parse a .tex file and extract math expressions
 * Supports: $...$, $$...$$, \[...\], \(...\), and common math environments
 */
function parseTexFile(testPath) {
    try {
        const content = fs.readFileSync(testPath, 'utf-8');
        const expressions = [];
        let index = 0;

        // Remove comments (lines starting with %)
        const lines = content.split('\n');
        const cleanLines = lines.filter(line => !line.trim().startsWith('%'));
        const cleanContent = stripCommandDefinitions(cleanLines.join('\n'));

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

        // Inline `$...$` may itself contain text-mode math switches, such as
        // `\textcolor{blue}{$x$}`.  A non-greedy regexp terminates at that
        // inner dollar, so scan balanced braces and accept a terminator only
        // at the outer math depth.
        let inlineStart = -1;
        let braceDepth = 0;
        let recoveryEnd = -1;
        const appendInlineExpression = (end) => {
            const latex = cleanContent.slice(inlineStart, end).trim();
            if (latex && !latex.includes('\\begin{document}') && !latex.includes('\\end{document}')) {
                expressions.push({
                    index: index++,
                    type: 'inline',
                    latex: latex
                });
            }
        };
        for (let i = 0; i < cleanContent.length; i++) {
            const ch = cleanContent[i];
            const escaped = i > 0 && cleanContent[i - 1] === '\\';
            if (inlineStart < 0) {
                if (ch === '$' && !escaped && cleanContent[i - 1] !== '$' && cleanContent[i + 1] !== '$') {
                    inlineStart = i + 1;
                    braceDepth = 0;
                    recoveryEnd = -1;
                }
                continue;
            }
            if (!escaped && ch === '{') braceDepth++;
            else if (!escaped && ch === '}' && braceDepth > 0) braceDepth--;
            else if (!escaped && ch === '$' && cleanContent[i - 1] !== '$' && cleanContent[i + 1] !== '$') {
                if (braceDepth === 0) {
                    appendInlineExpression(i);
                    inlineStart = -1;
                } else {
                    // Preserve MathLive's malformed-input recovery: if an
                    // outer formula never balances, its final nested dollar
                    // still delimits the recoverable formula fragment.
                    recoveryEnd = i;
                }
            }
        }
        if (inlineStart >= 0 && recoveryEnd >= inlineStart) appendInlineExpression(recoveryEnd);

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

        return {
            source: path.basename(testPath),
            expressions: expressions
        };
    } catch (error) {
        return { error: error.message };
    }
}

function stripCommandDefinitions(content) {
    // Macro bodies are source declarations, not document math. In particular,
    // `$#2$` in a two-argument macro must not enter the rendering corpus.
    const definitions = /\\(?:re)?newcommand\b/g;
    let result = '';
    let cursor = 0;
    let match;
    while ((match = definitions.exec(content)) !== null) {
        const start = match.index;
        let end = match.index + match[0].length;
        while (/\s/.test(content[end] || '')) end++;
        if (content[end] === '*') end++;
        while (/\s/.test(content[end] || '')) end++;
        end = skipDelimitedTexArgument(content, end, '{', '}');
        while (/\s/.test(content[end] || '')) end++;
        while (content[end] === '[') {
            end = skipDelimitedTexArgument(content, end, '[', ']');
            while (/\s/.test(content[end] || '')) end++;
        }
        end = skipDelimitedTexArgument(content, end, '{', '}');
        result += content.slice(cursor, start);
        cursor = end;
        definitions.lastIndex = end;
    }
    return result + content.slice(cursor);
}

function skipDelimitedTexArgument(content, start, open, close) {
    if (content[start] !== open) return start;
    let depth = 0;
    for (let i = start; i < content.length; i++) {
        if (content[i] === '\\') {
            i++;
            continue;
        }
        if (content[i] === open) depth++;
        else if (content[i] === close && --depth === 0) return i + 1;
    }
    return content.length;
}

/**
 * Load test data - handles both .tex and .json files
 */
function loadTestData(testPath) {
    if (testPath.endsWith('.json')) {
        return loadTestDataJson(testPath);
    } else if (testPath.endsWith('.tex')) {
        return parseTexFile(testPath);
    } else {
        return { error: `Unknown file format: ${testPath}` };
    }
}

/**
 * Render one expression through Lambda's supported script-level math API.
 * The retired `lambda.exe math --output-*` CLI cannot produce test artifacts.
 */
async function runLambdaParser(latex, options = {}) {
    const lambdaExe = path.join(PROJECT_ROOT, 'lambda.exe');

    // Check if lambda.exe exists
    if (!fs.existsSync(lambdaExe)) {
        return {
            ast: null,
            html: null,
            dvi: null,
            error: 'lambda.exe not found. Run "make build" first.'
        };
    }

    try {
        const tempDir = path.join(PROJECT_ROOT, 'temp', 'math_tests');
        const testId = Math.random().toString(36).substring(7);
        const result = render_lambda_math(latex, {
            display: true,
            lambda: lambdaExe,
            script: path.join(tempDir, `${testId}.ls`)
        });
        return {
            ast: result.ast ?? null,
            html: result.html ?? null,
            // The static math package has no DVI writer. Keep this explicit so
            // scoring redistributes the retired DVI weight to AST and HTML.
            dvi: null,
            error: result.error === 'no-error' ? null : result.error
        };
    } catch (err) {
        return {
            ast: null,
            html: null,
            dvi: null,
            error: `Failed to render Lambda math script: ${err.message}`
        };
    }
}

async function compareMathLiveHtml(lambdaHtml, latex, display) {
    const expectedHtml = await render_mathlive_markup(latex, { display });
    return compareHTML(lambda_to_mathlive_classes(lambdaHtml), expectedHtml, 'mathlive');
}

/**
 * Calculate test score from component results
 */
function calculateTestScore(astResult, htmlResult, dviResult, compareMode) {
    let weights = { ...CONFIG.weights };

    // Adjust weights if only comparing specific layer
    if (compareMode !== 'all') {
        weights = { ast: 0, html: 0, dvi: 0 };
        weights[compareMode] = 1.0;
    }

    const astScore = astResult ? astResult.passRate : 0;
    const htmlScore = htmlResult ? htmlResult.passRate : 0;
    const dviScore = dviResult ? dviResult.passRate : 0;

    // If DVI reference doesn't exist or failed to generate (generationError),
    // redistribute DVI weight to AST and HTML proportionally
    const dviUnavailable = !dviResult || dviResult.generationError ||
        (dviResult.differences && dviResult.differences.some(d =>
            d.issue && (d.issue.includes('Failed to generate') || d.issue.includes('did not produce'))));

    if (dviUnavailable && compareMode === 'all') {
        // Redistribute DVI weight: AST gets 50.5%, HTML gets 49.5%
        const totalNonDvi = weights.ast + weights.html;
        weights.ast = weights.ast / totalNonDvi;
        weights.html = weights.html / totalNonDvi;
        weights.dvi = 0;
    }

    const overall =
        astScore * weights.ast +
        htmlScore * weights.html +
        dviScore * weights.dvi;

    return {
        overall: Math.round(overall * 10) / 10,
        breakdown: {
            ast: { rate: astScore, weighted: Math.round(astScore * weights.ast * 10) / 10 },
            html: { rate: htmlScore, weighted: Math.round(htmlScore * weights.html * 10) / 10 },
            dvi: { rate: dviScore, weighted: Math.round(dviScore * weights.dvi * 10) / 10, unavailable: dviUnavailable }
        }
    };
}

/**
 * Generate DVI reference on demand using pdfTeX/LaTeX
 */
function generateDVIReference(latex, outputPath) {
    const tempDir = path.join(PROJECT_ROOT, 'temp', 'dvi_refs');
    if (!fs.existsSync(tempDir)) {
        fs.mkdirSync(tempDir, { recursive: true });
    }

    const tempTexFile = path.join(tempDir, 'temp_formula.tex');
    const tempDviFile = path.join(tempDir, 'temp_formula.dvi');

    // Create minimal LaTeX document
    const texContent = `\\documentclass{article}
\\usepackage{amsmath}
\\usepackage{amssymb}
\\pagestyle{empty}
\\begin{document}
$${latex}$
\\end{document}
`;

    try {
        fs.writeFileSync(tempTexFile, texContent);

        // Run latex to generate DVI
        execSync(`latex -interaction=nonstopmode -output-directory="${tempDir}" "${tempTexFile}"`, {
            cwd: tempDir,
            stdio: 'pipe',
            timeout: 10000
        });

        if (fs.existsSync(tempDviFile)) {
            fs.copyFileSync(tempDviFile, outputPath);
            return true;
        }
        return false;
    } catch (e) {
        // LaTeX compilation failed
        return false;
    } finally {
        // Cleanup temp files
        try {
            const extensions = ['.tex', '.dvi', '.aux', '.log'];
            for (const ext of extensions) {
                const file = path.join(tempDir, 'temp_formula' + ext);
                if (fs.existsSync(file)) fs.unlinkSync(file);
            }
        } catch (e) { /* ignore cleanup errors */ }
    }
}

/**
 * Run a single extended test (tex or JSON file with AST references)
 */
async function runExtendedTest(testInfo, options) {
    const testData = loadTestData(testInfo.path);

    if (testData.error) {
        return {
            name: testInfo.file,
            status: 'error',
            error: testData.error,
            score: { overall: 0 }
        };
    }

    if (!testData.expressions || testData.expressions.length === 0) {
        // A file without an extractable delimiter expression is not a renderer
        // case; macro bodies such as `$#2$` are declarations, not document math.
        return {
            name: testInfo.file,
            suite: testInfo.suite,
            status: 'skipped',
            reason: 'no extractable math expression',
            score: { overall: 0 }
        };
    }

    const expressionResults = [];

    for (const expr of (testData.expressions || [])) {
        // Run Lambda to get output
        const lambdaOutput = await runLambdaParser(expr.latex);

        if (lambdaOutput.error && lambdaOutput.error !== 'no-error') {
            const expectedError = mathlive_expected_error(expr.latex);
            if (expectedError === lambdaOutput.error) {
                expressionResults.push({
                    latex: expr.latex,
                    type: expr.type,
                    score: calculateTestScore(null, { passRate: 100, error: expectedError }, null, options.compare),
                    ast: null,
                    html: { passRate: 100, error: expectedError },
                    dvi: null
                });
                continue;
            }
            expressionResults.push({
                latex: expr.latex,
                type: expr.type,
                error: `${lambdaOutput.error} (MathLive: ${expectedError || 'no error contract'})`,
                score: { overall: 0 }
            });
            continue;
        }

        // Load reference files
        // Handle both .tex and .json source files
        const ext = path.extname(testInfo.file);
        const testBaseName = path.basename(testInfo.file, ext);
        const refBaseName = `${testBaseName}_${expr.index}`;

        // Compare AST - prefer MathLive AST (same branch structure) over MathML
        let astResult = null;
        if (options.compare === 'all' || options.compare === 'ast') {
            const refMathLiveAstPath = path.join(REFERENCE_DIR, `${refBaseName}.mathlive.json`);
            const refMathMLPath = path.join(REFERENCE_DIR, `${refBaseName}.mathml.json`);
            const refAstPath = path.join(REFERENCE_DIR, `${refBaseName}.ast.json`);

            if (fs.existsSync(refMathLiveAstPath) && lambdaOutput.ast) {
                // Preferred: Use MathLive AST reference (same branch structure as Lambda)
                const refMathLiveAst = JSON.parse(fs.readFileSync(refMathLiveAstPath, 'utf-8'));
                astResult = compareASTToMathLive(lambdaOutput.ast, refMathLiveAst);
                astResult.referenceType = 'mathlive-ast';
            } else if (fs.existsSync(refMathMLPath) && lambdaOutput.ast) {
                // Fallback: Use MathML reference (requires semantic mapping)
                const refMathML = JSON.parse(fs.readFileSync(refMathMLPath, 'utf-8'));
                astResult = compareASTToMathML(lambdaOutput.ast, refMathML);
                astResult.referenceType = 'mathml';
            } else if (fs.existsSync(refAstPath) && lambdaOutput.ast) {
                // Fallback to Lambda AST reference (for consistency testing)
                const refAst = JSON.parse(fs.readFileSync(refAstPath, 'utf-8'));
                astResult = compareAST(lambdaOutput.ast, refAst);
                // Mark as self-reference with reduced score
                astResult.selfReference = true;
                astResult.passRate = Math.min(astResult.passRate, 50); // Cap at 50% for self-reference
            } else if (lambdaOutput.ast) {
                astResult = { passRate: 0, differences: [{ issue: 'No reference AST available' }] };
            } else {
                astResult = { passRate: 0, differences: [{ issue: 'Lambda did not produce AST' }] };
            }
        }

        // HTML comparison with cross-reference
        let htmlResult = null;
        if (options.compare === 'all' || options.compare === 'html') {
            if (lambdaOutput.html) {
                try {
                    htmlResult = await compareMathLiveHtml(lambdaOutput.html, expr.latex, expr.type === 'display');
                } catch (err) {
                    htmlResult = { passRate: 0, differences: [{ issue: `MathLive render failed: ${err.message}` }] };
                }
            } else {
                htmlResult = { passRate: 0, differences: [{ issue: 'Lambda did not produce HTML' }] };
            }
        }

        // DVI comparison against pdfTeX reference (authoritative)
        let dviResult = null;
        if (options.compare === 'all' || options.compare === 'dvi') {
            const refPdfTexDviPath = path.join(REFERENCE_DIR, `${refBaseName}.pdftex.dvi`);
            const refDviPath = path.join(REFERENCE_DIR, `${refBaseName}.dvi`);

            if (lambdaOutput.dvi && fs.existsSync(refPdfTexDviPath)) {
                // Use pdfTeX DVI reference (authoritative)
                dviResult = compareDVI(lambdaOutput.dvi, refPdfTexDviPath, { tolerance: options.tolerance });
            } else if (lambdaOutput.dvi && fs.existsSync(refDviPath)) {
                // Fallback to existing DVI reference
                dviResult = compareDVI(lambdaOutput.dvi, refDviPath, { tolerance: options.tolerance });
            } else if (lambdaOutput.dvi) {
                // Try to generate DVI reference on demand
                const generatedDviPath = path.join(REFERENCE_DIR, `${refBaseName}.pdftex.dvi`);
                if (generateDVIReference(expr.latex, generatedDviPath)) {
                    dviResult = compareDVI(lambdaOutput.dvi, generatedDviPath, { tolerance: options.tolerance });
                    dviResult.generated = true; // Mark as newly generated
                } else {
                    // Failed to generate - report error with 0 score
                    dviResult = {
                        passRate: 0,
                        differences: [{ issue: `Failed to generate DVI reference for: ${expr.latex}` }],
                        generationError: true
                    };
                }
            } else {
                // Lambda didn't produce DVI
                dviResult = {
                    passRate: 0,
                    differences: [{ issue: 'Lambda did not produce DVI output' }]
                };
            }
        }

        const score = calculateTestScore(astResult, htmlResult, dviResult, options.compare);

        expressionResults.push({
            latex: expr.latex,
            type: expr.type,
            score,
            ast: astResult,
            html: htmlResult,
            dvi: dviResult
        });
    }

    // Calculate overall test score
    const avgScore = expressionResults.length > 0
        ? expressionResults.reduce((sum, r) => sum + r.score.overall, 0) / expressionResults.length
        : 0;

    // Average breakdowns for reporting
    const avgBreakdown = expressionResults.length > 0 ? {
        ast: {
            rate: expressionResults.reduce((sum, r) => sum + (r.score.breakdown?.ast?.rate || 0), 0) / expressionResults.length,
            weighted: expressionResults.reduce((sum, r) => sum + (r.score.breakdown?.ast?.weighted || 0), 0) / expressionResults.length
        },
        html: {
            rate: expressionResults.reduce((sum, r) => sum + (r.score.breakdown?.html?.rate || 0), 0) / expressionResults.length,
            weighted: expressionResults.reduce((sum, r) => sum + (r.score.breakdown?.html?.weighted || 0), 0) / expressionResults.length
        },
        dvi: {
            rate: expressionResults.reduce((sum, r) => sum + (r.score.breakdown?.dvi?.rate || 0), 0) / expressionResults.length,
            weighted: expressionResults.reduce((sum, r) => sum + (r.score.breakdown?.dvi?.weighted || 0), 0) / expressionResults.length
        }
    } : null;

    return {
        name: testInfo.file,
        suite: testInfo.suite,
        status: avgScore >= options.threshold ? 'passed' : 'failed',
        expressionCount: expressionResults.length,
        score: {
            overall: Math.round(avgScore * 10) / 10,
            breakdown: avgBreakdown,
            expressions: options.verbose ? expressionResults : undefined
        }
    };
}

/**
 * Run a baseline test (passes if DVI is 100% OR weighted score is 100%)
 *
 * Baseline tests can pass in two ways:
 * 1. DVI comparison passes 100% (strict typographic match)
 * 2. Weighted score passes 100% (using AST+HTML+DVI formula from extended tests)
 */
async function runBaselineTest(testInfo, options) {
    const testName = path.basename(testInfo.file, '.tex');
    // Baseline tests contain a single expression, so use index 0
    const refBaseName = `${testName}_0`;

    // Check for DVI reference - prefer pdftex.dvi, fall back to .dvi
    let referenceDVI = path.join(REFERENCE_DIR, `${refBaseName}.pdftex.dvi`);
    if (!fs.existsSync(referenceDVI)) {
        referenceDVI = path.join(REFERENCE_DIR, `${refBaseName}.dvi`);
    }
    const hasDVIRef = fs.existsSync(referenceDVI);

    // Read the .tex file to get the LaTeX content
    const texContent = fs.readFileSync(testInfo.path, 'utf-8');

    // Extract math content between \[ and \] or $ and $
    const displayMatch = texContent.match(/\\\[(.*?)\\\]/s) || texContent.match(/\$\$(.*?)\$\$/s);
    const inlineMatch = texContent.match(/\$(.*?)\$/);
    const latex = displayMatch ? displayMatch[1].trim() : (inlineMatch ? inlineMatch[1].trim() : null);

    if (!latex) {
        return {
            name: testInfo.file,
            suite: 'baseline',
            status: 'error',
            error: 'Could not extract LaTeX math from .tex file',
            score: { overall: 0 }
        };
    }

    // Run Lambda parser
    const lambdaOutput = await runLambdaParser(latex);

    if (lambdaOutput.error && lambdaOutput.error !== 'no-error') {
        const expectedError = mathlive_expected_error(latex);
        if (expectedError === lambdaOutput.error) {
            return {
                name: testInfo.file,
                suite: 'baseline',
                status: 'passed',
                errorContract: expectedError,
                score: {
                    overall: 100,
                    breakdown: {
                        ast: { rate: 0, weighted: 0 },
                        html: { rate: 100, weighted: 100 },
                        dvi: { rate: 0, weighted: 0, unavailable: true }
                    }
                },
                passedViaDVI: false,
                passedViaWeighted: true
            };
        }
        return {
            name: testInfo.file,
            suite: 'baseline',
            status: 'failed',
            error: `${lambdaOutput.error} (MathLive: ${expectedError || 'no error contract'})`,
            score: { overall: 0 }
        };
    }

    // Run DVI comparison if reference exists and Lambda produced DVI
    let dviResult = null;
    if (hasDVIRef && lambdaOutput.dvi) {
        dviResult = compareDVI(lambdaOutput.dvi, referenceDVI, {
            tolerance: options.tolerance,
            lenient: true  // Enable lenient mode for baseline tests
        });
    }

    // Check if DVI passes 100%
    const dviPasses = dviResult && dviResult.passRate === 100;

    // Also compute weighted score using extended test formula (AST + HTML + DVI)
    // This allows tests to pass even if DVI isn't 100%, as long as weighted score is 100%
    let astResult = null;
    let htmlResult = null;

    // Compare AST - prefer MathLive AST over MathML
    const refMathLiveAstPath = path.join(REFERENCE_DIR, `${refBaseName}.mathlive.json`);
    const refMathMLPath = path.join(REFERENCE_DIR, `${refBaseName}.mathml.json`);
    const refAstPath = path.join(REFERENCE_DIR, `${refBaseName}.ast.json`);

    if (fs.existsSync(refMathLiveAstPath) && lambdaOutput.ast) {
        const refMathLiveAst = JSON.parse(fs.readFileSync(refMathLiveAstPath, 'utf-8'));
        astResult = compareASTToMathLive(lambdaOutput.ast, refMathLiveAst);
        astResult.referenceType = 'mathlive-ast';
    } else if (fs.existsSync(refMathMLPath) && lambdaOutput.ast) {
        const refMathML = JSON.parse(fs.readFileSync(refMathMLPath, 'utf-8'));
        astResult = compareASTToMathML(lambdaOutput.ast, refMathML);
        astResult.referenceType = 'mathml';
    } else if (fs.existsSync(refAstPath) && lambdaOutput.ast) {
        const refAst = JSON.parse(fs.readFileSync(refAstPath, 'utf-8'));
        astResult = compareAST(lambdaOutput.ast, refAst);
        astResult.selfReference = true;
        astResult.passRate = Math.min(astResult.passRate, 50);
    } else if (lambdaOutput.ast) {
        astResult = { passRate: 0, differences: [{ issue: 'No reference AST available' }] };
    } else {
        astResult = { passRate: 0, differences: [{ issue: 'Lambda did not produce AST' }] };
    }

    // HTML comparison with cross-reference
    if (lambdaOutput.html) {
        try {
            htmlResult = await compareMathLiveHtml(lambdaOutput.html, latex, displayMatch != null);
        } catch (err) {
            htmlResult = { passRate: 0, differences: [{ issue: `MathLive render failed: ${err.message}` }] };
        }
    } else {
        htmlResult = { passRate: 0, differences: [{ issue: 'Lambda did not produce HTML' }] };
    }

    // Use the selected active comparison contract. The default HTML contract
    // matches the script-level static renderer, not the retired CLI artifacts.
    const weightedScore = calculateTestScore(astResult, htmlResult, dviResult, options.compare);

    // Pass if DVI is 100% OR weighted score is 100%
    const weightedPasses = Math.round(weightedScore.overall) >= 100;
    const passed = dviPasses || weightedPasses;

    return {
        name: testInfo.file,
        suite: 'baseline',
        status: passed ? 'passed' : 'failed',
        score: {
            overall: dviResult ? dviResult.passRate : weightedScore.overall,
            dvi: dviResult,
            breakdown: weightedScore.breakdown,
            passedViaDVI: dviPasses,
            passedViaWeighted: weightedPasses
        }
    };
}

/**
 * Group test results by feature prefix
 */
function groupResultsByFeature(results) {
    const groups = {};

    for (const result of results) {
        // Extract feature group from filename
        let group = 'other';
        for (const prefix of FEATURE_GROUPS) {
            if (result.name.startsWith(prefix + '_') || result.name.startsWith(prefix + '.')) {
                group = prefix;
                break;
            }
        }

        if (!groups[group]) {
            groups[group] = [];
        }
        groups[group].push(result);
    }

    return groups;
}

/**
 * Print results in console format
 */
function printResults(results, options) {
    const baselineResults = results.filter(r => r.suite === 'baseline');
    const extendedResults = results.filter(r => r.suite === 'extended');

    console.log('');
    console.log('================================================================================');
    console.log('📊 LaTeX Math Test Results');
    console.log('================================================================================');
    console.log('');

    // Baseline results
    if (baselineResults.length > 0) {
        console.log('📂 Baseline Tests (HTML 100%)');
        console.log('--------------------------------------------------------------------------------');

        for (const result of baselineResults) {
            const icon = result.status === 'passed' ? '✅' :
                        result.status === 'skipped' ? '⏭️' : '❌';
            const htmlScore = result.score.breakdown?.html?.rate;
            const scoreText = htmlScore != null ? htmlScore.toFixed(1) : 'N/A';

            // Show which criterion passed
            let passMethod = '';
            if (result.status === 'passed') {
                if (result.score.passedViaDVI) {
                    passMethod = ' (DVI)';
                } else if (result.score.passedViaWeighted) {
                    passMethod = ' (HTML)';
                }
            }

            console.log(`  ${icon} ${result.name.padEnd(30)} HTML: ${scoreText}%${passMethod}`);

            if (options.verbose && result.score.dvi?.differences?.length > 0) {
                for (const diff of result.score.dvi.differences.slice(0, 3)) {
                    console.log(`     └─ ${diff.issue || JSON.stringify(diff)}`);
                }
            }
        }

        const baselinePassed = baselineResults.filter(r => r.status === 'passed').length;
        console.log('--------------------------------------------------------------------------------');
        console.log(`  Baseline: ${baselinePassed}/${baselineResults.length} passed (${(baselinePassed / baselineResults.length * 100).toFixed(0)}%)`);
        console.log('');
    }

    // Extended results by feature group
    if (extendedResults.length > 0) {
        console.log('📂 Extended Tests by Feature Group');
        console.log('--------------------------------------------------------------------------------');

        const groups = groupResultsByFeature(extendedResults);
        const groupScores = {};

        for (const [group, groupResults] of Object.entries(groups).sort()) {
            console.log(`  📁 ${group} (${groupResults.length} tests)`);

            for (const result of groupResults) {
                const icon = result.status === 'passed' ? '✅' :
                            result.status === 'skipped' ? '⏭️' : '❌';

                // Show component scores if available
                const html = result.score.breakdown?.html?.rate ?? 'N/A';
                const overall = result.score.overall;

                if (options.verbose) {
                    console.log(`     ${icon} ${result.name.padEnd(25)} HTML: ${String(html).padStart(5)}%  → ${overall.toFixed(1)}%`);
                } else {
                    console.log(`     ${icon} ${result.name.padEnd(25)} → ${overall.toFixed(1)}%`);
                }
            }

            // Group average
            const groupAvg = groupResults.reduce((sum, r) => sum + r.score.overall, 0) / groupResults.length;
            groupScores[group] = groupAvg;
            console.log(`     Group Average: ${groupAvg.toFixed(1)}%`);
            console.log('');
        }

        // Feature group summary
        console.log('--------------------------------------------------------------------------------');
        console.log('📊 Feature Group Summary');
        console.log('--------------------------------------------------------------------------------');

        for (const [group, score] of Object.entries(groupScores).sort((a, b) => b[1] - a[1])) {
            const bar = '█'.repeat(Math.floor(score / 5)) + '░'.repeat(20 - Math.floor(score / 5));
            console.log(`  ${group.padEnd(12)} ${score.toFixed(1).padStart(5)}%  ${bar}`);
        }
    }

    // Overall summary
    console.log('');
    console.log('================================================================================');
    console.log('📈 OVERALL SUMMARY');
    console.log('================================================================================');

    const totalTests = results.length;
    const passedTests = results.filter(r => r.status === 'passed').length;
    const failedTests = results.filter(r => r.status === 'failed').length;
    const skippedTests = results.filter(r => r.status === 'skipped').length;

    console.log(`  Total Tests: ${totalTests}`);
    console.log(`  Passed: ${passedTests} (${(passedTests / totalTests * 100).toFixed(1)}%)`);
    console.log(`  Failed: ${failedTests} (${(failedTests / totalTests * 100).toFixed(1)}%)`);
    if (skippedTests > 0) {
        console.log(`  Skipped: ${skippedTests}`);
    }

    // Component averages
    const validResults = results.filter(r => r.score.breakdown);
    if (validResults.length > 0) {
        const htmlAvg = validResults.reduce((sum, r) => sum + (r.score.breakdown?.html?.rate || 0), 0) / validResults.length;

        console.log('');
        console.log('  HTML Average:');
        console.log(`    HTML: ${htmlAvg.toFixed(1)}%  (target: 90%)`);
    }

    const overallAvg = results.reduce((sum, r) => sum + r.score.overall, 0) / totalTests;
    console.log('');
    console.log(`  Overall HTML Score: ${overallAvg.toFixed(1)}%`);
    console.log('================================================================================');
    console.log('');
}

/**
 * Print results as JSON
 */
function printResultsJSON(results) {
    const summary = {
        timestamp: new Date().toISOString(),
        totalTests: results.length,
        passed: results.filter(r => r.status === 'passed').length,
        failed: results.filter(r => r.status === 'failed').length,
        skipped: results.filter(r => r.status === 'skipped').length,
        averageScore: results.reduce((sum, r) => sum + r.score.overall, 0) / results.length,
        results
    };

    console.log(JSON.stringify(summary, null, 2));
}

/**
 * Print detailed failure report for a test
 */
function printFailureReport(result) {
    console.log('');
    console.log('================================================================================');
    console.log(`❌ DETAILED FAILURE REPORT: ${result.name}`);
    console.log('================================================================================');

    if (result.score.ast && result.score.ast.differences?.length > 0) {
        console.log('');
        console.log(`[AST COMPARISON] Score: ${result.score.ast.passRate}%`);
        console.log('--------------------------------------------------------------------------------');
        console.log(`├─ Total Nodes: ${result.score.ast.totalNodes || 'N/A'}`);
        console.log(`├─ Matched: ${result.score.ast.matchedNodes || 'N/A'}`);
        console.log('└─ Differences:');

        for (const diff of result.score.ast.differences) {
            console.log(`   ${diff.path || ''}: ${diff.issue || JSON.stringify(diff)}`);
            if (diff.expected) console.log(`      Expected: ${JSON.stringify(diff.expected)}`);
            if (diff.got) console.log(`      Got:      ${JSON.stringify(diff.got)}`);
        }
    }

    if (result.score.html && result.score.html.differences?.length > 0) {
        console.log('');
        console.log(`[HTML COMPARISON] Score: ${result.score.html.passRate}%`);
        console.log('--------------------------------------------------------------------------------');

        for (const diff of result.score.html.differences) {
            console.log(`   ${diff.path || ''}: ${diff.issue || JSON.stringify(diff)}`);
        }
    }

    if (result.score.dvi && result.score.dvi.differences?.length > 0) {
        console.log('');
        console.log(`[DVI COMPARISON] Score: ${result.score.dvi.passRate}%`);
        console.log('--------------------------------------------------------------------------------');
        console.log(`├─ Total Glyphs: ${result.score.dvi.totalGlyphs || 'N/A'}`);
        console.log(`├─ Matched: ${result.score.dvi.matchedGlyphs || 'N/A'}`);
        console.log(`├─ Tolerance: ${result.score.dvi.positionTolerance || 'N/A'}pt`);
        console.log('└─ Differences:');

        for (const diff of result.score.dvi.differences) {
            if (diff.char) {
                console.log(`   Glyph '${diff.char}':`);
                console.log(`      Expected: ${JSON.stringify(diff.expected)}`);
                console.log(`      Got:      ${JSON.stringify(diff.got)}`);
            } else {
                console.log(`   ${diff.issue || JSON.stringify(diff)}`);
            }
        }
    }

    console.log('================================================================================');
}

/**
 * Main entry point
 */
async function main() {
    const options = parseArgs();

    // Collect test files
    const tests = collectTestFiles(options);

    if (tests.length === 0) {
        console.error('No tests found. Check your --suite, --test, or --group options.');
        process.exit(1);
    }

    // Run tests
    const results = [];

    for (const test of tests) {
        let result;

        if (test.suite === 'baseline') {
            result = await runBaselineTest(test, options);
        } else {
            result = await runExtendedTest(test, options);
        }

        results.push(result);

        // Print detailed failure report in verbose mode
        if (options.verbose && result.status === 'failed') {
            printFailureReport(result);
        }
    }

    // Output results
    if (options.json) {
        printResultsJSON(results);
    } else {
        printResults(results, options);
    }

    // Exit with error code if any tests failed
    const failed = results.filter(r => r.status === 'failed').length;
    process.exit(failed > 0 ? 1 : 0);
}

// Run main
main().catch(error => {
    console.error('Test runner error:', error);
    process.exit(1);
});
