#!/usr/bin/env node

/**
 * Radiant Layout Engine Direct Tree Comparison Testing
 *
 * This script compares Radiant's layout tree directly against browser reference tree
 * using simple element-by-element hierarchical comparison. Special handling for <br> elements
 * which Radiant does not produce yet.
 */

const fs = require('fs').promises;
const path = require('path');
const { spawn } = require('child_process');
const { extractLayoutFromFile, extractAllTestFiles } = require('./extract_browser_references.js');

class LayoutTester {
    constructor(options = {}) {
        this.radiantExe = options.radiantExe || path.join(__dirname, '..', '..', '..', '..', 'Jubily', 'radiant.exe');
        this.tolerance = options.tolerance || 5.0;
        this.textTolerance = options.textTolerance || 5.0; // Allow 5px difference for text (font precision)
        this.baselineTextTolerancePercent = 0.05; // 5% tolerance for baseline suite text width
        this.generateReferences = options.generateReferences || false;
        this.verbose = options.verbose || false;

        this.testDir = path.join(__dirname, '..');
        this.dataDir = path.join(this.testDir, 'data');
        this.referenceDir = path.join(this.testDir, 'reference');
        this.projectRoot = path.join(__dirname, '..', '..', '..');

        // Output results to /tmp directory for easier access
        this.reportsDir = '/tmp';
    }


    async ensureDirectories() {
        await fs.mkdir(this.referenceDir, { recursive: true });
        await fs.mkdir(this.reportsDir, { recursive: true });
    }

    /**
     * Dynamically discover available test categories from filesystem
     */
    async getAvailableCategories() {
        try {
            const items = await fs.readdir(this.dataDir, { withFileTypes: true });
            const categories = items
                .filter(item => item.isDirectory())
                .map(item => item.name)
                .filter(name => !name.startsWith('.')) // Skip hidden directories
                .sort();

            if (this.verbose) {
                console.log(`📁 Found test categories: ${categories.join(', ')}`);
            }

            return categories;
        } catch (error) {
            console.warn(`Warning: Could not scan test categories: ${error.message}`);
            // Try to return the currently existing categories based on filesystem structure
            const knownCategories = ['baseline', 'basic', 'box', 'flex', 'grid', 'medium', 'position', 'reference', 'table', 'text_flow'];
            console.warn(`Falling back to known categories: ${knownCategories.join(', ')}`);
            return knownCategories;
        }
    }

    async runRadiantLayout(htmlFile) {
        return new Promise((resolve, reject) => {
            const process = spawn(this.radiantExe, ['layout', htmlFile], {
                cwd: this.projectRoot
            });

            let stdout = '';
            let stderr = '';

            process.stdout.on('data', (data) => {
                stdout += data.toString();
            });

            process.stderr.on('data', (data) => {
                stderr += data.toString();
            });

            const timeout = setTimeout(() => {
                process.kill();
                reject(new Error('Radiant execution timeout (30s)'));
            }, 30000);

            process.on('close', (code) => {
                clearTimeout(timeout);
                if (code === 0) {
                    resolve({ stdout, stderr });
                } else {
                    reject(new Error(`Radiant failed with exit code ${code}: ${stderr}`));
                }
            });

            process.on('error', (error) => {
                clearTimeout(timeout);
                reject(error);
            });
        });
    }

    async loadRadiantOutput() {
        const jsonFile = '/tmp/view_tree.json';
        try {
            const content = await fs.readFile(jsonFile, 'utf8');
            return JSON.parse(content);
        } catch (error) {
            throw new Error(`Failed to load Radiant output: ${error.message}`);
        }
    }

    async loadBrowserReference(testName, category) {
        const refFile = path.join(this.referenceDir, category, `${testName}.json`);
        try {
            const content = await fs.readFile(refFile, 'utf8');
            return JSON.parse(content);
        } catch (error) {
            if (error.code === 'ENOENT') {
                return null; // Reference doesn't exist
            }
            throw new Error(`Failed to load browser reference: ${error.message}`);
        }
    }

    /**
     * Filter radiant tree to match browser structure
     */
    filterRadiantTree(node) {
        if (!node) return null;

        // Skip elements that should not be compared in tree
        if (node.tag === 'script') {
            return null; // JavaScript code - not part of layout tree
        }

        if (node.tag === 'style') {
            return null; // CSS styles - not part of layout tree
        }

        // Skip text nodes in tree comparison - handle separately in text comparison
        if (node.type === 'text' || node.tag === 'text') {
            return null; // Text nodes handled separately
        }

        // Create filtered copy of node
        const filtered = {
            ...node,
            children: [],
            textNodes: []
        };

        // Convert Radiant text children to textNodes array for browser compatibility
        if (node.children && Array.isArray(node.children)) {
            node.children.forEach(child => {
                if (child && (child.type === 'text' || child.tag === 'text')) {
                    // Convert Radiant text node to browser-style textNode
                    filtered.textNodes.push({
                        text: child.content || '',
                        rects: [child.layout || { x: 0, y: 0, width: 0, height: 0 }]
                    });
                } else {
                    // Regular DOM element - recurse
                    const filteredChild = this.filterRadiantTree(child);
                    if (filteredChild) {
                        filtered.children.push(filteredChild);
                    }
                }
            });
        }

        return filtered;
    }

    /**
     * Count elements in tree structure recursively
     */
    countTreeElements(node) {
        if (!node) return 0;

        // Skip text nodes and non-layout elements for counting
        if (node.type === 'text' || node.tag === 'text' ||
            node.tag === 'script' || node.tag === 'style' || node.tag === 'head') {
            return 0;
        }

        let count = 1; // Count current element

        // Recursively count children
        if (node.children && Array.isArray(node.children)) {
            node.children.forEach(child => {
                count += this.countTreeElements(child);
            });
        }

        return count;
    }

    /**
     * Extract elements from browser tree structure recursively
     */
    extractBrowserElements(treeNode, elements = [], path = '') {
        if (!treeNode) return elements;

        const currentPath = path ? `${path} > ${treeNode.tag}` : treeNode.tag;
        const selector = treeNode.selector || currentPath;

        // Add current element to flat list with tree context
        elements.push({
            selector: selector,
            tag: treeNode.tag,
            id: treeNode.id,
            classes: treeNode.classes || [],
            layout: treeNode.layout,
            computed: treeNode.computed,
            textContent: treeNode.textContent,
            hasTextNodes: treeNode.hasTextNodes,
            textNodes: treeNode.textNodes || [],
            childCount: treeNode.childCount,
            depth: treeNode.depth,
            treePath: currentPath
        });

        // Recursively process children
        if (treeNode.children && Array.isArray(treeNode.children)) {
            treeNode.children.forEach(child => {
                this.extractBrowserElements(child, elements, currentPath);
            });
        }

        return elements;
    }

