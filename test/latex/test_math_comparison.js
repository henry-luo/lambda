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

const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

// Import comparators
const { compareAST } = require('./comparators/ast_comparator');
const { compareHTML } = require('./comparators/html_comparator');
const { compareDVI, validateDVI } = require('./comparators/dvi_comparator');

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
    
    // Extended tests (from math-ast directory)
    if (options.suite === 'extended' || options.suite === 'all') {
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
    // Check math-ast directory first
    const jsonPath = path.join(MATH_AST_DIR, name.endsWith('.json') ? name : `${name}.json`);
    if (fs.existsSync(jsonPath)) {
        return { file: path.basename(jsonPath), path: jsonPath, suite: 'extended' };
    }
    
    // Check baseline directory
    const texPath = path.join(BASELINE_DIR, name.endsWith('.tex') ? name : `${name}.tex`);
    if (fs.existsSync(texPath)) {
        return { file: path.basename(texPath), path: texPath, suite: 'baseline' };
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
 * Run Lambda's LaTeX parser on an expression
 */
async function runLambdaParser(latex) {
    return new Promise((resolve) => {
        // For now, return a placeholder - actual Lambda integration would go here
        // This would call lambda.exe with appropriate arguments to parse LaTeX
        resolve({
            ast: null,
            html: null,
            dvi: null,
            error: 'Lambda parser integration not yet implemented'
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
        // Compare AST
        let astResult = null;
        if (options.compare === 'all' || options.compare === 'ast') {
            // Lambda AST would be obtained from running Lambda parser
            // For now, compare reference AST structure validity
            astResult = {
                passRate: expr.ast && !expr.ast.error ? 100 : 0,
                differences: expr.ast?.error ? [{ issue: expr.ast.error }] : []
            };
        }
        
        // HTML comparison would require Lambda's HTML output
        let htmlResult = null;
        if (options.compare === 'all' || options.compare === 'html') {
            htmlResult = { passRate: 0, differences: [{ issue: 'Lambda HTML output not available' }] };
        }
        
        // DVI comparison for baseline tests only
        let dviResult = null;
        
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
            error: 'Reference DVI not found',
            score: { overall: 0 }
        };
    }
    
    // Lambda DVI output path (would be generated by Lambda)
    const lambdaDVI = `/tmp/lambda_${testName}.dvi`;
    
    // Run DVI comparison
    let dviResult = { passRate: 0, differences: [{ issue: 'Lambda DVI output not available' }] };
    
    if (fs.existsSync(lambdaDVI)) {
        dviResult = compareDVI(lambdaDVI, referenceDVI, { tolerance: options.tolerance });
    }
    
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
