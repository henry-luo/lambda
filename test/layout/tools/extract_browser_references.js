#!/usr/bin/env node

/**
 * Simple Browser Layout Extractor
 * Extracts layout data from a single HTML file using Puppeteer
 */

const puppeteer = require('puppeteer');
const fs = require('fs').promises;
const path = require('path');

async function extractLayoutFromFile(htmlFilePath) {
    console.log(`🔍 Extracting layout from: ${htmlFilePath}`);
    
    let browser = null;
    try {
        // Launch browser
        console.log('🚀 Launching browser...');
        browser = await puppeteer.launch({
            headless: 'new',
            args: [
                '--no-sandbox',
                '--disable-web-security',
                '--font-render-hinting=none',
                '--disable-dev-shm-usage',
                '--disable-gpu',
                '--disable-features=VizDisplayCompositor',
                '--disable-extensions',
                '--no-first-run',
                '--disable-default-apps'
            ],
            timeout: 30000
        });
        
        const page = await browser.newPage();
        
        // Set consistent viewport and disable animations (from extract_layout.js)
        await page.setViewport({ width: 1200, height: 800, deviceScaleFactor: 1 });
        await page.evaluateOnNewDocument(() => {
            // Disable animations for consistent layout
            const style = document.createElement('style');
            style.textContent = `
                *, *::before, *::after {
                    animation-duration: 0s !important;
                    animation-delay: 0s !important;
                    transition-duration: 0s !important;
                    transition-delay: 0s !important;
                }
            `;
            document.head.appendChild(style);
        });
        console.log('✅ Browser ready');
        
        // Load HTML file
        console.log('📄 Loading HTML file...');
        const htmlContent = await fs.readFile(htmlFilePath, 'utf8');
        await page.setContent(htmlContent, { waitUntil: 'networkidle0' });
        
        // Wait for fonts and layout to stabilize
        await page.evaluate(() => document.fonts.ready);
        await new Promise(resolve => setTimeout(resolve, 200));
        console.log('✅ HTML loaded and rendered');
        
        // Extract layout data
        console.log('📊 Extracting layout data...');
        const layoutData = await page.evaluate(() => {
            // Helper to extract text node positions
            const extractTextNodePositions = (element) => {
                const textNodes = [];
                
                // Walk through all child nodes to find text nodes
                const walker = document.createTreeWalker(
                    element,
                    NodeFilter.SHOW_TEXT,
                    {
                        acceptNode: function(node) {
                            // Only include text nodes with non-whitespace content
                            if (node.nodeType === Node.TEXT_NODE && node.textContent.trim()) {
                                return NodeFilter.FILTER_ACCEPT;
                            }
                            return NodeFilter.FILTER_REJECT;
                        }
                    }
                );
                
                let textNode;
                while (textNode = walker.nextNode()) {
                    try {
                        // Create range for this text node
                        const range = document.createRange();
                        range.selectNodeContents(textNode);
                        const rects = range.getClientRects(); // one per line fragment
                        
                        // Convert ClientRects to plain objects and round coordinates
                        const rectArray = Array.from(rects).map(rect => ({
                            x: Math.round(rect.left * 100) / 100,
                            y: Math.round(rect.top * 100) / 100,
                            width: Math.round(rect.width * 100) / 100,
                            height: Math.round(rect.height * 100) / 100,
                            right: Math.round(rect.right * 100) / 100,
                            bottom: Math.round(rect.bottom * 100) / 100
                        }));
                        
                        // Only add if we have valid rects
                        if (rectArray.length > 0) {
                            textNodes.push({
                                text: textNode.textContent,
                                parentElement: textNode.parentElement?.tagName.toLowerCase() || null,
                                rects: rectArray,
                                length: textNode.textContent.length,
                                // Additional metadata
                                isWhitespaceOnly: !textNode.textContent.trim(),
                                startOffset: 0, // Could be enhanced to track actual offset in parent
                                endOffset: textNode.textContent.length
                            });
                        }
                        
                        range.detach(); // Clean up range
                    } catch (error) {
                        // Skip text nodes that can't be measured (e.g., in hidden elements)
                        console.warn('Could not extract position for text node:', textNode.textContent, error);
                    }
                }
                
                return textNodes;
            };
            
            // Helper to generate enhanced CSS selector (from extract_layout.js)
            const generateSelector = (element) => {
                if (element.id) return `#${element.id}`;
                
                let selector = element.tagName.toLowerCase();
                if (element.className) {
                    selector += '.' + element.className.split(' ').filter(c => c.trim()).join('.');
                }
                
                // Add index if there are siblings with same tag
                const parent = element.parentElement;
                if (parent) {
                    const siblings = Array.from(parent.children).filter(s => s.tagName === element.tagName);
                    if (siblings.length > 1) {
                        const index = siblings.indexOf(element);
                        selector += `:nth-of-type(${index + 1})`;
                    }
                }
                
                return selector;
            };
            
            const elements = document.querySelectorAll('*');
            const results = {};
            
            elements.forEach((element, index) => {
                const rect = element.getBoundingClientRect();
                const computed = window.getComputedStyle(element);
                
                // Generate enhanced selector (from extract_layout.js)
                const selector = generateSelector(element);
                
                // Use selector as key, but fall back to indexed key if needed
                const key = selector || `${element.tagName.toLowerCase()}_${index}`;
                
                results[key] = {
                    selector: key,
                    tag: element.tagName.toLowerCase(),
                    id: element.id || null,
                    classes: element.className ? element.className.split(' ').filter(c => c.trim()) : [],
                    // Layout properties (enhanced from extract_layout.js)
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
                        scrollHeight: element.scrollHeight
                    },
                    
                    // Comprehensive CSS properties (enhanced from extract_layout.js)
                    computed: {
                        display: computed.display,
                        position: computed.position,
                        
                        // Box model
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
                        flexGrow: parseFloat(computed.flexGrow) || 0,
                        flexShrink: parseFloat(computed.flexShrink) || 1,
                        flexBasis: computed.flexBasis,
                        alignSelf: computed.alignSelf,
                        order: parseInt(computed.order) || 0,
                        gap: computed.gap,
                        
                        // Typography
                        fontSize: parseFloat(computed.fontSize) || 16,
                        lineHeight: computed.lineHeight,
                        fontFamily: computed.fontFamily,
                        fontWeight: computed.fontWeight,
                        textAlign: computed.textAlign,
                        verticalAlign: computed.verticalAlign,
                        
                        // Positioning
                        top: computed.top,
                        right: computed.right,
                        bottom: computed.bottom,
                        left: computed.left,
                        zIndex: computed.zIndex,
                        
                        // Overflow
                        overflow: computed.overflow,
                        overflowX: computed.overflowX,
                        overflowY: computed.overflowY
                    },
                    // Text content information
                    textContent: element.textContent?.trim() || null,
                    hasTextNodes: Array.from(element.childNodes).some(n => n.nodeType === 3 && n.textContent.trim()),
                    
                    // Text node positions using Range.getClientRects()
                    textNodes: extractTextNodePositions(element),
                    
                    // Hierarchy information (from extract_layout.js)
                    childCount: element.children.length,
                    depth: (selector.match(/>/g) || []).length
                };
            });
            
            // Add viewport information (enhanced from extract_layout.js)
            results['__viewport__'] = {
                width: window.innerWidth,
                height: window.innerHeight,
                devicePixelRatio: window.devicePixelRatio
            };
            
            // Add test metadata if available (from extract_layout.js)
            const metadataElement = document.getElementById('test-metadata');
            if (metadataElement) {
                try {
                    results['__metadata__'] = JSON.parse(metadataElement.textContent);
                } catch (e) {
                    results['__metadata__'] = { error: 'Failed to parse metadata' };
                }
            }
            
            return {
                viewport: {
                    width: window.innerWidth,
                    height: window.innerHeight,
                    devicePixelRatio: window.devicePixelRatio
                },
                elements: results
            };
        });
        
        console.log('✅ Layout data extracted');
        console.log(`📈 Found ${Object.keys(layoutData.elements).length} elements`);
        
        // Count text nodes for reporting
        const totalTextNodes = Object.values(layoutData.elements)
            .reduce((sum, elem) => sum + (elem.textNodes ? elem.textNodes.length : 0), 0);
        console.log(`📝 Extracted ${totalTextNodes} text nodes with position data`);
        
        // Create enhanced reference JSON (from extract_layout.js)
        const reference = {
            test_file: path.basename(htmlFilePath),
            extraction_date: new Date().toISOString(),
            browser_info: {
                userAgent: await page.evaluate(() => navigator.userAgent),
                viewport: await page.viewport()
            },
            layout_data: layoutData.elements  // Use elements from the enhanced structure
        };
        
        // Save to reference directory
        const baseName = path.basename(htmlFilePath, '.html');
        const category = path.basename(path.dirname(htmlFilePath));
        const outputDir = path.join(__dirname, '..', 'reference', category);
        const outputFile = path.join(outputDir, `${baseName}.json`);
        
        // Ensure output directory exists
        await fs.mkdir(outputDir, { recursive: true });
        
        // Write JSON file
        await fs.writeFile(outputFile, JSON.stringify(reference, null, 2));
        
        console.log(`💾 Reference saved to: ${outputFile}`);
        
        // Show sample of extracted data
        console.log('\n📋 Sample extracted data:');
        const sampleElements = Object.entries(layoutData.elements).slice(0, 3);
        sampleElements.forEach(([key, data]) => {
            console.log(`  ${key}: ${data.layout.width}x${data.layout.height} at (${data.layout.x}, ${data.layout.y})`);
        });
        
        return reference;
        
    } catch (error) {
        console.error('❌ Error during extraction:', error.message);
        throw error;
    } finally {
        if (browser) {
            await browser.close();
        }
    }
}

