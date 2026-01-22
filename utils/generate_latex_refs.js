#!/usr/bin/env node
/**
 * generate_latex_refs.js - Consolidated LaTeX reference generator
 * 
 * This script generates HTML reference files from .tex fixtures:
 * - For fixtures under latexjs/: uses latex.js -> transforms to hybrid .html
 * - For other fixtures: uses latexml -> transforms to hybrid .html
 * 
 * Usage:
 *   node utils/generate_latex_refs.js [options]
 * 
 * Options:
 *   --force           Regenerate even if output already exists
 *   --verbose         Show detailed output
 *   --test=<pattern>  Only process files matching pattern
 *   --test-dir=<dir>  Test directory relative to project root (default: test/latex)
 *   --output-format=<fmt>  Output format: html (default) or dvi
 *   --clean           Remove existing output files first
 *   --dry-run         Show what would be done without doing it
 * 
 * Requirements:
 *   - For HTML: latex.js built: cd latex-js && npm install --legacy-peer-deps && npm run build
 *   - For HTML: latexml installed: brew install latexml
 *   - For DVI: tex installed: brew install --cask mactex
 */

const fs = require('fs');
const path = require('path');
const { execSync, spawnSync } = require('child_process');

// Configuration
const PROJECT_ROOT = path.resolve(__dirname, '..');
const LATEXJS_PATH = path.join(PROJECT_ROOT, 'latex-js', 'bin', 'latex.js');

// Parse arguments
const args = process.argv.slice(2);
const options = {
    force: args.includes('--force'),
    verbose: args.includes('--verbose'),
    clean: args.includes('--clean'),
    dryRun: args.includes('--dry-run'),
    test: null,
    testDir: 'test/latex', // default test directory
    outputFormat: 'html' // default output format: 'html' or 'dvi'
};

// Parse --test=pattern, --test-dir=dir, and --output-format=fmt
for (const arg of args) {
    if (arg.startsWith('--test=')) {
        options.test = arg.substring(7);
    } else if (arg.startsWith('--test-dir=')) {
        options.testDir = arg.substring(11);
    } else if (arg.startsWith('--output-format=')) {
        options.outputFormat = arg.substring(16);
    }
}

// Compute directories based on --test-dir
const FIXTURES_DIR = path.join(PROJECT_ROOT, options.testDir, 'fixtures');
const EXPECTED_DIR = path.join(PROJECT_ROOT, options.testDir, 'expected');

// ============================================================================
// Transformation functions (ported from generate_hybrid_refs.py)
// ============================================================================

/**
 * Replace <span class="X">...</span> with <tag>...</tag>, handling nesting
 */
function replaceSpanWithTag(html, spanClass, tagName) {
    const pattern = `<span class="${spanClass}">`;
    let result = html;
    let iterations = 0;
    const maxIterations = 100;

    while (result.includes(pattern) && iterations < maxIterations) {
        iterations++;
        const start = result.indexOf(pattern);
        if (start === -1) break;

        const afterTag = start + pattern.length;
        let depth = 1;
        let pos = afterTag;

        while (depth > 0 && pos < result.length) {
            if (result.substring(pos, pos + 6) === '<span ') {
                depth++;
                pos += 6;
            } else if (result.substring(pos, pos + 5) === '<span') {
                depth++;
                pos += 5;
            } else if (result.substring(pos, pos + 7) === '</span>') {
                depth--;
                if (depth === 0) {
                    const openingTag = `<${tagName}>`;
                    const closingTag = `</${tagName}>`;
                    result = result.substring(0, start) + openingTag + 
                             result.substring(afterTag, pos) + closingTag + 
                             result.substring(pos + 7);
                    break;
                }
                pos += 7;
            } else {
                pos++;
            }
        }

        if (depth > 0) break;
    }

    return result;
}

/**
 * Replace quote environment divs with blockquotes
 */
function replaceQuoteEnv(html, envClass) {
    const pattern = `<div class="list ${envClass}">`;
    const replacement = `<blockquote class="${envClass}">`;
    let result = html;

    while (result.includes(pattern)) {
        const start = result.indexOf(pattern);
        if (start === -1) break;

        const afterTag = start + pattern.length;
        let depth = 1;
        let pos = afterTag;

        while (depth > 0 && pos < result.length) {
            if (result.substring(pos, pos + 5) === '<div ') {
                depth++;
                pos += 5;
            } else if (result.substring(pos, pos + 4) === '<div') {
                depth++;
                pos += 4;
            } else if (result.substring(pos, pos + 6) === '</div>') {
                depth--;
                if (depth === 0) {
                    result = result.substring(0, start) + replacement + 
                             result.substring(afterTag, pos) + '</blockquote>' + 
                             result.substring(pos + 6);
                    break;
                }
                pos += 6;
            } else {
                pos++;
            }
        }

        if (depth > 0) {
            result = result.substring(0, start) + replacement + result.substring(afterTag);
            break;
        }
    }

    return result;
}

