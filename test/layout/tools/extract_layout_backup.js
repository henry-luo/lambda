#!/usr/bin/env node

/**
 * Layout Extractor for Radiant Testing
 * 
 * Uses Puppeteer to extract comprehensive layout data from HTML/CSS test cases
 * in a headless browser for validation against Radiant's layout engine.
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
        console.log('🚀 Initializing Puppeteer...');
        
        this.browser = await puppeteer.launch({
            headless: 'new',
            args: [
                '--no-sandbox',
                '--disable-setuid-sandbox',
                '--disable-web-security',
                '--font-render-hinting=none',
                '--disable-features=TranslateUI',
                '--disable-dev-shm-usage'
            ]
        });
        
        this.page = await this.browser.newPage();
        
        // Set consistent viewport for reproducible results
        await this.page.setViewport({ 
            width: 1200, 
            height: 800, 
            deviceScaleFactor: 1 
        });
        
        // Inject CSS to disable animations and ensure consistent rendering
        await this.page.evaluateOnNewDocument(() => {
            const style = document.createElement('style');
            style.textContent = `
                *, *::before, *::after {
                    animation-duration: 0s !important;
                    animation-delay: 0s !important;
                    transition-duration: 0s !important;
                    transition-delay: 0s !important;
                    animation-fill-mode: none !important;
                    transition-timing-function: linear !important;
                }
            `;
            document.head.appendChild(style);
        });
        
        console.log('✅ Puppeteer initialized');
    }

    async close() {
        if (this.browser) {
            await this.browser.close();
            console.log('🔒 Browser closed');
        }
    }

    /**
     * Extract comprehensive layout data from HTML file
     */
    async extractCompleteLayout(htmlFile) {
        const htmlContent = await fs.readFile(htmlFile, 'utf8');
        await this.page.setContent(htmlContent, { 
            waitUntil: 'networkidle0',
            timeout: 10000
        });

        // Wait for fonts and rendering to stabilize
        await this.page.evaluate(() => document.fonts.ready);
        await this.page.waitForTimeout(100); // Small delay for stability

        // Extract comprehensive layout data
        const layoutData = await this.page.evaluate(() => {
            const extractElementData = (element) => {
                const rect = element.getBoundingClientRect();
                const computed = window.getComputedStyle(element);
                
                // Generate unique selector
                const selector = generateSelector(element);
                
                const data = {
                    selector,
                    tag: element.tagName.toLowerCase(),
                    id: element.id || null,
                    classes: element.className ? element.className.split(' ').filter(c => c.trim()) : [],
                    
                    // Layout properties (rounded to avoid floating point issues)
                    layout: {
                        x: Math.round(rect.left * 100) / 100,
                        y: Math.round(rect.top * 100) / 100,
                        width: Math.round(rect.width * 100) / 100,
                        height: Math.round(rect.height * 100) / 100,
                        
                        // Content box dimensions
                        contentWidth: element.clientWidth,
                        contentHeight: element.clientHeight,
                        
                        // Scroll dimensions
                        scrollWidth: element.scrollWidth,
                        scrollHeight: element.scrollHeight,
                        
                        // Offset properties
                        offsetTop: element.offsetTop,
                        offsetLeft: element.offsetLeft,
                        offsetWidth: element.offsetWidth,
                        offsetHeight: element.offsetHeight
                    },
                    
                    // Computed CSS properties
                    computed: {
                        display: computed.display,
                        position: computed.position,
                        
                        // Box model (parse to numbers)
                        marginTop: parseFloat(computed.marginTop) || 0,
                        marginRight: parseFloat(computed.marginRight) || 0,
                        marginBottom: parseFloat(computed.marginBottom) || 0,
                        marginLeft: parseFloat(computed.marginLeft) || 0,
                        
                        paddingTop: parseFloat(computed.paddingTop) || 0,
                        paddingRight: parseFloat(computed.paddingRight) || 0,
                        paddingBottom: parseFloat(computed.paddingBottom) || 0,
                        paddingLeft: parseFloat(computed.paddingLeft) || 0,
                        
                        borderTopWidth: parseFloat(computed.borderTopWidth) || 0,
                        borderRightWidth: parseFloat(computed.borderRightWidth) || 0,
                        borderBottomWidth: parseFloat(computed.borderBottomWidth) || 0,
                        borderLeftWidth: parseFloat(computed.borderLeftWidth) || 0,
                        
                        // Flexbox properties
                        flexDirection: computed.flexDirection,
                        flexWrap: computed.flexWrap,
                        justifyContent: computed.justifyContent,
                        alignItems: computed.alignItems,
                        alignContent: computed.alignContent,
                        alignSelf: computed.alignSelf,
                        flexGrow: parseFloat(computed.flexGrow) || 0,
                        flexShrink: parseFloat(computed.flexShrink) || 1,
                        flexBasis: computed.flexBasis,
                        order: parseInt(computed.order) || 0,
                        
                        // Typography
                        fontSize: parseFloat(computed.fontSize) || 16,
                        lineHeight: computed.lineHeight,
                        fontFamily: computed.fontFamily,
                        fontWeight: computed.fontWeight,
                        fontStyle: computed.fontStyle,
                        textAlign: computed.textAlign,
                        verticalAlign: computed.verticalAlign,
                        textDecoration: computed.textDecoration,
                        
                        // Sizing
                        width: computed.width,
                        height: computed.height,
                        minWidth: computed.minWidth,
                        maxWidth: computed.maxWidth,
                        minHeight: computed.minHeight,
                        maxHeight: computed.maxHeight,
                        
                        // Positioning
                        top: computed.top,
                        right: computed.right,
                        bottom: computed.bottom,
                        left: computed.left,
                        zIndex: computed.zIndex,
                        
                        // Overflow
                        overflow: computed.overflow,
                        overflowX: computed.overflowX,
                        overflowY: computed.overflowY,
                        
                        // Visibility
                        visibility: computed.visibility,
                        opacity: parseFloat(computed.opacity) || 1,
                        
                        // Background and border
                        backgroundColor: computed.backgroundColor,
                        borderStyle: computed.borderStyle,
                        borderColor: computed.borderColor
                    },
                    
                    // Text content information
                    textContent: element.textContent?.trim() || null,
                    hasTextNodes: Array.from(element.childNodes).some(n => 
                        n.nodeType === Node.TEXT_NODE && n.textContent.trim()
                    ),
                    
                    // Hierarchy information
                    childCount: element.children.length,
                    parentSelector: element.parentElement ? generateSelector(element.parentElement) : null
                };
                
                return data;
            };
            
            // Helper to generate CSS selector
            function generateSelector(element) {
                // Use ID if available
                if (element.id) {
                    return `#${element.id}`;
                }
                
                // Build selector from tag and classes
                let selector = element.tagName.toLowerCase();
                
                if (element.className) {
                    const classes = element.className.split(' ').filter(c => c.trim());
                    if (classes.length > 0) {
                        selector += '.' + classes.join('.');
                    }
                }
                
                // Add nth-child if there are siblings with same tag+class combo
                const parent = element.parentElement;
                if (parent) {
                    const siblings = Array.from(parent.children).filter(sibling => {
                        if (sibling.tagName !== element.tagName) return false;
                        if (sibling.className !== element.className) return false;
                        return true;
                    });
                    
                    if (siblings.length > 1) {
                        const index = siblings.indexOf(element) + 1;
                        selector += `:nth-child(${index})`;
                    }
                }
                
                return selector;
            }
            
            // Extract data for all elements
            const allElements = document.querySelectorAll('*');
            const layoutData = {};
            
            allElements.forEach((element) => {
                const data = extractElementData(element);
                layoutData[data.selector] = data;
            });
            
            // Add viewport information
            layoutData['__viewport__'] = {
                width: window.innerWidth,
                height: window.innerHeight,
                devicePixelRatio: window.devicePixelRatio,
                scrollX: window.scrollX,
                scrollY: window.scrollY
            };
            
            // Add test metadata if available
            const metadataElement = document.getElementById('test-metadata');
            if (metadataElement) {
                try {
                    layoutData['__metadata__'] = JSON.parse(metadataElement.textContent);
                } catch (e) {
                    layoutData['__metadata__'] = { 
                        error: 'Failed to parse metadata',
                        raw: metadataElement.textContent 
                    };
                }
            }
            
            // Add document information
            layoutData['__document__'] = {
                title: document.title,
                url: document.URL,
                documentElement: {
                    clientWidth: document.documentElement.clientWidth,
                    clientHeight: document.documentElement.clientHeight,
                    scrollWidth: document.documentElement.scrollWidth,
                    scrollHeight: document.documentElement.scrollHeight
                }
            };
            
            return layoutData;
        });

        return layoutData;
    }

    /**
     * Generate test reference JSON file
     */
    async generateReference(htmlFile, outputFile) {
        console.log(`  📄 Processing: ${path.basename(htmlFile)}`);
        
        try {
            const layoutData = await this.extractCompleteLayout(htmlFile);
            
            const reference = {
                test_file: path.basename(htmlFile),
                extraction_date: new Date().toISOString(),
                browser_info: {
                    userAgent: await this.page.evaluate(() => navigator.userAgent),
                    viewport: await this.page.viewport(),
                    version: await this.browser.version()
                },
                layout_data: layoutData,
                statistics: {
                    total_elements: Object.keys(layoutData).length - 3, // Exclude __viewport__, __metadata__, __document__
                    elements_with_ids: Object.values(layoutData).filter(el => el && el.id).length,
                    flex_containers: Object.values(layoutData).filter(el => 
                        el && el.computed && el.computed.display === 'flex'
                    ).length,
                    block_elements: Object.values(layoutData).filter(el => 
                        el && el.computed && el.computed.display === 'block'
                    ).length
                }
            };
            
            await fs.writeFile(outputFile, JSON.stringify(reference, null, 2));
            console.log(`  ✅ Generated: ${path.basename(outputFile)}`);
            
            return reference;
            
        } catch (error) {
            console.error(`  ❌ Error processing ${path.basename(htmlFile)}:`, error.message);
            throw error;
        }
    }

    /**
     * Process all tests in a category
     */
    async processCategory(category) {
        const dataDir = `./data/${category}`;
        const referenceDir = `./reference/${category}`;
        
        // Ensure reference directory exists
        await fs.mkdir(referenceDir, { recursive: true });
        
        // Find all HTML files
        const files = await fs.readdir(dataDir);
        const htmlFiles = files.filter(f => f.endsWith('.html'));
        
        console.log(`📂 Processing ${htmlFiles.length} ${category} tests...`);
        
        const results = [];
        let successCount = 0;
        let errorCount = 0;
        
        for (const htmlFile of htmlFiles) {
            const htmlPath = path.join(dataDir, htmlFile);
            const baseName = path.basename(htmlFile, '.html');
            const referenceFile = path.join(referenceDir, `${baseName}.json`);
            
            try {
                const reference = await this.generateReference(htmlPath, referenceFile);
                
                results.push({
                    test: baseName,
                    status: 'success',
                    elements: reference.statistics.total_elements,
                    reference_file: referenceFile
                });
                successCount++;
                
            } catch (error) {
                results.push({
                    test: baseName,
                    status: 'error',
                    error: error.message
                });
                errorCount++;
            }
        }
        
        console.log(`  ✅ ${successCount} successful, ❌ ${errorCount} errors`);
        return results;
    }

    /**
     * Process all test categories
     */
    async processAllTests() {
        console.log('🔄 Processing all test categories...');
        
        const categories = ['basic', 'intermediate', 'advanced'];
        const allResults = {};
        let totalSuccess = 0;
        let totalErrors = 0;
        
        for (const category of categories) {
            try {
                const results = await this.processCategory(category);
                allResults[category] = results;
                
                const success = results.filter(r => r.status === 'success').length;
                const errors = results.filter(r => r.status === 'error').length;
                totalSuccess += success;
                totalErrors += errors;
                
            } catch (error) {
                console.error(`❌ Failed to process category ${category}:`, error.message);
                allResults[category] = { error: error.message };
            }
        }
        
        // Generate summary report
        const summary = {
            extraction_date: new Date().toISOString(),
            categories: allResults,
            totals: {
                successful: totalSuccess,
                errors: totalErrors,
                total: totalSuccess + totalErrors
            },
            browser_info: {
                version: await this.browser.version(),
                userAgent: await this.page.evaluate(() => navigator.userAgent)
            }
        };
        
        await fs.writeFile('./reports/extraction_summary.json', JSON.stringify(summary, null, 2));
        
        console.log('\n📊 Extraction Summary:');
        console.log(`   ✅ ${totalSuccess} tests processed successfully`);
        console.log(`   ❌ ${totalErrors} tests failed`);
        console.log(`   📁 Results saved to ./reports/extraction_summary.json`);
        
        return summary;
    }
}