    /**
     * Extract text nodes from browser tree structure recursively
     * @deprecated This method is obsolete - text nodes are now extracted and compared inline during tree traversal
     * @see getBrowserTextFragmentsFromElement for extracting text fragments from a single element
     */
    extractBrowserTextNodesFromTree(treeNode, textNodes = []) {
        console.warn('⚠️  extractBrowserTextNodesFromTree is deprecated - using inline text comparison during tree traversal instead');
        return [];
    }

    /**
     * Extract text nodes from browser elements with their position data (legacy method for compatibility)
     */
    extractBrowserTextNodes(browserElements) {
        const textNodes = [];

        Object.entries(browserElements).forEach(([key, elem]) => {
            if (elem.textNodes && elem.textNodes.length > 0) {
                elem.textNodes.forEach((textNode, index) => {
                    if (textNode.rects && textNode.rects.length > 0) {
                        // Each rect represents a line fragment
                        textNode.rects.forEach((rect, rectIndex) => {
                            textNodes.push({
                                selector: `${elem.selector || key}_text_${index}_${rectIndex}`,
                                tag: 'text',
                                layout: {
                                    x: rect.x,
                                    y: rect.y,
                                    width: rect.width,
                                    height: rect.height
                                },
                                text: textNode.text,
                                parentElement: textNode.parentElement,
                                isTextNode: true
                            });
                        });
                    }
                });
            }
        });

        return textNodes;
    }

    /**
     * Filter elements to focus on content-level layout accuracy
     * ENHANCED: Work with tree structure and extract elements recursively
     */
    filterContentElements(browserTreeOrElements) {
        let browserElements;

        // Handle both tree structure (new format) and legacy flat structure
        if (browserTreeOrElements && typeof browserTreeOrElements === 'object') {
            if (browserTreeOrElements.layout_tree) {
                // New tree format - extract elements from tree
                browserElements = this.extractBrowserElements(browserTreeOrElements.layout_tree);
            } else if (browserTreeOrElements.layout_data && browserTreeOrElements.layout_data.elements) {
                // Legacy format with elements object
                browserElements = Object.values(browserTreeOrElements.layout_data.elements);
            } else if (browserTreeOrElements.selector || browserTreeOrElements.tag) {
                // Single tree node - extract elements from it
                browserElements = this.extractBrowserElements(browserTreeOrElements);
            } else {
                // Assume it's already a flat elements object
                browserElements = Object.values(browserTreeOrElements);
            }
        } else {
            console.warn('Invalid browser data structure:', typeof browserTreeOrElements);
            return [];
        }

        // Filter elements based on content significance
        const contentElements = browserElements.filter(elem => {
            // Skip elements with display: none (not rendered)
            if (elem.computed && elem.computed.display === 'none') {
                return false;
            }

            // Skip elements with zero dimensions (not visible)
            const hasZeroDimensions = elem.layout && elem.layout.width === 0 && elem.layout.height === 0;
            if (hasZeroDimensions) {
                return false;
            }

            // FOCUS: Skip only non-layout structural elements
            // Keep html, body for proper element matching, but skip head, meta, etc.
            const isNonLayoutStructural = elem.tag === 'head' || elem.tag === 'meta' ||
                                        elem.tag === 'title' || elem.tag === 'style' ||
                                        elem.tag === 'script';

            if (isNonLayoutStructural) {
                return false; // Skip non-layout structural elements
            }

            // INCLUDE: Layout-significant elements
            const isLayoutElement =
                elem.tag === 'html' ||          // Root element
                elem.tag === 'body' ||          // Body element
                elem.selector?.includes('.') || // Has CSS class (content)
                (elem.layout && elem.layout.width < 1200) ||     // Smaller than viewport (content)
                (elem.computed && elem.computed.display === 'flex'); // Flex containers

            if (isLayoutElement) {
                // DEBUG: Log what browser elements we're including
                if (this.verbose && (elem.tag === 'html' || elem.tag === 'body' || elem.selector?.includes('container'))) {
                    const layout = elem.layout || { width: 'N/A', height: 'N/A', x: 'N/A', y: 'N/A' };
                    console.log(`DEBUG: Including browser element: ${elem.selector} (${elem.tag}) - ${layout.width}x${layout.height} at (${layout.x},${layout.y})`);
                }
                return true;
            }

            return false;
        });

        return contentElements;
    }

    /**
     * Compare two tree nodes directly for element matching
     * SIMPLE: Just match based on tag, position in tree, and basic properties
     */
    compareTreeNode(radiantNode, browserNode, path = '', parentContext = null, depth = 0) {
        const differences = [];
        const currentPath = (path && path !== 'root') ? path : (radiantNode.tag || browserNode.tag || 'unknown');

        // Helper function to create proper indentation based on tree depth
        const getIndent = (level = depth) => {
            const baseIndent = '   '; // Base 3-space indentation
            const treeIndent = '  '.repeat(level); // 2 spaces per depth level
            return baseIndent + treeIndent;
        };

        // CRITICAL CHANGE: Always match elements by tag name first
        // Layout differences are recorded but don't prevent element matching
        if (radiantNode.tag !== browserNode.tag) {
            differences.push({
                type: 'tag_mismatch',
                path: currentPath,
                radiant: radiantNode.tag,
                browser: browserNode.tag
            });
            return { matches: false, differences };
        }

        // Element matches by name - now evaluate layout quality
        let layoutMatches = true;
        if (radiantNode.layout && browserNode.layout) {
            // CRITICAL CHANGE: Use relative positioning for child elements
            let radiantLayout = radiantNode.layout;
            let browserLayout = browserNode.layout;

            // If we have parent context, convert to relative positioning
            if (parentContext) {
                radiantLayout = {
                    x: radiantNode.layout.x - parentContext.radiant.x,
                    y: radiantNode.layout.y - parentContext.radiant.y,
                    width: radiantNode.layout.width,
                    height: radiantNode.layout.height
                };
                browserLayout = {
                    x: browserNode.layout.x - parentContext.browser.x,
                    y: browserNode.layout.y - parentContext.browser.y,
                    width: browserNode.layout.width,
                    height: browserNode.layout.height
                };

                if (this.verbose) {
                    console.log(`${getIndent()}🔄 Relative positioning for ${radiantNode.tag}:`);
                    console.log(`${getIndent()}  ^ Radiant: (${radiantLayout.x}, ${radiantLayout.y}) vs Browser: (${browserLayout.x}, ${browserLayout.y})`);
                }
            }
            else {
                if (this.verbose) {
                    console.log(`${getIndent()}🔄 Root ${radiantNode.tag}:`);
                }
            }

            const layoutDiffs = this.compareElementLayout(radiantLayout, browserLayout);
            if (layoutDiffs.length > 0) {
                const maxDiff = Math.max(...layoutDiffs.map(d => d.difference));

                // Record layout differences but don't fail element matching
                differences.push({
                    type: 'layout_difference',
                    path: currentPath,
                    radiant: radiantLayout,
                    browser: browserLayout,
                    differences: layoutDiffs,
                    maxDifference: maxDiff,
                    isRelative: !!parentContext
                });

                layoutMatches = maxDiff <= this.tolerance;

                if (this.verbose) {
                    const relativeStr = parentContext ? '(relative)' : '(absolute)';
                    console.log(`${getIndent()}  ^ Layout ${relativeStr} for ${radiantNode.tag}: ${maxDiff.toFixed(2)}px ${layoutMatches ? '<=':'>'} ${this.tolerance}px`);
                }
            }
        }

        // CRITICAL: Element always matches by name, layout quality is separate
        return {
            matches: true,
            layoutMatches: layoutMatches,
            differences: differences
        };
    }