/**
 * Remove itemlabel spans
 */
function removeItemlabels(html) {
    const pattern = '<span class="itemlabel">';
    let result = html;

    while (result.includes(pattern)) {
        const start = result.indexOf(pattern);
        if (start === -1) break;

        const afterTag = start + pattern.length;
        let depth = 1;
        let pos = afterTag;

        while (depth > 0 && pos < result.length) {
            if (result.substring(pos, pos + 5) === '<span') {
                depth++;
                pos += 5;
            } else if (result.substring(pos, pos + 7) === '</span>') {
                depth--;
                if (depth === 0) {
                    result = result.substring(0, start) + result.substring(pos + 7);
                    break;
                }
                pos += 7;
            } else {
                pos++;
            }
        }

        if (depth > 0) break;
    }

    return result;
}

/**
 * Transform latex.js HTML to Lambda hybrid format
 */
function transformLatexjsToHybrid(html) {
    let result = html;

    // Document wrapper: <div class="body"> -> <article class="latex-document">
    result = result.replace(/<div class="body">/g, '<article class="latex-document">');
    result = result.trimEnd().replace(/<\/div>\s*$/, '</article>');

    // Text formatting: semantic HTML5 tags
    result = replaceSpanWithTag(result, 'bf', 'strong');
    result = replaceSpanWithTag(result, 'it', 'em');
    result = replaceSpanWithTag(result, 'tt', 'code');
    result = replaceSpanWithTag(result, 'underline', 'u');
    result = replaceSpanWithTag(result, 'sout', 's');

    // Quote environments
    result = replaceQuoteEnv(result, 'quote');
    result = replaceQuoteEnv(result, 'quotation');
    result = replaceQuoteEnv(result, 'verse');

    // Lists: update class names
    result = result.replace(/<ul class="list">/g, '<ul class="itemize">');
    result = result.replace(/<ul class="list (\w+)">/g, '<ul class="itemize $1">');
    result = result.replace(/<ol class="list">/g, '<ol class="enumerate">');
    result = result.replace(/<ol class="list (\w+)">/g, '<ol class="enumerate $1">');
    result = result.replace(/<dl class="list">/g, '<dl class="description">');
    result = result.replace(/<dl class="list (\w+)">/g, '<dl class="description $1">');

    // Remove itemlabel spans
    result = removeItemlabels(result);

    // Verbatim: code class="tt" -> code
    result = result.replace(/<code class="tt">/g, '<code>');

    // Center/alignment environments
    result = result.replace(/<div class="list center">/g, '<div class="center">');
    result = result.replace(/<div class="list flushleft">/g, '<div class="flushleft">');
    result = result.replace(/<div class="list flushright">/g, '<div class="flushright">');

    // Clean up empty spans from hbox
    result = result.replace(/<span class="hbox"><span><\/span><\/span>/g, '');
    result = result.replace(/<span class="hbox llap"><span><\/span><\/span>/g, '');

    return result;
}

/**
 * Transform LaTeXML HTML to Lambda hybrid format
 */
