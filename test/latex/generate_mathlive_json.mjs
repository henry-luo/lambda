#!/usr/bin/env node

/**
 * Generate MathLive AST References (Internal JSON Format)
 *
 * This script generates reference AST files using MathLive's internal
 * toJson() serialization, which uses the same branch structure as Lambda:
 * - body, above, below, superscript, subscript
 *
 * This is the proper format for comparing Lambda's AST against MathLive.
 *
 * Output: reference/<testname>_<index>.mathlive.json
 *
 * Usage:
 *   node generate_mathlive_json.mjs                    # Generate for all test files
 *   node generate_mathlive_json.mjs --test fracs_basic # Generate for specific test
 */

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { JSDOM } from 'jsdom';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const MATH_DIR = path.join(__dirname, 'math');
const MATH_AST_DIR = path.join(__dirname, 'math-ast');
const REFERENCE_DIR = path.join(__dirname, 'reference');

// Ensure directories exist
if (!fs.existsSync(REFERENCE_DIR)) {
    fs.mkdirSync(REFERENCE_DIR, { recursive: true });
}

/**
 * Load MathLive in a JSDOM environment
 */
async function loadMathLive() {
    // Create a minimal DOM environment
    const dom = new JSDOM('<!DOCTYPE html><html><body></body></html>', {
        runScripts: 'dangerously',
        resources: 'usable',
        pretendToBeVisual: true,
        url: 'http://localhost'
    });

    const { window } = dom;
    const { document } = window;

    // Polyfills needed by MathLive
    global.window = window;
    global.document = document;
    global.navigator = window.navigator;
    global.getComputedStyle = window.getComputedStyle;
    global.requestAnimationFrame = (cb) => setTimeout(cb, 16);
    global.cancelAnimationFrame = (id) => clearTimeout(id);
    global.ResizeObserver = class {
        observe() {}
        unobserve() {}
        disconnect() {}
    };
    global.MutationObserver = window.MutationObserver;
    global.IntersectionObserver = class {
        observe() {}
        unobserve() {}
        disconnect() {}
    };
    global.customElements = {
        define: () => {},
        get: () => undefined,
        whenDefined: () => Promise.resolve()
    };
    global.HTMLElement = window.HTMLElement;
    global.Element = window.Element;
    global.Node = window.Node;
    global.Event = window.Event;
    global.CustomEvent = window.CustomEvent;
    global.KeyboardEvent = window.KeyboardEvent;
    global.MouseEvent = window.MouseEvent;
    global.PointerEvent = window.PointerEvent || window.MouseEvent;
    global.CSS = { supports: () => false };

    // Import MathLive
    try {
        const mathlive = await import('mathlive');
        return { mathlive, dom };
    } catch (e) {
        console.error('Failed to load MathLive:', e.message);
        console.log('Falling back to MathLive CommonJS API...');

        // Try using the rendering API directly
        const mathlive = await import('mathlive');
        return { mathlive, dom };
    }
}

/**
 * Parse LaTeX and extract AST using MathLive's toJson()
 */
function parseLatexToAST(mathlive, latex) {
    try {
        // Use MathLive's serializeToJson if available (newer API)
        if (typeof mathlive.serializeToJson === 'function') {
            return mathlive.serializeToJson(latex);
        }

        // Fallback: parse via Mathfield element
        // This requires a DOM environment
        const MathfieldElement = mathlive.MathfieldElement;
        if (MathfieldElement) {
            const mf = new MathfieldElement();
            mf.style.position = 'absolute';
            mf.style.left = '-9999px';
            document.body.appendChild(mf);

            mf.value = latex;

            // Access internal model and root atom's toJson()
            const model = mf._mathfield?.model;
            const root = model?.root;

            let ast;
            if (root && typeof root.toJson === 'function') {
                ast = root.toJson();
            } else {
                ast = { error: 'Could not access atom.toJson()' };
            }

            document.body.removeChild(mf);
            return ast;
        }

        return { error: 'MathLive API not available' };
    } catch (e) {
        return { error: e.message, latex };
    }
}

/**
 * Use MathLive via puppeteer for proper AST extraction
 * This is more reliable as MathLive is designed for browser environments
 */
