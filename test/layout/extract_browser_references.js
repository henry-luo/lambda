#!/usr/bin/env node

/**
 * Simple Browser Layout Extractor
 * Extracts layout data from a single HTML file using Puppeteer
 */

const puppeteer = require('puppeteer');
const fs = require('fs').promises;
const path = require('path');

async function extractLayoutFromFile(htmlFilePath, forceRegenerate = false, platform = null) {
    console.log(`🔍 Checking layout extraction for: ${htmlFilePath}`);

    // Determine output file path first
    // Handle both .html and .htm extensions
    const ext = htmlFilePath.endsWith('.htm') && !htmlFilePath.endsWith('.html') ? '.htm' : '.html';
    const baseName = path.basename(htmlFilePath, ext);
    // Write directly to flat reference directory (combined structure)
    // If platform is specified, add platform suffix to filename (e.g., test_name.linux.json)
    const outputDir = path.join(__dirname, 'reference');
    const outputFile = platform 
        ? path.join(outputDir, `${baseName}.${platform}.json`)
        : path.join(outputDir, `${baseName}.json`);

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
        // Launch browser - use system Chromium on ARM Linux if available
        console.log('🚀 Launching browser...');
        const launchOptions = {
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
        };
        
        // On ARM Linux, use system Chromium instead of Puppeteer's bundled Chrome
        const os = require('os');
        const { execSync } = require('child_process');
        if (os.arch() === 'arm64' && os.platform() === 'linux') {
            try {
                const chromiumPath = execSync('which chromium-browser || which chromium', { encoding: 'utf8' }).trim();
                if (chromiumPath) {
                    console.log(`📦 Using system Chromium: ${chromiumPath}`);
                    launchOptions.executablePath = chromiumPath;
                }
            } catch (e) {
                // Fall back to bundled Chrome
            }
        }
        
        browser = await puppeteer.launch(launchOptions);

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

        // Load HTML file using file:// URL to preserve relative paths for @font-face
        console.log('📄 Loading HTML file...');
        const fileUrl = `file://${htmlFilePath}`;
        await page.goto(fileUrl, { waitUntil: 'networkidle0' });

        // Wait for fonts and layout to stabilize
        await page.evaluate(() => document.fonts.ready);
        await new Promise(resolve => setTimeout(resolve, 200));
        console.log('✅ HTML loaded and rendered');

        // Extract layout data
        console.log('📊 Extracting layout data...');
        const layoutData = await page.evaluate(() => {
            // Helper to get className as string (handles SVGAnimatedString for SVG elements)
            const getClassNameString = (element) => {
                if (!element.className) return '';
                // SVG elements have className as SVGAnimatedString with baseVal property
                if (typeof element.className === 'object' && element.className.baseVal !== undefined) {
                    return element.className.baseVal;
                }
                return typeof element.className === 'string' ? element.className : '';
            };

            // Helper to generate enhanced CSS selector
            const generateSelector = (element) => {
                if (element.id) return `#${element.id}`;

                let selector = element.tagName.toLowerCase();
                const classNameStr = getClassNameString(element);
                if (classNameStr) {
                    selector += '.' + classNameStr.split(' ').filter(c => c.trim()).join('.');
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

            // Helper function to extract text node data
            const extractTextNodeData = (textNode, nodeIndex) => {
                try {
                    const text = textNode.textContent;
                    const range = document.createRange();
                    range.selectNodeContents(textNode);
                    const rects = range.getClientRects();

                    // Convert ClientRects to plain objects and round coordinates
                    let rectArray = Array.from(rects).map(rect => ({
                        x: Math.round(rect.left * 100) / 100,
                        y: Math.round(rect.top * 100) / 100,
                        width: Math.round(rect.width * 100) / 100,
                        height: Math.round(rect.height * 100) / 100,
                        right: Math.round(rect.right * 100) / 100,
                        bottom: Math.round(rect.bottom * 100) / 100
                    }));

                    // Additional debugging: check parent element visibility
                    const parentElement = textNode.parentElement;
                    const parentComputed = parentElement ? window.getComputedStyle(parentElement) : null;
                    const isParentVisible = parentComputed ?
                        (parentComputed.display !== 'none' && parentComputed.visibility !== 'hidden') :
                        false;

                    // Enhanced: For multi-line text, return multiple text nodes (one per line) to match Radiant's behavior
                    if (rectArray.length > 1 && text.trim().length > 0) {
                        try {
                            // Use character-by-character analysis to determine line breaks
                            const charPositions = [];
                            for (let i = 0; i < text.length; i++) {
                                range.setStart(textNode, i);
                                range.setEnd(textNode, i + 1);
                                const charRects = range.getClientRects();
                                if (charRects.length > 0) {
                                    const rect = charRects[0];
                                    charPositions.push({
                                        char: text[i],
                                        index: i,
                                        y: Math.round(rect.top * 100) / 100,
                                        x: Math.round(rect.left * 100) / 100
                                    });
                                }
                            }

                            // Group characters by Y position to determine lines (with tolerance for slight variations)
                            const tolerance = 2;
                            const lines = [];
                            charPositions.forEach(charData => {
                                let foundLine = lines.find(line => Math.abs(line.y - charData.y) <= tolerance);
                                if (!foundLine) {
                                    foundLine = { y: charData.y, chars: [] };
                                    lines.push(foundLine);
                                }
                                foundLine.chars.push(charData);
                            });

                            // Sort lines by Y position
                            lines.sort((a, b) => a.y - b.y);

                            // Return array of separate text nodes (one per line) to match Radiant's behavior
                            const lineTextNodes = [];
                            lines.forEach((line, lineIndex) => {
                                if (line.chars.length > 0 && lineIndex < rectArray.length) {
                                    const startIndex = line.chars[0].index;
                                    const endIndex = line.chars[line.chars.length - 1].index + 1;
                                    const segmentText = text.substring(startIndex, endIndex);
                                    const rect = rectArray[lineIndex];

                                    lineTextNodes.push({
                                        nodeType: 'text',
                                        text: segmentText,
                                        length: segmentText.length,
                                        isWhitespaceOnly: !segmentText.trim(),
                                        layout: {
                                            rects: [{
                                                ...rect,
                                                text: segmentText,
                                                startIndex: 0, // Relative to this text segment
                                                endIndex: segmentText.length
                                            }],
                                            hasLayout: true,
                                            parentVisible: isParentVisible,
                                            parentDisplay: parentComputed?.display || 'unknown',
                                            parentVisibility: parentComputed?.visibility || 'unknown'
                                        },
                                        depth: 0, // Will be set by parent
                                        nodeIndex: nodeIndex
                                    });
                                }
                            });

                            range.detach();
                            return lineTextNodes; // Return array instead of single node
                        } catch (mappingError) {
                            // If detailed mapping fails, fall through to single node approach
                            console.warn('Line-based text mapping failed:', mappingError.message);
                        }
                    }

                    // Single line text or fallback - create enhanced rectangle with text mapping
                    if (rectArray.length === 1 && text.trim().length > 0) {
                        rectArray[0].text = text;
                        rectArray[0].startIndex = 0;
                        rectArray[0].endIndex = text.length;
                    }

                    range.detach();

                    return {
                        nodeType: 'text',
                        text: textNode.textContent,
                        length: textNode.textContent.length,
                        isWhitespaceOnly: !textNode.textContent.trim(),
                        layout: {
                            rects: rectArray,
                            hasLayout: rectArray.length > 0,
                            parentVisible: isParentVisible,
                            parentDisplay: parentComputed?.display || 'unknown',
                            parentVisibility: parentComputed?.visibility || 'unknown'
                        },
                        depth: 0, // Will be set by parent
                        nodeIndex: nodeIndex
                    };
                } catch (error) {
                    // Return basic text node data if layout extraction fails
                    return {
                        nodeType: 'text',
                        text: textNode.textContent,
                        length: textNode.textContent.length,
                        isWhitespaceOnly: !textNode.textContent.trim(),
                        layout: {
                            rects: [],
                            hasLayout: false,
                            error: error.message,
                            parentVisible: false,
                            parentDisplay: 'unknown',
                            parentVisibility: 'unknown'
                        },
                        depth: 0,
                        nodeIndex: nodeIndex
                    };
                }
            };

            // Helper function to extract element data
            const extractElementData = (element, elementIndex) => {
                const rect = element.getBoundingClientRect();
                const computed = window.getComputedStyle(element);

                // Generate enhanced selector
                const selector = generateSelector(element);
                const key = selector || `${element.tagName.toLowerCase()}_${elementIndex}`;

                const classNameStr = getClassNameString(element);
                return {
                    nodeType: 'element',
                    selector: key,
                    tag: element.tagName.toLowerCase(),
                    id: element.id || null,
                    classes: classNameStr ? classNameStr.split(' ').filter(c => c.trim()) : [],

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

                    // Hierarchy information
                    depth: 0,  // Will be calculated during tree building
                    elementIndex: elementIndex
                };
            };

            // Recursive function to build DOM tree in document order
            const buildDOMTree = (node, depth = 0, nodeIndex = 0) => {
                let nodeData;

                if (node.nodeType === Node.ELEMENT_NODE) {
                    // Element node
                    nodeData = extractElementData(node, nodeIndex);
                    nodeData.depth = depth;

                    // Process all child nodes in document order (both elements and text nodes)
                    nodeData.children = [];
                    if (node.childNodes && node.childNodes.length > 0) {
                        for (let i = 0; i < node.childNodes.length; i++) {
                            const childNode = node.childNodes[i];

                            if (childNode.nodeType === Node.ELEMENT_NODE) {
                                // Always include element nodes
                                const childTree = buildDOMTree(childNode, depth + 1, i);
                                nodeData.children.push(childTree);
                            } else if (childNode.nodeType === Node.TEXT_NODE) {
                                // Include text nodes that have layout rectangles OR are hidden (display:none, visibility:hidden)
                                const childResult = buildDOMTree(childNode, depth + 1, i);

                                // Handle case where multi-line text returns multiple text nodes
                                if (Array.isArray(childResult)) {
                                    // Multiple text nodes from line wrapping
                                    childResult.forEach(textNode => {
                                        if (textNode && textNode.layout) {
                                            const hasLayout = textNode.layout.hasLayout && textNode.layout.rects.length > 0;
                                            const isHidden = !textNode.layout.parentVisible ||
                                                           textNode.layout.parentDisplay === 'none' ||
                                                           textNode.layout.parentVisibility === 'hidden';

                                            // Include if it has layout OR if it's hidden (for testing purposes)
                                            if (hasLayout || isHidden) {
                                                textNode.depth = depth + 1;
                                                nodeData.children.push(textNode);
                                            }
                                        }
                                    });
                                } else if (childResult && childResult.layout) {
                                    // Single text node
                                    const hasLayout = childResult.layout.hasLayout && childResult.layout.rects.length > 0;
                                    const isHidden = !childResult.layout.parentVisible ||
                                                   childResult.layout.parentDisplay === 'none' ||
                                                   childResult.layout.parentVisibility === 'hidden';

                                    // Include if it has layout OR if it's hidden (for testing purposes)
                                    if (hasLayout || isHidden) {
                                        nodeData.children.push(childResult);
                                    }
                                }
                                // Only skip text nodes that have no layout AND are not hidden (e.g., whitespace-only nodes)
                            }
                        }
                    }

                } else if (node.nodeType === Node.TEXT_NODE) {
                    // Text node - may return single node or array of nodes for multi-line text
                    const textResult = extractTextNodeData(node, nodeIndex);

                    if (Array.isArray(textResult)) {
                        // Multi-line text returns multiple text nodes
                        textResult.forEach(textNode => {
                            textNode.depth = depth;
                            textNode.children = []; // Text nodes have no children
                        });
                        return textResult; // Return array of text nodes
                    } else {
                        // Single text node
                        textResult.depth = depth;
                        textResult.children = []; // Text nodes have no children
                        return textResult;
                    }
                }

                return nodeData;
            };

            // Start with the root HTML element to build the tree
            const htmlElement = document.documentElement;
            const elementTree = buildDOMTree(htmlElement, 0, 0);

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

        // Helper function to count nodes in tree (elements and text nodes)
        const countNodes = (node) => {
            let count = 1; // Count the current node
            if (node.children && node.children.length > 0) {
                node.children.forEach(child => {
                    count += countNodes(child);
                });
            }
            return count;
        };

        // Helper function to count elements only in tree
        const countElements = (node) => {
            let count = node.nodeType === 'element' ? 1 : 0;
            if (node.children && node.children.length > 0) {
                node.children.forEach(child => {
                    count += countElements(child);
                });
            }
            return count;
        };

        // Helper function to count text nodes only in tree
        const countTextNodes = (node) => {
            let count = node.nodeType === 'text' ? 1 : 0;
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
        console.log(`📈 Found ${countNodes(layoutData)} total nodes (${countElements(layoutData)} elements + ${countTextNodes(layoutData)} text nodes)`);

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

            if (node.nodeType === 'element') {
                console.log(`  ${indent}${node.selector}: ${node.layout.width}x${node.layout.height} at (${node.layout.x}, ${node.layout.y})`);
            } else if (node.nodeType === 'text') {
                const textPreview = node.text.length > 20 ? node.text.substring(0, 20) + '...' : node.text;
                const layoutInfo = node.layout.hasLayout ?
                    `${node.layout.rects.length} rect(s)` :
                    'no layout';
                console.log(`  ${indent}TEXT: "${textPreview}" (${layoutInfo})`);
            }

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

async function extractAllTestFiles(category = null, forceRegenerate = false, includeCss21 = false, platform = null) {
    console.log('🔍 Scanning for test HTML files...');
    if (platform) {
        console.log(`📦 Platform-specific reference: ${platform}`);
    }

    const dataDir = path.join(__dirname, 'data');

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
            const files = await fs.readdir(categoryDir, { withFileTypes: true });
            const htmlFiles = files
                .filter(entry => (typeof entry === 'string' ? entry : entry.name).endsWith('.html') || (typeof entry === 'string' ? entry : entry.name).endsWith('.htm'))
                .filter(entry => typeof entry === 'string' || entry.isFile())
                .map(entry => {
                    const name = typeof entry === 'string' ? entry : entry.name;
                    return {
                        category: cat,
                        file: name,
                        path: path.join(categoryDir, name)
                    };
                });

            // For suites with nested subdirectories (e.g., css2.1 has html4/, xhtml1/, etc.),
            // also scan subdirectories for HTML files
            const subDirs = files.filter(entry => typeof entry !== 'string' && entry.isDirectory() && !entry.name.startsWith('.'));
            for (const subDir of subDirs) {
                const subDirPath = path.join(categoryDir, subDir.name);
                try {
                    const subFiles = await fs.readdir(subDirPath);
                    const subHtmlFiles = subFiles
                        .filter(file => file.endsWith('.html') || file.endsWith('.htm'))
                        .map(file => ({
                            category: cat,
                            file: file,
                            path: path.join(subDirPath, file)
                        }));
                    htmlFiles.push(...subHtmlFiles);
                    if (subHtmlFiles.length > 0) {
                        console.log(`   📁 Found ${subHtmlFiles.length} HTML files in ${cat}/${subDir.name}/`);
                    }
                } catch (error) {
                    // Skip unreadable subdirectories
                }
            }

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
            const result = await extractLayoutFromFile(fileInfo.path, forceRegenerate, platform);
            const wasSkipped = result._wasSkipped || false;
            delete result._wasSkipped; // Clean up the flag

            // Helper to count nodes in tree (for compatibility with new format)
            const countNodesInResult = (result) => {
                if (result.layout_tree) {
                    // New tree format - count all nodes (elements and text)
                    const countAllNodes = (node) => {
                        let count = 1; // Current node
                        if (node.children && node.children.length > 0) {
                            node.children.forEach(child => {
                                count += countAllNodes(child);
                            });
                        }
                        return count;
                    };
                    return countAllNodes(result.layout_tree);
                } else if (result.layout_data) {
                    // Legacy flat format
                    return Object.keys(result.layout_data).length;
                } else {
                    return 0;
                }
            };

            const nodeCount = countNodesInResult(result);

            results.push({
                ...fileInfo,
                success: true,
                wasSkipped: wasSkipped,
                nodeCount: nodeCount,
                result: result
            });

            if (wasSkipped) {
                skippedCount++;
                console.log(`⏭️  Skipped: ${nodeCount} nodes (file already exists)`);
            } else {
                successCount++;
                console.log(`✅ Success: ${nodeCount} nodes extracted`);
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
            details = `${result.nodeCount} nodes (${action})`;
        } else {
            status = '❌';
            details = `Error: ${result.error}`;
        }
        console.log(`  ${status} ${result.category}/${result.file} - ${details}`);
    });

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
    let platform = null;

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
        } else if (arg === '--platform' || arg === '-p') {
            platform = args[++i];
            // Validate platform value
            const validPlatforms = ['linux', 'darwin', 'win32'];
            if (!validPlatforms.includes(platform)) {
                console.error(`❌ Invalid platform: ${platform}. Valid values: ${validPlatforms.join(', ')}`);
                process.exit(1);
            }
        } else if (arg.endsWith('.html') || arg.endsWith('.htm')) {
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
  --platform, -p <name>   Generate platform-specific reference (linux, darwin, win32)
                          Output: <test_name>.<platform>.json (e.g., test_name.linux.json)
  --force, -f             Force regeneration of existing reference files
  --include-css21         Include css2.1 test suite (excluded by default)
  --help, -h              Show this help message

Arguments:
  html_file               Path to a single HTML or HTM file to extract (optional)

Examples:
  node extract_browser_references.js                                    # Extract all test files (excludes css2.1)
  node extract_browser_references.js --category baseline                # Extract only baseline tests
  node extract_browser_references.js --platform linux --category baseline  # Extract baseline as Linux-specific
  node extract_browser_references.js --include-css21                    # Extract all including css2.1 suite
  node extract_browser_references.js --force                           # Force regenerate all references
  node extract_browser_references.js data/basic/flex_001.html           # Extract single .html file
  node extract_browser_references.js data/css2.1/blocks-001.htm         # Extract single .htm file
  node extract_browser_references.js --platform linux data/baseline/test.html  # Single file, Linux-specific

Generated files:
  reference/<test_name>.json                                # Generic reference files
  reference/<test_name>.<platform>.json                     # Platform-specific reference files

Note: By default, existing reference files are skipped. Use --force to regenerate them.
      The css2.1 test suite is excluded by default due to its size. Use --include-css21 to include it.
      Both .html and .htm file extensions are supported.
`);
        process.exit(0);
    }

    try {
        if (singleFile) {
            // Single file mode - resolve path correctly
            let resolvedPath = singleFile;

            // If path starts with 'test/', it's relative to project root, so adjust for current directory
            if (singleFile.startsWith('test/')) {
                resolvedPath = path.join(__dirname, '..', '..', singleFile);
            }
            // If path starts with '../', it's already relative to layout directory
            else if (!path.isAbsolute(singleFile)) {
                resolvedPath = path.resolve(singleFile);
            }

            console.log(`📍 Resolved path: ${resolvedPath}`);
            if (platform) {
                console.log(`📦 Platform-specific reference: ${platform}`);
            }
            await fs.access(resolvedPath);
            const result = await extractLayoutFromFile(resolvedPath, forceRegenerate, platform);

            // Count all nodes in tree structure (elements and text nodes)
            const countTreeNodes = (node) => {
                let count = 1; // Current node
                if (node.children && node.children.length > 0) {
                    node.children.forEach(child => {
                        count += countTreeNodes(child);
                    });
                }
                return count;
            };

            const nodeCount = result.layout_tree ? countTreeNodes(result.layout_tree) : 0;

            console.log(`\n🎉 Extraction completed successfully!`);
            console.log(`✅ Reference JSON created with ${nodeCount} nodes in tree structure`);
        } else {
            // Batch mode
            await extractAllTestFiles(category, forceRegenerate, includeCss21, platform);
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