function transformLatexmlToHybrid(html) {
    let result = html;

    // Extract body content if full document
    const bodyMatch = result.match(/<body[^>]*>([\s\S]*)<\/body>/);
    if (bodyMatch) {
        result = bodyMatch[1].trim();
    }

    // Remove LaTeXML footer
    result = result.replace(/<footer[^>]*>[\s\S]*?<\/footer>/g, '');

    // Remove page wrapper divs
    result = result.replace(/<div class="ltx_page_main">\s*/g, '');
    result = result.replace(/<div class="ltx_page_content">\s*/g, '');
    result = result.replace(/<\/div>\s*<\/div>\s*$/g, '');

    // Remove LaTeXML ERROR spans
    result = result.replace(/<span class="ltx_ERROR[^"]*">[^<]*<\/span>\s*/g, '');

    // Document wrapper
    result = result.replace(/<article class="ltx_document[^"]*">/g, '<article class="latex-document">');

    // Remove ltx_para wrapper divs
    result = result.replace(/<div\s+class="ltx_para">\s*/g, '');

    // Section classes
    result = result.replace(/<section\s+class="ltx_chapter">/g, '<section class="chapter">');
    result = result.replace(/<section\s+class="ltx_section">/g, '<section class="section">');
    result = result.replace(/<section\s+class="ltx_subsection">/g, '<section class="subsection">');
    result = result.replace(/<section\s+class="ltx_subsubsection">/g, '<section class="subsubsection">');

    // Headers
    result = result.replace(/<h1 class="ltx_title ltx_title_document">/g, '<h1 class="document-title">');
    result = result.replace(/<h2 class="ltx_title ltx_title_section">/g, '<h2 class="section-title">');
    result = result.replace(/<h3 class="ltx_title ltx_title_subsection">/g, '<h3 class="subsection-title">');
    result = result.replace(/<h4 class="ltx_title ltx_title_subsubsection">/g, '<h4 class="subsubsection-title">');

    // Section number tags
    result = result.replace(/<span class="ltx_tag ltx_tag_section">/g, '<span class="section-number">');
    result = result.replace(/<span class="ltx_tag ltx_tag_subsection">/g, '<span class="subsection-number">');

    // Text formatting
    result = result.replace(/<span class="ltx_text ltx_font_bold">([^<]*)<\/span>/g, '<strong>$1</strong>');
    result = result.replace(/<span class="ltx_text ltx_font_italic">([^<]*)<\/span>/g, '<em>$1</em>');
    result = result.replace(/<span class="ltx_text ltx_font_typewriter">([^<]*)<\/span>/g, '<code>$1</code>');

    // Paragraphs
    result = result.replace(/<p class="ltx_p">/g, '<p>');
    result = result.replace(/<p class="ltx_p ([^"]+)">/g, '<p class="$1">');

    // Quote environments
    result = result.replace(/<blockquote class="ltx_quote">/g, '<blockquote class="quote">');
    result = result.replace(/<blockquote class="ltx_quote ltx_role_verse">/g, '<blockquote class="verse">');

    // Lists
    result = result.replace(/<ul class="ltx_itemize">/g, '<ul class="itemize">');
    result = result.replace(/<ol class="ltx_enumerate">/g, '<ol class="enumerate">');
    result = result.replace(/<dl class="ltx_description">/g, '<dl class="description">');
    result = result.replace(/<li class="ltx_item"[^>]*>/g, '<li>');

    // Remove ltx_tag spans
    result = result.replace(/<span class="ltx_tag[^"]*">[^<]*<\/span>\s*/g, '');

    // Verbatim/code
    result = result.replace(/<code class="ltx_verbatim ltx_font_typewriter">/g, '<code>');
    result = result.replace(/<pre class="ltx_verbatim">/g, '<pre class="verbatim">');

    // Line breaks
    result = result.replace(/<br class="ltx_break">/g, '<br>');

    // Links
    result = result.replace(/<a href="([^"]*)" class="ltx_ref[^"]*">/g, '<a href="$1">');
    result = result.replace(/<a href="([^"]*)" title="[^"]*" class="ltx_ref[^"]*">/g, '<a href="$1">');

    // Remove dates div
    result = result.replace(/<div class="ltx_dates">[^<]*<\/div>\s*/g, '');

    // Clean up remaining ltx_ classes
    result = result.replace(/ class="ltx_[^"]*"/g, '');

    // Remove orphan </div> tags
    result = result.replace(/<\/p>\s*<\/div>/g, '</p>');
    result = result.replace(/<\/li>\s*<\/div>/g, '</li>');
    result = result.replace(/<\/dd>\s*<\/div>/g, '</dd>');
    result = result.replace(/<\/article>\s*<\/div>/g, '</article>');
    result = result.replace(/<\/section>\s*<\/div>/g, '</section>');

    // Remove empty divs
    result = result.replace(/<div>\s*<\/div>/g, '');
    result = result.replace(/<div\s+>\s*<\/div>/g, '');

    // Remove trailing orphan </div> tags
    while (result.trimEnd().endsWith('</div>')) {
        result = result.trimEnd().slice(0, -6);
    }

    // Clean up extra whitespace
    result = result.replace(/\n\s*\n/g, '\n');
    result = result.trim();

    return result;
}

// ============================================================================
// File discovery and processing
// ============================================================================

/**
 * Recursively find all .tex files in a directory
 */
