#!/usr/bin/env node

/**
 * Automated Radiant Layout Test Script
 *
 * This script runs Radiant layout engine tests and compares the output against
 * browser reference data. It focuses on element structure and layout bounds
 * comparison with configurable tolerance for text positioning.
 */

const fs = require('fs').promises;
const path = require('path');
const { spawn } = require('child_process');
const os = require('os');

// Get current platform for loading platform-specific references
const CURRENT_PLATFORM = os.platform(); // 'linux', 'darwin', or 'win32'

class RadiantLayoutTester {
    constructor(options = {}) {
        this.engine = options.engine || 'lambda-css'; // Default to 'lambda-css'
        this.radiantExe = options.radiantExe || './lambda.exe'; // Default to lambda.exe
        this.tolerance = options.tolerance || 5; // 5px tolerance for layout differences
        this.elementThreshold = options.elementThreshold || 100.0; // 100% overall element match threshold
        this.textThreshold = options.textThreshold || 100.0; // 100% overall text match threshold
        this.testDataDir = path.join(__dirname, 'data');
        this.referenceDir = path.join(__dirname, 'reference');
        this.outputFile = '/tmp/view_tree.json';
        this.verbose = options.verbose || false;
        this.json = options.json || false; // JSON output mode
        this.projectRoot = options.projectRoot || process.cwd();
        this.maxConcurrency = options.maxConcurrency || 5; // Max parallel tests
        this.testIdCounter = 0; // Counter for unique test IDs
        this.singleTestMode = options.singleTestMode || false; // Single test mode for detailed failure reports
        this.batchSize = options.batchSize || 0; // Batch size for layout (0 = disabled, use single file mode)
        this.batchOutputDir = '/tmp/layout_batch'; // Directory for batch output files
    }

    /**
     * Generate a unique output file path for parallel test execution
     */
    getUniqueOutputFile() {
        const testId = ++this.testIdCounter;
        return `/tmp/view_tree_${process.pid}_${testId}.json`;
    }

    /**
     * Run layout engine on a test file
     * @param {string} htmlFile - Path to the HTML file to test
     * @param {string} outputFile - Optional unique output file path for parallel execution
     */
    async runRadiantLayout(htmlFile, outputFile = null) {
        return new Promise((resolve, reject) => {
            // Always use standard viewport size (1200x800) to match browser reference
            // Note: Lambda defaults to 1200x800, but we pass args explicitly for clarity
            const args = ['layout', htmlFile, '-vw', '1200', '-vh', '800'];

            // Add view output file argument if specified (for parallel execution)
            if (outputFile) {
                args.push('--view-output', outputFile);
            }

            const proc = spawn(this.radiantExe, args, {
                cwd: this.projectRoot
            });

            let stdout = '';
            let stderr = '';

            proc.stdout.on('data', (data) => {
                stdout += data.toString();
            });

            proc.stderr.on('data', (data) => {
                stderr += data.toString();
            });

            const timeout = setTimeout(() => {
                proc.kill();
                reject(new Error(`${this.engine === 'lambda-css' ? 'Lambda CSS' : 'Radiant'} execution timeout (30s)`));
            }, 30000);

            proc.on('close', (code) => {
                clearTimeout(timeout);
                if (code === 0) {
                    resolve({ stdout, stderr, outputFile });
                } else {
                    reject(new Error(`${this.engine === 'lambda-css' ? 'Lambda CSS' : 'Radiant'} failed with exit code ${code}: ${stderr}`));
                }
            });

            proc.on('error', (error) => {
                clearTimeout(timeout);
                reject(error);
            });
        });
    }

    /**
     * Run layout engine on multiple files in batch mode.
     * This is much faster than running individual files as it reuses the UiContext.
     * @param {Array<string>} htmlFiles - Array of HTML file paths to process
     * @returns {Promise<Map<string, string>>} - Map of input file -> output JSON file path
     */
    async runBatchLayout(htmlFiles) {
        // Ensure batch output directory exists
        const { mkdir } = require('fs').promises;
        try {
            await mkdir(this.batchOutputDir, { recursive: true });
        } catch (e) {
            // Directory may already exist
        }

        return new Promise((resolve, reject) => {
            // Build command: layout file1.html file2.html ... --output-dir /tmp/layout_batch/
            const args = ['layout', ...htmlFiles, '--output-dir', this.batchOutputDir, '--continue-on-error'];

            if (this.verbose) {
                console.log(`   🚀 Batch layout: ${htmlFiles.length} files`);
            }

            const proc = spawn(this.radiantExe, args, {
                cwd: this.projectRoot
            });

            let stdout = '';
            let stderr = '';

            proc.stdout.on('data', (data) => {
                stdout += data.toString();
            });

            proc.stderr.on('data', (data) => {
                stderr += data.toString();
            });

            // Longer timeout for batch processing
            const timeout = setTimeout(() => {
                proc.kill();
                reject(new Error(`Batch layout timeout (${htmlFiles.length} files, 120s limit)`));
            }, 120000);

            proc.on('close', (code) => {
                clearTimeout(timeout);
                // Build map of input file -> output file
                const outputMap = new Map();
                for (const htmlFile of htmlFiles) {
                    const basename = path.basename(htmlFile).replace(/\.(html|htm)$/i, '');
                    const outputFile = path.join(this.batchOutputDir, `${basename}.json`);
                    outputMap.set(htmlFile, outputFile);
                }
                // We continue even on non-zero exit code since --continue-on-error was used
                resolve(outputMap);
            });

            proc.on('error', (error) => {
                clearTimeout(timeout);
                reject(error);
            });
        });
    }

    /**
     * Load Radiant output from specified file or default /tmp/view_tree.json
     * @param {string} testContext - Context for error messages
     * @param {string} outputFile - Optional specific output file to read
     */
    async loadRadiantOutput(testContext = '', outputFile = null) {
        const fileToRead = outputFile || this.outputFile;
        let content = '';
        try {
            content = await fs.readFile(fileToRead, 'utf8');
            return JSON.parse(content);
        } catch (error) {
            if (error instanceof SyntaxError) {
                // JSON parsing error - provide more detailed information
                const contextInfo = testContext ? ` for test file: ${testContext}` : '';
                const lines = error.message.includes('line') ? '' : this.getJsonErrorContext(content, error);
                throw new Error(`Failed to parse Radiant JSON output${contextInfo}: ${error.message}${lines}`);
            } else {
                // File reading error
                const contextInfo = testContext ? ` for test file: ${testContext}` : '';
                throw new Error(`Failed to load Radiant output${contextInfo}: ${error.message}`);
            }
        }
    }

    /**
     * Helper method to provide context around JSON parsing errors
     */
    getJsonErrorContext(content, error) {
        try {
            // Try to extract line/column information from error message
            const match = error.message.match(/position (\d+)/);
            if (match) {
                const position = parseInt(match[1]);
                const lines = content.split('\n');
                let currentPos = 0;
                let lineNum = 1;

                for (const line of lines) {
                    if (currentPos + line.length >= position) {
                        const columnNum = position - currentPos + 1;
                        const startLine = Math.max(1, lineNum - 2);
                        const endLine = Math.min(lines.length, lineNum + 2);

                        let context = '\nJSON context:\n';
                        for (let i = startLine; i <= endLine; i++) {
                            const prefix = i === lineNum ? '>>> ' : '    ';
                            context += `${prefix}${i}: ${lines[i - 1]}\n`;
                        }
                        context += `     ${' '.repeat(columnNum + 2)}^ Error here`;
                        return context;
                    }
                    currentPos += line.length + 1; // +1 for newline
                    lineNum++;
                }
            }
        } catch (e) {
            // If context extraction fails, just return empty string
        }
        return '';
    }

    /**
     * Load browser reference data
     * First tries platform-specific reference (e.g., test_name.linux.json),
     * then falls back to generic reference (test_name.json)
     */
    async loadBrowserReference(testName, category) {
        // Try platform-specific reference first (e.g., test_name.linux.json)
        const platformRefFile = path.join(this.referenceDir, `${testName}.${CURRENT_PLATFORM}.json`);
        try {
            const content = await fs.readFile(platformRefFile, 'utf8');
            if (this.verbose) {
                console.log(`   📦 Using platform-specific reference: ${testName}.${CURRENT_PLATFORM}.json`);
            }
            return JSON.parse(content);
        } catch (error) {
            if (error.code !== 'ENOENT') {
                throw new Error(`Failed to load platform reference: ${error.message}`);
            }
            // Platform-specific reference not found, try generic reference
        }

        // Try flat directory structure (merged references)
        const flatRefFile = path.join(this.referenceDir, `${testName}.json`);
        try {
            const content = await fs.readFile(flatRefFile, 'utf8');
            return JSON.parse(content);
        } catch (error) {
            if (error.code !== 'ENOENT') {
                throw new Error(`Failed to load browser reference: ${error.message}`);
            }
            // Fall back to category subdirectory for backwards compatibility
            const categoryRefFile = path.join(this.referenceDir, category, `${testName}.json`);
            try {
                const content = await fs.readFile(categoryRefFile, 'utf8');
                return JSON.parse(content);
            } catch (error) {
                if (error.code === 'ENOENT') {
                    return null; // Reference doesn't exist
                }
                throw new Error(`Failed to load browser reference: ${error.message}`);
            }
        }
    }

