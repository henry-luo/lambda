/**
 * Enhanced Browser Layout Extraction Tool
 * 
 * Extracts comprehensive layout information from HTML test files including:
 * - Element positions, dimensions, and CSS properties
 * - Text node positions using Range.getClientRects() for line fragments
 * - Complete box model data (margins, padding, borders)
 * - Flexbox and typography properties
 * - Viewport and metadata information
 */

const puppeteer = require('puppeteer');
const fs = require('fs').promises;
const path = require('path');
const { glob } = require('glob');

class LayoutExtractor {
    constructor() {
        this.browser = null;
        this.page = null;
    }
    
    async initialize() {
        console.log('Launching browser...');
        this.browser = await puppeteer.launch({
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
        this.page = await this.browser.newPage();
        
        // Set consistent viewport and disable animations
        await this.page.setViewport({ width: 1200, height: 800, deviceScaleFactor: 1 });
        await this.page.evaluateOnNewDocument(() => {
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
    }
    
    async extractCompleteLayout(htmlFile) {
        const htmlContent = await fs.readFile(htmlFile, 'utf8');
        await this.page.setContent(htmlContent, { waitUntil: 'networkidle0' });
        
        // Wait for fonts and rendering to stabilize
        await this.page.evaluate(() => document.fonts.ready);
        await new Promise(resolve => setTimeout(resolve, 100)); // Small delay for stability
        
        // Extract comprehensive layout data
        const layoutData = await this.page.evaluate(() => {
            const extractElementData = (element, path = '') => {
                const rect = element.getBoundingClientRect();
                const computed = window.getComputedStyle(element);
                
                // Generate unique selector path
                const selector = generateSelector(element, path);
                
                const data = {
                    selector,
                    tag: element.tagName.toLowerCase(),
                    id: element.id || null,
                    classes: element.className ? element.className.split(' ').filter(c => c) : [],
                    
                    // Layout properties
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
                    
                    // Computed CSS properties
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
                    
                    // Text node positions
                    textNodes: extractTextNodePositions(element),
                    
                    // Hierarchy information
                    childCount: element.children.length,
                    depth: (path.match(/>/g) || []).length
                };
                
                return data;
            };
            
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
            
            // Helper to generate CSS selector
            const generateSelector = (element, basePath) => {
                if (element.id) return `#${element.id}`;
                
                let selector = element.tagName.toLowerCase();
                if (element.className) {
                    selector += '.' + element.className.split(' ').filter(c => c).join('.');
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
                
                return basePath ? `${basePath} > ${selector}` : selector;
            };
            
            // Extract data for all elements
            const allElements = document.querySelectorAll('*');
            const layoutData = {};
            
            allElements.forEach((element, index) => {
                const data = extractElementData(element);
                layoutData[data.selector] = data;
            });
            
            // Add viewport information
            layoutData['__viewport__'] = {
                width: window.innerWidth,
                height: window.innerHeight,
                devicePixelRatio: window.devicePixelRatio
            };
            
            // Add test metadata if available
            const metadataElement = document.getElementById('test-metadata');
            if (metadataElement) {
                try {
                    layoutData['__metadata__'] = JSON.parse(metadataElement.textContent);
                } catch (e) {
                    layoutData['__metadata__'] = { error: 'Failed to parse metadata' };
                }
            }
            
            return layoutData;
        });
        
        return layoutData;
    }
    
    // Generate test reference JSON
    async generateReference(htmlFile, outputFile) {
        const layoutData = await this.extractCompleteLayout(htmlFile);
        
        const reference = {
            test_file: path.basename(htmlFile),
            extraction_date: new Date().toISOString(),
            browser_info: {
                userAgent: await this.page.evaluate(() => navigator.userAgent),
                viewport: await this.page.viewport()
            },
            layout_data: layoutData
        };
        
        await fs.writeFile(outputFile, JSON.stringify(reference, null, 2));
        return reference;
    }
    
    async close() {
        if (this.browser) {
            await this.browser.close();
        }
    }
}

// Main execution
async function main() {
    const extractor = new LayoutExtractor();
    
    try {
        await extractor.initialize();
        
        // Process all HTML files in data/basic directory
        const htmlFiles = await glob('../data/basic/*.html');
        
        console.log(`Found ${htmlFiles.length} test files to process`);
        
        for (const htmlFile of htmlFiles) {
            const baseName = path.basename(htmlFile, '.html');
            const outputFile = `../reference/basic/${baseName}.json`;
            
            console.log(`Processing: ${baseName}`);
            
            try {
                await extractor.generateReference(htmlFile, outputFile);
                console.log(`  ✅ Generated reference: ${outputFile}`);
            } catch (error) {
                console.error(`  ❌ Error processing ${baseName}:`, error.message);
            }
        }
        
    } catch (error) {
        console.error('Fatal error:', error);
    } finally {
        await extractor.close();
    }
}

// Run if called directly
if (require.main === module) {
    main().catch(console.error);
}

module.exports = { LayoutExtractor };