function findTexFiles(dir, baseDir = dir) {
    const files = [];
    
    if (!fs.existsSync(dir)) {
        return files;
    }

    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const fullPath = path.join(dir, entry.name);
        if (entry.isDirectory()) {
            files.push(...findTexFiles(fullPath, baseDir));
        } else if (entry.name.endsWith('.tex')) {
            files.push({
                texPath: fullPath,
                relPath: path.relative(baseDir, fullPath)
            });
        }
    }

    return files;
}

/**
 * Check if a path is under latexjs/ subdirectory
 */
function isLatexjsFixture(relPath) {
    return relPath.startsWith('latexjs' + path.sep) || relPath.startsWith('latexjs/');
}

/**
 * Generate HTML using latex.js
 */
function generateWithLatexjs(texPath) {
    if (!fs.existsSync(LATEXJS_PATH)) {
        throw new Error(`latex.js not found at ${LATEXJS_PATH}. Build with: cd latex-js && npm install --legacy-peer-deps && npm run build`);
    }

    const result = spawnSync('node', [LATEXJS_PATH, '-b', texPath], {
        encoding: 'utf-8',
        maxBuffer: 10 * 1024 * 1024
    });

    if (result.status !== 0) {
        throw new Error(`latex.js failed: ${result.stderr || 'unknown error'}`);
    }

    return result.stdout;
}

/**
 * Generate DVI using tex/latex
 */
function generateWithTex(texPath, dviPath) {
    // Check if latex is available
    try {
        execSync('which latex', { encoding: 'utf-8', stdio: 'pipe' });
    } catch {
        throw new Error('latex not found. Install with: brew install --cask mactex');
    }

    const texDir = path.dirname(texPath);
    const texFile = path.basename(texPath);
    const baseName = path.basename(texPath, '.tex');

    // Run latex in the tex file's directory to handle relative paths
    const result = spawnSync('latex', [
        '-interaction=nonstopmode',
        '-output-directory=' + texDir,
        texFile
    ], {
        cwd: texDir,
        encoding: 'utf-8',
        maxBuffer: 10 * 1024 * 1024
    });

    // Check if DVI was created (latex may report errors but still produce output)
    const generatedDvi = path.join(texDir, baseName + '.dvi');
    if (!fs.existsSync(generatedDvi)) {
        throw new Error(`latex failed to create DVI: ${result.stderr || result.stdout || 'unknown error'}`);
    }

    // Move DVI to expected location
    const expectedDir = path.dirname(dviPath);
    fs.mkdirSync(expectedDir, { recursive: true });
    fs.copyFileSync(generatedDvi, dviPath);

    // Clean up auxiliary files in tex directory
    const auxFiles = ['.aux', '.log', '.dvi'];
    for (const ext of auxFiles) {
        const auxPath = path.join(texDir, baseName + ext);
        if (fs.existsSync(auxPath)) {
            fs.unlinkSync(auxPath);
        }
    }

    return true;
}

/**
 * Generate HTML using latexml
 */
function generateWithLatexml(texPath) {
    // Check if latexmlc is available
    try {
        execSync('which latexmlc', { encoding: 'utf-8', stdio: 'pipe' });
    } catch {
        throw new Error('latexmlc not found. Install with: brew install latexml');
    }

    const result = spawnSync('latexmlc', [
        '--format=html5',
        '--nocomments',
        '--pmml',
        texPath
    ], {
        encoding: 'utf-8',
        maxBuffer: 10 * 1024 * 1024
    });

    if (result.status !== 0) {
        throw new Error(`latexml failed: ${result.stderr || 'unknown error'}`);
    }

    // Post-process: remove IDs, metadata, timestamps, and normalize paths
    let html = result.stdout;
    
    // Remove LaTeXML generation comments with timestamps
    html = html.replace(/<!--Generated on [^>]+ by LaTeXML[^>]*-->/g, '');
    html = html.replace(/<!-- Generated by LaTeXML .* -->/g, '');
    
    // Remove the ltx_page_logo div with "Generated on..." timestamp and Mascot Sammy
    html = html.replace(/<div class="ltx_page_logo">Generated\s+on[^<]*<a[^>]*class="ltx_LaTeXML_logo"[^>]*>[\s\S]*?<\/a>\s*<\/div>/g, '');
    
    // Fix absolute stylesheet paths to relative paths pointing to css/ subdirectory
    html = html.replace(/href="\/[^"]*\/(LaTeXML\.css)"/g, 'href="css/$1"');
    html = html.replace(/href="\/[^"]*\/(ltx-[^"]+\.css)"/g, 'href="css/$1"');
    
    // Also fix any already-relative paths that don't have css/ prefix
    html = html.replace(/href="(LaTeXML\.css)"/g, 'href="css/$1"');
    html = html.replace(/href="(ltx-[^"]+\.css)"/g, 'href="css/$1"');
    
    // Remove IDs and other metadata
    html = html.replace(/id="[a-zA-Z0-9._-]*"/g, '');
    html = html.replace(/about="[^"]*"/g, '');
    html = html.replace(/resource="[^"]*"/g, '');
    html = html.replace(/xml:id="[^"]*"/g, '');

    return html;
}

