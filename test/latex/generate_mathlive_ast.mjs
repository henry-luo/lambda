#!/usr/bin/env node
/**
 * Generate MathLive AST for LaTeX math expressions
 * 
 * This script:
 * 1. Reads .tex files from ./fixtures/math
 * 2. Extracts math expressions  
 * 3. Generates an HTML file that uses MathLive to parse and dump AST
 * 4. The HTML can be opened in a browser, or run with puppeteer
 * 
 * Usage: 
 *   node generate_mathlive_ast.mjs           # Generate HTML + run with puppeteer
 *   node generate_mathlive_ast.mjs --html    # Only generate HTML (open manually)
 */

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Directories
const INPUT_DIR = path.join(__dirname, 'fixtures', 'math');
const OUTPUT_DIR = path.join(__dirname, 'math-ast');
const MATHLIVE_PATH = '../../mathlive/dist/mathlive.mjs';

// Create output directory
if (!fs.existsSync(OUTPUT_DIR)) {
    fs.mkdirSync(OUTPUT_DIR, { recursive: true });
}

/**
 * Extract math expressions from a .tex file
 */
function extractMathExpressions(content) {
    const expressions = [];
    
    // Display math: $$...$$ or \[...\]
    const displayRegex = /\$\$([\s\S]*?)\$\$|\\\[([\s\S]*?)\\\]/g;
    let match;
    while ((match = displayRegex.exec(content)) !== null) {
        const expr = (match[1] || match[2] || '').trim();
        if (expr) expressions.push({ type: 'display', latex: expr });
    }
    
    // Inline math: $...$ (not $$)
    const inlineRegex = /(?<!\$)\$(?!\$)((?:[^$\\]|\\.)+?)\$(?!\$)/g;
    while ((match = inlineRegex.exec(content)) !== null) {
        const expr = (match[1] || '').trim();
        if (expr) expressions.push({ type: 'inline', latex: expr });
    }
    
    // \(...\)
    const parenRegex = /\\\(([\s\S]*?)\\\)/g;
    while ((match = parenRegex.exec(content)) !== null) {
        const expr = (match[1] || '').trim();
        if (expr) expressions.push({ type: 'inline', latex: expr });
    }
    
    return expressions;
}

/**
 * Collect all expressions from all .tex files
 */
function collectAllExpressions() {
    const texFiles = fs.readdirSync(INPUT_DIR)
        .filter(f => f.endsWith('.tex'))
        .sort();
    
    const allFiles = [];
    
    for (const texFile of texFiles) {
        const content = fs.readFileSync(path.join(INPUT_DIR, texFile), 'utf-8');
        const expressions = extractMathExpressions(content);
        
        if (expressions.length > 0) {
            allFiles.push({
                filename: texFile,
                expressions: expressions
            });
        }
    }
    
    return allFiles;
}

/**
 * Generate HTML that parses LaTeX with MathLive and outputs JSON
 */