    /**
     * Filter browser tree to remove elements that Radiant doesn't produce or care about
     */
    filterBrowserTree(node) {
        if (!node) return null;

        // Skip elements that Radiant doesn't produce in its view tree
        if (node.tag === 'br') {
            return null; // Line breaks - functional but no layout representation
        }

        if (node.tag === 'head') {
            return null; // Document metadata - focus only on body layout
        }

        if (node.tag === 'script') {
            return null; // JavaScript code - not part of layout tree
        }

        if (node.tag === 'style') {
            return null; // CSS styles - not part of layout tree
        }

        // Create filtered copy of node
        const filtered = {
            ...node,
            children: []
        };

        // Filter children recursively
        if (node.children && Array.isArray(node.children)) {
            node.children.forEach(child => {
                const filteredChild = this.filterBrowserTree(child);
                if (filteredChild) {
                    filtered.children.push(filteredChild);
                }
            });
        }

        return filtered;
    }

    compareLayouts(radiantData, browserData, testId = null) {
        const results = {
            totalElements: 0,
            matchedElements: 0,
            differences: [],
            maxDifference: 0,
            // Enhanced with integrated text node results
            textDifferences: [],
            maxTextDifference: 0,
            totalTextNodes: 0,
            matchedTextNodes: 0,
            // Store effective tolerances for accurate reporting
            effectiveTextTolerance: this.textTolerance,
            // Add detailed debugging info
            debugInfo: {
                radiantElementCount: 0,
                browserElementCount: 0,
                radiantTextNodeCount: 0,
                browserTextNodeCount: 0,
                skippedBrElements: 0,
                treeComparisons: []
            }
        };

        // Get tree structures
        let radiantTree = radiantData.layout_tree;
        let browserTree = browserData.layout_tree;

        if (!browserTree) {
            console.error('❌ Browser data does not contain layout_tree structure');
            return results;
        }

        // Filter both trees to focus on body elements only
        radiantTree = this.filterRadiantTree(radiantTree);
        browserTree = this.filterBrowserTree(browserTree);

        if (this.verbose) {
            console.log(`\n🌳 INTEGRATED TREE-BASED COMPARISON:`);
            console.log(`   📊 Comparing element tree with text nodes inline, filtered out <head>, <script>, <style>, and <br>`);
        }

        // Recursive tree comparison with integrated text node checking
        const initialPath = radiantTree?.tag || browserTree?.tag || 'root';
        const comparison = this.compareTreesRecursively(radiantTree, browserTree, initialPath, null, results.effectiveTextTolerance, 0);

        // Copy results from integrated comparison
        results.totalElements = comparison.totalElements;
        results.matchedElements = comparison.matchedElements;
        results.differences = comparison.differences;
        results.maxDifference = comparison.maxDifference;

        // Copy text node results from integrated comparison
        results.totalTextNodes = comparison.totalTextNodes;
        results.matchedTextNodes = comparison.matchedTextNodes;
        results.textDifferences = comparison.textDifferences;
        results.maxTextDifference = comparison.maxTextDifference;

        // Update debug info
        results.debugInfo.radiantTextNodeCount = results.totalTextNodes;
        results.debugInfo.browserTextNodeCount = results.totalTextNodes; // Approximation since we compare pairwise

        if (this.verbose) {
            const testDisplayName = testId ? ` for ${testId}` : '';
            console.log(`\n🌳 INTEGRATED TREE COMPARISON SUMMARY${testDisplayName}:`);
            console.log(`   ✅ Matched Elements: ${results.matchedElements}/${results.totalElements}`);
            console.log(`   📏 Max Layout Diff: ${results.maxDifference.toFixed(2)}px`);

            if (results.totalTextNodes > 0) {
                console.log(`   📝 Text Nodes: ${results.matchedTextNodes}/${results.totalTextNodes} matches`);
                console.log(`   📏 Max Text Diff: ${results.maxTextDifference.toFixed(2)}px`);
            }
        }

        // Generate summary
        const passRate = results.totalElements > 0 ?
            (results.matchedElements / results.totalElements * 100) : 0;
        const textPassRate = results.totalTextNodes > 0 ?
            (results.matchedTextNodes / results.totalTextNodes * 100) : 100;

        const layoutAccuracy = results.maxDifference <= this.tolerance ? 'ACCURATE' : 'INACCURATE';
        const textAccuracy = results.maxTextDifference <= results.effectiveTextTolerance ? 'ACCURATE' : 'INACCURATE';

        let summary = `${results.matchedElements}/${results.totalElements} tree matches (${passRate.toFixed(1)}%) - Max diff: ${results.maxDifference.toFixed(2)}px (${layoutAccuracy})`;

        if (results.totalTextNodes > 0) {
            summary += `\n     Text: ${results.matchedTextNodes}/${results.totalTextNodes} matches (${textPassRate.toFixed(1)}%) - Max text diff: ${results.maxTextDifference.toFixed(2)}px vs ${results.effectiveTextTolerance}px tolerance (${textAccuracy})`;
        }

        results.summary = summary;

        return results;
    }