/**
 * Process a single fixture for HTML output
 */
function processFixtureHtml(texFile, stats) {
    const { texPath, relPath } = texFile;
    const baseName = path.basename(relPath, '.tex');
    const dirName = path.dirname(relPath);
    
    const expectedDir = path.join(EXPECTED_DIR, dirName);
    const htmlPath = path.join(expectedDir, baseName + '.html');

    // Apply test pattern filter
    if (options.test && !relPath.includes(options.test)) {
        return;
    }

    // Check if output exists
    if (fs.existsSync(htmlPath) && !options.force) {
        stats.skipped++;
        if (options.verbose) {
            console.log(`  [SKIP] ${relPath} (exists)`);
        }
        return;
    }

    if (options.dryRun) {
        const tool = isLatexjsFixture(relPath) ? 'latex.js' : 'latexml';
        console.log(`  [DRY-RUN] Would process ${relPath} with ${tool}`);
        stats.processed++;
        return;
    }

    try {
        let rawHtml;
        let hybridHtml;
        let usedTool;

        if (isLatexjsFixture(relPath)) {
            // Try latex.js first for latexjs/ fixtures
            try {
                rawHtml = generateWithLatexjs(texPath);
                hybridHtml = transformLatexjsToHybrid(rawHtml);
                usedTool = 'latex.js';
                
                // Also save raw latexjs.html for reference
                const latexjsPath = path.join(expectedDir, baseName + '.latexjs.html');
                fs.mkdirSync(expectedDir, { recursive: true });
                fs.writeFileSync(latexjsPath, rawHtml);
            } catch (latexjsErr) {
                // Fall back to latexml if latex.js fails
                if (options.verbose) {
                    console.log(`  [FALLBACK] ${relPath}: latex.js failed, trying latexml...`);
                }
                rawHtml = generateWithLatexml(texPath);
                hybridHtml = transformLatexmlToHybrid(rawHtml);
                usedTool = 'latexml (fallback)';
                
                // Save raw latexml.html for reference
                const latexmlPath = path.join(expectedDir, baseName + '.latexml.html');
                fs.mkdirSync(expectedDir, { recursive: true });
                fs.writeFileSync(latexmlPath, rawHtml);
            }
        } else {
            // Use latexml for other fixtures
            rawHtml = generateWithLatexml(texPath);
            hybridHtml = transformLatexmlToHybrid(rawHtml);
            usedTool = 'latexml';
            
            // Also save raw latexml.html for reference
            const latexmlPath = path.join(expectedDir, baseName + '.latexml.html');
            fs.mkdirSync(expectedDir, { recursive: true });
            fs.writeFileSync(latexmlPath, rawHtml);
        }

        // Save hybrid HTML
        fs.mkdirSync(expectedDir, { recursive: true });
        fs.writeFileSync(htmlPath, hybridHtml);

        stats.success++;
        if (options.verbose) {
            console.log(`  [OK] ${relPath} (${usedTool})`);
        } else {
            process.stdout.write('.');
        }
    } catch (err) {
        stats.failed++;
        if (options.verbose) {
            console.log(`  [FAIL] ${relPath}: ${err.message}`);
        } else {
            process.stdout.write('F');
        }
        stats.errors.push({ relPath, error: err.message });
    }
}

/**
 * Process a single fixture for DVI output
 */