function generateHtml(allFiles) {
    const dataJson = JSON.stringify(allFiles, null, 2);
    
    return `<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>MathLive AST Generator</title>
    <style>
        body { font-family: monospace; padding: 20px; }
        .status { margin: 10px 0; padding: 10px; background: #f0f0f0; }
        .error { background: #ffcccc; }
        .success { background: #ccffcc; }
        pre { background: #f5f5f5; padding: 10px; overflow: auto; max-height: 400px; }
        button { padding: 10px 20px; margin: 5px; cursor: pointer; }
        #results { margin-top: 20px; }
    </style>
</head>
<body>
    <h1>MathLive AST Generator</h1>
    <div class="status" id="status">Loading MathLive...</div>
    
    <div id="controls" style="display: none;">
        <button onclick="generateAll()">Generate All AST Files</button>
        <button onclick="downloadAll()">Download All as ZIP</button>
    </div>
    
    <div id="results"></div>
    
    <script type="module">
        // Input data from .tex files
        const inputData = ${dataJson};
        
        // Import MathLive
        import * as MathLive from '${MATHLIVE_PATH}';
        
        // Store results globally
        window.astResults = {};
        
        // Helper to convert atom to JSON recursively
        function atomToJson(atom) {
            if (!atom) return null;
            
            const result = {
                type: atom.type || 'unknown',
                command: atom.command || undefined,
                value: atom.value || undefined
            };
            
            // Add style info if present
            if (atom.style) {
                const style = {};
                if (atom.style.color) style.color = atom.style.color;
                if (atom.style.backgroundColor) style.backgroundColor = atom.style.backgroundColor;
                if (atom.style.fontSize) style.fontSize = atom.style.fontSize;
                if (atom.style.fontFamily) style.fontFamily = atom.style.fontFamily;
                if (Object.keys(style).length > 0) result.style = style;
            }
            
            // Add branches (body, superscript, subscript, etc.)
            const branches = ['body', 'above', 'below', 'superscript', 'subscript'];
            for (const branch of branches) {
                if (atom[branch] && Array.isArray(atom[branch]) && atom[branch].length > 0) {
                    result[branch] = atom[branch].map(a => atomToJson(a)).filter(a => a);
                }
            }
            
            // For array atoms, add cells
            if (atom.cells && Array.isArray(atom.cells)) {
                result.cells = atom.cells.map(row => 
                    row.map(cell => cell.map(a => atomToJson(a)))
                );
            }
            
            // Use toJson if available (more complete)
            if (typeof atom.toJson === 'function') {
                try {
                    return atom.toJson();
                } catch (e) {
                    // Fall back to our manual conversion
                }
            }
            
            return result;
        }
        
        // Parse LaTeX and get AST
        function parseLatex(latex) {
            try {
                // Create a temporary mathfield to parse
                const mf = new MathLive.MathfieldElement();
                mf.style.position = 'absolute';
                mf.style.left = '-9999px';
                document.body.appendChild(mf);
                
                mf.value = latex;
                
                // Access internal model and root atom
                const model = mf._mathfield?.model;
                const root = model?.root;
                
                let ast;
                if (root && typeof root.toJson === 'function') {
                    ast = root.toJson();
                } else if (root) {
                    ast = atomToJson(root);
                } else {
                    ast = { error: 'Could not access atom tree' };
                }
                
                document.body.removeChild(mf);
                return ast;
            } catch (error) {
                return { error: error.message, latex: latex };
            }
        }
        
        // Generate AST for all files
        window.generateAll = function() {
            const resultsDiv = document.getElementById('results');
            resultsDiv.innerHTML = '<h2>Processing...</h2>';
            
            const allResults = [];
            
            for (const file of inputData) {
                const fileResult = {
                    source: file.filename,
                    generatedAt: new Date().toISOString(),
                    generator: 'MathLive',
                    expressions: []
                };
                
                for (let i = 0; i < file.expressions.length; i++) {
                    const expr = file.expressions[i];
                    const ast = parseLatex(expr.latex);
                    
                    fileResult.expressions.push({
                        index: i,
                        type: expr.type,
                        latex: expr.latex,
                        ast: ast
                    });
                }
                
                window.astResults[file.filename] = fileResult;
                allResults.push(fileResult);
            }
            
            // Display results
            resultsDiv.innerHTML = '<h2>Results</h2>';
            for (const result of allResults) {
                const div = document.createElement('div');
                div.innerHTML = \`
                    <h3>\${result.source}</h3>
                    <p>\${result.expressions.length} expressions</p>
                    <details>
                        <summary>View JSON</summary>
                        <pre>\${JSON.stringify(result, null, 2)}</pre>
                    </details>
                    <button onclick="downloadFile('\${result.source}')">Download JSON</button>
                \`;
                resultsDiv.appendChild(div);
            }
            
            document.getElementById('status').textContent = 
                'Done! Processed ' + allResults.length + ' files.';
            document.getElementById('status').className = 'status success';
            
            // For puppeteer: signal completion
            window.generationComplete = true;
        };
        
        // Download single file
        window.downloadFile = function(filename) {
            const result = window.astResults[filename];
            if (!result) return;
            
            const jsonStr = JSON.stringify(result, null, 2);
            const blob = new Blob([jsonStr], { type: 'application/json' });
            const url = URL.createObjectURL(blob);
            
            const a = document.createElement('a');
            a.href = url;
            a.download = filename.replace('.tex', '.json');
            a.click();
            
            URL.revokeObjectURL(url);
        };
        
        // Download all files
        window.downloadAll = function() {
            for (const filename of Object.keys(window.astResults)) {
                window.downloadFile(filename);
            }
        };
        
        // Get all results (for puppeteer)
        window.getAllResults = function() {
            return window.astResults;
        };
        
        // Initialize
        document.getElementById('status').textContent = 'MathLive loaded. Ready to generate AST.';
        document.getElementById('status').className = 'status success';
        document.getElementById('controls').style.display = 'block';
        
        // Auto-generate for headless mode
        if (window.autoGenerate) {
            window.generateAll();
        }
    </script>
</body>
</html>`;
}