async function generateWithPuppeteer(testFiles, options = {}) {
    const puppeteer = await import('puppeteer');

    console.log('Launching browser...');
    const browser = await puppeteer.launch({ headless: 'new' });
    const page = await browser.newPage();

    // Create a simple HTML page that loads MathLive
    const htmlContent = `<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>MathLive AST Generator</title>
</head>
<body>
    <div id="container"></div>
    <script type="module">
        import * as MathLive from 'https://unpkg.com/mathlive@0.98.6/dist/mathlive.mjs';

        window.MathLive = MathLive;

        // Function to parse LaTeX and get AST
        window.parseLatex = function(latex) {
            try {
                const mf = new MathLive.MathfieldElement();
                mf.style.position = 'absolute';
                mf.style.left = '-9999px';
                document.body.appendChild(mf);

                mf.value = latex;

                // Get the internal atom tree and call toJson()
                const model = mf._mathfield?.model;
                const root = model?.root;

                let ast;
                if (root && typeof root.toJson === 'function') {
                    ast = root.toJson();
                } else {
                    ast = { error: 'Could not access toJson()' };
                }

                document.body.removeChild(mf);
                return ast;
            } catch (e) {
                return { error: e.message };
            }
        };

        window.mathLiveReady = true;
    </script>
</body>
</html>`;

    // Navigate to a data URL or serve the content
    await page.setContent(htmlContent, { waitUntil: 'networkidle0' });

    // Wait for MathLive to be ready
    await page.waitForFunction('window.mathLiveReady === true', { timeout: 30000 });
    console.log('MathLive loaded successfully');

    const results = [];

    for (const testFile of testFiles) {
        const testPath = testFile.path;
        const testName = path.basename(testPath, '.json');

        console.log(`Processing: ${testName}`);

        let testData;
        try {
            testData = JSON.parse(fs.readFileSync(testPath, 'utf-8'));
        } catch (e) {
            console.error(`  Failed to read ${testPath}: ${e.message}`);
            continue;
        }

        const expressions = testData.expressions || [];
        let successCount = 0;

        for (const expr of expressions) {
            const latex = expr.latex;
            const index = expr.index;
            const baseName = `${testName}_${index}`;

            try {
                // Parse LaTeX in browser context
                const ast = await page.evaluate((latex) => {
                    return window.parseLatex(latex);
                }, latex);

                // Create reference object
                const reference = {
                    format: 'mathlive-ast',
                    latex: latex,
                    description: expr.description || null,
                    type: expr.type || 'display',
                    generatedAt: new Date().toISOString(),
                    generator: 'MathLive toJson()',
                    ast: ast
                };

                // Save to file
                const outputPath = path.join(REFERENCE_DIR, `${baseName}.mathlive.json`);
                fs.writeFileSync(outputPath, JSON.stringify(reference, null, 2));
                successCount++;

                if (options.verbose) {
                    console.log(`  ✓ ${baseName}: ${JSON.stringify(ast).substring(0, 50)}...`);
                }
            } catch (e) {
                console.error(`  ✗ ${baseName}: ${e.message}`);
            }
        }

        console.log(`  Generated ${successCount}/${expressions.length} AST files`);
        results.push({ testName, total: expressions.length, success: successCount });
    }

    await browser.close();
    return results;
}

/**
 * Collect test files from math/ and math-ast/ directories
 */
function collectTestFiles(options = {}) {
    const files = [];

    // Check math directory
    if (fs.existsSync(MATH_DIR)) {
        const mathFiles = fs.readdirSync(MATH_DIR)
            .filter(f => f.endsWith('.json'))
            .map(f => ({ file: f, path: path.join(MATH_DIR, f) }));
        files.push(...mathFiles);
    }

    // Check math-ast directory
    if (fs.existsSync(MATH_AST_DIR)) {
        const astFiles = fs.readdirSync(MATH_AST_DIR)
            .filter(f => f.endsWith('.json'))
            .map(f => ({ file: f, path: path.join(MATH_AST_DIR, f) }));
        files.push(...astFiles);
    }

    // Filter if specific test requested
    if (options.test) {
        return files.filter(f =>
            f.file.startsWith(options.test) ||
            f.file === options.test + '.json'
        );
    }

    return files;
}

/**
 * Parse command line arguments
 */
function parseArgs() {
    const args = process.argv.slice(2);
    const options = {
        test: null,
        verbose: false
    };

    for (let i = 0; i < args.length; i++) {
        if (args[i] === '--test' && args[i + 1]) {
            options.test = args[i + 1];
            i++;
        } else if (args[i] === '--verbose' || args[i] === '-v') {
            options.verbose = true;
        }
    }

    return options;
}

/**
 * Main function
 */
async function main() {
    console.log('====================================================');
    console.log(' MathLive AST Reference Generator');
    console.log(' (Using MathLive toJson() - TeX-like branch structure)');
    console.log('====================================================\n');

    const options = parseArgs();

    // Collect test files
    const testFiles = collectTestFiles(options);

    if (testFiles.length === 0) {
        console.log('No test files found.');
        return;
    }

    console.log(`Found ${testFiles.length} test files\n`);

    // Generate using puppeteer (most reliable)
    try {
        const results = await generateWithPuppeteer(testFiles, options);

        console.log('\n====================================================');
        console.log(' Summary');
        console.log('====================================================');

        let totalSuccess = 0;
        let totalCount = 0;

        for (const r of results) {
            totalSuccess += r.success;
            totalCount += r.total;
        }

        console.log(`Total: ${totalSuccess}/${totalCount} AST references generated`);
        console.log(`Output directory: ${REFERENCE_DIR}`);
        console.log('');
    } catch (e) {
        console.error('Failed to generate with Puppeteer:', e.message);
        console.error('Make sure puppeteer is installed: npm install puppeteer');
        process.exit(1);
    }
}

main().catch(console.error);