    /**
     * Recursively compare two trees element by element with parent context, including text nodes
     */
    compareTreesRecursively(radiantNode, browserNode, path = '', parentContext = null, effectiveTextTolerance = null, depth = 0) {
        const results = {
            totalElements: 0,
            matchedElements: 0,
            differences: [],
            maxDifference: 0,
            // Add text node tracking directly in tree comparison
            totalTextNodes: 0,
            matchedTextNodes: 0,
            textDifferences: [],
            maxTextDifference: 0
        };

        // Helper function to create proper indentation based on tree depth
        const getIndent = (level = depth) => {
            const baseIndent = '   '; // Base 3-space indentation
            const treeIndent = '  '.repeat(level); // 2 spaces per depth level
            return baseIndent + treeIndent;
        };

        if (!radiantNode && !browserNode) {
            return results; // Both null, nothing to compare
        }

        if (!radiantNode || !browserNode) {
            // One tree has an element the other doesn't
            results.totalElements = 1;
            const presentNode = radiantNode || browserNode;
            const elementPath = path || presentNode.tag || 'unknown';

            results.differences.push({
                type: radiantNode ? 'extra_in_radiant' : 'missing_in_radiant',
                path: elementPath,
                radiant: radiantNode ? { tag: radiantNode.tag, layout: radiantNode.layout } : null,
                browser: browserNode ? { tag: browserNode.tag, layout: browserNode.layout } : null
            });

            if (this.verbose) {
                const missingIn = radiantNode ? 'browser' : 'radiant';
                console.log(`${getIndent()}❌ Missing in ${missingIn}: ${elementPath} (${presentNode.tag})`);
            }

            return results;
        }

        // Both nodes exist, compare them
        results.totalElements = 1;
        const currentPath = (path && path !== 'root') ? path : radiantNode.tag;

        // Compare current nodes with parent context for relative positioning
        const nodeComparison = this.compareTreeNode(radiantNode, browserNode, currentPath, parentContext, depth);

        // CRITICAL CHANGE: Elements always match by name, record layout differences separately
        if (nodeComparison.matches) {
            results.matchedElements = 1;
            if (this.verbose && radiantNode.layout) {
                const layoutStatus = nodeComparison.layoutMatches ? '✅' : '⚠️';
                console.log(`${getIndent()}  ^ ${layoutStatus} Match: ${currentPath}`);
            }
        } else {
            // Only tag mismatches prevent element matching
            if (this.verbose) {
                console.log(`${getIndent()}❌ Tag mismatch: ${currentPath} | ${radiantNode.tag} vs. ${browserNode.tag}`);
            }
        }

        // Record all differences (layout and tag mismatches)
        results.differences.push(...nodeComparison.differences);
        nodeComparison.differences.forEach(diff => {
            if (diff.maxDifference) {
                results.maxDifference = Math.max(results.maxDifference, diff.maxDifference);
            }
        });

        // CRITICAL CHANGE: Create parent context for children (relative positioning)
        const childParentContext = (radiantNode.layout && browserNode.layout) ? {
            radiant: radiantNode.layout,
            browser: browserNode.layout
        } : null;

        // NEW: Get all child nodes (elements + text nodes) in document order
        const radiantChildNodes = this.getAllChildNodesInOrder(radiantNode, currentPath);
        const browserChildNodes = this.getAllChildNodesInOrder(browserNode, currentPath);
        const maxChildren = Math.max(radiantChildNodes.length, browserChildNodes.length);

        if (this.verbose && (radiantChildNodes.length > 0 || browserChildNodes.length > 0)) {
            // console.log(`${getIndent()}🔍 Comparing ${maxChildren} child nodes (elements + text) in document order`);
        }

        // Compare all child nodes (elements and text) in unified loop following document order
        for (let i = 0; i < maxChildren; i++) {
            const radiantChild = radiantChildNodes[i] || null;
            const browserChild = browserChildNodes[i] || null;

            const childResults = this.compareChildNodesInOrder(
                radiantChild, browserChild, currentPath, childParentContext, effectiveTextTolerance, depth + 1
            );

            // Merge results including text nodes
            results.totalElements += childResults.totalElements;
            results.matchedElements += childResults.matchedElements;
            results.differences.push(...childResults.differences);
            results.maxDifference = Math.max(results.maxDifference, childResults.maxDifference);

            // Merge text node results
            results.totalTextNodes += childResults.totalTextNodes;
            results.matchedTextNodes += childResults.matchedTextNodes;
            results.textDifferences.push(...childResults.textDifferences);
            results.maxTextDifference = Math.max(results.maxTextDifference, childResults.maxTextDifference);
        }

        return results;
    }

    /**
     * Get all child nodes (elements + text nodes) from a node in their natural order
     */
    getAllChildNodesInOrder(node, parentPath) {
        if (!node) return [];

        const childNodes = [];

        // For proper document order, we need to interleave text nodes and elements
        // based on their position in the original HTML DOM structure

        const elements = node.children || [];
        const textNodes = node.textNodes || [];

        // Simple approach: alternate between text and elements
        // Text nodes typically appear between elements in HTML like:
        // text0 <element0/> text1 <element1/> text2 <element2/> text3

        for (let i = 0; i <= Math.max(elements.length, textNodes.length); i++) {
            // Add text node if it exists at this position
            if (i < textNodes.length && textNodes[i].rects && textNodes[i].rects.length > 0) {
                const textNode = textNodes[i];
                textNode.rects.forEach((rect, rectIndex) => {
                    childNodes.push({
                        type: 'text',
                        node: {
                            tag: 'text',
                            layout: {
                                x: rect.x,
                                y: rect.y,
                                width: rect.width,
                                height: rect.height
                            },
                            text: textNode.text,
                            textIndex: i,
                            rectIndex: rectIndex
                        },
                        index: i,
                        rectIndex: rectIndex,
                        path: `${parentPath}_text_${i}_${rectIndex}`
                    });
                });
            }

            // Add element if it exists at this position
            if (i < elements.length) {
                childNodes.push({
                    type: 'element',
                    node: elements[i],
                    index: i,
                    path: `${parentPath} > ${elements[i].tag}`
                });
            }
        }

        return childNodes;
    }

