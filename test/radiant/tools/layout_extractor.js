#!/usr/bin/env node

/**
 * Layout Extractor - Generate Radiant test data from browser layout
 * 
 * This tool uses Puppeteer to load HTML/CSS content in a headless browser,
 * extract computed layout properties, and generate JSON test descriptors
 * for the Radiant layout testing framework.
 */

const puppeteer = require('puppeteer');
const fs = require('fs').promises;
const path = require('path');

class LayoutExtractor {
    constructor() {
        this.browser = null;
        this.page = null;
    }

    async initialize() {
        this.browser = await puppeteer.launch({
            headless: true,
            args: ['--no-sandbox', '--disable-setuid-sandbox']
        });
        this.page = await this.browser.newPage();
        
        // Set a consistent viewport for reproducible results
        await this.page.setViewport({ width: 1200, height: 800 });
    }

    async close() {
        if (this.browser) {
            await this.browser.close();
        }
    }

    /**
     * Extract layout data from HTML/CSS content
     */
    async extractLayout(html, css, testConfig = {}) {
        const fullHtml = this.buildFullHtml(html, css);
        
        await this.page.setContent(fullHtml, { 
            waitUntil: 'networkidle0' 
        });

        // Wait for fonts and any async rendering to complete
        await this.page.evaluate(() => {
            return document.fonts.ready;
        });

        // Extract layout data using browser APIs
        const layoutData = await this.page.evaluate((config) => {
            const results = {};
            
            // Helper function to get computed layout for an element
            function getElementLayout(element, selector) {
                const rect = element.getBoundingClientRect();
                const computed = window.getComputedStyle(element);
                
                return {
                    selector: selector,
                    x: Math.round(rect.left),
                    y: Math.round(rect.top),
                    width: Math.round(rect.width),
                    height: Math.round(rect.height),
                    // Additional computed properties
                    computed_style: {
                        display: computed.display,
                        position: computed.position,
                        flex_direction: computed.flexDirection,
                        justify_content: computed.justifyContent,
                        align_items: computed.alignItems,
                        flex_grow: computed.flexGrow,
                        flex_shrink: computed.flexShrink,
                        flex_basis: computed.flexBasis,
                        margin: {
                            top: parseInt(computed.marginTop) || 0,
                            right: parseInt(computed.marginRight) || 0,
                            bottom: parseInt(computed.marginBottom) || 0,
                            left: parseInt(computed.marginLeft) || 0
                        },
                        padding: {
                            top: parseInt(computed.paddingTop) || 0,
                            right: parseInt(computed.paddingRight) || 0,
                            bottom: parseInt(computed.paddingBottom) || 0,
                            left: parseInt(computed.paddingLeft) || 0
                        },
                        border: {
                            top: parseInt(computed.borderTopWidth) || 0,
                            right: parseInt(computed.borderRightWidth) || 0,
                            bottom: parseInt(computed.borderBottomWidth) || 0,
                            left: parseInt(computed.borderLeftWidth) || 0
                        }
                    }
                };
            }

            // Extract layout for all elements with meaningful selectors
            const elements = document.querySelectorAll('*');
            elements.forEach((element, index) => {
                let selector = element.tagName.toLowerCase();
                
                // Build a meaningful selector
                if (element.id) {
                    selector = `#${element.id}`;
                } else if (element.className) {
                    selector = `.${element.className.split(' ')[0]}`;
                } else if (element.parentElement) {
                    const siblings = Array.from(element.parentElement.children);
                    const elementIndex = siblings.indexOf(element);
                    if (siblings.length > 1) {
                        selector = `${selector}[${elementIndex}]`;
                    }
                }

                // Skip elements that are not visible or meaningful
                const rect = element.getBoundingClientRect();
                if (rect.width > 0 || rect.height > 0) {
                    results[selector] = getElementLayout(element, selector);
                }
            });

            return results;
        }, testConfig);

        return layoutData;
    }

    /**
     * Generate test descriptor JSON from HTML/CSS and extracted layout
     */
    async generateTestDescriptor(testId, html, css, options = {}) {
        const layoutData = await this.extractLayout(html, css, options);
        
        // Detect the primary layout type
        const layoutType = this.detectLayoutType(html, css);
        
        const descriptor = {
            test_id: testId,
            category: layoutType,
            spec_reference: options.specReference || "",
            description: options.description || `${layoutType} layout test`,
            html: html.trim(),
            css: css.trim(),
            expected_layout: layoutData,
            properties_to_test: options.propertiesToTest || ["position", "dimensions"],
            browser_engine: "chromium",
            browser_version: await this.getBrowserVersion(),
            extraction_date: new Date().toISOString(),
            tolerance_px: options.tolerancePx || 1.0
        };

        return descriptor;
    }

    /**
     * Build complete HTML document with CSS
     */
    buildFullHtml(html, css) {
        return `
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <style>
        /* Reset some default styles for consistency */
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: Arial, sans-serif; font-size: 16px; line-height: 1.4; }
        
        /* User CSS */
        ${css}
    </style>
</head>
<body>
    ${html}
</body>
</html>`;
    }

