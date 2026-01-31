#!/usr/bin/env node
/**
 * Generate missing HTML references from existing MathLive JSON AST files
 *
 * This script:
 * 1. Finds all .mathlive.json files without corresponding .mathlive.html
 * 2. Extracts LaTeX from the JSON
 * 3. Generates HTML using MathLive and KaTeX Node.js APIs
 *
 * Usage:
 *   node generate_missing_html.mjs [--dry-run] [--limit N]
 */

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import * as mathlive from 'mathlive';
import katex from 'katex';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const REFERENCE_DIR = path.join(__dirname, 'reference');

/**
 * Generate MathLive HTML for a LaTeX expression
 */
function generateMathLiveHTML(latex) {
    try {
        return mathlive.convertLatexToMarkup(latex, { defaultMode: 'math' });
    } catch (e) {
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
        return null;
    }
}

/**
 * Find all JSON files missing HTML counterparts
 */
function findMissingHtml() {
    const jsonFiles = fs.readdirSync(REFERENCE_DIR)
        .filter(f => f.endsWith('.mathlive.json'));

    const missing = [];
    for (const jsonFile of jsonFiles) {
        const baseName = jsonFile.replace('.mathlive.json', '');
        const htmlFile = `${baseName}.mathlive.html`;

        if (!fs.existsSync(path.join(REFERENCE_DIR, htmlFile))) {
            missing.push({
                baseName,
                jsonPath: path.join(REFERENCE_DIR, jsonFile),
                mathliveHtmlPath: path.join(REFERENCE_DIR, htmlFile),
                katexHtmlPath: path.join(REFERENCE_DIR, `${baseName}.katex.html`)
            });
        }
    }

    return missing;
}

/**
 * Extract LaTeX from JSON file
 */
function extractLatex(jsonPath) {
    try {
        const data = JSON.parse(fs.readFileSync(jsonPath, 'utf-8'));
        return {
            latex: data.latex || '',
            type: data.type || 'inline'
        };
    } catch (e) {
        console.error(`Error reading ${jsonPath}: ${e.message}`);
        return null;
    }
}

/**
 * Main function
 */
async function main() {
    const args = process.argv.slice(2);
    const dryRun = args.includes('--dry-run');
    const limitIdx = args.indexOf('--limit');
    const limit = limitIdx >= 0 ? parseInt(args[limitIdx + 1]) : Infinity;

    console.log('='.repeat(60));
    console.log('Generate Missing HTML References');
    console.log('='.repeat(60));

    let missing = findMissingHtml();
    console.log(`Found ${missing.length} JSON files without HTML`);

    if (dryRun) {
        console.log('\nDry run - would generate:');
        missing.slice(0, 20).forEach(m => console.log(`  ${m.baseName}`));
        if (missing.length > 20) console.log(`  ... and ${missing.length - 20} more`);
        return;
    }

    // Apply limit
    if (missing.length > limit) {
        console.log(`Limiting to first ${limit} files`);
        missing = missing.slice(0, limit);
    }

    let mathliveGenerated = 0;
    let katexGenerated = 0;
    let errors = 0;

    for (const item of missing) {
        const data = extractLatex(item.jsonPath);
        if (!data || !data.latex) {
            console.error(`  Skipping ${item.baseName}: no latex found`);
            errors++;
            continue;
        }

        const displayMode = data.type === 'display';

        // Generate MathLive HTML
        const mathliveHTML = generateMathLiveHTML(data.latex);
        if (mathliveHTML) {
            fs.writeFileSync(item.mathliveHtmlPath, mathliveHTML);
            mathliveGenerated++;
        } else {
            console.error(`  MathLive failed for ${item.baseName}`);
            errors++;
        }

        // Generate KaTeX HTML if it doesn't exist
        if (!fs.existsSync(item.katexHtmlPath)) {
            const katexHTML = generateKaTeXHTML(data.latex, displayMode);
            if (katexHTML) {
                fs.writeFileSync(item.katexHtmlPath, katexHTML);
                katexGenerated++;
            }
        }
    }

    console.log();
    console.log('='.repeat(60));
    console.log(`Results:`);
    console.log(`  MathLive HTML generated: ${mathliveGenerated}`);
    console.log(`  KaTeX HTML generated: ${katexGenerated}`);
    console.log(`  Errors: ${errors}`);
    console.log('='.repeat(60));
}

main().catch(console.error);