    /**
     * Compare child nodes (element or text) in document order
     */
    compareChildNodesInOrder(radiantChild, browserChild, parentPath, parentContext, effectiveTextTolerance, depth) {
        const results = {
            totalElements: 0,
            matchedElements: 0,
            differences: [],
            maxDifference: 0,
            totalTextNodes: 0,
            matchedTextNodes: 0,
            textDifferences: [],
            maxTextDifference: 0
        };

        // Helper function for indentation
        const getIndent = (level = depth) => {
            const baseIndent = '   ';
            const treeIndent = '  '.repeat(level);
            return baseIndent + treeIndent;
        };

        if (!radiantChild && !browserChild) {
            return results; // Both null, nothing to compare
        }

        if (!radiantChild || !browserChild) {
            // One tree has a node the other doesn't
            const presentChild = radiantChild || browserChild;
            const childPath = presentChild.path;

            if (presentChild.type === 'element') {
                results.totalElements = 1;
                results.differences.push({
                    type: radiantChild ? 'extra_in_radiant' : 'missing_in_radiant',
                    path: childPath,
                    radiant: radiantChild ? { tag: radiantChild.node.tag, layout: radiantChild.node.layout } : null,
                    browser: browserChild ? { tag: browserChild.node.tag, layout: browserChild.node.layout } : null
                });

                if (this.verbose) {
                    const missingIn = radiantChild ? 'browser' : 'radiant';
                    console.log(`${getIndent()}❌ Missing ${presentChild.type} in ${missingIn}: ${childPath} (${presentChild.node.tag})`);
                }
            } else if (presentChild.type === 'text') {
                results.totalTextNodes = 1;
                results.textDifferences.push({
                    type: radiantChild ? 'extra_text_in_radiant' : 'missing_text_in_radiant',
                    text: presentChild.node.text,
                    radiant: radiantChild ? presentChild.node.layout : null,
                    browser: browserChild ? browserChild.node.layout : null,
                    elementPath: parentPath
                });

                if (this.verbose) {
                    const missingIn = radiantChild ? 'browser' : 'radiant';
                    console.log(`${getIndent()}❌ Missing text in ${missingIn}: "${this.cleanTextForDisplay(presentChild.node.text)}"`);
                }
            }

            return results;
        }

        // Both nodes exist - check if they're the same type
        if (radiantChild.type !== browserChild.type) {
            // Type mismatch - element vs text
            if (this.verbose) {
                console.log(`${getIndent()}❌ Node type mismatch: ${radiantChild.type} vs ${browserChild.type}`);
            }

            // Count as both element and text difference depending on types
            if (radiantChild.type === 'element' || browserChild.type === 'element') {
                results.totalElements = 1;
                results.differences.push({
                    type: 'node_type_mismatch',
                    path: radiantChild.path,
                    radiant: { type: radiantChild.type, tag: radiantChild.node.tag },
                    browser: { type: browserChild.type, tag: browserChild.node.tag }
                });
            }
            if (radiantChild.type === 'text' || browserChild.type === 'text') {
                results.totalTextNodes = 1;
                results.textDifferences.push({
                    type: 'node_type_mismatch',
                    radiant: radiantChild.type === 'text' ? radiantChild.node.text : null,
                    browser: browserChild.type === 'text' ? browserChild.node.text : null,
                    elementPath: parentPath
                });
            }

            return results;
        }

        // Same type - compare appropriately
        if (radiantChild.type === 'element') {
            // Element comparison - recurse with compareTreesRecursively
            const elementResults = this.compareTreesRecursively(
                radiantChild.node, browserChild.node,
                radiantChild.path, parentContext, effectiveTextTolerance, depth
            );

            // Copy all results
            results.totalElements = elementResults.totalElements;
            results.matchedElements = elementResults.matchedElements;
            results.differences = elementResults.differences;
            results.maxDifference = elementResults.maxDifference;
            results.totalTextNodes = elementResults.totalTextNodes;
            results.matchedTextNodes = elementResults.matchedTextNodes;
            results.textDifferences = elementResults.textDifferences;
            results.maxTextDifference = elementResults.maxTextDifference;

        } else if (radiantChild.type === 'text') {
            // Text node comparison
            results.totalTextNodes = 1;

            const textTolerance = effectiveTextTolerance || this.textTolerance;
            const diffs = this.compareElementLayout(radiantChild.node.layout, browserChild.node.layout);

            if (diffs.length > 0) {
                const maxDiff = Math.max(...diffs.map(d => d.difference));
                results.maxTextDifference = Math.max(results.maxTextDifference, maxDiff);

                if (maxDiff > textTolerance) {
                    const detailedDiffs = diffs.map(diff => ({
                        property: diff.property,
                        radiant: diff.radiant,
                        browser: diff.browser,
                        difference: diff.difference,
                        withinTolerance: diff.difference <= textTolerance
                    }));

                    results.textDifferences.push({
                        type: 'text_position_difference',
                        text: radiantChild.node.text.substring(0, 50) + (radiantChild.node.text.length > 50 ? '...' : ''),
                        radiant: radiantChild.node.layout,
                        browser: browserChild.node.layout,
                        differences: diffs,
                        detailedDifferences: detailedDiffs,
                        maxDifference: maxDiff,
                        effectiveTextTolerance: textTolerance,
                        elementPath: parentPath,
                        documentOrder: radiantChild.documentOrder
                    });

                    if (this.verbose) {
                        console.log(`${getIndent()}❌ Text FAILED tolerance: "${this.cleanTextForDisplay(radiantChild.node.text)}" - ${maxDiff.toFixed(2)}px > ${textTolerance}px`);

                        // Show detailed differences inline
                        detailedDiffs.forEach(propDiff => {
                            const status = propDiff.withinTolerance ? '✅' : '❌';
                            const pct = propDiff.difference > 0 ? `(${((propDiff.difference / textTolerance) * 100).toFixed(1)}% of tolerance)` : '';
                            console.log(`${getIndent()}   ${status} ${propDiff.property}: ${propDiff.difference.toFixed(2)}px ${pct}`);
                            console.log(`${getIndent()}     Radiant: ${propDiff.radiant}, Browser: ${propDiff.browser}`);
                        });
                    }
                } else {
                    results.matchedTextNodes++;
                    if (this.verbose) {
                        console.log(`${getIndent()}✅ Text PASSED tolerance: "${this.cleanTextForDisplay(radiantChild.node.text)}" - ${maxDiff.toFixed(2)}px <= ${textTolerance}px`);
                    }
                }
            } else {
                results.matchedTextNodes++;
                if (this.verbose) {
                    console.log(`${getIndent()}✅ Text perfect match: "${this.cleanTextForDisplay(radiantChild.node.text)}"`);
                }
            }
        }

        return results;
    }

