#!/usr/bin/env node
/**
 * Generate reference files using Lambda's typeset-math command
 *
 * This creates baseline references from Lambda's output so the test framework
 * can run and verify consistency. For true cross-comparison, you should also
 * generate MathLive and KaTeX references.
 *
 * Usage:
 *   node generate_lambda_references.mjs           # Generate all references
 *   node generate_lambda_references.mjs --test fracs_basic  # Single test
 */

import fs from 'fs';
import path from 'path';
import { spawn } from 'child_process';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Directories
const MATH_DIR = path.join(__dirname, 'math');
const REFERENCE_DIR = path.join(__dirname, 'reference');
const PROJECT_ROOT = path.join(__dirname, '..', '..');
const LAMBDA_EXE = path.join(PROJECT_ROOT, 'lambda.exe');

// Ensure reference directory exists
if (!fs.existsSync(REFERENCE_DIR)) {
    fs.mkdirSync(REFERENCE_DIR, { recursive: true });
}

/**
 * Run Lambda's typeset-math command
 */
async function runLambda(latex) {
    return new Promise((resolve) => {
        const tempId = Math.random().toString(36).substring(7);
        const tempDir = path.join(PROJECT_ROOT, 'temp', 'refs');
        if (!fs.existsSync(tempDir)) {
            fs.mkdirSync(tempDir, { recursive: true });
        }

        const astFile = path.join(tempDir, `${tempId}.ast.json`);
        const htmlFile = path.join(tempDir, `${tempId}.html`);
        const dviFile = path.join(tempDir, `${tempId}.dvi`);

        const args = [
            'math',
            latex,
            '--output-ast', astFile,
            '--output-html', htmlFile,
            '--output-dvi', dviFile
        ];

        const child = spawn(LAMBDA_EXE, args, {
            cwd: PROJECT_ROOT,
            timeout: 30000
        });

        let stderr = '';
        child.stderr.on('data', (data) => {
            stderr += data.toString();
        });

        child.on('close', (code) => {
            if (code !== 0) {
                resolve({ error: `Exit code ${code}: ${stderr}` });
                return;
            }

            const result = {};
            try {
                if (fs.existsSync(astFile)) {
                    result.ast = JSON.parse(fs.readFileSync(astFile, 'utf-8'));
                    fs.unlinkSync(astFile);
                }
                if (fs.existsSync(htmlFile)) {
                    result.html = fs.readFileSync(htmlFile, 'utf-8');
                    fs.unlinkSync(htmlFile);
                }
                if (fs.existsSync(dviFile)) {
                    result.dviPath = dviFile; // Keep DVI file
                }
            } catch (e) {
                result.error = e.message;
            }
            resolve(result);
        });

        child.on('error', (err) => {
            resolve({ error: err.message });
        });
    });
}

/**
 * Generate references for a single test file
 */
async function generateReferencesForTest(testPath) {
    const testName = path.basename(testPath, '.json');
    console.log(`\n📁 Processing: ${testName}`);

    let testData;
    try {
        testData = JSON.parse(fs.readFileSync(testPath, 'utf-8'));
    } catch (e) {
        console.log(`   ❌ Failed to parse: ${e.message}`);
        return { success: 0, failed: 1 };
    }

    const expressions = testData.expressions || [];
    let success = 0, failed = 0;

    for (const expr of expressions) {
        const refBaseName = `${testName}_${expr.index}`;
        process.stdout.write(`   [${expr.index}] ${expr.latex.substring(0, 40)}... `);

        const result = await runLambda(expr.latex);

        if (result.error) {
            console.log(`❌ ${result.error}`);
            failed++;
            continue;
        }

        // Save AST reference
        if (result.ast) {
            const astPath = path.join(REFERENCE_DIR, `${refBaseName}.ast.json`);
            fs.writeFileSync(astPath, JSON.stringify(result.ast, null, 2));
        }

        // Save HTML reference (as Lambda HTML - for self-consistency testing)
        if (result.html) {
            const htmlPath = path.join(REFERENCE_DIR, `${refBaseName}.lambda.html`);
            fs.writeFileSync(htmlPath, result.html);
        }

        // Copy DVI reference
        if (result.dviPath && fs.existsSync(result.dviPath)) {
            const dviPath = path.join(REFERENCE_DIR, `${refBaseName}.dvi`);
            fs.copyFileSync(result.dviPath, dviPath);
            fs.unlinkSync(result.dviPath);
        }

        console.log('✅');
        success++;
    }

    return { success, failed };
}

/**
 * Main entry point
 */
async function main() {
    console.log('Lambda Reference Generator');
    console.log('==========================\n');

    // Check Lambda exists
    if (!fs.existsSync(LAMBDA_EXE)) {
        console.error('❌ lambda.exe not found. Run "make build" first.');
        process.exit(1);
    }

    // Parse args
    const args = process.argv.slice(2);
    let specificTest = null;
    for (let i = 0; i < args.length; i++) {
        if (args[i] === '--test' && args[i + 1]) {
            specificTest = args[i + 1];
        }
    }

    // Collect test files
    let testFiles;
    if (specificTest) {
        const testPath = path.join(MATH_DIR, specificTest.endsWith('.json') ? specificTest : `${specificTest}.json`);
        if (!fs.existsSync(testPath)) {
            console.error(`❌ Test file not found: ${testPath}`);
            process.exit(1);
        }
        testFiles = [testPath];
    } else {
        testFiles = fs.readdirSync(MATH_DIR)
            .filter(f => f.endsWith('.json'))
            .map(f => path.join(MATH_DIR, f))
            .sort();
    }

    console.log(`Found ${testFiles.length} test files\n`);

    let totalSuccess = 0, totalFailed = 0;

    for (const testFile of testFiles) {
        const { success, failed } = await generateReferencesForTest(testFile);
        totalSuccess += success;
        totalFailed += failed;
    }

    console.log('\n==========================');
    console.log(`✅ Generated: ${totalSuccess} references`);
    if (totalFailed > 0) {
        console.log(`❌ Failed: ${totalFailed}`);
    }
    console.log(`📁 Output: ${REFERENCE_DIR}`);
}

main().catch(err => {
    console.error('Fatal error:', err);
    process.exit(1);
});