function processFixtureDvi(texFile, stats) {
    const { texPath, relPath } = texFile;
    const baseName = path.basename(relPath, '.tex');
    const dirName = path.dirname(relPath);
    
    const expectedDir = path.join(EXPECTED_DIR, dirName);
    const dviPath = path.join(expectedDir, baseName + '.dvi');

    // Apply test pattern filter
    if (options.test && !relPath.includes(options.test)) {
        return;
    }

    // Check if output exists
    if (fs.existsSync(dviPath) && !options.force) {
        stats.skipped++;
        if (options.verbose) {
            console.log(`  [SKIP] ${relPath} (exists)`);
        }
        return;
    }

    if (options.dryRun) {
        console.log(`  [DRY-RUN] Would process ${relPath} with tex`);
        stats.processed++;
        return;
    }

    try {
        generateWithTex(texPath, dviPath);

        stats.success++;
        if (options.verbose) {
            console.log(`  [OK] ${relPath} (tex)`);
        } else {
            process.stdout.write('.');
        }
    } catch (err) {
        stats.failed++;
        if (options.verbose) {
            console.log(`  [FAIL] ${relPath}: ${err.message}`);
        } else {
            process.stdout.write('F');
        }
        stats.errors.push({ relPath, error: err.message });
    }
}

/**
 * Process a single fixture (dispatches to HTML or DVI based on options)
 */
function processFixture(texFile, stats) {
    if (options.outputFormat === 'dvi') {
        processFixtureDvi(texFile, stats);
    } else {
        processFixtureHtml(texFile, stats);
    }
}

/**
 * Clean existing output files
 */
function cleanOutputFiles() {
    const ext = options.outputFormat === 'dvi' ? '.dvi' : '.html';
    console.log(`Cleaning existing ${ext} files...`);
    
    function cleanDir(dir) {
        if (!fs.existsSync(dir)) return;
        
        for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
            const fullPath = path.join(dir, entry.name);
            if (entry.isDirectory()) {
                cleanDir(fullPath);
            } else if (options.outputFormat === 'dvi') {
                if (entry.name.endsWith('.dvi')) {
                    fs.unlinkSync(fullPath);
                    if (options.verbose) {
                        console.log(`  Removed: ${fullPath}`);
                    }
                }
            } else if (entry.name.endsWith('.html') && 
                       !entry.name.endsWith('.latexjs.html') && 
                       !entry.name.endsWith('.latexml.html')) {
                fs.unlinkSync(fullPath);
                if (options.verbose) {
                    console.log(`  Removed: ${fullPath}`);
                }
            }
        }
    }
    
    cleanDir(EXPECTED_DIR);
    console.log();
}

// ============================================================================
// Main
// ============================================================================

function main() {
    console.log('LaTeX Reference Generator');
    console.log('=========================');
    console.log(`Test Dir: ${options.testDir}`);
    console.log(`Fixtures: ${FIXTURES_DIR}`);
    console.log(`Expected: ${EXPECTED_DIR}`);
    console.log(`Output:   ${options.outputFormat}`);
    console.log(`Options:  force=${options.force}, verbose=${options.verbose}, test=${options.test || 'all'}`);
    console.log();

    // Check dependencies based on output format
    if (options.outputFormat === 'dvi') {
        try {
            execSync('which latex', { encoding: 'utf-8', stdio: 'pipe' });
        } catch {
            console.warn('Warning: latex not found.');
            console.warn('         DVI generation will fail. Install with:');
            console.warn('         brew install --cask mactex');
            console.warn();
        }
    } else {
        if (!fs.existsSync(LATEXJS_PATH)) {
            console.warn(`Warning: latex.js not found at ${LATEXJS_PATH}`);
            console.warn('         latexjs/ fixtures will fail. Build with:');
            console.warn('         cd latex-js && npm install --legacy-peer-deps && npm run build');
            console.warn();
        }
    }

    // Clean if requested
    if (options.clean) {
        cleanOutputFiles();
    }

    // Find all .tex files
    const texFiles = findTexFiles(FIXTURES_DIR);
    console.log(`Found ${texFiles.length} .tex files`);
    console.log();

    // Process files
    const stats = {
        processed: 0,
        success: 0,
        skipped: 0,
        failed: 0,
        errors: []
    };

    console.log('Processing fixtures...');
    for (const texFile of texFiles.sort((a, b) => a.relPath.localeCompare(b.relPath))) {
        processFixture(texFile, stats);
    }

    if (!options.verbose && !options.dryRun) {
        console.log(); // newline after dots
    }

    // Summary
    console.log();
    console.log('Summary');
    console.log('-------');
    console.log(`Success: ${stats.success}`);
    console.log(`Skipped: ${stats.skipped}`);
    console.log(`Failed:  ${stats.failed}`);

    if (stats.errors.length > 0) {
        console.log();
        console.log('Failed files:');
        for (const err of stats.errors) {
            console.log(`  ${err.relPath}: ${err.error}`);
        }
    }

    process.exit(stats.failed > 0 ? 1 : 0);
}

main();