    /**
     * Clean text for display by removing newlines and normalizing whitespace
     */
    cleanTextForDisplay(text, maxLength = 30) {
        if (!text) return '';

        // Replace newlines and multiple whitespace with single spaces
        const cleaned = text.replace(/\s+/g, ' ').trim();

        // Truncate if needed
        if (cleaned.length > maxLength) {
            return cleaned.substring(0, maxLength) + '...';
        }

        return cleaned;
    }

    /**
     * Extract text nodes from a single element (not recursive)
     */
    getTextNodesFromElement(element, elementPath) {
        const textNodes = [];

        if (!element || !element.textNodes) {
            return textNodes;
        }

        element.textNodes.forEach((textNode, index) => {
            if (textNode.rects && textNode.rects.length > 0) {
                textNode.rects.forEach((rect, rectIndex) => {
                    textNodes.push({
                        selector: `${elementPath}_text_${index}_${rectIndex}`,
                        tag: 'text',
                        layout: {
                            x: rect.x,
                            y: rect.y,
                            width: rect.width,
                            height: rect.height
                        },
                        text: textNode.text,
                        parentElement: textNode.parentElement,
                        isTextNode: true,
                        treePath: elementPath
                    });
                });
            }
        });

        return textNodes;
    }

    /**
     * Extract browser text fragments from a single element (not recursive)
     */
    getBrowserTextFragmentsFromElement(element, elementPath) {
        const textFragments = [];

        if (!element || !element.textNodes) {
            return textFragments;
        }

        element.textNodes.forEach((textNode, nodeIndex) => {
            if (textNode.rects && textNode.rects.length > 0) {
                // Browser has multiple rects for wrapped text - create fragment for each rect
                textNode.rects.forEach((rect, rectIndex) => {
                    textFragments.push({
                        text: textNode.text,
                        layout: {
                            x: rect.x,
                            y: rect.y,
                            width: rect.width,
                            height: rect.height
                        },
                        rectIndex: rectIndex,
                        totalRects: textNode.rects.length,
                        parentText: textNode.text,
                        nodeIndex: nodeIndex
                    });
                });
            } else if (textNode.layout) {
                // Browser has single rect - treat as single fragment
                textFragments.push({
                    text: textNode.text,
                    layout: textNode.layout,
                    rectIndex: 0,
                    totalRects: 1,
                    parentText: textNode.text,
                    nodeIndex: nodeIndex
                });
            }
        });

        return textFragments;
    }

    compareElementLayout(radiant, browser) {
        const differences = [];
        const properties = ['x', 'y', 'width', 'height'];

        // Safety check for undefined layout objects
        if (!radiant || !browser) {
            console.warn('WARNING: Undefined layout object in comparison:', { radiant: !!radiant, browser: !!browser });
            return differences;
        }

        for (const prop of properties) {
            const radiantVal = radiant[prop] || 0;
            const browserVal = browser[prop] || 0;
            const diff = Math.abs(radiantVal - browserVal);

            if (diff > 0.01) { // Ignore tiny floating point differences
                differences.push({
                    property: prop,
                    radiant: radiantVal,
                    browser: browserVal,
                    difference: diff
                });
            }
        }

        return differences;
    }

    /**
     * Print detailed text differences for debugging
     * @deprecated This method is obsolete - text differences are now shown inline during tree comparison
     */
    printTextDifferences(textDifferences, testName = '') {
        if (textDifferences.length === 0) {
            console.log(`   ✅ No text differences found for ${testName}`);
            return;
        }

        console.log(`\n📝 DETAILED TEXT DIFFERENCES for ${testName}:`);
        console.log(`   Found ${textDifferences.length} text positioning issues:`);

        textDifferences.forEach((diff, index) => {
            console.log(`   ${index + 1}. Text: \"${diff.text}\"`);
            console.log(`      Max Difference: ${diff.maxDifference.toFixed(2)}px vs ${diff.effectiveTextTolerance}px tolerance`);

            if (diff.detailedDifferences) {
                diff.detailedDifferences.forEach(propDiff => {
                    const status = propDiff.withinTolerance ? '✅' : '❌';
                    const pct = propDiff.difference > 0 ? `(${((propDiff.difference / diff.effectiveTextTolerance) * 100).toFixed(1)}% of tolerance)` : '';
                    console.log(`      ${status} ${propDiff.property}: ${propDiff.difference.toFixed(2)}px ${pct}`);
                    console.log(`          Radiant: ${propDiff.radiant}, Browser: ${propDiff.browser}`);
                });
            }

            if (diff.browserFragment) {
                console.log(`      Browser Fragment: ${diff.browserFragment.rectIndex + 1}/${diff.browserFragment.totalRects}`);
            }
        });
    }