// CLI interface
async function main() {
    const args = process.argv.slice(2);
    const command = args[0] || 'all';
    
    const extractor = new LayoutExtractor();
    
    try {
        await extractor.initialize();
        
        switch (command) {
            case 'all':
                await extractor.processAllTests();
                break;
                
            case 'category':
                const category = args[1];
                if (!category) {
                    console.error('Usage: node extract_layout.js category <basic|intermediate|advanced>');
                    process.exit(1);
                }
                await extractor.processCategory(category);
                break;
                
            case 'single':
                const htmlFile = args[1];
                const outputFile = args[2];
                if (!htmlFile || !outputFile) {
                    console.error('Usage: node extract_layout.js single <html_file> <output_file>');
                    process.exit(1);
                }
                await extractor.generateReference(htmlFile, outputFile);
                break;
                
            default:
                console.log(`
Usage: node extract_layout.js <command> [options]

Commands:
  all                           Process all test categories
  category <name>              Process specific category (basic|intermediate|advanced)
  single <html_file> <output>  Process single HTML file

Examples:
  node extract_layout.js all
  node extract_layout.js category basic
  node extract_layout.js single test.html reference.json
`);
        }
        
    } finally {
        await extractor.close();
    }
}

if (require.main === module) {
    main().catch(console.error);
}

module.exports = LayoutExtractor;
