#!/usr/bin/env node

/**
 * Simple Browser Layout Extractor
 * Extracts layout data from a single HTML file using Puppeteer
 */

const puppeteer = require('puppeteer');
const fs = require('fs').promises;
const path = require('path');

async function extractLayoutFromFile(htmlFilePath, forceRegenerate = false) {
    console.log(`🔍 Checking layout extraction for: ${htmlFilePath}`);

    // Determine output file path first
    const baseName = path.basename(htmlFilePath, '.html');
    const category = path.basename(path.dirname(htmlFilePath));
    const outputDir = path.join(__dirname, '..', 'reference', category);
    const outputFile = path.join(outputDir, `${baseName}.json`);

    // Check if output file already exists (unless force regeneration is requested)
    if (!forceRegenerate) {
        try {
            await fs.access(outputFile);
            console.log(`⏭️  Reference file already exists, skipping: ${outputFile}`);
            console.log(`   Use --force flag to regenerate existing files`);

            // Read existing file to return consistent data structure
            const existingContent = await fs.readFile(outputFile, 'utf8');
            const existingData = JSON.parse(existingContent);
            existingData._wasSkipped = true; // Flag to indicate this was skipped
            return existingData;
        } catch (error) {
            // File doesn't exist, proceed with generation
            console.log(`📝 Generating new reference file: ${outputFile}`);
        }
    } else {
        console.log(`🔄 Force regenerating reference file: ${outputFile}`);
    }

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

                // FIXED: Only walk through DIRECT child nodes to avoid duplication
                // Previously used TreeWalker which traversed ALL descendants, causing duplication
                for (let i = 0; i < element.childNodes.length; i++) {
                    const node = element.childNodes[i];

                    // Only process direct text node children
                    if (node.nodeType === Node.TEXT_NODE && node.textContent.trim()) {
                        try {
                            // Create range for this text node
                            const range = document.createRange();
                            range.selectNodeContents(node);
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
                                    text: node.textContent,
                                    parentElement: node.parentElement?.tagName.toLowerCase() || null,
                                    rects: rectArray,
                                    length: node.textContent.length,
                                    // Additional metadata
                                    isWhitespaceOnly: !node.textContent.trim(),
                                    startOffset: 0, // Could be enhanced to track actual offset in parent
                                    endOffset: node.textContent.length
                                });
                            }

                            range.detach(); // Clean up range
                        } catch (error) {
                            // Skip text nodes that can't be measured (e.g., in hidden elements)
                            console.warn('Could not extract position for text node:', node.textContent, error);
                        }
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

            // Helper function to extract element data (same as before but reusable)
            const extractElementData = (element, index) => {
                const rect = element.getBoundingClientRect();
                const computed = window.getComputedStyle(element);

                // Generate enhanced selector
                const selector = generateSelector(element);
                const key = selector || `${element.tagName.toLowerCase()}_${index}`;

                return {
                    selector: key,
                    tag: element.tagName.toLowerCase(),
                    id: element.id || null,
                    classes: element.className ? element.className.split(' ').filter(c => c.trim()) : [],
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

                    // Comprehensive CSS properties
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

                    // Hierarchy information
                    childCount: element.children.length,
                    depth: 0  // Will be calculated during tree building
                };
            };

            // Recursive function to build element tree
            const buildElementTree = (element, depth = 0, index = 0) => {
                const elementData = extractElementData(element, index);
                elementData.depth = depth;

                // Add children recursively
                elementData.children = [];
                if (element.children && element.children.length > 0) {
                    for (let i = 0; i < element.children.length; i++) {
                        const childElement = element.children[i];
                        const childTree = buildElementTree(childElement, depth + 1, i);
                        elementData.children.push(childTree);
                    }
                }

                return elementData;
            };

            // Start with the root HTML element to build the tree
            const htmlElement = document.documentElement;
            const elementTree = buildElementTree(htmlElement, 0, 0);

            // Add viewport information to the tree structure
            elementTree.viewport = {
                width: window.innerWidth,
                height: window.innerHeight
            };

            // Add test metadata if available
            const metadataElement = document.getElementById('test-metadata');
            if (metadataElement) {
                try {
                    elementTree.metadata = JSON.parse(metadataElement.textContent);
                } catch (e) {
                    elementTree.metadata = { error: 'Failed to parse metadata' };
                }
            }

            return elementTree;
        });

        // Helper function to count elements in tree
        const countElements = (node) => {
            let count = 1;
            if (node.children && node.children.length > 0) {
                node.children.forEach(child => {
                    count += countElements(child);
                });
            }
            return count;
        };

        // Helper function to count text nodes in tree
        const countTextNodes = (node) => {
            let count = node.textNodes ? node.textNodes.length : 0;
            if (node.children && node.children.length > 0) {
                node.children.forEach(child => {
                    count += countTextNodes(child);
                });
            }
            return count;
        };

        // Get browser info for summary reporting
        const devicePixelRatio = await page.evaluate(() => window.devicePixelRatio);
        const userAgent = await page.evaluate(() => navigator.userAgent);
        const viewport = await page.viewport();

        console.log('✅ Layout data extracted');
        console.log(`📈 Found ${countElements(layoutData)} elements in tree structure`);
        console.log(`📝 Extracted ${countTextNodes(layoutData)} text nodes with position data`);

        // Create enhanced reference JSON with tree structure (minimal browser info)
        const reference = {
            test_file: path.basename(htmlFilePath),
            browser_info: {
                viewport: {
                    width: viewport.width,
                    height: viewport.height
                }
            },
            layout_tree: layoutData,  // NEW: Tree structure starting from <html>
            _devicePixelRatio: devicePixelRatio,  // Internal use for summary only
            _userAgent: userAgent,  // Internal use for summary only
            _fullViewport: viewport  // Internal use for summary only
        };

        // Save to reference directory (outputFile path already determined)
        // Ensure output directory exists
        await fs.mkdir(outputDir, { recursive: true });

        // Clean up internal fields before saving
        const cleanedReference = { ...reference };
        delete cleanedReference._devicePixelRatio;
        delete cleanedReference._userAgent;
        delete cleanedReference._fullViewport;

        // Write JSON file
        await fs.writeFile(outputFile, JSON.stringify(cleanedReference, null, 2));

        console.log(`💾 Reference saved to: ${outputFile}`);

        // Show sample of extracted data from tree structure
        console.log('\n📋 Sample extracted data:');
        const showSampleData = (node, depth = 0, maxSamples = 3, currentCount = 0) => {
            if (currentCount >= maxSamples) return currentCount;

            const indent = '  '.repeat(depth);
            console.log(`  ${indent}${node.selector}: ${node.layout.width}x${node.layout.height} at (${node.layout.x}, ${node.layout.y})`);
            currentCount++;

            if (node.children && node.children.length > 0 && currentCount < maxSamples) {
                for (const child of node.children) {
                    currentCount = showSampleData(child, depth + 1, maxSamples, currentCount);
                    if (currentCount >= maxSamples) break;
                }
            }
            return currentCount;
        };

        showSampleData(layoutData);

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

