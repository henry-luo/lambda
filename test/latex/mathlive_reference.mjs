#!/usr/bin/env node

/**
 * MathLive Reference Generator
 * 
 * Generates reference AST and HTML from MathLive for LaTeX math expressions.
 * This script can run in browser (via Puppeteer) or generate HTML for manual testing.
 * 
 * Usage:
 *   node mathlive_reference.mjs [options]
 * 
 * Options:
 *   --input <file>    Input JSON file with LaTeX expressions
 *   --output <dir>    Output directory for reference files
 *   --html-only       Only generate HTML (no Puppeteer)
 *   --all             Process all JSON files in math-ast directory
 */

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Directories
const MATH_AST_DIR = path.join(__dirname, 'math-ast');
const REFERENCE_DIR = path.join(__dirname, 'reference');
const MATHLIVE_PATH = '../../mathlive/dist/mathlive.mjs';

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
        input: null,
        output: REFERENCE_DIR,
        htmlOnly: false,
        all: false
    };
    
    for (let i = 0; i < args.length; i++) {
        const arg = args[i];
        const next = args[i + 1];
        
        switch (arg) {
            case '--input':
                options.input = next;
                i++;
                break;
            case '--output':
                options.output = next;
                i++;
                break;
            case '--html-only':
                options.htmlOnly = true;
                break;
            case '--all':
                options.all = true;
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
MathLive Reference Generator

Usage: node mathlive_reference.mjs [options]

Options:
  --input <file>    Input JSON file with LaTeX expressions
  --output <dir>    Output directory for reference files
  --html-only       Only generate HTML for manual browser testing
  --all             Process all JSON files in math-ast directory
  --help, -h        Show this help

Examples:
  node mathlive_reference.mjs --input fracs.json
  node mathlive_reference.mjs --all --html-only
`);
}

/**
 * Collect input files
 */
function collectInputFiles(options) {
    if (options.input) {
        const inputPath = path.resolve(options.input);
        if (fs.existsSync(inputPath)) {
            return [inputPath];
        }
        // Try math-ast directory
        const mathPath = path.join(MATH_AST_DIR, options.input);
        if (fs.existsSync(mathPath)) {
            return [mathPath];
        }
        console.error(`Input file not found: ${options.input}`);
        return [];
    }
    
    if (options.all) {
        if (!fs.existsSync(MATH_AST_DIR)) {
            console.error(`Math AST directory not found: ${MATH_AST_DIR}`);
            return [];
        }
        return fs.readdirSync(MATH_AST_DIR)
            .filter(f => f.endsWith('.json'))
            .map(f => path.join(MATH_AST_DIR, f));
    }
    
    console.error('No input specified. Use --input <file> or --all');
    return [];
}

/**
 * Load expressions from a JSON file
 */
function loadExpressions(filePath) {
    try {
        const content = fs.readFileSync(filePath, 'utf-8');
        const data = JSON.parse(content);
        return data.expressions || [];
    } catch (error) {
        console.error(`Error loading ${filePath}: ${error.message}`);
        return [];
    }
}

/**
 * Generate HTML page for browser-based AST/HTML generation
 */
function generateBrowserHTML(expressions, sourceName) {
    const expressionsJson = JSON.stringify(expressions, null, 2);
    
    return `<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>MathLive Reference Generator - ${sourceName}</title>
    <style>
        body { 
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            padding: 20px;
            max-width: 1200px;
            margin: 0 auto;
        }
        .header { 
            display: flex; 
            justify-content: space-between; 
            align-items: center;
            margin-bottom: 20px;
        }
        .status { 
            padding: 10px 15px; 
            border-radius: 4px;
            background: #f0f0f0;
        }
        .status.loading { background: #fff3cd; }
        .status.success { background: #d4edda; color: #155724; }
        .status.error { background: #f8d7da; color: #721c24; }
        
        .controls { margin: 20px 0; }
        button { 
            padding: 10px 20px; 
            margin-right: 10px;
            cursor: pointer;
            border: none;
            border-radius: 4px;
            background: #007bff;
            color: white;
            font-size: 14px;
        }
        button:hover { background: #0056b3; }
        button:disabled { background: #ccc; cursor: not-allowed; }
        
        .results { margin-top: 20px; }
        .expression { 
            border: 1px solid #ddd; 
            margin: 10px 0; 
            padding: 15px;
            border-radius: 4px;
        }
        .expression-header { 
            display: flex; 
            justify-content: space-between;
            margin-bottom: 10px;
        }
        .latex { 
            font-family: monospace; 
            background: #f5f5f5; 
            padding: 5px 10px;
            border-radius: 3px;
        }
        .rendered { margin: 10px 0; font-size: 1.2em; }
        .output { margin-top: 10px; }
        .output-label { font-weight: bold; margin-bottom: 5px; }
        pre { 
            background: #f5f5f5; 
            padding: 10px; 
            overflow: auto; 
            max-height: 300px;
            font-size: 12px;
            border-radius: 4px;
        }
        .html-output { 
            background: #fff; 
            border: 1px solid #ddd;
            padding: 10px;
            margin: 5px 0;
        }
    </style>
</head>
<body>
    <div class="header">
        <h1>MathLive Reference Generator</h1>
        <div class="status" id="status">Loading MathLive...</div>
    </div>
    
    <p>Source: <strong>${sourceName}</strong> | Expressions: <span id="count">0</span></p>
    
    <div class="controls">
        <button id="generateBtn" onclick="generateAll()" disabled>Generate All References</button>
        <button id="downloadBtn" onclick="downloadAll()" disabled>Download JSON</button>
        <button id="copyBtn" onclick="copyToClipboard()" disabled>Copy to Clipboard</button>
    </div>
    
    <div id="results" class="results"></div>
    
    <script type="module">
        // Input expressions
        const expressions = ${expressionsJson};
        document.getElementById('count').textContent = expressions.length;
        
        // Import MathLive
        let MathLive;
        try {
            MathLive = await import('${MATHLIVE_PATH}');
            document.getElementById('status').textContent = 'MathLive loaded ✓';
            document.getElementById('status').className = 'status success';
            document.getElementById('generateBtn').disabled = false;
        } catch (error) {
            document.getElementById('status').textContent = 'Failed to load MathLive: ' + error.message;
            document.getElementById('status').className = 'status error';
        }
        
        // Store generated references
        window.generatedRefs = [];
        
        // Generate reference for a single expression
        function generateReference(latex, type) {
            try {
                // Create mathfield element
                const mf = new MathLive.MathfieldElement();
                mf.style.position = 'absolute';
                mf.style.left = '-9999px';
                document.body.appendChild(mf);
                
                // Set value
                mf.value = latex;
                
                // Get AST
                let ast = null;
                const model = mf._mathfield?.model;
                const root = model?.root;
                if (root && typeof root.toJson === 'function') {
                    ast = root.toJson();
                }
                
                // Get HTML
                const html = MathLive.convertLatexToMarkup(latex, {
                    mathstyle: type === 'display' ? 'displaystyle' : 'textstyle'
                });
                
                document.body.removeChild(mf);
                
                return { ast, html };
            } catch (error) {
                return { error: error.message };
            }
        }
        
        // Generate all references
        window.generateAll = function() {
            const resultsDiv = document.getElementById('results');
            resultsDiv.innerHTML = '';
            window.generatedRefs = [];
            
            document.getElementById('status').textContent = 'Generating references...';
            document.getElementById('status').className = 'status loading';
            
            let successCount = 0;
            let errorCount = 0;
            
            for (let i = 0; i < expressions.length; i++) {
                const expr = expressions[i];
                const ref = generateReference(expr.latex, expr.type);
                
                // Create display element
                const div = document.createElement('div');
                div.className = 'expression';
                
                const hasError = ref.error;
                if (hasError) errorCount++;
                else successCount++;
                
                div.innerHTML = \`
                    <div class="expression-header">
                        <span class="latex">\${escapeHtml(expr.latex)}</span>
                        <span>\${hasError ? '❌' : '✓'} #\${i + 1}</span>
                    </div>
                    <div class="rendered">
                        \${ref.html || '<em>No HTML output</em>'}
                    </div>
                    <details>
                        <summary>AST</summary>
                        <pre>\${JSON.stringify(ref.ast, null, 2)}</pre>
                    </details>
                    <details>
                        <summary>HTML Source</summary>
                        <pre>\${escapeHtml(ref.html || '')}</pre>
                    </details>
                \`;
                
                resultsDiv.appendChild(div);
                
                // Store reference
                window.generatedRefs.push({
                    index: i,
                    latex: expr.latex,
                    type: expr.type,
                    ast: ref.ast,
                    html: ref.html,
                    error: ref.error
                });
            }
            
            document.getElementById('status').textContent = 
                \`Generated \${successCount} references (\${errorCount} errors)\`;
            document.getElementById('status').className = errorCount > 0 ? 'status error' : 'status success';
            document.getElementById('downloadBtn').disabled = false;
            document.getElementById('copyBtn').disabled = false;
        };
        
        // Download as JSON
        window.downloadAll = function() {
            const output = {
                source: '${sourceName}',
                generatedAt: new Date().toISOString(),
                generator: 'MathLive',
                references: window.generatedRefs
            };
            
            const blob = new Blob([JSON.stringify(output, null, 2)], { type: 'application/json' });
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = '${sourceName}'.replace('.json', '.mathlive.json');
            a.click();
            URL.revokeObjectURL(url);
        };
        
        // Copy to clipboard
        window.copyToClipboard = function() {
            const output = {
                source: '${sourceName}',
                generatedAt: new Date().toISOString(),
                generator: 'MathLive',
                references: window.generatedRefs
            };
            
            navigator.clipboard.writeText(JSON.stringify(output, null, 2))
                .then(() => alert('Copied to clipboard!'))
                .catch(err => alert('Failed to copy: ' + err.message));
        };
        
        // HTML escaping
        function escapeHtml(text) {
            const div = document.createElement('div');
            div.textContent = text;
            return div.innerHTML;
        }
    </script>
</body>
</html>`;
}

/**
 * Generate HTML file for browser testing
 */
function generateHTMLFile(inputPath, outputDir) {
    const sourceName = path.basename(inputPath);
    const expressions = loadExpressions(inputPath);
    
    if (expressions.length === 0) {
        console.log(`  Skipping ${sourceName}: no expressions found`);
        return;
    }
    
    const html = generateBrowserHTML(expressions, sourceName);
    const outputName = sourceName.replace('.json', '.mathlive.html');
    const outputPath = path.join(outputDir, outputName);
    
    fs.writeFileSync(outputPath, html);
    console.log(`  Generated: ${outputName} (${expressions.length} expressions)`);
}

/**
 * Main entry point
 */
async function main() {
    const options = parseArgs();
    const inputFiles = collectInputFiles(options);
    
    if (inputFiles.length === 0) {
        process.exit(1);
    }
    
    console.log(`\nMathLive Reference Generator`);
    console.log(`============================`);
    console.log(`Output directory: ${options.output}`);
    console.log(`Files to process: ${inputFiles.length}`);
    console.log('');
    
    // Ensure output directory exists
    if (!fs.existsSync(options.output)) {
        fs.mkdirSync(options.output, { recursive: true });
    }
    
    // Generate HTML files
    console.log('Generating HTML files for browser testing:');
    for (const inputPath of inputFiles) {
        generateHTMLFile(inputPath, options.output);
    }
    
    console.log('');
    console.log('Done! Open the generated HTML files in a browser to generate references.');
    console.log('The browser will use MathLive to parse expressions and generate AST/HTML.');
}

main().catch(error => {
    console.error('Error:', error);
    process.exit(1);
});