    /**
     * Detect the primary layout type from CSS
     */
    detectLayoutType(html, css) {
        if (css.includes('display: flex') || css.includes('display:flex')) {
            return 'flexbox';
        }
        if (css.includes('display: grid') || css.includes('display:grid')) {
            return 'grid';
        }
        if (css.includes('position: absolute') || css.includes('position: fixed')) {
            return 'positioning';
        }
        if (css.includes('float:') || css.includes('float ')) {
            return 'float';
        }
        return 'block';
    }

    /**
     * Get browser version for test metadata
     */
    async getBrowserVersion() {
        const version = await this.browser.version();
        return version;
    }

    /**
     * Process a directory of HTML/CSS test files
     */
    async processTestsFromDirectory(inputDir, outputDir) {
        const testFiles = await fs.readdir(inputDir);
        const results = [];

        for (const file of testFiles) {
            if (!file.endsWith('.html')) continue;

            const htmlPath = path.join(inputDir, file);
            const cssPath = path.join(inputDir, file.replace('.html', '.css'));
            
            try {
                const html = await fs.readFile(htmlPath, 'utf8');
                let css = '';
                
                try {
                    css = await fs.readFile(cssPath, 'utf8');
                } catch (e) {
                    // CSS file might be embedded in HTML or not exist
                    console.warn(`No CSS file found for ${file}, extracting from HTML`);
                    css = this.extractCssFromHtml(html);
                }

                const testId = path.basename(file, '.html');
                const descriptor = await this.generateTestDescriptor(testId, html, css);
                
                const outputPath = path.join(outputDir, `${testId}.json`);
                await fs.writeFile(outputPath, JSON.stringify(descriptor, null, 2));
                
                results.push({ testId, status: 'success', outputPath });
                console.log(`✓ Generated test: ${testId}`);
                
            } catch (error) {
                results.push({ testId: file, status: 'error', error: error.message });
                console.error(`✗ Failed to process ${file}:`, error.message);
            }
        }

        return results;
    }

    /**
     * Extract CSS from HTML style tags
     */
    extractCssFromHtml(html) {
        const styleMatch = html.match(/<style[^>]*>([\s\S]*?)<\/style>/i);
        return styleMatch ? styleMatch[1] : '';
    }
}

/**
 * CLI Interface
 */
async function main() {
    const args = process.argv.slice(2);
    
    if (args.length < 2) {
        console.log(`
Usage: node layout_extractor.js <command> <options>

Commands:
  extract-single <html_file> <css_file> [output_file]
    Extract layout from single HTML/CSS pair

  extract-batch <input_dir> <output_dir>
    Process all HTML files in input directory

  extract-inline <html_string> <css_string> [test_id]
    Extract layout from inline HTML/CSS strings

Examples:
  node layout_extractor.js extract-single test.html test.css test.json
  node layout_extractor.js extract-batch ./test_cases ./output
  node layout_extractor.js extract-inline "<div class='flex'>...</div>" ".flex { display: flex; }"
`);
        process.exit(1);
    }

    const extractor = new LayoutExtractor();
    await extractor.initialize();

    try {
        const command = args[0];

        switch (command) {
            case 'extract-single':
                await extractSingle(extractor, args.slice(1));
                break;
            
            case 'extract-batch':
                await extractBatch(extractor, args.slice(1));
                break;
            
            case 'extract-inline':
                await extractInline(extractor, args.slice(1));
                break;
            
            default:
                console.error(`Unknown command: ${command}`);
                process.exit(1);
        }
    } finally {
        await extractor.close();
    }
}

async function extractSingle(extractor, args) {
    const [htmlFile, cssFile, outputFile] = args;
    
    const html = await fs.readFile(htmlFile, 'utf8');
    const css = await fs.readFile(cssFile, 'utf8');
    const testId = path.basename(htmlFile, '.html');
    
    const descriptor = await extractor.generateTestDescriptor(testId, html, css);
    
    const output = outputFile || `${testId}.json`;
    await fs.writeFile(output, JSON.stringify(descriptor, null, 2));
    
    console.log(`✓ Generated test descriptor: ${output}`);
}

async function extractBatch(extractor, args) {
    const [inputDir, outputDir] = args;
    
    // Ensure output directory exists
    await fs.mkdir(outputDir, { recursive: true });
    
    const results = await extractor.processTestsFromDirectory(inputDir, outputDir);
    
    console.log(`\nProcessed ${results.length} files:`);
    console.log(`✓ Success: ${results.filter(r => r.status === 'success').length}`);
    console.log(`✗ Errors: ${results.filter(r => r.status === 'error').length}`);
}

async function extractInline(extractor, args) {
    const [html, css, testId = 'inline_test'] = args;
    
    const descriptor = await extractor.generateTestDescriptor(testId, html, css);
    
    console.log(JSON.stringify(descriptor, null, 2));
}

// Run if called directly
if (require.main === module) {
    main().catch(console.error);
}

module.exports = LayoutExtractor;
