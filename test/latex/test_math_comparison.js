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
import { spawn } from 'child_process';
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

// Configuration
const CONFIG = {
    weights: {
        ast: 0.50,   // 50% - semantic structure
        html: 0.40,  // 40% - visual representation
        dvi: 0.10    // 10% - precise typographics
    },
    defaultThreshold: 80,
    dviTolerance: 0.5
};

// Directories
const TEST_DIR = __dirname;
const MATH_DIR = path.join(TEST_DIR, 'math');
const MATH_AST_DIR = path.join(TEST_DIR, 'math-ast');
const BASELINE_DIR = path.join(TEST_DIR, 'baseline');
const REFERENCE_DIR = path.join(TEST_DIR, 'reference');
const PROJECT_ROOT = path.join(TEST_DIR, '..', '..');

// Feature group prefixes
const FEATURE_GROUPS = [
    'accents', 'arrows', 'bigops', 'choose', 'delims',
    'fonts', 'fracs', 'greek', 'matrix', 'not',
    'operators', 'scripts', 'spacing', 'sqrt', 'complex'
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
        compare: 'all',
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

    // Extended tests (from math and math-ast directories)
    if (options.suite === 'extended' || options.suite === 'all') {
        // Check math directory
        if (fs.existsSync(MATH_DIR)) {
            let mathTests = fs.readdirSync(MATH_DIR)
                .filter(f => f.endsWith('.json'))
                .map(f => ({ file: f, path: path.join(MATH_DIR, f), suite: 'extended' }));

            // Filter by group if specified
            if (options.group) {
                mathTests = mathTests.filter(t =>
                    t.file.startsWith(options.group + '_') ||
                    t.file.startsWith(options.group + '.')
                );
            }

            tests.push(...mathTests);
        }

        // Check math-ast directory
        if (fs.existsSync(MATH_AST_DIR)) {
            let extendedTests = fs.readdirSync(MATH_AST_DIR)
                .filter(f => f.endsWith('.json'))
                .map(f => ({ file: f, path: path.join(MATH_AST_DIR, f), suite: 'extended' }));

            // Filter by group if specified
            if (options.group) {
                extendedTests = extendedTests.filter(t =>
                    t.file.startsWith(options.group + '_') ||
                    t.file.startsWith(options.group + '.')
                );
            }

            tests.push(...extendedTests);
        }
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

    // Check math directory for .json files
    const mathJsonPath = path.join(MATH_DIR, name.endsWith('.json') ? name : `${name}.json`);
    if (fs.existsSync(mathJsonPath)) {
        return { file: path.basename(mathJsonPath), path: mathJsonPath, suite: 'extended' };
    }

    // Check math-ast directory for .json files
    const jsonPath = path.join(MATH_AST_DIR, name.endsWith('.json') ? name : `${name}.json`);
    if (fs.existsSync(jsonPath)) {
        return { file: path.basename(jsonPath), path: jsonPath, suite: 'extended' };
    }

    return null;
}

/**
 * Load test data from a JSON file
 */
function loadTestData(testPath) {
    try {
        const content = fs.readFileSync(testPath, 'utf-8');
        return JSON.parse(content);
    } catch (error) {
        return { error: error.message };
    }
}

/**
 * Run Lambda's LaTeX math typesetter on an expression
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

    return new Promise((resolve) => {
        const tempDir = path.join(PROJECT_ROOT, 'temp', 'math_tests');
        if (!fs.existsSync(tempDir)) {
            fs.mkdirSync(tempDir, { recursive: true });
        }

        const testId = Math.random().toString(36).substring(7);
        const astFile = path.join(tempDir, `${testId}.ast.json`);
        const htmlFile = path.join(tempDir, `${testId}.html`);
        const dviFile = path.join(tempDir, `${testId}.dvi`);

        // Build Lambda command
        const args = [
            'math',
            latex,
            '--output-ast', astFile,
            '--output-html', htmlFile,
            '--output-dvi', dviFile
        ];

        const child = spawn(lambdaExe, args, {
            cwd: PROJECT_ROOT,
            timeout: 10000 // 10 second timeout
        });

        let stdout = '';
        let stderr = '';

        child.stdout.on('data', (data) => {
            stdout += data.toString();
        });

        child.stderr.on('data', (data) => {
            stderr += data.toString();
        });

        child.on('close', (code) => {
            if (code !== 0) {
                resolve({
                    ast: null,
                    html: null,
                    dvi: null,
                    error: `Lambda exited with code ${code}: ${stderr || stdout}`
                });
                return;
            }

            // Read output files
            const result = {
                ast: null,
                html: null,
                dvi: null,
                error: null
            };

            try {
                if (fs.existsSync(astFile)) {
                    const astContent = fs.readFileSync(astFile, 'utf-8');
                    result.ast = JSON.parse(astContent);
                }

                if (fs.existsSync(htmlFile)) {
                    result.html = fs.readFileSync(htmlFile, 'utf-8');
                }

                if (fs.existsSync(dviFile)) {
                    result.dvi = dviFile; // Return path to DVI file
                }

                // Clean up temp files
                if (fs.existsSync(astFile)) fs.unlinkSync(astFile);
                if (fs.existsSync(htmlFile)) fs.unlinkSync(htmlFile);
                // Keep DVI for comparison if needed

            } catch (err) {
                result.error = `Failed to read output files: ${err.message}`;
            }

            resolve(result);
        });

        child.on('error', (err) => {
            resolve({
                ast: null,
                html: null,
                dvi: null,
                error: `Failed to spawn Lambda: ${err.message}`
            });
        });
    });
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

    const overall =
        astScore * weights.ast +
        htmlScore * weights.html +
        dviScore * weights.dvi;

    return {
        overall: Math.round(overall * 10) / 10,
        breakdown: {
            ast: { rate: astScore, weighted: Math.round(astScore * weights.ast * 10) / 10 },
            html: { rate: htmlScore, weighted: Math.round(htmlScore * weights.html * 10) / 10 },
            dvi: { rate: dviScore, weighted: Math.round(dviScore * weights.dvi * 10) / 10 }
        }
    };
}

/**
 * Run a single extended test (JSON format with AST references)
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

    const expressionResults = [];

    for (const expr of (testData.expressions || [])) {
        // Run Lambda to get output
        const lambdaOutput = await runLambdaParser(expr.latex);

        if (lambdaOutput.error) {
            expressionResults.push({
                latex: expr.latex,
                type: expr.type,
                error: lambdaOutput.error,
                score: { overall: 0 }
            });
            continue;
        }

        // Load reference files
        const testBaseName = path.basename(testInfo.file, '.json');
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
            const refMathLiveHtml = path.join(REFERENCE_DIR, `${refBaseName}.mathlive.html`);
            const refKatexHtml = path.join(REFERENCE_DIR, `${refBaseName}.katex.html`);
            const refLambdaHtml = path.join(REFERENCE_DIR, `${refBaseName}.lambda.html`);

            if (lambdaOutput.html) {
                let mathLiveScore = null;
                let katexScore = null;
                let lambdaScore = null;

                if (fs.existsSync(refMathLiveHtml)) {
                    const refHtml = fs.readFileSync(refMathLiveHtml, 'utf-8');
                    mathLiveScore = compareHTML(lambdaOutput.html, refHtml, 'mathlive');
                }

                if (fs.existsSync(refKatexHtml)) {
                    const refHtml = fs.readFileSync(refKatexHtml, 'utf-8');
                    katexScore = compareHTML(lambdaOutput.html, refHtml, 'katex');
                }

                // Also check Lambda's own reference for consistency testing
                if (fs.existsSync(refLambdaHtml)) {
                    const refHtml = fs.readFileSync(refLambdaHtml, 'utf-8');
                    lambdaScore = compareHTML(lambdaOutput.html, refHtml, 'lambda');
                }

                // Take best score if we have both MathLive and KaTeX
                if (mathLiveScore && katexScore) {
                    htmlResult = {
                        passRate: Math.max(mathLiveScore.passRate, katexScore.passRate),
                        bestReference: mathLiveScore.passRate >= katexScore.passRate ? 'mathlive' : 'katex',
                        mathliveScore: mathLiveScore.passRate,
                        katexScore: katexScore.passRate,
                        differences: mathLiveScore.passRate >= katexScore.passRate ?
                            mathLiveScore.differences : katexScore.differences
                    };
                } else if (mathLiveScore || katexScore) {
                    htmlResult = mathLiveScore || katexScore;
                } else if (lambdaScore) {
                    // Use Lambda's own reference for consistency testing
                    htmlResult = lambdaScore;
                } else {
                    htmlResult = { passRate: 50, differences: [{ issue: 'No HTML reference available' }] };
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
                // Fallback to Lambda DVI reference (for consistency testing)
                dviResult = compareDVI(lambdaOutput.dvi, refDviPath, { tolerance: options.tolerance });
                // Mark as self-reference with reduced score
                dviResult.selfReference = true;
                dviResult.passRate = Math.min(dviResult.passRate, 50); // Cap at 50% for self-reference
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

    return {
        name: testInfo.file,
        suite: testInfo.suite,
        status: avgScore >= options.threshold ? 'passed' : 'failed',
        expressionCount: expressionResults.length,
        score: {
            overall: Math.round(avgScore * 10) / 10,
            expressions: options.verbose ? expressionResults : undefined
        }
    };
}

/**
 * Run a baseline test (DVI comparison required to pass 100%)
 */
async function runBaselineTest(testInfo, options) {
    const testName = path.basename(testInfo.file, '.tex');
    const referenceDVI = path.join(REFERENCE_DIR, `${testName}.dvi`);

    // Check if reference DVI exists
    if (!fs.existsSync(referenceDVI)) {
        return {
            name: testInfo.file,
            suite: 'baseline',
            status: 'skipped',
            error: 'Reference DVI not found. Run "make test-tex-reference" first.',
            score: { overall: 0 }
        };
    }

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

    if (lambdaOutput.error || !lambdaOutput.dvi) {
        return {
            name: testInfo.file,
            suite: 'baseline',
            status: 'failed',
            error: lambdaOutput.error || 'Lambda did not produce DVI output',
            score: { overall: 0 }
        };
    }

    // Run DVI comparison
    const dviResult = compareDVI(lambdaOutput.dvi, referenceDVI, { tolerance: options.tolerance });

    // Baseline tests require 100% DVI match
    const passed = dviResult.passRate === 100;

    return {
        name: testInfo.file,
        suite: 'baseline',
        status: passed ? 'passed' : 'failed',
        score: {
            overall: dviResult.passRate,
            dvi: dviResult
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
        console.log('📂 Baseline Tests (DVI must pass 100%)');
        console.log('--------------------------------------------------------------------------------');

        for (const result of baselineResults) {
            const icon = result.status === 'passed' ? '✅' :
                        result.status === 'skipped' ? '⏭️' : '❌';
            const dviScore = result.score.dvi ? result.score.dvi.passRate.toFixed(1) : 'N/A';
            console.log(`  ${icon} ${result.name.padEnd(30)} DVI: ${dviScore}%`);

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
                const ast = result.score.breakdown?.ast?.rate ?? 'N/A';
                const html = result.score.breakdown?.html?.rate ?? 'N/A';
                const dvi = result.score.breakdown?.dvi?.rate ?? 'N/A';
                const overall = result.score.overall;

                if (options.verbose) {
                    console.log(`     ${icon} ${result.name.padEnd(25)} AST: ${String(ast).padStart(5)}%  HTML: ${String(html).padStart(5)}%  DVI: ${String(dvi).padStart(5)}%  → ${overall.toFixed(1)}%`);
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
        const astAvg = validResults.reduce((sum, r) => sum + (r.score.breakdown?.ast?.rate || 0), 0) / validResults.length;
        const htmlAvg = validResults.reduce((sum, r) => sum + (r.score.breakdown?.html?.rate || 0), 0) / validResults.length;
        const dviAvg = validResults.reduce((sum, r) => sum + (r.score.breakdown?.dvi?.rate || 0), 0) / validResults.length;

        console.log('');
        console.log('  Component Averages:');
        console.log(`    AST:  ${astAvg.toFixed(1)}%  (target: 95%)`);
        console.log(`    HTML: ${htmlAvg.toFixed(1)}%  (target: 90%)`);
        console.log(`    DVI:  ${dviAvg.toFixed(1)}%  (target: 80%)`);
    }

    const overallAvg = results.reduce((sum, r) => sum + r.score.overall, 0) / totalTests;
    console.log('');
    console.log(`  Weighted Average Score: ${overallAvg.toFixed(1)}%`);
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