async function extractAllTestFiles(category = null) {
    console.log('🔍 Scanning for test HTML files...');
    
    const dataDir = path.join(__dirname, '..', 'data');
    const categories = category ? [category] : ['basic', 'intermediate', 'advanced'];
    
    let allFiles = [];
    let totalFiles = 0;
    
    for (const cat of categories) {
        const categoryDir = path.join(dataDir, cat);
        try {
            const files = await fs.readdir(categoryDir);
            const htmlFiles = files
                .filter(file => file.endsWith('.html'))
                .map(file => ({
                    category: cat,
                    file: file,
                    path: path.join(categoryDir, file)
                }));
            
            allFiles = allFiles.concat(htmlFiles);
            totalFiles += htmlFiles.length;
            console.log(`📁 Found ${htmlFiles.length} HTML files in ${cat}/`);
        } catch (error) {
            console.log(`⚠️  Category ${cat}/ not found or empty`);
        }
    }
    
    if (totalFiles === 0) {
        console.log('❌ No HTML test files found');
        return;
    }
    
    console.log(`\n🎯 Processing ${totalFiles} test files...`);
    console.log('==========================================');
    
    let successCount = 0;
    let failCount = 0;
    const results = [];
    
    for (const fileInfo of allFiles) {
        console.log(`\n📄 Processing: ${fileInfo.category}/${fileInfo.file}`);
        
        try {
            const result = await extractLayoutFromFile(fileInfo.path);
            results.push({
                ...fileInfo,
                success: true,
                elementCount: Object.keys(result.layout_data).length,
                result: result
            });
            successCount++;
            console.log(`✅ Success: ${Object.keys(result.layout_data).length} elements extracted`);
        } catch (error) {
            results.push({
                ...fileInfo,
                success: false,
                error: error.message
            });
            failCount++;
            console.log(`❌ Failed: ${error.message}`);
        }
    }
    
    // Generate summary
    console.log('\n📊 Extraction Summary');
    console.log('=====================');
    console.log(`Total files processed: ${totalFiles}`);
    console.log(`✅ Successful: ${successCount}`);
    console.log(`❌ Failed: ${failCount}`);
    console.log(`📈 Success rate: ${Math.round(successCount / totalFiles * 100)}%`);
    
    // Show detailed results
    console.log('\n📋 Detailed Results:');
    results.forEach(result => {
        const status = result.success ? '✅' : '❌';
        const details = result.success 
            ? `${result.elementCount} elements`
            : `Error: ${result.error}`;
        console.log(`  ${status} ${result.category}/${result.file} - ${details}`);
    });
    
    // Save summary to file
    const summaryFile = path.join(__dirname, '..', 'reports', 'extraction_summary.json');
    await fs.mkdir(path.dirname(summaryFile), { recursive: true });
    
    const summary = {
        extraction_date: new Date().toISOString(),
        total_files: totalFiles,
        successful: successCount,
        failed: failCount,
        success_rate: Math.round(successCount / totalFiles * 100),
        results: results.map(r => ({
            category: r.category,
            file: r.file,
            success: r.success,
            element_count: r.elementCount || 0,
            error: r.error || null
        }))
    };
    
    await fs.writeFile(summaryFile, JSON.stringify(summary, null, 2));
    console.log(`\n💾 Summary saved to: ${summaryFile}`);
    
    return results;
}