/**
 * Main
 */
async function main() {
    const htmlOnly = process.argv.includes('--html');
    
    console.log('MathLive AST Generator');
    console.log('======================\n');
    
    // Collect expressions
    console.log('Scanning .tex files...');
    const allFiles = collectAllExpressions();
    console.log(`Found ${allFiles.length} files with math expressions\n`);
    
    let totalExpressions = 0;
    for (const f of allFiles) {
        totalExpressions += f.expressions.length;
        console.log(`  ${f.filename}: ${f.expressions.length} expressions`);
    }
    console.log(`\nTotal: ${totalExpressions} expressions\n`);
    
    // Generate HTML
    const html = generateHtml(allFiles);
    const htmlPath = path.join(__dirname, 'mathlive_ast_generator.html');
    fs.writeFileSync(htmlPath, html);
    console.log(`Generated: ${htmlPath}\n`);
    
    if (htmlOnly) {
        console.log('HTML-only mode. Open the HTML file in a browser to generate AST.');
        console.log('Click "Generate All AST Files" then download the results.');
        return;
    }
    
    // Try to run with puppeteer
    try {
        console.log('Attempting to run with Puppeteer...');
        const puppeteer = await import('puppeteer');
        const http = await import('http');
        
        // Create a simple HTTP server to serve files
        const projectRoot = path.resolve(__dirname, '../..');
        const server = http.createServer((req, res) => {
            let filePath = path.join(projectRoot, req.url);
            if (req.url === '/') {
                filePath = htmlPath;
            }
            
            const ext = path.extname(filePath);
            const contentTypes = {
                '.html': 'text/html',
                '.js': 'application/javascript',
                '.mjs': 'application/javascript',
                '.css': 'text/css',
                '.json': 'application/json',
                '.woff2': 'font/woff2',
                '.woff': 'font/woff',
                '.ttf': 'font/ttf'
            };
            
            fs.readFile(filePath, (err, data) => {
                if (err) {
                    res.writeHead(404);
                    res.end('Not found: ' + req.url);
                    return;
                }
                res.writeHead(200, { 'Content-Type': contentTypes[ext] || 'text/plain' });
                res.end(data);
            });
        });
        
        await new Promise(resolve => server.listen(8765, resolve));
        console.log('Started local server on http://localhost:8765');
        
        const browser = await puppeteer.default.launch({ 
            headless: 'new',
            args: ['--no-sandbox', '--disable-setuid-sandbox']
        });
        const page = await browser.newPage();
        
        // Add console logging
        page.on('console', msg => console.log('Browser:', msg.text()));
        page.on('pageerror', err => console.error('Page error:', err.message));
        
        // Set auto-generate flag before loading
        await page.evaluateOnNewDocument(() => {
            window.autoGenerate = true;
        });
        
        console.log('Loading page...');
        await page.goto('http://localhost:8765/', { waitUntil: 'networkidle0', timeout: 30000 });
        
        console.log('Waiting for generation to complete...');
        // Wait for generation to complete with timeout
        await page.waitForFunction('window.generationComplete === true', { timeout: 60000 });
        
        // Get results
        const results = await page.evaluate(() => window.getAllResults());
        
        // Save each file
        for (const [filename, result] of Object.entries(results)) {
            const outputPath = path.join(OUTPUT_DIR, filename.replace('.tex', '.json'));
            fs.writeFileSync(outputPath, JSON.stringify(result, null, 2));
            console.log(`  Wrote: ${path.basename(outputPath)}`);
        }
        
        await browser.close();
        server.close();
        console.log('\nDone! AST files saved to:', OUTPUT_DIR);
        
    } catch (error) {
        if (error.code === 'ERR_MODULE_NOT_FOUND') {
            console.log('Puppeteer not installed. Install with: npm install puppeteer');
            console.log('\nAlternatively, open the generated HTML in a browser:');
            console.log(`  open ${htmlPath}`);
        } else {
            console.error('Error running Puppeteer:', error.message);
            console.error(error.stack);
            console.log('\nOpen the HTML file manually in a browser to generate AST.');
        }
    }
}

main().catch(console.error);