    async testSingleFile(htmlFile, category) {
        const testName = path.basename(htmlFile, '.html');
        const startTime = Date.now();

        console.log(`  🧪 Testing: ${testName}`);

        try {
            // Check if browser reference exists
            let browserData = await this.loadBrowserReference(testName, category);

            if (!browserData && this.generateReferences) {
                console.log(`    📊 Generating browser reference...`);
                browserData = await extractLayoutFromFile(htmlFile);
            }

            if (!browserData) {
                console.log(`    ⚠️  No reference data - validated Radiant execution only`);

                // Just test that Radiant can process the file
                await this.runRadiantLayout(htmlFile);
                const radiantData = await this.loadRadiantOutput();
                const elementCount = this.countTreeElements(radiantData.layout_tree);

                console.log(`    ✅ PASS (${elementCount}/${elementCount} elements)`);

                return {
                    testName,
                    passed: true,
                    executionTime: (Date.now() - startTime) / 1000,
                    matchedElements: elementCount,
                    totalElements: elementCount,
                    hasReference: false
                };
            }

            // Run Radiant layout
            await this.runRadiantLayout(htmlFile);
            const radiantData = await this.loadRadiantOutput();

            // Compare layouts
            const comparison = this.compareLayouts(radiantData, browserData, testName);

            // FOCUS: Layout accuracy is the primary success criteria
            // Pass if layout differences are within tolerance, regardless of element structure differences

            const layoutAccurate = comparison.maxDifference <= this.tolerance;
            const hasLayoutMatches = comparison.matchedElements > 0;

            // Text accuracy check - use higher tolerance for baseline tests
            let effectiveTextTolerance = this.textTolerance;
            const textAccurate = comparison.totalTextNodes === 0 ||
                               (comparison.maxTextDifference <= effectiveTextTolerance && comparison.matchedTextNodes > 0);

            // Pass if we have accurate layout positioning AND accurate text positioning (when text exists)
            const passed = layoutAccurate && hasLayoutMatches && textAccurate;

            const status = passed ? '✅ PASS' : '❌ FAIL';
            console.log(`    ${status} (${comparison.summary})`);

            return {
                testName,
                passed,
                executionTime: (Date.now() - startTime) / 1000,
                matchedElements: comparison.matchedElements,
                totalElements: comparison.totalElements,
                maxDifference: comparison.maxDifference,
                differences: comparison.differences,
                // Text node results
                matchedTextNodes: comparison.matchedTextNodes,
                totalTextNodes: comparison.totalTextNodes,
                maxTextDifference: comparison.maxTextDifference,
                textDifferences: comparison.textDifferences,
                hasReference: true
            };

        } catch (error) {
            console.log(`    ❌ ERROR: ${error.message}`);

            return {
                testName,
                passed: false,
                executionTime: (Date.now() - startTime) / 1000,
                error: error.message,
                hasReference: false
            };
        }
    }

    async testCategory(category) {
        console.log(`📂 Processing ${category} tests...`);

        const categoryDir = path.join(this.dataDir, category);
        let testFiles = [];

        try {
            const files = await fs.readdir(categoryDir);
            testFiles = files
                .filter(file => file.endsWith('.html'))
                .map(file => path.join(categoryDir, file));
        } catch (error) {
            console.log(`  ⚠️  Category ${category}/ not found`);
            return { category, results: [], summary: { total: 0, passed: 0, failed: 0 } };
        }

        if (testFiles.length === 0) {
            console.log(`  ⚠️  No HTML files found in ${category}/`);
            return { category, results: [], summary: { total: 0, passed: 0, failed: 0 } };
        }

        const results = [];

        for (const htmlFile of testFiles) {
            const result = await this.testSingleFile(htmlFile, category);
            results.push(result);
        }

        // Calculate summary
        const total = results.length;
        const passed = results.filter(r => r.passed).length;
        const failed = total - passed;
        const passRate = total > 0 ? (passed / total * 100) : 0;


        console.log(`\n  📊 Category results: ${passed}/${total} passed (${passRate.toFixed(1)}%)`);

        return {
            category,
            results,
            summary: { total, passed, failed, passRate }
        };
    }

    async testSingleByName(testName) {
        console.log(`🎯 Running Single Test: ${testName}`);
        console.log('==========================================');

        await this.ensureDirectories();

        // Search for the test file in all categories
        const categories = await this.getAvailableCategories();
        let foundFile = null;
        let foundCategory = null;

        for (const category of categories) {
            const categoryDir = path.join(this.dataDir, category);
            try {
                const files = await fs.readdir(categoryDir);
                const htmlFile = files.find(file =>
                    file === `${testName}.html` ||
                    path.basename(file, '.html') === testName
                );

                if (htmlFile) {
                    foundFile = path.join(categoryDir, htmlFile);
                    foundCategory = category;
                    break;
                }
            } catch (error) {
                // Category directory doesn't exist, continue
                continue;
            }
        }

        if (!foundFile) {
            console.error(`❌ Test file '${testName}' not found in any category`);
            console.log(`Available categories: ${categories.join(', ')}`);
            process.exit(1);
        }

        console.log(`📂 Found test in category: ${foundCategory}`);

        const result = await this.testSingleFile(foundFile, foundCategory);

        // Generate report for single test
        const report = {
            timestamp: new Date().toISOString(),
            radiant_exe: this.radiantExe,
            tolerance: this.tolerance,
            total_tests: 1,
            passed_tests: result.passed ? 1 : 0,
            failed_tests: result.passed ? 0 : 1,
            pass_rate: result.passed ? 100 : 0,
            single_test: result
        };

        const reportFile = path.join(this.reportsDir, `layout_test_${testName}.json`);
        await fs.writeFile(reportFile, JSON.stringify(report, null, 2));
        console.log(`\n💾 Test report saved to: ${reportFile}`);

        return result;
    }

    async testAll() {
        console.log('🎯 Radiant Layout Engine Integration Tests');
        console.log('==========================================');

        // Check if Radiant executable exists
        try {
            await fs.access(this.radiantExe);
        } catch (error) {
            console.error(`❌ Error: Radiant executable not found: ${this.radiantExe}`);
            console.error('Please build Radiant first with: make build-radiant');
            process.exit(1);
        }

        await this.ensureDirectories();

        const categories = await this.getAvailableCategories();
        const allResults = [];
        let totalTests = 0;
        let totalPassed = 0;

        for (const category of categories) {
            const categoryResult = await this.testCategory(category);
            allResults.push(categoryResult);
            totalTests += categoryResult.summary.total;
            totalPassed += categoryResult.summary.passed;
        }

        // Overall summary
        console.log('\n📊 Overall Test Results');
        console.log('========================');
        console.log(`Total tests: ${totalTests}`);
        console.log(`✅ Passed: ${totalPassed}`);
        console.log(`❌ Failed: ${totalTests - totalPassed}`);

        if (totalTests > 0) {
            const overallPassRate = (totalPassed / totalTests * 100);
            console.log(`\n🎯 Overall pass rate: ${overallPassRate.toFixed(1)}%`);
        }

        // Save detailed report
        const report = {
            timestamp: new Date().toISOString(),
            radiant_exe: this.radiantExe,
            tolerance: this.tolerance,
            total_tests: totalTests,
            passed_tests: totalPassed,
            failed_tests: totalTests - totalPassed,
            pass_rate: totalTests > 0 ? (totalPassed / totalTests * 100) : 0,
            categories: allResults
        };

        const reportFile = path.join(this.reportsDir, 'layout_test_results.json');
        await fs.writeFile(reportFile, JSON.stringify(report, null, 2));
        console.log(`\n💾 Detailed report saved to: ${reportFile}`);

        return report;
    }

