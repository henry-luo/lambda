#!/usr/bin/env node
/**
 * Generate reference files for baseline tests
 *
 * Baseline tests are in test/latex/baseline/*.tex and contain a single expression each.
 * This script generates reference DVI files using Lambda's output.
 *
 * Usage:
 *   node generate_baseline_references.mjs           # Generate all baseline references
 *   node generate_baseline_references.mjs --test fracs_basic  # Single test
 */

import fs from 'fs';
import path from 'path';
import { spawn } from 'child_process';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Directories
const BASELINE_DIR = path.join(__dirname, 'baseline');
const REFERENCE_DIR = path.join(__dirname, 'reference');
const PROJECT_ROOT = path.join(__dirname, '..', '..');
const LAMBDA_EXE = path.join(PROJECT_ROOT, 'lambda.exe');

// Ensure reference directory exists
if (!fs.existsSync(REFERENCE_DIR)) {
    fs.mkdirSync(REFERENCE_DIR, { recursive: true });
}

/**
 * Extract LaTeX math expression from a .tex file
 */
function extractLatex(texContent) {
    // Try display math first
    const displayMatch = texContent.match(/\\\[([\s\S]*?)\\\]/);
    if (displayMatch) {
        return displayMatch[1].trim();
    }

    // Try $$...$$ display math
    const displayDollarMatch = texContent.match(/\$\$([\s\S]*?)\$\$/);
    if (displayDollarMatch) {
        return displayDollarMatch[1].trim();
    }

    // Try inline math $...$
    const inlineMatch = texContent.match(/\$(.*?)\$/);
    if (inlineMatch) {
        return inlineMatch[1].trim();
    }

    return null;
}

/**
 * Run Lambda to generate DVI
 */
async function runLambda(latex, outputPath) {
    return new Promise((resolve) => {
        const args = ['math', latex, '--output-dvi', outputPath];

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
            } else if (fs.existsSync(outputPath)) {
                resolve({ success: true, path: outputPath });
            } else {
                resolve({ error: 'DVI file not created' });
            }
        });
    });
}

/**
 * Process all baseline tests
 */
async function main() {
    const args = process.argv.slice(2);
    let singleTest = null;

    for (let i = 0; i < args.length; i++) {
        if (args[i] === '--test' && args[i + 1]) {
            singleTest = args[i + 1];
            break;
        }
    }

    // Find all baseline .tex files
    const texFiles = fs.readdirSync(BASELINE_DIR)
        .filter(f => f.endsWith('.tex'))
        .filter(f => !singleTest || path.basename(f, '.tex') === singleTest);

    console.log(`Generating baseline references for ${texFiles.length} test(s)...`);
    console.log('=' .repeat(60));

    let success = 0;
    let failed = 0;

    for (const texFile of texFiles) {
        const testName = path.basename(texFile, '.tex');
        const texPath = path.join(BASELINE_DIR, texFile);
        const texContent = fs.readFileSync(texPath, 'utf-8');

        const latex = extractLatex(texContent);
        if (!latex) {
            console.log(`  ❌ ${testName}: Could not extract LaTeX`);
            failed++;
            continue;
        }

        // Baseline tests have a single expression, so use index 0
        const dviPath = path.join(REFERENCE_DIR, `${testName}_0.dvi`);

        const result = await runLambda(latex, dviPath);
        if (result.success) {
            console.log(`  ✅ ${testName}: ${latex.substring(0, 40)}...`);
            success++;
        } else {
            console.log(`  ❌ ${testName}: ${result.error}`);
            failed++;
        }
    }

    console.log('=' .repeat(60));
    console.log(`Generated: ${success}, Failed: ${failed}`);
}

main().catch(console.error);