// Main execution
async function main() {
    const args = process.argv.slice(2);
    
    console.log('🎯 Radiant Layout Browser Reference Extractor');
    console.log('==============================================');
    
    // Parse arguments
    let singleFile = null;
    let category = null;
    let showHelp = false;
    
    for (let i = 0; i < args.length; i++) {
        const arg = args[i];
        if (arg === '--help' || arg === '-h') {
            showHelp = true;
        } else if (arg === '--category' || arg === '-c') {
            category = args[++i];
            if (!['basic', 'intermediate', 'advanced'].includes(category)) {
                console.error(`❌ Invalid category: ${category}. Must be basic, intermediate, or advanced`);
                process.exit(1);
            }
        } else if (arg.endsWith('.html')) {
            singleFile = arg;
        } else {
            console.error(`❌ Unknown argument: ${arg}`);
            showHelp = true;
        }
    }
    
    if (showHelp) {
        console.log(`
Usage: node simple_extract.js [options] [html_file]

Options:
  --category, -c <name>   Extract only from specific category (basic|intermediate|advanced)
  --help, -h              Show this help message

Examples:
  node simple_extract.js                                    # Extract all test files
  node simple_extract.js --category basic                   # Extract only basic tests
  node simple_extract.js ../data/basic/flex_001.html        # Extract single file

Generated files:
  ../reference/<category>/<test_name>.json                  # Individual reference files
  ../reports/extraction_summary.json                        # Summary report
`);
        process.exit(0);
    }
    
    try {
        if (singleFile) {
            // Single file mode - resolve path correctly
            let resolvedPath = singleFile;
            
            // If path starts with 'test/', it's relative to project root, so adjust for current directory
            if (singleFile.startsWith('test/')) {
                resolvedPath = path.join(__dirname, '..', '..', '..', singleFile);
            }
            // If path starts with '../', it's already relative to tools directory
            else if (!path.isAbsolute(singleFile)) {
                resolvedPath = path.resolve(singleFile);
            }
            
            console.log(`📍 Resolved path: ${resolvedPath}`);
            await fs.access(resolvedPath);
            const result = await extractLayoutFromFile(resolvedPath);
            console.log(`\n🎉 Extraction completed successfully!`);
            console.log(`✅ Reference JSON created with ${Object.keys(result.layout_data).length} elements`);
        } else {
            // Batch mode
            await extractAllTestFiles(category);
            console.log('\n🎉 Batch extraction completed!');
        }
        
    } catch (error) {
        if (error.code === 'ENOENT') {
            console.error(`❌ File not found: ${singleFile}`);
        } else {
            console.error('❌ Extraction failed:', error.message);
        }
        process.exit(1);
    }
}

// Run if called directly
if (require.main === module) {
    main().catch(console.error);
}

module.exports = { extractLayoutFromFile, extractAllTestFiles };
