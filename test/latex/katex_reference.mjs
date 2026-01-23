#!/usr/bin/env node

/**
 * KaTeX Reference Generator
 * 
 * Generates reference HTML from KaTeX for LaTeX math expressions.
 * This provides a second reference point for HTML comparison.
 * 
 * Usage:
 *   node katex_reference.mjs [options]
 * 
 * Options:
 *   --input <file>    Input JSON file with LaTeX expressions
 *   --output <dir>    Output directory for reference files
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

// KaTeX CDN URL
const KATEX_CSS = 'https://cdn.jsdelivr.net/npm/katex@0.16.9/dist/katex.min.css';
const KATEX_JS = 'https://cdn.jsdelivr.net/npm/katex@0.16.9/dist/katex.min.js';

/**
 * Parse command line arguments
 */
function parseArgs() {
    const args = process.argv.slice(2);
    const options = {
        input: null,
        output: REFERENCE_DIR,
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
KaTeX Reference Generator

Usage: node katex_reference.mjs [options]

Options:
  --input <file>    Input JSON file with LaTeX expressions
  --output <dir>    Output directory for reference files
  --all             Process all JSON files in math-ast directory
  --help, -h        Show this help

Examples:
  node katex_reference.mjs --input fracs.json
  node katex_reference.mjs --all
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
 * Generate HTML page for browser-based KaTeX rendering
 */
function generateBrowserHTML(expressions, sourceName) {
    const expressionsJson = JSON.stringify(expressions, null, 2);
    
    return `<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>KaTeX Reference Generator - ${sourceName}</title>
    <link rel="stylesheet" href="${KATEX_CSS}">
    <script src="${KATEX_JS}"></script>
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
        .status.success { background: #d4edda; color: #155724; }
        .status.error { background: #f8d7da; color: #721c24; }
        
        .controls { margin: 20px 0; }
        button { 
            padding: 10px 20px; 
            margin-right: 10px;
            cursor: pointer;
            border: none;
            border-radius: 4px;
            background: #28a745;
            color: white;
            font-size: 14px;
        }
        button:hover { background: #218838; }
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
        pre { 
            background: #f5f5f5; 
            padding: 10px; 
            overflow: auto; 
            max-height: 300px;
            font-size: 12px;
            border-radius: 4px;
        }
    </style>
</head>
<body>
    <div class="header">
        <h1>KaTeX Reference Generator</h1>
        <div class="status" id="status">Ready</div>
    </div>
    
    <p>Source: <strong>${sourceName}</strong> | Expressions: <span id="count">0</span></p>
    
    <div class="controls">
        <button id="generateBtn" onclick="generateAll()">Generate All References</button>
        <button id="downloadBtn" onclick="downloadAll()" disabled>Download JSON</button>
        <button id="copyBtn" onclick="copyToClipboard()" disabled>Copy to Clipboard</button>
    </div>
    
    <div id="results" class="results"></div>
    
    <script>
        // Input expressions
        const expressions = ${expressionsJson};
        document.getElementById('count').textContent = expressions.length;
        
        // Store generated references
        window.generatedRefs = [];
        
        // Generate reference for a single expression
        function generateReference(latex, type) {
            try {
                const html = katex.renderToString(latex, {
                    displayMode: type === 'display',
                    throwOnError: false,
                    strict: false
                });
                return { html };
            } catch (error) {
                return { error: error.message };
            }
        }
        
        // Generate all references
        window.generateAll = function() {
            const resultsDiv = document.getElementById('results');
            resultsDiv.innerHTML = '';
            window.generatedRefs = [];
            
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
                        \${ref.html || '<em>Error: ' + (ref.error || 'Unknown') + '</em>'}
                    </div>
                    <details>
                        <summary>HTML Source</summary>
                        <pre>\${escapeHtml(ref.html || ref.error || '')}</pre>
                    </details>
                \`;
                
                resultsDiv.appendChild(div);
                
                // Store reference
                window.generatedRefs.push({
                    index: i,
                    latex: expr.latex,
                    type: expr.type,
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
                generator: 'KaTeX',
                references: window.generatedRefs
            };
            
            const blob = new Blob([JSON.stringify(output, null, 2)], { type: 'application/json' });
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = '${sourceName}'.replace('.json', '.katex.json');
            a.click();
            URL.revokeObjectURL(url);
        };
        
        // Copy to clipboard
        window.copyToClipboard = function() {
            const output = {
                source: '${sourceName}',
                generatedAt: new Date().toISOString(),
                generator: 'KaTeX',
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
    const outputName = sourceName.replace('.json', '.katex.html');
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
    
    console.log(`\nKaTeX Reference Generator`);
    console.log(`=========================`);
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
}

main().catch(error => {
    console.error('Error:', error);
    process.exit(1);
});