    /**
     * Compare layout bounds with tolerance
     */
    compareLayout(radiantLayout, browserLayout, isText = false, isCentered = false) {
        const differences = [];
        const properties = ['x', 'y', 'width', 'height'];
        const widthDiff = Math.abs((radiantLayout.width || 0) - (browserLayout.width || 0));

        for (const prop of properties) {
            const radiantVal = radiantLayout[prop] || 0;
            const browserVal = browserLayout[prop] || 0;
            const diff = Math.abs(radiantVal - browserVal);

            // Always include the difference, regardless of tolerance
            let tolerance = Math.max(isText ? (prop == 'width' || prop == 'y' ? (browserVal * (browserVal > 150 ? 0.07 : 0.03)) : 0) :
                (prop == 'height' || prop == 'y' ? browserVal * 0.03 : 0), this.tolerance);

            // For centered text, x position shifts by ~half the width difference
            if (prop === 'x' && isText && isCentered) {
                tolerance += widthDiff / 6;
            }

            differences.push({
                property: prop,
                radiant: radiantVal,
                browser: browserVal,
                difference: diff,
                exceedsTolerance: diff > tolerance,
                tolerance: tolerance
            });
        }

        return differences;
    }

    /**
     * Compare computed properties for span elements specifically
     */
    compareComputedProperties(radiantComputed, browserComputed) {
        const differences = [];
        const matches = [];

        // Base property mappings that are always checked
        let propertyMappings = {
            // Display and positioning
            'display': 'display'
        };

        // Conditionally add font properties only if Radiant has font object
        if (radiantComputed && radiantComputed.font) {
            // Only add font properties that exist in the Radiant font object
            if (radiantComputed.font.family !== undefined) {
                propertyMappings['font.family'] = 'fontFamily';
            }
            if (radiantComputed.font.size !== undefined) {
                propertyMappings['font.size'] = 'fontSize';
            }
            if (radiantComputed.font.weight !== undefined) {
                propertyMappings['font.weight'] = 'fontWeight';
            }
            // Note: fontStyle not included as browser reference doesn't capture it
        }

        // Conditionally add other properties if they exist in Radiant computed
        if (radiantComputed && radiantComputed.color !== undefined) {
            propertyMappings['color'] = 'color';
        }

        // Note: Flexbox and text properties commented out as not typically available in span computed properties
        // if (radiantComputed && radiantComputed.flexWrap !== undefined) {
        //     propertyMappings['flexWrap'] = 'flexWrap';
        // }
        // if (radiantComputed && radiantComputed.text_align !== undefined) {
        //     propertyMappings['text_align'] = 'textAlign';
        // }

        // Helper function to get nested property value
        const getNestedValue = (obj, path) => {
            return path.split('.').reduce((current, key) => {
                return current && current[key] !== undefined ? current[key] : undefined;
            }, obj);
        };

        // Helper function to normalize values for comparison
        const normalizeValue = (value, property) => {
            if (value === undefined || value === null) return null;

            // Convert numeric values to strings for comparison
            if (typeof value === 'number') {
                if (property === 'fontSize') {
                    return value.toString();
                }
                return value.toString();
            }

            // Normalize font family (remove quotes and extra whitespace)
            if (property === 'fontFamily') {
                return value.replace(/["']/g, '').replace(/,\s*/g, ', ').trim();
            }

            // Normalize font weight (convert numeric to descriptive)
            if (property === 'fontWeight') {
                const weightMap = {
                    '400': 'normal',
                    '700': 'bold',
                    'normal': 'normal',
                    'bold': 'bold'
                };
                return weightMap[value.toString()] || value.toString();
            }

            // Normalize text align values
            if (property === 'textAlign') {
                const alignMap = {
                    'left': 'start',
                    'start': 'start'
                };
                return alignMap[value] || value;
            }

            // Normalize color values
            if (property === 'color') {
                // Convert rgba hex format to rgb/rgba format for comparison
                if (typeof value === 'string' && value.startsWith('#') && value.length === 9) {
                    // Convert #rrggbbaa to rgba format for easier comparison
                    const r = parseInt(value.substr(1, 2), 16);
                    const g = parseInt(value.substr(3, 2), 16);
                    const b = parseInt(value.substr(5, 2), 16);
                    const a = parseInt(value.substr(7, 2), 16) / 255;
                    return a === 1 ? `rgb(${r}, ${g}, ${b})` : `rgba(${r}, ${g}, ${b}, ${a})`;
                }
                return value;
            }

            return value.toString();
        };

        // Compare each mapped property
        for (const [radiantPath, browserProperty] of Object.entries(propertyMappings)) {
            const radiantValue = getNestedValue(radiantComputed, radiantPath);
            const browserValue = browserComputed ? browserComputed[browserProperty] : undefined;

            // Skip comparison if the property doesn't exist in browser reference
            // (unless it's a required property like 'display')
            if (browserValue === undefined && browserProperty !== 'display') {
                continue; // Skip this property comparison
            }

            const normalizedRadiant = normalizeValue(radiantValue, browserProperty);
            const normalizedBrowser = normalizeValue(browserValue, browserProperty);

            // Special comparison for font family - check if browser font family starts with radiant font family
            let match;
            if (browserProperty === 'fontFamily' && normalizedRadiant && normalizedBrowser) {
                // For font family, consider it a match if browser font family starts with radiant font family
                const radiantFamily = normalizedRadiant.toLowerCase();
                const browserFamily = normalizedBrowser.toLowerCase();
                match = browserFamily.startsWith(radiantFamily) || normalizedRadiant === normalizedBrowser;
            } else {
                match = normalizedRadiant === normalizedBrowser;
            }

            const comparison = {
                property: browserProperty,
                radiantPath: radiantPath,
                radiant: radiantValue,
                browser: browserValue,
                normalizedRadiant: normalizedRadiant,
                normalizedBrowser: normalizedBrowser,
                match: match
            };

            if (match) {
                matches.push(comparison);
            } else {
                differences.push(comparison);
            }
        }

        return {
            totalProperties: matches.length + differences.length,
            matches: matches.length,
            differences: differences,
            matchedProperties: matches
        };
    }

    /**
     * Get all children (elements + text nodes) from a node in document order
     */
    getAllChildren(node, isRadiant = false) {
        if (!node) return [];

        const children = [];

        if (isRadiant) {
            // Radiant format: text nodes are children with type: 'text'
            if (node.children && Array.isArray(node.children)) {
                node.children.forEach((child, index) => {
                    children.push({
                        type: child.type === 'text' ? 'text' : 'element',
                        node: child,
                        index: index
                    });
                });
            }
        } else {
            // Browser format: elements in children array, text nodes in separate textNodes array
            // We need to combine them in document order

            // First, collect element children
            if (node.children && Array.isArray(node.children)) {
                node.children.forEach((child, index) => {
                    children.push({
                        type: child.nodeType === 'text' ? 'text' : 'element',
                        node: child,
                        index: index
                    });
                });
            }

            // Then, add text nodes from the textNodes array
            // Text nodes are stored separately in browser format and need to be included
            if (node.textNodes && Array.isArray(node.textNodes)) {
                node.textNodes.forEach((textNode, index) => {
                    // Mark it as a text node with nodeType for comparison
                    children.push({
                        type: 'text',
                        node: { ...textNode, nodeType: 'text' },
                        index: children.length + index
                    });
                });
            }
        }

        return children;
    }

    /**
     * Filter out non-layout elements but keep text nodes for unified comparison
     */
    /**
     * Flatten children by unwrapping CSS anonymous boxes
     * Anonymous boxes (like ::anon-tbody) exist only for layout purposes
     * and should not affect DOM structure comparison
     */
    flattenAnonymousBoxes(children) {
        if (!Array.isArray(children)) return [];

        const flattened = [];
        for (const child of children) {
            if (!child || !child.node) continue;

            // If this is an anonymous box, include its children instead
            if (child.type === 'element' && child.node.tag && child.node.tag.startsWith('::')) {
                // Get all children of the anonymous box and flatten them recursively
                const anonChildren = this.getAllChildren(child.node, true);
                const flattenedAnonChildren = this.flattenAnonymousBoxes(anonChildren);
                flattened.push(...flattenedAnonChildren);
            } else {
                flattened.push(child);
            }
        }
        return flattened;
    }

    filterForComparison(children, isRadiant = false) {
        if (!Array.isArray(children)) return [];

        // First flatten anonymous boxes for Radiant nodes
        let processedChildren = isRadiant ? this.flattenAnonymousBoxes(children) : children;

        return processedChildren.filter(child => {
            if (!child || !child.node) return false;

            // Keep text nodes for comparison
            if (child.type === 'text') {
                // Keep text nodes that have layout info (including zero-size text for font-size: 0)
                if (child.node.layout && child.node.layout.rects) {
                    return child.node.layout.rects.length > 0; // Browser format with nested rects
                } else if (child.node.rects && child.node.rects.length > 0) {
                    return true; // Browser text nodes have rects at top level
                } else if (child.node.layout && typeof child.node.layout.width === 'number') {
                    return true; // Radiant format (allow zero-width text e.g. font-size: 0)
                }
                return false;
            }

            // For elements, skip non-layout tags
            const skipTags = ['head', 'script', 'style', 'meta', 'title', 'link'];
            if (skipTags.includes(child.node.tag)) return false;

            // Skip display: none elements - Radiant doesn't create views for these
            // since they don't participate in layout
            if (child.node.computed && child.node.computed.display === 'none') {
                return false;
            }

            return true;
        });
    }

    /**
     * Compare two tree structures node by node with unified child comparison
     */
    compareNodes(radiantNode, browserNode, path = '', results = null, depth = 0, parentComputed = null) {
        if (!results) {
            results = {
                totalElements: 0,
                matchedElements: 0,
                totalTextNodes: 0,
                matchedTextNodes: 0,
                totalSpanElements: 0,
                matchedSpanElements: 0,
                spanComputedProperties: {
                    totalComparisons: 0,
                    totalMatches: 0,
                    differences: []
                },
                differences: []
            };
        }

        // Helper function for hierarchical indentation
        const indent = (level = depth) => '  '.repeat(level);

        if (!radiantNode && !browserNode) {
            return results;
        }

        if (!radiantNode || !browserNode) {
            if (radiantNode && radiantNode.type === 'text') {
                results.totalTextNodes++;
            } else if (browserNode && browserNode.nodeType === 'text') {
                results.totalTextNodes++;
            } else {
                results.totalElements++;
            }

            results.differences.push({
                type: 'missing_node',
                path: path,
                radiant: radiantNode ? {
                    tag: radiantNode.tag || 'text',
                    type: radiantNode.type,
                    content: radiantNode.content || radiantNode.text
                } : null,
                browser: browserNode ? {
                    tag: browserNode.tag || 'text',
                    type: browserNode.nodeType,
                    content: browserNode.text
                } : null
            });

            if (this.verbose) {
                const presentNode = radiantNode || browserNode;
                const missingIn = radiantNode ? 'Browser' : 'Radiant';
                const presentIn = radiantNode ? 'Radiant' : 'Browser';

                if (presentNode.type === 'text' || presentNode.nodeType === 'text') {
                    const content = presentNode.content || presentNode.text || '';
                    console.log(`${indent()}❌ Missing text in ${missingIn}: "${content.substring(0, 10)}${content.length > 10 ? '...' : ''}"`);
                } else {
                    console.log(`${indent()}❌ Missing element in ${missingIn}: <${presentNode.tag}>`);
                }
            }

            return results;
        }

        // Compare current nodes
        const radiantIsText = radiantNode.type === 'text';
        const browserIsText = browserNode.nodeType === 'text';

        if (radiantIsText && browserIsText) {
            // Both are text nodes - compare content and layout
            results.totalTextNodes++;

            if (this.verbose) {
                const radiantContentStr = radiantNode.content || '';
                const browserContentStr = browserNode.text || '';
                const radiantContent = radiantContentStr.substring(0, 20) + (radiantContentStr.length > 20 ? '...' : '');
                const browserContent = browserContentStr.substring(0, 20) + (browserContentStr.length > 20 ? '...' : '');
                // console.log("browser text:", browserNode.text);
                console.log(`${indent()}📝 Comparing text: "${radiantContent}" vs. "${browserContent}"`);
            }

            const contentMatch = (radiantNode.content || '').trim() === (browserNode.text || '').trim();

            // Browser text nodes have rects at top level, not under layout
            const browserHasLayout = browserNode.layout || (browserNode.rects && browserNode.rects.length > 0);

            if (contentMatch && radiantNode.layout && browserHasLayout) {
                // Compare layout - browser may have multiple rects for wrapped text
                const radiantLayout = radiantNode.layout;
                let browserLayout;

                // If browser has rects at top level (text node format), use the first rect
                if (browserNode.rects && browserNode.rects.length > 0) {
                    browserLayout = browserNode.rects[0];
                } else if (browserNode.layout && browserNode.layout.rects && browserNode.layout.rects.length > 0) {
                    // If browser has rects nested under layout
                    browserLayout = browserNode.layout.rects[0];
                } else {
                    browserLayout = browserNode.layout;
                }

                // Check if text is center-aligned (from parent's computed styles)
                const isCentered = parentComputed &&
                    (parentComputed.text_align === 'center' || parentComputed.textAlign === 'center');

                const layoutDiffs = this.compareLayout(radiantLayout, browserLayout, true, isCentered);
                const maxDiff = layoutDiffs.length > 0 ? Math.max(...layoutDiffs.map(d => d.difference)) : 0;
                const exceedsToleranceCount = layoutDiffs.filter(d => d.exceedsTolerance).length;

                if (this.verbose) {
                    console.log(`${indent()}   Radiant: (${radiantLayout.x}, ${radiantLayout.y}, ${radiantLayout.width}×${radiantLayout.height})`);
                    console.log(`${indent()}   Browser: (${browserLayout.x}, ${browserLayout.y}, ${browserLayout.width}×${browserLayout.height})`);
                }

                const maxTolerance = Math.max(...layoutDiffs.map(d => d.tolerance)).toFixed(1);
                if (exceedsToleranceCount === 0) {
                    results.matchedTextNodes++;
                    if (this.verbose) {
                        console.log(`${indent()}   ✅ TEXT MATCH (${maxDiff.toFixed(1)}px diff <= ${maxTolerance}px)`);
                    }
                } else {
                    results.differences.push({
                        type: 'text_layout_mismatch',
                        path: path,
                        radiant: { content: radiantNode.content, layout: radiantLayout },
                        browser: { content: browserNode.text, layout: browserLayout },
                        maxDifference: maxDiff,
                        maxTolerance: maxTolerance,
                    });
                    if (this.verbose) {
                        // Show per-property comparison for failed text nodes
                        const failedProps = layoutDiffs.filter(d => d.exceedsTolerance);
                        for (const d of failedProps) {
                            console.log(`${indent()}   ❌ ${d.property}: ${d.radiant.toFixed(1)} vs ${d.browser.toFixed(1)} (diff=${d.difference.toFixed(1)}px > tol=${d.tolerance.toFixed(1)}px)`);
                        }
                    }
                }
            } else {
                results.differences.push({
                    type: 'text_content_mismatch',
                    path: path,
                    radiant: { content: radiantNode.content },
                    browser: { content: browserNode.text }
                });
                if (this.verbose) {
                    console.log(`${indent()}   ❌ TEXT CONTENT FAIL`);
                }
            }
        } else if (!radiantIsText && !browserIsText) {
            // Both are elements - compare tags and layout
            results.totalElements++;

            const radiantTag = radiantNode.tag;
            const browserTag = browserNode.tag;

            if (this.verbose) {
                console.log(`${indent()}🏗️  Comparing elements: <${radiantTag}> vs <${browserTag}>`);
            }

            if (radiantTag !== browserTag) {
                results.differences.push({
                    type: 'tag_mismatch',
                    path: path,
                    radiant: radiantTag,
                    browser: browserTag
                });
                if (this.verbose) {
                    console.log(`${indent()}   ❌ TAG MISMATCH`);
                }
            } else {
                // Tags match, compare layout if both have layout info
                let layoutMatches = true;
                if (radiantNode.layout && browserNode.layout) {
                    const layoutDiffs = this.compareLayout(radiantNode.layout, browserNode.layout);
                    const maxDiff = layoutDiffs.length > 0 ? Math.max(...layoutDiffs.map(d => d.difference)) : 0;
                    const exceedsToleranceCount = layoutDiffs.filter(d => d.exceedsTolerance).length;

                    if (this.verbose) {
                        console.log(`${indent()}   Radiant: (${radiantNode.layout.x}, ${radiantNode.layout.y}, ${radiantNode.layout.width}×${radiantNode.layout.height})`);
                        console.log(`${indent()}   Browser: (${browserNode.layout.x}, ${browserNode.layout.y}, ${browserNode.layout.width}×${browserNode.layout.height})`);
                    }

                    const maxTolerance = Math.max(...layoutDiffs.map(d => d.tolerance)).toFixed(1);
                    if (exceedsToleranceCount === 0) {
                        if (this.verbose) {
                            console.log(`${indent()}   ✅ LAYOUT MATCH (${maxDiff.toFixed(1)}px diff <= ${maxTolerance}px)`);
                        }
                    } else {
                        layoutMatches = false;
                        results.differences.push({
                            type: 'layout_difference',
                            path: path,
                            tag: radiantTag,
                            radiant: radiantNode.layout,
                            browser: browserNode.layout,
                            differences: layoutDiffs,
                            maxDifference: maxDiff
                        });
                        if (this.verbose) {
                            console.log(`${indent()}   ❌ LAYOUT FAIL (${maxDiff.toFixed(1)}px > ${maxTolerance}px)`);
                        }
                    }
                }

                // Special handling for span elements: compare computed properties
                if (radiantTag === 'span') {
                    results.totalSpanElements++;

                    if (radiantNode.computed && browserNode.computed) {
                        const computedComparison = this.compareComputedProperties(radiantNode.computed, browserNode.computed);

                        results.spanComputedProperties.totalComparisons += computedComparison.totalProperties;
                        results.spanComputedProperties.totalMatches += computedComparison.matches;

                        if (this.verbose) {
                            console.log(`${indent()}   🎨 Computed properties: ${computedComparison.matches}/${computedComparison.totalProperties} matches`);

                            // Show property matches in verbose mode
                            for (const match of computedComparison.matchedProperties) {
                                console.log(`${indent()}     ✅ ${match.property}: ${match.normalizedRadiant}`);
                            }

                            // Show property mismatches
                            for (const diff of computedComparison.differences) {
                                const radiantDisplay = diff.normalizedRadiant === null ? 'undefined' : diff.normalizedRadiant;
                                const browserDisplay = diff.normalizedBrowser === null ? 'undefined' : diff.normalizedBrowser;
                                console.log(`${indent()}     ❌ ${diff.property}: "${radiantDisplay}" vs "${browserDisplay}"`);
                            }
                        }

                        // Store differences for detailed reporting
                        if (computedComparison.differences.length > 0) {
                            results.spanComputedProperties.differences.push({
                                path: path,
                                selector: radiantNode.selector,
                                differences: computedComparison.differences,
                                matches: computedComparison.matches,
                                total: computedComparison.totalProperties
                            });
                        }

                        // Consider span as matched if layout and most computed properties match
                        const computedPassRate = computedComparison.matches / computedComparison.totalProperties;
                        if (layoutMatches && computedPassRate >= 0.8) { // 80% computed property match threshold
                            results.matchedSpanElements++;
                            results.matchedElements++; // Also count toward general element matches
                            if (this.verbose) {
                                console.log(`${indent()}   ✅ SPAN MATCH (layout + ${(computedPassRate * 100).toFixed(1)}% computed)`);
                            }
                        } else {
                            if (this.verbose) {
                                const reason = !layoutMatches ? 'layout' : `computed ${(computedPassRate * 100).toFixed(1)}%`;
                                console.log(`${indent()}   ❌ SPAN FAIL (${reason})`);
                            }
                        }
                    } else {
                        // No computed properties to compare, use layout result only
                        if (layoutMatches) {
                            results.matchedSpanElements++;
                            results.matchedElements++; // Also count toward general element matches
                        }
                        if (this.verbose) {
                            console.log(`${indent()}   ⚠️  SPAN (no computed properties to compare)`);
                        }
                    }
                }

                // Count general element match (for non-span elements or when we can't do computed comparison)
                if (radiantTag !== 'span' && layoutMatches) {
                    results.matchedElements++;
                    if (this.verbose) {
                        console.log(`${indent()}   ✅ ELEMENT MATCH`);
                    }
                }
            }
        } else {
            // Type mismatch: text vs element
            if (radiantIsText) {
                results.totalTextNodes++;
            } else {
                results.totalElements++;
            }
            if (browserIsText) {
                results.totalTextNodes++;
            } else {
                results.totalElements++;
            }

            results.differences.push({
                type: 'node_type_mismatch',
                path: path,
                radiant: {
                    type: radiantIsText ? 'text' : 'element',
                    tag: radiantNode.tag || undefined,
                    content: radiantIsText ? (radiantNode.content || radiantNode.text) : undefined
                },
                browser: {
                    type: browserIsText ? 'text' : 'element',
                    tag: browserNode.tag || undefined,
                    content: browserIsText ? browserNode.text : undefined
                }
            });

            if (this.verbose) {
                const radiantDesc = radiantIsText ?
                    `text:"${(radiantNode.content || '').substring(0, 10)}..."` :
                    `<${radiantNode.tag}>`;
                const browserDesc = browserIsText ?
                    `text:"${(browserNode.text || '').substring(0, 10)}..."` :
                    `<${browserNode.tag}>`;
                console.log(`${indent()}⚠️  Comparing: radiant ${radiantDesc} vs. browser ${browserDesc}`);
                console.log(`${indent()}   ❌ TYPE MISMATCH`);
            }
        }

        // Get all children (elements + text nodes) in document order
        // Pass isRadiant=true for radiant nodes to flatten anonymous boxes
        const radiantChildren = this.filterForComparison(this.getAllChildren(radiantNode, true), true);
        const browserChildren = this.filterForComparison(this.getAllChildren(browserNode, false), false);

        const maxChildren = Math.max(radiantChildren.length, browserChildren.length);

        if (this.verbose && maxChildren > 0) {
            const currentNodeDesc = radiantIsText ?
                `text:"${(radiantNode.content || '').substring(0, 10)}..."` :
                (browserIsText ? `text:"${(browserNode.text || '').substring(0, 10)}..."` : `<${radiantNode?.tag || browserNode?.tag}>`);
            console.log(`${indent()}📁 Children of ${currentNodeDesc}: ${maxChildren} child nodes`);
        }

        for (let i = 0; i < maxChildren; i++) {
            const radiantChild = radiantChildren[i]?.node || null;
            const browserChild = browserChildren[i]?.node || null;
            const childType = radiantChildren[i]?.type || browserChildren[i]?.type || 'unknown';
            const childPath = path ? `${path} > ${childType}[${i}]` : `${childType}[${i}]`;

            // Pass current node's computed styles to children for text-align detection
            const currentComputed = radiantNode?.computed || browserNode?.computed || null;

            try {
                this.compareNodes(radiantChild, browserChild, childPath, results, depth + 1, currentComputed);
            } catch (childError) {
                // Log child comparison error but continue with other children
                results.differences.push({
                    type: 'comparison_error',
                    path: childPath,
                    error: childError.message,
                    radiant: radiantChild ? {
                        tag: radiantChild.tag,
                        type: radiantChild.type
                    } : null,
                    browser: browserChild ? {
                        tag: browserChild.tag,
                        type: browserChild.nodeType
                    } : null
                });

                if (this.verbose) {
                    console.log(`${indent()}⚠️  Error comparing child at ${childPath}: ${childError.message}`);
                }
            }
        }

        return results;
    }    /**
     * Compare text nodes between Radiant and browser (deprecated - now handled in unified comparison)
     */
    compareTextNodes(radiantTextNodes, browserTextNodes) {
        const textResults = {
            totalTextNodes: Math.max(radiantTextNodes.length, browserTextNodes.length),
            matchedTextNodes: 0,
            textDifferences: []
        };

        // Simple matching: try to match text nodes by content and position
        const usedBrowserIndices = new Set();

        for (let i = 0; i < radiantTextNodes.length; i++) {
            const radiantText = radiantTextNodes[i];
            let bestMatch = null;
            let bestMatchIndex = -1;
            let bestScore = Infinity;

            // Find best matching browser text node
            for (let j = 0; j < browserTextNodes.length; j++) {
                if (usedBrowserIndices.has(j)) continue;

                const browserText = browserTextNodes[j];

                // Calculate matching score based on content similarity and layout distance
                const contentMatch = radiantText.content.trim() === browserText.content.trim();
                const layoutDiffs = this.compareLayout(radiantText.layout, browserText.layout, true);
                const maxLayoutDiff = layoutDiffs.length > 0 ? Math.max(...layoutDiffs.map(d => d.difference)) : 0;

                const score = contentMatch ? maxLayoutDiff : 1000 + maxLayoutDiff;

                if (score < bestScore) {
                    bestScore = score;
                    bestMatch = browserText;
                    bestMatchIndex = j;
                }
            }

            if (bestMatch && bestScore <= this.tolerance) {
                textResults.matchedTextNodes++;
                usedBrowserIndices.add(bestMatchIndex);

                if (this.verbose && bestScore > 0) {
                    console.log(`    ✓ Text matched with ${bestScore.toFixed(1)}px diff: "${radiantText.content.substring(0, 30)}..."`);
                }
            } else {
                textResults.textDifferences.push({
                    type: 'text_mismatch',
                    radiant: radiantText,
                    browser: bestMatch,
                    score: bestScore
                });

                if (this.verbose) {
                    console.log(`    ✗ Text failed to match: "${radiantText.content.substring(0, 30)}..." (score: ${bestScore.toFixed(1)})`);
                }
            }
        }

        return textResults;
    }

    /**
     * Generate hierarchical report of comparison results
     */
    generateReport(results, textResults, testName, metadata = {}) {
        const report = {
            testName: testName,
            timestamp: new Date().toISOString(),
            htmlFile: metadata.htmlFile || null,
            resultFile: metadata.resultFile || null,
            viewFile: metadata.viewFile || null,
            elementComparison: {
                total: results.totalElements,
                matched: results.matchedElements,
                failed: results.totalElements - results.matchedElements,
                passRate: results.totalElements > 0 ? (results.matchedElements / results.totalElements * 100) : 0
            },
            textComparison: {
                total: results.totalTextNodes || 0,
                matched: results.matchedTextNodes || 0,
                failed: (results.totalTextNodes || 0) - (results.matchedTextNodes || 0),
                passRate: results.totalTextNodes > 0 ? (results.matchedTextNodes / results.totalTextNodes * 100) : 100
            },
            spanComparison: {
                total: results.totalSpanElements || 0,
                matched: results.matchedSpanElements || 0,
                failed: (results.totalSpanElements || 0) - (results.matchedSpanElements || 0),
                passRate: results.totalSpanElements > 0 ? (results.matchedSpanElements / results.totalSpanElements * 100) : 100,
                computedProperties: {
                    totalComparisons: results.spanComputedProperties.totalComparisons || 0,
                    totalMatches: results.spanComputedProperties.totalMatches || 0,
                    passRate: results.spanComputedProperties.totalComparisons > 0 ?
                        (results.spanComputedProperties.totalMatches / results.spanComputedProperties.totalComparisons * 100) : 100,
                    differences: results.spanComputedProperties.differences || []
                }
            },
            differences: results.differences,
            tolerance: this.tolerance
        };

        return report;
    }

    /**
     * Print hierarchical report to console
     */
    printReport(report) {
        if (this.json) return; // Skip console output in JSON mode

        // Overall result - include span information in summary
        const overallSuccess = report.elementComparison.passRate >= this.elementThreshold &&
                              report.textComparison.passRate >= this.textThreshold;
        const status = overallSuccess ? '✅ PASS' : '❌ FAIL';

        // For single test mode with failure, print detailed sectioned report
        if (this.singleTestMode && !overallSuccess) {
            this.printDetailedFailureReport(report);
            return;
        }

        console.log(`\n📊 Test Case: ${report.testName}`);
        // Only show detailed statistics in verbose mode
        if (this.verbose) {
            // Element comparison results
            console.log(`🏗️  Element Structure Comparison:`);
            console.log(`   Total Elements: ${report.elementComparison.total}`);
            console.log(`   ✅ Matched: ${report.elementComparison.matched} (${report.elementComparison.passRate.toFixed(1)}%)`);
            if (report.elementComparison.failed > 0) console.log(`   ❌ Failed: ${report.elementComparison.failed}`);

            // Span comparison results
            if (report.spanComparison.total > 0) {
                console.log(`🎨 Span Element Comparison:`);
                console.log(`   Total Spans: ${report.spanComparison.total}`);
                console.log(`   ✅ Matched: ${report.spanComparison.matched} (${report.spanComparison.passRate.toFixed(1)}%)`);
                if (report.spanComparison.failed > 0) console.log(`   ❌ Failed: ${report.spanComparison.failed}`);

                // Computed properties details
                if (report.spanComparison.computedProperties.totalComparisons > 0) {
                    console.log(`   📐 Computed Properties: ${report.spanComparison.computedProperties.totalMatches}/${report.spanComparison.computedProperties.totalComparisons} (${report.spanComparison.computedProperties.passRate.toFixed(1)}%)`);
                }
            }

            // Text comparison results
            if (report.textComparison.total > 0) {
                console.log(`📝 Text Node Comparison:`);
                console.log(`   Total Text Nodes: ${report.textComparison.total}`);
                console.log(`   ✅ Matched: ${report.textComparison.matched} (${report.textComparison.passRate.toFixed(1)}%)`);
                if (report.textComparison.failed > 0) console.log(`   ❌ Failed: ${report.textComparison.failed}`);
            }
            console.log('');
        }

        let summaryText = `${status} Overall: Elements ${report.elementComparison.passRate.toFixed(1)}%`;
        if (report.spanComparison.total > 0) {
            summaryText += `, Spans ${report.spanComparison.passRate.toFixed(1)}%`;
        }
        summaryText += `, Text ${report.textComparison.passRate.toFixed(1)}%`;

        console.log(summaryText);
    }

    /**
     * Print detailed sectioned failure report for single test mode
     * Shows comprehensive debugging information when a test fails
     */
    printDetailedFailureReport(report) {
        const line = (char = '=', len = 80) => char.repeat(len);
        const c = {
            reset: '\x1b[0m',
            red: '\x1b[31m',
            green: '\x1b[32m',
            yellow: '\x1b[33m',
            blue: '\x1b[34m',
            cyan: '\x1b[36m',
            bold: '\x1b[1m',
            dim: '\x1b[2m',
        };

        console.log();
        console.log(line('='));
        console.log(`${c.bold}${c.red}LAYOUT TEST FAILED: ${report.testName}${c.reset}`);
        console.log(line('='));
        console.log();

        // Section 1: Test Overview
        console.log(`${c.bold}[TEST OVERVIEW]${c.reset}`);
        console.log(line('-', 40));
        console.log(`├─ Test: ${report.htmlFile || report.testName}`);
        console.log(`├─ Result: ${report.resultFile || '/tmp/view_tree.json'}`);
        console.log(`├─ View: ${report.viewFile || '/tmp/view_tree.txt'}`);
        console.log(`├─ Tolerance: ${report.tolerance}px`);
        console.log(`└─ Reference: test/layout/reference/${report.testName}.json`);
        console.log();

        // Section 2: Match Statistics
        console.log(`${c.bold}[MATCH STATISTICS]${c.reset}`);
        console.log(line('-', 40));

        const elemStatus = report.elementComparison.passRate >= this.elementThreshold ? `${c.green}✓${c.reset}` : `${c.red}✗${c.reset}`;
        const textStatus = report.textComparison.passRate >= this.textThreshold ? `${c.green}✓${c.reset}` : `${c.red}✗${c.reset}`;

        console.log(`├─ ${elemStatus} Elements: ${report.elementComparison.matched}/${report.elementComparison.total} (${report.elementComparison.passRate.toFixed(1)}%)`);
        if (report.spanComparison.total > 0) {
            const spanStatus = report.spanComparison.passRate >= 80 ? `${c.green}✓${c.reset}` : `${c.red}✗${c.reset}`;
            console.log(`├─ ${spanStatus} Spans: ${report.spanComparison.matched}/${report.spanComparison.total} (${report.spanComparison.passRate.toFixed(1)}%)`);
        }
        console.log(`└─ ${textStatus} Text Nodes: ${report.textComparison.matched}/${report.textComparison.total} (${report.textComparison.passRate.toFixed(1)}%)`);
        console.log();

        // Section 3: Element Differences (the most important debugging info)
        if (report.differences && report.differences.length > 0) {
            console.log(`${c.bold}[ELEMENT DIFFERENCES]${c.reset}`);
            console.log(line('-', 40));

            // Group differences by type (handle both 'layout_mismatch' and 'layout_difference')
            const layoutDiffs = report.differences.filter(d =>
                d.type === 'layout_mismatch' || d.type === 'layout_difference');
            const missingNodes = report.differences.filter(d => d.type === 'missing_node');
            const structureDiffs = report.differences.filter(d => d.type === 'structure_mismatch');

            // Show layout mismatches with detailed comparison
            if (layoutDiffs.length > 0) {
                console.log(`\n${c.yellow}Layout Mismatches (${layoutDiffs.length}):${c.reset}`);

                // Sort by largest difference for priority debugging
                const sortedDiffs = [...layoutDiffs].sort((a, b) => {
                    // Use maxDifference if available, otherwise calculate from differences array
                    const maxDiffA = a.maxDifference || Math.max(...(a.differences || []).map(d => Math.abs(d.diff || 0)), 0);
                    const maxDiffB = b.maxDifference || Math.max(...(b.differences || []).map(d => Math.abs(d.diff || 0)), 0);
                    return maxDiffB - maxDiffA;
                });

                // Show top 10 or fewer
                const showCount = Math.min(sortedDiffs.length, 10);
                for (let i = 0; i < showCount; i++) {
                    const diff = sortedDiffs[i];
                    const tag = diff.tag || diff.radiant?.tag || diff.browser?.tag || 'unknown';
                    const selector = tag;

                    console.log(`\n  ${c.red}✗${c.reset} ${c.bold}${selector}${c.reset} ${c.dim}(${diff.path})${c.reset}`);

                    // Handle the differences array (which contains layout property diffs)
                    // Layout diffs have: property, radiant, browser, difference, exceedsTolerance, tolerance
                    const propDiffs = diff.differences || diff.layoutDiffs || [];
                    for (const ld of propDiffs) {
                        const propName = ld.property || ld.prop || 'unknown';
                        const diffVal = ld.difference ?? ld.diff ?? 0;
                        const exceedsTol = ld.exceedsTolerance !== undefined ? ld.exceedsTolerance :
                            (Math.abs(diffVal) > (ld.tolerance || report.tolerance || 5));
                        if (exceedsTol) {
                            const diffStr = diffVal > 0 ? `+${diffVal.toFixed(1)}` : diffVal.toFixed(1);
                            const radVal = ld.radiant ?? ld.lambda ?? 0;
                            const browVal = ld.browser ?? 0;
                            console.log(`      ${propName.padEnd(8)}: Lambda=${radVal.toFixed(1).padStart(8)}  Browser=${browVal.toFixed(1).padStart(8)}  ${c.red}Δ ${diffStr}${c.reset}`);
                        }
                    }
                }

                if (sortedDiffs.length > showCount) {
                    console.log(`\n  ${c.dim}... and ${sortedDiffs.length - showCount} more layout mismatches${c.reset}`);
                }
            }

            // Show missing nodes
            if (missingNodes.length > 0) {
                console.log(`\n${c.yellow}Missing/Extra Nodes (${missingNodes.length}):${c.reset}`);
                const showCount = Math.min(missingNodes.length, 5);
                for (let i = 0; i < showCount; i++) {
                    const diff = missingNodes[i];
                    if (diff.radiant && !diff.browser) {
                        console.log(`  ${c.yellow}+${c.reset} Extra in Lambda: ${diff.radiant.tag || 'text'} at ${diff.path}`);
                    } else if (!diff.radiant && diff.browser) {
                        console.log(`  ${c.red}-${c.reset} Missing in Lambda: ${diff.browser.tag || 'text'} at ${diff.path}`);
                    }
                }
                if (missingNodes.length > showCount) {
                    console.log(`  ${c.dim}... and ${missingNodes.length - showCount} more${c.reset}`);
                }
            }

            // Show structure mismatches
            if (structureDiffs.length > 0) {
                console.log(`\n${c.yellow}Structure Mismatches (${structureDiffs.length}):${c.reset}`);
                const showCount = Math.min(structureDiffs.length, 5);
                for (let i = 0; i < showCount; i++) {
                    const diff = structureDiffs[i];
                    console.log(`  ${c.red}✗${c.reset} ${diff.path}: ${diff.message || 'Structure differs'}`);
                }
            }
            console.log();
        }

        // Section 4: Top Differences Summary (quick reference)
        if (report.differences && report.differences.length > 0) {
            const layoutDiffsForTop = report.differences.filter(d =>
                (d.type === 'layout_mismatch' || d.type === 'layout_difference') &&
                (d.differences || d.layoutDiffs));
            if (layoutDiffsForTop.length > 0) {
                console.log(`${c.bold}[TOP DIFFERENCES BY SIZE]${c.reset}`);
                console.log(line('-', 40));

                // Flatten all property differences with their element context
                const allPropDiffs = [];
                for (const diff of layoutDiffsForTop) {
                    const tag = diff.tag || diff.radiant?.tag || diff.browser?.tag || 'unknown';
                    const selector = tag;

                    // Layout diffs have: property, radiant, browser, difference, exceedsTolerance, tolerance
                    const propDiffs = diff.differences || diff.layoutDiffs || [];
                    for (const ld of propDiffs) {
                        const propName = ld.property || ld.prop || 'unknown';
                        const diffVal = ld.difference ?? ld.diff ?? 0;
                        const exceedsTol = ld.exceedsTolerance !== undefined ? ld.exceedsTolerance :
                            (Math.abs(diffVal) > (ld.tolerance || report.tolerance || 5));
                        if (exceedsTol && diffVal !== 0) {
                            allPropDiffs.push({
                                selector,
                                prop: propName,
                                diff: diffVal,
                                radiant: ld.radiant ?? ld.lambda ?? 0,
                                browser: ld.browser ?? 0
                            });
                        }
                    }
                }

                // Sort by absolute difference
                allPropDiffs.sort((a, b) => Math.abs(b.diff) - Math.abs(a.diff));

                // Show top 5
                const topCount = Math.min(allPropDiffs.length, 5);
                for (let i = 0; i < topCount; i++) {
                    const d = allPropDiffs[i];
                    const diffStr = d.diff > 0 ? `+${d.diff.toFixed(1)}` : d.diff.toFixed(1);
                    console.log(`  ${i + 1}. ${d.selector}.${d.prop}: ${c.red}${diffStr}px${c.reset} (${d.radiant.toFixed(1)} vs ${d.browser.toFixed(1)})`);
                }
                console.log();
            }
        }

        // Final summary line
        console.log(line('='));
        console.log(`${c.bold}${c.red}RESULT: FAILED${c.reset} - Elements ${report.elementComparison.passRate.toFixed(1)}%, Text ${report.textComparison.passRate.toFixed(1)}%`);
        console.log(line('='));
        console.log();
    }

    /**
     * Test a single HTML file
     * @param {string} htmlFile - Path to the HTML file
     * @param {string} category - Test category name
     * @param {string} outputFile - Optional unique output file for parallel execution
     */
    async testSingleFile(htmlFile, category, outputFile = null) {
        // Handle both .html and .htm extensions
        const ext = htmlFile.endsWith('.htm') && !htmlFile.endsWith('.html') ? '.htm' : '.html';
        const testName = path.basename(htmlFile, ext);
        const testFileName = path.basename(htmlFile);
        // console.log(`\n🧪 Testing: ${testName}`);

        try {
            // Run Radiant layout with optional unique output file
            const layoutResult = await this.runRadiantLayout(htmlFile, outputFile);
            const actualOutputFile = layoutResult.outputFile || this.outputFile;
            const radiantData = await this.loadRadiantOutput(testFileName, actualOutputFile);

            // Clean up unique output file if used
            if (outputFile) {
                try {
                    await fs.unlink(outputFile);
                } catch (e) {
                    // Ignore cleanup errors
                }
            }

            // Load browser reference
            const browserData = await this.loadBrowserReference(testName, category);
            if (!browserData) {
                console.log(`   ⚠️  No browser reference found for ${testName}`);
                return null;
            }

            // Compare tree structures with unified element and text comparison
            const radiantTree = radiantData.layout_tree;
            const browserTree = browserData.layout_tree;

            if (this.verbose) {
                console.log(`\n🔍 Starting hierarchical comparison:`);
            }

            let results;
            try {
                results = this.compareNodes(radiantTree, browserTree, 'root', null, 0);
            } catch (compareError) {
                // Handle comparison errors gracefully - still report what we can
                console.log(`\n📊 Test Case: ${testName}`);
                console.log(`❌ FAIL Overall: Error during hierarchical comparison`);
                console.log(`   💥 COMPARISON ERROR: ${compareError.message}`);
                if (this.verbose && compareError.stack) {
                    console.log(`   📍 Stack trace:`);
                    const stackLines = compareError.stack.split('\n').slice(1, 6);
                    stackLines.forEach(line => console.log(`      ${line.trim()}`));
                }

                return {
                    testName,
                    testFile: testFileName,
                    error: `Comparison error: ${compareError.message}`,
                    timestamp: new Date().toISOString()
                };
            }

            // Generate and print report (no separate text results needed)
            const metadata = {
                htmlFile: htmlFile,
                resultFile: actualOutputFile,
                viewFile: actualOutputFile.replace('.json', '.txt')
            };
            const report = this.generateReport(results, null, testName, metadata);
            this.printReport(report);

            return report;

        } catch (error) {
            // Enhanced error reporting with test file context
            const testFileInfo = `${testFileName} (${category}/${testName})`;

            // Print error in FAIL format for consistency
            console.log(`\n📊 Test Case: ${testName}`);
            console.log(`❌ FAIL Overall: Error during test execution`);
            console.log(`   💥 ERROR: ${error.message}`);

            // Show stack trace in verbose mode for better debugging
            if (this.verbose && error.stack) {
                console.log(`   📍 Stack trace:`);
                const stackLines = error.stack.split('\n').slice(1, 8);
                stackLines.forEach(line => console.log(`      ${line.trim()}`));
            }

            // If it's a JSON parsing error and in verbose mode, show more context
            if (this.verbose && error instanceof SyntaxError) {
                try {
                    const stats = await fs.stat(this.outputFile);
                    console.log(`   📄 Output file size: ${stats.size} bytes`);

                    // Show first few lines of the problematic JSON for debugging
                    if (stats.size > 0 && stats.size < 10000) { // Only for small files
                        const content = await fs.readFile(this.outputFile, 'utf8');
                        const lines = content.split('\n').slice(0, 5);
                        console.log(`   📝 First few lines of output:`);
                        lines.forEach((line, i) => {
                            console.log(`      ${i + 1}: ${line}`);
                        });
                        if (content.split('\n').length > 5) {
                            console.log(`      ... (truncated)`);
                        }
                    }
                } catch (statError) {
                    console.log(`   ⚠️  Could not read output file info: ${statError.message}`);
                }
            }

            return {
                testName,
                testFile: testFileInfo,
                error: error.message,
                timestamp: new Date().toISOString()
            };
        }
    }

    /**
     * Compare a single test result against browser reference (used in batch mode)
     * @param {string} htmlFile - Path to the HTML file
     * @param {string} category - Test category name
     * @param {string} outputFile - Path to the layout output JSON file
     */
    async compareTestResult(htmlFile, category, outputFile) {
        const ext = htmlFile.endsWith('.htm') && !htmlFile.endsWith('.html') ? '.htm' : '.html';
        const testName = path.basename(htmlFile, ext);
        const testFileName = path.basename(htmlFile);

        try {
            // Load the layout result
            const radiantData = await this.loadRadiantOutput(testFileName, outputFile);

            // Load browser reference
            const browserData = await this.loadBrowserReference(testName, category);
            if (!browserData) {
                console.log(`   ⚠️  No browser reference found for ${testName}`);
                return null;
            }

            // Compare tree structures
            const radiantTree = radiantData.layout_tree;
            const browserTree = browserData.layout_tree;

            let results;
            try {
                results = this.compareNodes(radiantTree, browserTree, 'root', null, 0);
            } catch (compareError) {
                console.log(`\n📊 Test Case: ${testName}`);
                console.log(`❌ FAIL Overall: Error during hierarchical comparison`);
                console.log(`   💥 COMPARISON ERROR: ${compareError.message}`);
                return {
                    testName,
                    testFile: testFileName,
                    error: `Comparison error: ${compareError.message}`,
                    timestamp: new Date().toISOString()
                };
            }

            // Generate and print report
            const metadata = {
                htmlFile: htmlFile,
                resultFile: outputFile,
                viewFile: outputFile.replace('.json', '.txt')
            };
            const report = this.generateReport(results, null, testName, metadata);
            this.printReport(report);

            return report;

        } catch (error) {
            const testFileInfo = `${testFileName} (${category}/${testName})`;
            console.log(`\n📊 Test Case: ${testName}`);
            console.log(`❌ FAIL Overall: Error during test comparison`);
            console.log(`   💥 ERROR: ${error.message}`);

            return {
                testName,
                testFile: testFileInfo,
                error: error.message,
                timestamp: new Date().toISOString()
            };
        }
    }

    /**
     * Run tests using batch layout mode.
     * Runs layout on multiple files at once, then compares results.
     * @param {Array} testTasks - Array of {htmlFile, category} objects
     * @returns {Array} - Array of test results
     */
    async runTestsInBatchMode(testTasks) {
        const results = [];
        const batchSize = this.batchSize;

        if (this.verbose) {
            console.log(`\n🚀 Batch mode: processing ${testTasks.length} files in batches of ${batchSize}`);
        }

        // Process in batches
        for (let i = 0; i < testTasks.length; i += batchSize) {
            const batch = testTasks.slice(i, i + batchSize);
            const htmlFiles = batch.map(task => task.htmlFile);

            if (this.verbose) {
                console.log(`\n📦 Batch ${Math.floor(i / batchSize) + 1}: ${batch.length} files`);
            }

            // Run batch layout
            const outputMap = await this.runBatchLayout(htmlFiles);

            // Compare each result against reference (in parallel)
            const comparePromises = batch.map(async (task) => {
                const outputFile = outputMap.get(task.htmlFile);
                return this.compareTestResult(task.htmlFile, task.category, outputFile);
            });

            const batchResults = await Promise.all(comparePromises);
            results.push(...batchResults.filter(r => r !== null));
        }

        return results;
    }

    /**
     * Run tests in parallel with a dynamic pool of up to maxConcurrency tests
     * If batchSize is set, uses batch mode for better performance.
     * @param {Array} testTasks - Array of {htmlFile, category} objects
     * @returns {Array} - Array of test results
     */
    async runTestsInPool(testTasks) {
        // Use batch mode if batchSize is configured
        if (this.batchSize > 0) {
            return this.runTestsInBatchMode(testTasks);
        }

        // Original single-file parallel mode
        const results = [];
        const pool = new Map(); // Map of testId -> Promise
        let taskIndex = 0;

        // Helper to start a new test and add it to the pool
        const startNextTest = () => {
            if (taskIndex >= testTasks.length) return null;

            const task = testTasks[taskIndex++];
            const outputFile = this.getUniqueOutputFile();
            const testId = this.testIdCounter;

            const testPromise = this.testSingleFile(task.htmlFile, task.category, outputFile)
                .then(result => ({ testId, result }))
                .catch(error => ({ testId, result: { error: error.message } }));

            pool.set(testId, testPromise);
            return testPromise;
        };

        // Fill the initial pool up to maxConcurrency
        while (pool.size < this.maxConcurrency && taskIndex < testTasks.length) {
            startNextTest();
        }

        // Process tests as they complete, maintaining pool size
        while (pool.size > 0) {
            // Wait for any test in the pool to complete
            const completed = await Promise.race(pool.values());

            // Remove completed test from pool
            pool.delete(completed.testId);

            // Store result
            if (completed.result) {
                results.push(completed.result);
            }

            // Start next test if there are more tasks
            if (taskIndex < testTasks.length) {
                startNextTest();
            }
        }

        return results;
    }

    /**
     * Test all files in a category
     */
    async testCategory(category) {
        // Skip css2.1 suite
        if (category === 'css2.1') {
            console.log(`\n⚠️  Skipping css2.1 suite (excluded from testing)`);
            return [];
        }

        if (!this.json) {
            console.log(`\n📂 Testing category: ${category}`);
            console.log('=' .repeat(50));
        }

        const categoryDir = path.join(this.testDataDir, category);

        try {
            const files = await fs.readdir(categoryDir);
            const htmlFiles = files.filter(file => file.endsWith('.html') || file.endsWith('.htm'));

            if (htmlFiles.length === 0) {
                console.log(`No HTML files found in ${category}/`);
                return;
            }

            // Prepare test tasks
            const testTasks = htmlFiles.map(htmlFile => ({
                htmlFile: path.join(categoryDir, htmlFile),
                category: category
            }));

            // Run tests in parallel with pool management
            const results = await this.runTestsInPool(testTasks);

            let errorCount = 0;
            const errorFiles = [];
            for (const result of results) {
                if (result && result.error) {
                    errorCount++;
                    errorFiles.push(result.testFile || result.testName);
                }
            }

            // Summary - properly count passed/failed tests based on pass rates
            const failedTests = [];
            const successful = results.filter(r => {
                if (r.error) {
                    failedTests.push({ name: r.testName || r.testFile, reason: `Error: ${r.error}` });
                    return false; // Tests with errors are failures
                }
                // Use same criteria as printReport: configurable element/text thresholds
                const elementPassRate = r.elementComparison ? r.elementComparison.passRate : 0;
                const textPassRate = r.textComparison ? r.textComparison.passRate : 100;
                const passed = elementPassRate >= this.elementThreshold && textPassRate >= this.textThreshold;
                if (!passed) {
                    failedTests.push({
                        name: r.testName || r.testFile,
                        reason: `Elements ${elementPassRate.toFixed(1)}%` +
                               (textPassRate < this.textThreshold ? `, Text ${textPassRate.toFixed(1)}%` : '')
                    });
                }
                return passed;
            }).length;
            const failed = results.length - successful;

            if (this.json) {
                // Output results as JSON for GTest integration
                console.log(JSON.stringify({
                    total: results.length,
                    successful: successful,
                    failed: failed,
                    errors: errorCount,
                    results: results.map(r => ({
                        name: r.testName || r.testFile,
                        passed: !r.error && (r.elementComparison?.passRate || 0) >= this.elementThreshold && (r.textComparison?.passRate || 100) >= this.textThreshold,
                        elementPassRate: r.elementComparison?.passRate || 0,
                        textPassRate: r.textComparison?.passRate || 100,
                        error: r.error || null
                    }))
                }, null, 2));
            } else {
                console.log(`\n📋 Category Summary:`);
                console.log(`   Total Tests: ${results.length}`);
                console.log(`   ✅ Successful: ${successful}`);
                if (failed > 0) console.log(`   ❌ Failed: ${failed}`);
                if (errorCount > 0) {
                    console.log(`   💥 Errors: ${errorCount}`);
                    console.log(`   📄 Files with errors:`);
                    errorFiles.forEach(file => console.log(`      - ${file}`));
                }

                // List failed tests at the end (only if 10 or fewer)
                if (failedTests.length > 0 && failedTests.length <= 10) {
                    console.log(`\n❌ Failed Tests:`);
                    failedTests.forEach(test => {
                        console.log(`   - ${test.name}: ${test.reason}`);
                    });
                } else if (failedTests.length > 10) {
                    console.log(`\n⚠️  Too many failures (${failedTests.length}) - detailed list omitted`);
                }
            }

            return results;

        } catch (error) {
            console.log(`   ❌ Error reading category directory: ${error.message}`);
            return [];
        }
    }

    /**
     * Get available test categories
     */
    async getAvailableCategories() {
        try {
            const items = await fs.readdir(this.testDataDir, { withFileTypes: true });
            return items
                .filter(item => item.isDirectory())
                .map(item => item.name)
                .filter(name => !name.startsWith('.'))
                .filter(name => name !== 'css2.1') // Skip css2.1 suite
                .sort();
        } catch (error) {
            console.error(`Error scanning test categories: ${error.message}`);
            return [];
        }
    }

    /**
     * Test files matching a pattern across all categories (or a specific category)
     * @param {string} pattern - Pattern to match in file names
     * @param {string} filterCategory - Optional category to limit search to
     */
    async testByPattern(pattern, filterCategory = null) {
        const engineName = this.engine === 'lambda-css' ? 'Lambda CSS' : 'Radiant';
        const categoryMsg = filterCategory ? ` in category "${filterCategory}"` : '';
        console.log(`\n🔍 Testing files matching pattern: "${pattern}"${categoryMsg} (${engineName} engine)`);
        console.log('=' .repeat(50));

        let categories = await this.getAvailableCategories();
        if (filterCategory) {
            categories = categories.filter(c => c === filterCategory);
            if (categories.length === 0) {
                console.log(`\n⚠️  Category "${filterCategory}" not found`);
                return [];
            }
        }
        const allResults = [];
        let totalMatches = 0;

        for (const category of categories) {
            const categoryDir = path.join(this.testDataDir, category);

            try {
                const files = await fs.readdir(categoryDir);
                const matchingFiles = files.filter(file =>
                    (file.endsWith('.html') || file.endsWith('.htm')) && file.includes(pattern)
                );

                if (matchingFiles.length > 0) {
                    console.log(`\n📂 Found ${matchingFiles.length} matching files in ${category}:`);
                    matchingFiles.forEach(f => console.log(`   🎯 ${f}`));

                    // Prepare test tasks for this category
                    const testTasks = matchingFiles.map(htmlFile => ({
                        htmlFile: path.join(categoryDir, htmlFile),
                        category: category
                    }));

                    // Run tests in parallel with pool management
                    const categoryResults = await this.runTestsInPool(testTasks);
                    for (const result of categoryResults) {
                        if (result) {
                            allResults.push(result);
                            totalMatches++;
                        }
                    }
                }
            } catch (error) {
                console.log(`   ⚠️  Error reading category ${category}: ${error.message}`);
            }
        }

        if (totalMatches === 0) {
            console.log(`\n⚠️  No files found matching pattern "${pattern}"`);
            return [];
        }

        // Summary
        const failedTests = [];
        const successful = allResults.filter(r => {
            if (r.error) {
                failedTests.push({ name: r.testName || r.testFile, reason: `Error: ${r.error}` });
                return false;
            }
            const elementPassRate = r.elementComparison ? r.elementComparison.passRate : 0;
            const textPassRate = r.textComparison ? r.textComparison.passRate : 100;
            const passed = elementPassRate >= this.elementThreshold && textPassRate >= this.textThreshold;
            if (!passed) {
                failedTests.push({
                    name: r.testName || r.testFile,
                    reason: `Elements ${elementPassRate.toFixed(1)}%` +
                           (textPassRate < this.textThreshold ? `, Text ${textPassRate.toFixed(1)}%` : '')
                });
            }
            return passed;
        }).length;
        const failed = allResults.length - successful;

        console.log(`\n📋 Pattern Matching Summary:`);
        console.log(`   Pattern: "${pattern}"`);
        console.log(`   Total Matches: ${totalMatches}`);
        console.log(`   ✅ Successful: ${successful}`);
        if (failed > 0) console.log(`   ❌ Failed: ${failed}`);
        console.log(`   Success Rate: ${allResults.length > 0 ? (successful / allResults.length * 100).toFixed(1) : 0}%`);

        // List failed tests at the end (only if 10 or fewer)
        if (failedTests.length > 0 && failedTests.length <= 10) {
            console.log(`\n❌ Failed Tests:`);
            failedTests.forEach(test => {
                console.log(`   - ${test.name}: ${test.reason}`);
            });
        } else if (failedTests.length > 10) {
            console.log(`\n⚠️  Too many failures (${failedTests.length}) - detailed list omitted`);
        }
        return allResults;
    }

    /**
     * Test all categories
     */
    async testAll() {
        const engineName = this.engine === 'lambda-css' ? 'Lambda CSS' : 'Radiant';
        console.log(`🎯 ${engineName} Layout Engine Automated Tests`);
        console.log('=========================================');

        const categories = await this.getAvailableCategories();
        console.log(`Found categories: ${categories.join(', ')}`);

        const allResults = [];
        for (const category of categories) {
            const categoryResults = await this.testCategory(category);
            allResults.push(...categoryResults);
        }

        // Overall summary - properly count passed/failed tests based on pass rates
        const failedTests = [];
        const successful = allResults.filter(r => {
            if (r.error) {
                failedTests.push({ name: r.testName || r.testFile, reason: `Error: ${r.error}` });
                return false; // Tests with errors are failures
            }
            // Use same criteria as printReport: configurable element/text thresholds
            const elementPassRate = r.elementComparison ? r.elementComparison.passRate : 0;
            const textPassRate = r.textComparison ? r.textComparison.passRate : 100;
            const passed = elementPassRate >= this.elementThreshold && textPassRate >= this.textThreshold;
            if (!passed) {
                failedTests.push({
                    name: r.testName || r.testFile,
                    reason: `Elements ${elementPassRate.toFixed(1)}%` +
                           (textPassRate < this.textThreshold ? `, Text ${textPassRate.toFixed(1)}%` : '')
                });
            }
            return passed;
        }).length;
        const failed = allResults.length - successful;

        if (this.json) {
            // Output results as JSON for GTest integration
            console.log(JSON.stringify({
                total: allResults.length,
                successful: successful,
                failed: failed,
                results: allResults.map(r => ({
                    name: r.testName || r.testFile,
                    passed: !r.error && (r.elementComparison?.passRate || 0) >= this.elementThreshold && (r.textComparison?.passRate || 100) >= this.textThreshold,
                    elementPassRate: r.elementComparison?.passRate || 0,
                    textPassRate: r.textComparison?.passRate || 100,
                    error: r.error || null
                }))
            }, null, 2));
        } else {
            console.log(`\n🎯 OVERALL SUMMARY`);
            console.log('==================');
            console.log(`Total Tests: ${allResults.length}`);
            console.log(`✅ Successful: ${successful}`);
            console.log(`❌ Failed: ${failed}`);
            console.log(`Success Rate: ${allResults.length > 0 ? (successful / allResults.length * 100).toFixed(1) : 0}%`);

            // List failed tests at the end (only if 10 or fewer)
            if (failedTests.length > 0 && failedTests.length <= 10) {
                console.log(`\n❌ Failed Tests:`);
                failedTests.forEach(test => {
                    console.log(`   - ${test.name}: ${test.reason}`);
                });
            } else if (failedTests.length > 10) {
                console.log(`\n⚠️  Too many failures (${failedTests.length}) - detailed list omitted`);
            }
        }

        return allResults;
    }
}

// Main execution
async function main() {
    const args = process.argv.slice(2);

    // Parse arguments
    const options = {
        tolerance: 5.2,
        verbose: false,
        engine: 'radiant', // default to radiant engine
        json: false, // JSON output mode
        maxConcurrency: 5, // Max parallel tests
        batchSize: 20 // Default batch size for layout (20 files at once)
    };

    let category = null;
    let testFile = null;
    let pattern = null;
    let showHelp = false;

    for (let i = 0; i < args.length; i++) {
        const arg = args[i];
        switch (arg) {
            case '--help':
            case '-h':
                showHelp = true;
                break;
            case '--engine':
            case '-e':
                options.engine = args[++i];
                if (options.engine !== 'radiant' && options.engine !== 'lambda-css') {
                    console.error(`Invalid engine: ${options.engine}. Must be 'radiant' or 'lambda-css'`);
                    process.exit(1);
                }
                break;
            case '--category':
            case '-c':
                category = args[++i];
                break;
            case '--test':
            case '-t':
                testFile = args[++i];
                break;
            case '--pattern':
            case '-p':
                pattern = args[++i];
                break;
            case '--tolerance':
                options.tolerance = parseFloat(args[++i]);
                break;
            case '--verbose':
            case '-v':
                options.verbose = true;
                break;
            case '--radiant-exe':
                options.radiantExe = args[++i];
                break;
            case '--element-threshold':
                options.elementThreshold = parseFloat(args[++i]);
                break;
            case '--text-threshold':
                options.textThreshold = parseFloat(args[++i]);
                break;
            case '--json':
                options.json = true;
                break;
            case '--concurrency':
            case '-j':
                options.maxConcurrency = parseInt(args[++i], 10);
                if (isNaN(options.maxConcurrency) || options.maxConcurrency < 1) {
                    console.error('Invalid concurrency value. Must be a positive integer.');
                    process.exit(1);
                }
                break;
            case '--batch-size':
            case '-b':
                options.batchSize = parseInt(args[++i], 10);
                if (isNaN(options.batchSize) || options.batchSize < 0) {
                    console.error('Invalid batch size. Must be 0 (disabled) or a positive integer.');
                    process.exit(1);
                }
                break;
            case '--no-batch':
                options.batchSize = 0; // Disable batch mode
                break;
            default:
                console.error(`Unknown argument: ${arg}`);
                showHelp = true;
        }
    }

    if (showHelp) {
        console.log(`
Radiant Layout Engine Automated Test Script

Usage: node test/layout/test_radiant_layout.js [options]

Options:
  --engine, -e <name>      Layout engine to use: 'lambda-css' (default: lambda-css)
  --category, -c <name>    Test specific category (e.g., basic, flex, grid)
  --test, -t <file>        Test specific HTML file
  --pattern, -p <text>     Test files containing pattern (runs in verbose mode)
  --tolerance <pixels>     Layout difference tolerance in pixels (default: 5.0)
  --element-threshold <pct> Element match threshold percentage (default: 80.0)
  --text-threshold <pct>   Text match threshold percentage (default: 70.0)
  --concurrency, -j <n>    Number of parallel tests to run (default: 5)
  --batch-size, -b <n>     Batch size for layout processing (default: 20, 0 to disable)
  --no-batch               Disable batch mode (same as --batch-size 0)
  --verbose, -v            Show detailed output
  --json                   Output results in JSON format
  --radiant-exe <path>     Path to layout engine executable (default: ./lambda.exe)
  --help, -h               Show this help message

Batch Mode:
  By default, tests are processed in batches of 20 files to improve performance.
  The layout engine's UiContext is initialized once per batch, reducing overhead.
  Use --no-batch to disable batch mode and process files individually.

Engines:
  lambda-css   - Lambda CSS layout engine (custom CSS cascade and layout)

Examples:
  node test/layout/test_radiant_layout.js                              # Test all categories with Lambda CSS
  node test/layout/test_radiant_layout.js --engine lambda-css          # Test all categories with Lambda CSS
  node test/layout/test_radiant_layout.js -e lambda-css -c baseline    # Test baseline category with Lambda CSS
  node test/layout/test_radiant_layout.js -c baseline                  # Test baseline category with Radiant
  node test/layout/test_radiant_layout.js -t baseline_801_display_block.html  # Test specific file
  node test/layout/test_radiant_layout.js -p float                     # Test all files containing "float"
  node test/layout/test_radiant_layout.js --tolerance 2.0              # Use 2px tolerance
  node test/layout/test_radiant_layout.js --element-threshold 90       # Require 90% element match
  node test/layout/test_radiant_layout.js --text-threshold 80          # Require 80% text match
  node test/layout/test_radiant_layout.js -j 10                        # Run 10 tests in parallel
  node test/layout/test_radiant_layout.js -b 50                        # Process layout in batches of 50
  node test/layout/test_radiant_layout.js --no-batch                   # Disable batch mode
  node test/layout/test_radiant_layout.js -v                           # Verbose output

Note: Run this script from the project root directory.
`);
        process.exit(0);
    }

    // Enable single test mode when testing a specific file (for detailed failure reports)
    if (testFile) {
        options.singleTestMode = true;
    }

    const tester = new RadiantLayoutTester(options);

    try {
        if (testFile) {
            // Find the test file in available categories
            const categories = await tester.getAvailableCategories();
            let foundFile = null;
            let foundCategory = null;

            for (const cat of categories) {
                const filePath = path.join(tester.testDataDir, cat, testFile);
                try {
                    await fs.access(filePath);
                    foundFile = filePath;
                    foundCategory = cat;
                    break;
                } catch (error) {
                    // File not found in this category, continue
                }
            }

            if (foundFile) {
                await tester.testSingleFile(foundFile, foundCategory);
            } else {
                console.error(`Test file '${testFile}' not found in any category`);
                process.exit(1);
            }

        } else if (pattern) {
            // Force verbose mode for pattern matching
            tester.verbose = true;
            // Pass category filter if specified
            await tester.testByPattern(pattern, category || null);

        } else if (category) {
            await tester.testCategory(category);
        } else {
            await tester.testAll();
        }
    } catch (error) {
        console.error('Test execution failed:', error.message);
        process.exit(1);
    }
}

// Run if called directly
if (require.main === module) {
    main().catch(console.error);
}

module.exports = { RadiantLayoutTester };