async function extractAllTestFiles(category = null, forceRegenerate = false, includeCss21 = false) {
    console.log('🔍 Scanning for test HTML files...');

    const dataDir = path.join(__dirname, '..', 'data');

    // Auto-discover available categories if none specified
    let categories;
    if (category) {
        categories = [category];
    } else {
        try {
            const allEntries = await fs.readdir(dataDir, { withFileTypes: true });
            categories = allEntries
                .filter(entry => entry.isDirectory() && !entry.name.startsWith('.'))
                .filter(entry => includeCss21 || entry.name !== 'css2.1') // Exclude css2.1 unless explicitly included
                .map(entry => entry.name)
                .sort();

            if (!includeCss21 && allEntries.some(entry => entry.name === 'css2.1')) {
                console.log(`📁 Auto-discovered categories: ${categories.join(', ')} (excluded css2.1)`);
                console.log(`   Use --include-css21 flag to include css2.1 suite`);
            } else {
                console.log(`📁 Auto-discovered categories: ${categories.join(', ')}`);
            }
        } catch (error) {
            console.log('⚠️  Could not auto-discover categories, using defaults');
            categories = ['basic', 'intermediate', 'advanced'];
        }
    }

    // Validate single category if specified
    if (category && !categories.includes(category)) {
        console.error(`❌ Invalid category: ${category}. Available categories: ${categories.join(', ')}`);
        process.exit(1);
    }

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
    let skippedCount = 0;
    const results = [];

    for (const fileInfo of allFiles) {
        console.log(`\n📄 Processing: ${fileInfo.category}/${fileInfo.file}`);

        try {
            const result = await extractLayoutFromFile(fileInfo.path, forceRegenerate);
            const wasSkipped = result._wasSkipped || false;
            delete result._wasSkipped; // Clean up the flag

            // Helper to count elements in tree (for compatibility)
            const countElementsInResult = (result) => {
                if (result.layout_tree) {
                    // New tree format
                    const countNodes = (node) => {
                        let count = 1;
                        if (node.children && node.children.length > 0) {
                            node.children.forEach(child => {
                                count += countNodes(child);
                            });
                        }
                        return count;
                    };
                    return countNodes(result.layout_tree);
                } else if (result.layout_data) {
                    // Legacy flat format
                    return Object.keys(result.layout_data).length;
                } else {
                    return 0;
                }
            };

            const elementCount = countElementsInResult(result);

            results.push({
                ...fileInfo,
                success: true,
                wasSkipped: wasSkipped,
                elementCount: elementCount,
                result: result
            });

            if (wasSkipped) {
                skippedCount++;
                console.log(`⏭️  Skipped: ${elementCount} elements (file already exists)`);
            } else {
                successCount++;
                console.log(`✅ Success: ${elementCount} elements extracted`);
            }
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
    console.log(`✅ Generated: ${successCount}`);
    console.log(`⏭️  Skipped (already exist): ${skippedCount}`);
    console.log(`❌ Failed: ${failCount}`);
    console.log(`📈 Success rate: ${Math.round((successCount + skippedCount) / totalFiles * 100)}%`);

    // Show detailed results
    console.log('\n📋 Detailed Results:');
    results.forEach(result => {
        let status, details;
        if (result.success) {
            status = result.wasSkipped ? '⏭️ ' : '✅';
            const action = result.wasSkipped ? 'skipped' : 'generated';
            details = `${result.elementCount} elements (${action})`;
        } else {
            status = '❌';
            details = `Error: ${result.error}`;
        }
        console.log(`  ${status} ${result.category}/${result.file} - ${details}`);
    });

    // Save summary to file
    const summaryFile = path.join(__dirname, '..', 'reports', 'extraction_summary.json');
    await fs.mkdir(path.dirname(summaryFile), { recursive: true });

    // Extract browser info from first successful result for summary
    const firstSuccessResult = results.find(r => r.success && r.result)?.result;
    const devicePixelRatio = firstSuccessResult?._devicePixelRatio || 1;
    const userAgent = firstSuccessResult?._userAgent || 'Unknown';
    const fullViewport = firstSuccessResult?._fullViewport || { width: 1200, height: 800, deviceScaleFactor: 1 };

    const summary = {
        total_files: totalFiles,
        generated: successCount,
        skipped: skippedCount,
        failed: failCount,
        success_rate: Math.round((successCount + skippedCount) / totalFiles * 100),
        browser_info: {
            userAgent: userAgent,
            viewport: fullViewport
        },
        results: results.map(r => ({
            category: r.category,
            file: r.file,
            success: r.success,
            was_skipped: r.wasSkipped || false,
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
    let forceRegenerate = false;
    let includeCss21 = false;

    for (let i = 0; i < args.length; i++) {
        const arg = args[i];
        if (arg === '--help' || arg === '-h') {
            showHelp = true;
        } else if (arg === '--category' || arg === '-c') {
            category = args[++i];
            // Validate category exists (we'll check this later when scanning directories)
        } else if (arg === '--force' || arg === '-f') {
            forceRegenerate = true;
        } else if (arg === '--include-css21') {
            includeCss21 = true;
        } else if (arg.endsWith('.html')) {
            singleFile = arg;
        } else {
            console.error(`❌ Unknown argument: ${arg}`);
            showHelp = true;
        }
    }

    if (showHelp) {
        console.log(`
Usage: node extract_browser_references.js [options] [html_file]

Options:
  --category, -c <name>   Extract only from specific category (auto-discovered from data/ directory)
  --force, -f             Force regeneration of existing reference files
  --include-css21         Include css2.1 test suite (excluded by default)
  --help, -h              Show this help message

Examples:
  node extract_browser_references.js                                    # Extract all test files (excludes css2.1)
  node extract_browser_references.js --category baseline                # Extract only baseline tests
  node extract_browser_references.js --include-css21                    # Extract all including css2.1 suite
  node extract_browser_references.js --force                           # Force regenerate all references
  node extract_browser_references.js ../data/basic/flex_001.html        # Extract single file

Generated files:
  ../reference/<category>/<test_name>.json                  # Individual reference files
  ../reports/extraction_summary.json                        # Summary report

Note: By default, existing reference files are skipped. Use --force to regenerate them.
      The css2.1 test suite is excluded by default due to its size. Use --include-css21 to include it.
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
            const result = await extractLayoutFromFile(resolvedPath, forceRegenerate);

            // Count elements in tree structure
            const countTreeElements = (node) => {
                let count = 1;
                if (node.children && node.children.length > 0) {
                    node.children.forEach(child => {
                        count += countTreeElements(child);
                    });
                }
                return count;
            };

            const elementCount = result.layout_tree ? countTreeElements(result.layout_tree) : 0;

            console.log(`\n🎉 Extraction completed successfully!`);
            console.log(`✅ Reference JSON created with ${elementCount} elements in tree structure`);
        } else {
            // Batch mode
            await extractAllTestFiles(category, forceRegenerate, includeCss21);
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