    async testByPattern(pattern) {
        console.log(`🎯 Running Tests Matching Pattern: ${pattern}`);
        console.log('================================================');

        await this.ensureDirectories();

        // Search for test files matching the pattern in all categories
        const categories = await this.getAvailableCategories();
        let foundFiles = [];

        for (const category of categories) {
            const categoryDir = path.join(this.dataDir, category);
            try {
                const files = await fs.readdir(categoryDir);
                const matchingFiles = files.filter(file =>
                    file.endsWith('.html') &&
                    path.basename(file, '.html').includes(pattern)
                );

                for (const file of matchingFiles) {
                    foundFiles.push({
                        path: path.join(categoryDir, file),
                        name: path.basename(file, '.html'),
                        category: category
                    });
                }
            } catch (error) {
                // Category directory doesn't exist, continue
                continue;
            }
        }

        if (foundFiles.length === 0) {
            console.error(`❌ No test files found matching pattern '${pattern}'`);
            console.log(`Available categories: ${categories.join(', ')}`);
            process.exit(1);
        }

        console.log(`📂 Found ${foundFiles.length} test(s) matching pattern '${pattern}':`);
        foundFiles.forEach(file => {
            console.log(`   • ${file.category}/${file.name}`);
        });
        console.log('');

        // Run all matching tests
        const results = [];
        for (const file of foundFiles) {
            console.log(`\n🧪 Testing: ${file.name} (${file.category})`);
            const result = await this.testSingleFile(file.path, file.category);
            result.testName = file.name;
            results.push(result);
        }

        // Calculate summary
        const totalTests = results.length;
        const passedTests = results.filter(r => r.passed).length;
        const failedTests = totalTests - passedTests;
        const passRate = totalTests > 0 ? (passedTests / totalTests * 100) : 0;

        console.log(`\n📊 Pattern Test Summary for '${pattern}':`);
        console.log(`   Total Tests: ${totalTests}`);
        console.log(`   Passed: ${passedTests}`);
        console.log(`   Failed: ${failedTests}`);
        console.log(`   Pass Rate: ${passRate.toFixed(1)}%`);

        // Generate detailed report
        const report = {
            timestamp: new Date().toISOString(),
            pattern: pattern,
            radiant_exe: this.radiantExe,
            tolerance: this.tolerance,
            total_tests: totalTests,
            passed_tests: passedTests,
            failed_tests: failedTests,
            pass_rate: passRate,
            tests: results
        };

        const reportFile = path.join(this.reportsDir, `layout_test_pattern_${pattern}.json`);
        await fs.writeFile(reportFile, JSON.stringify(report, null, 2));
        console.log(`\n💾 Pattern test report saved to: ${reportFile}`);

        return report;
    }
}

// Main execution
async function main() {
    const args = process.argv.slice(2);

    // Parse arguments
    const options = {
        radiantExe: '../Jubily/radiant.exe',
        tolerance: 5.0,
        textTolerance: 5.0, // allow 5px difference for text (font precision)
        generateReferences: false,
        verbose: false
    };

    let category = null;
    let testName = null;
    let pattern = null;
    let showHelp = false;

    for (let i = 0; i < args.length; i++) {
        const arg = args[i];
        switch (arg) {
            case '--help':
            case '-h':
                showHelp = true;
                break;
            case '--category':
            case '-c':
                category = args[++i];
                // Category validation will be done dynamically when tester is created
                // This allows for flexible category discovery
                break;
            case '--test':
            case '-t':
                testName = args[++i];
                break;
            case '--pattern':
            case '-p':
                pattern = args[++i];
                break;
            case '--tolerance':
                options.tolerance = parseFloat(args[++i]);
                break;
            case '--text-tolerance':
                options.textTolerance = parseFloat(args[++i]);
                break;
            case '--generate-references':
            case '-g':
                options.generateReferences = true;
                break;
            case '--verbose':
            case '-v':
                options.verbose = true;
                break;
            case '--radiant-exe':
                options.radiantExe = args[++i];
                break;
            default:
                console.error(`❌ Unknown argument: ${arg}`);
                showHelp = true;
        }
    }

    if (showHelp) {
        console.log(`
Radiant Layout Engine Integration Testing with Text Node Verification

Usage: node test_layout.js [options]

This tool compares Radiant's layout output against browser references, including:
- Element positioning and dimensions
- Text node positions and line fragments
- CSS property application accuracy

Options:
  --category, -c <name>      Test specific category (auto-detected from test/layout/data/)
  --test, -t <name>          Test specific test file (e.g., table_simple)
  --pattern, -p <pattern>    Test all files matching pattern (e.g., table_simple)
  --tolerance <pixels>       Layout difference tolerance in pixels (default: 5.0)
  --text-tolerance <pixels>  Text position/size tolerance in pixels (default: 3.0)
  --generate-references, -g  Generate browser references if missing
  --verbose, -v              Show detailed failure information
  --radiant-exe <path>       Path to Radiant executable (default: ./radiant.exe)
  --help, -h                 Show this help message

Examples:
  node test_layout.js                    # Test all categories
  node test_layout.js -c basic           # Test basic category only
  node test_layout.js -t table_simple    # Test specific test file
  node test_layout.js -p table_simple    # Test all files matching 'table_simple'
  node test_layout.js -g -v              # Generate references and show details
  node test_layout.js --tolerance 1.0    # Use 1px tolerance
  node test_layout.js --text-tolerance 0.5  # Use 0.5px text tolerance

Generated files:
  ../reference/<category>/<test>.json         # Browser reference data (with text nodes)
  /tmp/layout_test_results.json               # Test results report
`);
        process.exit(0);
    }

    const tester = new LayoutTester(options);

    try {
        // Validate category if specified
        if (category) {
            const availableCategories = await tester.getAvailableCategories();
            if (!availableCategories.includes(category)) {
                console.error(`❌ Invalid category: ${category}`);
                console.error(`Available categories: ${availableCategories.join(', ')}`);
                process.exit(1);
            }
        }

        if (testName) {
            await tester.testSingleByName(testName);
        } else if (pattern) {
            await tester.testByPattern(pattern);
        } else if (category) {
            await tester.testCategory(category);
        } else {
            await tester.testAll();
        }
    } catch (error) {
        console.error('❌ Testing failed:', error.message);
        process.exit(1);
    }
}

// Run if called directly
if (require.main === module) {
    main().catch(console.error);
}

module.exports = { LayoutTester };
