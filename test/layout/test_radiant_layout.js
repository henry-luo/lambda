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

class RadiantLayoutTester {
    constructor(options = {}) {
        this.radiantExe = options.radiantExe || './radiant.exe';
        this.tolerance = options.tolerance || 5.0; // 5px tolerance for layout differences
        this.elementThreshold = options.elementThreshold || 100.0; // 100% overall element match threshold
        this.textThreshold = options.textThreshold || 100.0; // 100% overall text match threshold
        this.testDataDir = path.join(__dirname, 'data');
        this.referenceDir = path.join(__dirname, 'reference');
        this.outputFile = '/tmp/view_tree.json';
        this.verbose = options.verbose || false;
        this.projectRoot = options.projectRoot || process.cwd();
    }

    /**
     * Run Radiant layout engine on a test file
     */
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

    /**
     * Load Radiant output from /tmp/view_tree.json
     */
    async loadRadiantOutput() {
        try {
            const content = await fs.readFile(this.outputFile, 'utf8');
            return JSON.parse(content);
        } catch (error) {
            throw new Error(`Failed to load Radiant output: ${error.message}`);
        }
    }

    /**
     * Load browser reference data
     */
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
     * Compare layout bounds with tolerance
     */
    compareLayout(radiantLayout, browserLayout, isText = false) {
        const differences = [];
        const properties = ['x', 'y', 'width', 'height'];

        for (const prop of properties) {
            const radiantVal = radiantLayout[prop] || 0;
            const browserVal = browserLayout[prop] || 0;
            const diff = Math.abs(radiantVal - browserVal);

            // Always include the difference, regardless of tolerance
            const tolerance = Math.max(isText && prop == 'width' ? radiantVal * 0.01 : 0, this.tolerance);
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
            // Browser format: elements and text nodes are mixed in children array
            if (node.children && Array.isArray(node.children)) {
                node.children.forEach((child, index) => {
                    children.push({
                        type: child.nodeType === 'text' ? 'text' : 'element',
                        node: child,
                        index: index
                    });
                });
            }
        }

        return children;
    }

    /**
     * Filter out non-layout elements but keep text nodes for unified comparison
     */
    filterForComparison(children) {
        if (!Array.isArray(children)) return [];

        return children.filter(child => {
            if (!child || !child.node) return false;

            // Keep text nodes for comparison
            if (child.type === 'text') {
                // Only keep text nodes that have visible layout
                if (child.node.layout && child.node.layout.rects) {
                    return child.node.layout.rects.length > 0; // Browser format
                } else if (child.node.layout && child.node.layout.width > 0) {
                    return true; // Radiant format
                }
                return false;
            }

            // For elements, skip non-layout tags
            const skipTags = ['head', 'script', 'style', 'meta', 'title', 'link'];
            if (skipTags.includes(child.node.tag)) return false;

            return true;
        });
    }

    /**
     * Compare two tree structures node by node with unified child comparison
     */
    compareNodes(radiantNode, browserNode, path = '', results = null, depth = 0) {
        if (!results) {
            results = {
                totalElements: 0,
                matchedElements: 0,
                totalTextNodes: 0,
                matchedTextNodes: 0,
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
                const radiantContent = (radiantNode.content || '').substring(0, 20) + (radiantNode.content.length > 20 ? '...' : '');
                const browserContent = (browserNode.text || '').substring(0, 20) + (browserNode.text.length > 20 ? '...' : '');
                // console.log("browser text:", browserNode.text);
                console.log(`${indent()}📝 Comparing text: "${radiantContent}" vs. "${browserContent}"`);
            }

            const contentMatch = (radiantNode.content || '').trim() === (browserNode.text || '').trim();
            if (contentMatch && radiantNode.layout && browserNode.layout) {
                // Compare layout - browser may have multiple rects for wrapped text
                const radiantLayout = radiantNode.layout;
                let browserLayout = browserNode.layout;

                // If browser has rects array, use the first rect for comparison
                if (browserNode.layout.rects && browserNode.layout.rects.length > 0) {
                    browserLayout = browserNode.layout.rects[0];
                }

                const layoutDiffs = this.compareLayout(radiantLayout, browserLayout, true);
                const maxDiff = layoutDiffs.length > 0 ? Math.max(...layoutDiffs.map(d => d.difference)) : 0;
                const exceedsToleranceCount = layoutDiffs.filter(d => d.exceedsTolerance).length;

                if (this.verbose) {
                    console.log(`${indent()}   Radiant: (${radiantLayout.x}, ${radiantLayout.y}, ${radiantLayout.width}×${radiantLayout.height})`);
                    console.log(`${indent()}   Browser: (${browserLayout.x}, ${browserLayout.y}, ${browserLayout.width}×${browserLayout.height})`);
                }

                const maxTolerance = Math.max(...layoutDiffs.map(d => d.tolerance));
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
                        console.log(`${indent()}   ❌ TEXT LAYOUT FAIL (${maxDiff.toFixed(1)}px > ${maxTolerance}px)`);
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
                if (radiantNode.layout && browserNode.layout) {
                    const layoutDiffs = this.compareLayout(radiantNode.layout, browserNode.layout);
                    const maxDiff = layoutDiffs.length > 0 ? Math.max(...layoutDiffs.map(d => d.difference)) : 0;
                    const exceedsToleranceCount = layoutDiffs.filter(d => d.exceedsTolerance).length;

                    if (this.verbose) {
                        console.log(`${indent()}   Radiant: (${radiantNode.layout.x}, ${radiantNode.layout.y}, ${radiantNode.layout.width}×${radiantNode.layout.height})`);
                        console.log(`${indent()}   Browser: (${browserNode.layout.x}, ${browserNode.layout.y}, ${browserNode.layout.width}×${browserNode.layout.height})`);
                    }

                    if (exceedsToleranceCount === 0) {
                        results.matchedElements++;
                        if (this.verbose) {
                            console.log(`${indent()}   ✅ ELEMENT MATCH (${maxDiff.toFixed(1)}px diff <= ${this.tolerance}px)`);
                        }
                    } else {
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
                            console.log(`${indent()}   ❌ ELEMENT LAYOUT FAIL (${maxDiff.toFixed(1)}px > ${this.tolerance}px)`);
                        }
                    }
                } else {
                    results.matchedElements++; // Count as match if no layout to compare
                    if (this.verbose) {
                        console.log(`${indent()}   ✅ ELEMENT MATCH (no layout)`);
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
                    tag: radiantNode.tag,
                    content: radiantNode.content
                },
                browser: {
                    type: browserIsText ? 'text' : 'element',
                    tag: browserNode.tag,
                    content: browserNode.text
                }
            });

            if (this.verbose) {
                const radiantDesc = radiantIsText ?
                    `text:"${(radiantNode.content || '').substring(0, 10)}..."` :
                    `<${radiantNode.tag}>`;
                const browserDesc = browserIsText ?
                    `text:"${(browserNode.text || '').substring(0, 10)}..."` :
                    `<${browserNode.tag}>`;
                console.log(`${indent()}⚠️  Comparing: ${radiantDesc} vs ${browserDesc}`);
                console.log(`${indent()}   ❌ TYPE MISMATCH`);
            }
        }

        // Get all children (elements + text nodes) in document order
        const radiantChildren = this.filterForComparison(this.getAllChildren(radiantNode, true));
        const browserChildren = this.filterForComparison(this.getAllChildren(browserNode, false));

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

            this.compareNodes(radiantChild, browserChild, childPath, results, depth + 1);
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
    generateReport(results, textResults, testName) {
        const report = {
            testName: testName,
            timestamp: new Date().toISOString(),
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
            differences: results.differences,
            tolerance: this.tolerance
        };

        return report;
    }

    /**
     * Print hierarchical report to console
     */
    printReport(report) {
        console.log(`\n📊 Test Case: ${report.testName}`);
        // Only show detailed statistics in verbose mode
        if (this.verbose) {
            // Element comparison results
            console.log(`🏗️  Element Structure Comparison:`);
            console.log(`   Total Elements: ${report.elementComparison.total}`);
            console.log(`   ✅ Matched: ${report.elementComparison.matched} (${report.elementComparison.passRate.toFixed(1)}%)`);
            if (report.elementComparison.failed > 0) console.log(`   ❌ Failed: ${report.elementComparison.failed}`);

            // Text comparison results
            if (report.textComparison.total > 0) {
                console.log(`📝 Text Node Comparison:`);
                console.log(`   Total Text Nodes: ${report.textComparison.total}`);
                console.log(`   ✅ Matched: ${report.textComparison.matched} (${report.textComparison.passRate.toFixed(1)}%)`);
                if (report.textComparison.failed > 0) console.log(`   ❌ Failed: ${report.textComparison.failed}`);
            }
            console.log('');
        }

        // Overall result
        const overallSuccess = report.elementComparison.passRate >= this.elementThreshold && report.textComparison.passRate >= this.textThreshold;
        const status = overallSuccess ? '✅ PASS' : '❌ FAIL';
        console.log(`${status} Overall: Elements ${report.elementComparison.passRate.toFixed(1)}%, Text ${report.textComparison.passRate.toFixed(1)}%`);
    }

    /**
     * Test a single HTML file
     */
    async testSingleFile(htmlFile, category) {
        const testName = path.basename(htmlFile, '.html');
        // console.log(`\n🧪 Testing: ${testName}`);

        try {
            // Run Radiant layout
            await this.runRadiantLayout(htmlFile);
            const radiantData = await this.loadRadiantOutput();

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

            const results = this.compareNodes(radiantTree, browserTree, 'root', null, 0);

            // Generate and print report (no separate text results needed)
            const report = this.generateReport(results, null, testName);
            this.printReport(report);

            return report;

        } catch (error) {
            console.log(`   ❌ ERROR: ${error.message}`);
            return {
                testName,
                error: error.message,
                timestamp: new Date().toISOString()
            };
        }
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

        console.log(`\n📂 Testing category: ${category}`);
        console.log('=' .repeat(50));

        const categoryDir = path.join(this.testDataDir, category);

        try {
            const files = await fs.readdir(categoryDir);
            const htmlFiles = files.filter(file => file.endsWith('.html'));

            if (htmlFiles.length === 0) {
                console.log(`No HTML files found in ${category}/`);
                return;
            }

            const results = [];
            for (const htmlFile of htmlFiles) {
                const result = await this.testSingleFile(path.join(categoryDir, htmlFile), category);
                if (result) {
                    results.push(result);
                }
            }

            // Summary - properly count passed/failed tests based on pass rates
            const successful = results.filter(r => {
                if (r.error) return false; // Tests with errors are failures
                // Use same criteria as printReport: configurable element/text thresholds
                const elementPassRate = r.elementComparison ? r.elementComparison.passRate : 0;
                const textPassRate = r.textComparison ? r.textComparison.passRate : 100;
                return elementPassRate >= this.elementThreshold && textPassRate >= this.textThreshold;
            }).length;
            const failed = results.length - successful;

            console.log(`\n📋 Category Summary:`);
            console.log(`   Total Tests: ${results.length}`);
            console.log(`   ✅ Successful: ${successful}`);
            if (failed > 0) console.log(`   ❌ Failed: ${failed}`);

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
     * Test files matching a pattern across all categories
     */
    async testByPattern(pattern) {
        console.log(`\n🔍 Testing files matching pattern: "${pattern}"`);
        console.log('=' .repeat(50));

        const categories = await this.getAvailableCategories();
        const allResults = [];
        let totalMatches = 0;

        for (const category of categories) {
            const categoryDir = path.join(this.testDataDir, category);

            try {
                const files = await fs.readdir(categoryDir);
                const matchingFiles = files.filter(file =>
                    file.endsWith('.html') && file.includes(pattern)
                );

                if (matchingFiles.length > 0) {
                    console.log(`\n📂 Found ${matchingFiles.length} matching files in ${category}:`);

                    for (const htmlFile of matchingFiles) {
                        console.log(`   🎯 ${htmlFile}`);
                        const result = await this.testSingleFile(path.join(categoryDir, htmlFile), category);
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
        const successful = allResults.filter(r => {
            if (r.error) return false;
            const elementPassRate = r.elementComparison ? r.elementComparison.passRate : 0;
            const textPassRate = r.textComparison ? r.textComparison.passRate : 100;
            return elementPassRate >= this.elementThreshold && textPassRate >= this.textThreshold;
        }).length;
        const failed = allResults.length - successful;

        console.log(`\n📋 Pattern Matching Summary:`);
        console.log(`   Pattern: "${pattern}"`);
        console.log(`   Total Matches: ${totalMatches}`);
        console.log(`   ✅ Successful: ${successful}`);
        if (failed > 0) console.log(`   ❌ Failed: ${failed}`);
        console.log(`   Success Rate: ${allResults.length > 0 ? (successful / allResults.length * 100).toFixed(1) : 0}%`);

        return allResults;
    }

    /**
     * Test all categories
     */
    async testAll() {
        console.log('🎯 Radiant Layout Engine Automated Tests');
        console.log('=========================================');

        const categories = await this.getAvailableCategories();
        console.log(`Found categories: ${categories.join(', ')}`);

        const allResults = [];
        for (const category of categories) {
            const categoryResults = await this.testCategory(category);
            allResults.push(...categoryResults);
        }

        // Overall summary - properly count passed/failed tests based on pass rates
        const successful = allResults.filter(r => {
            if (r.error) return false; // Tests with errors are failures
            // Use same criteria as printReport: configurable element/text thresholds
            const elementPassRate = r.elementComparison ? r.elementComparison.passRate : 0;
            const textPassRate = r.textComparison ? r.textComparison.passRate : 100;
            return elementPassRate >= this.elementThreshold && textPassRate >= this.textThreshold;
        }).length;
        const failed = allResults.length - successful;

        console.log(`\n🎯 OVERALL SUMMARY`);
        console.log('==================');
        console.log(`Total Tests: ${allResults.length}`);
        console.log(`✅ Successful: ${successful}`);
        console.log(`❌ Failed: ${failed}`);
        console.log(`Success Rate: ${allResults.length > 0 ? (successful / allResults.length * 100).toFixed(1) : 0}%`);

        return allResults;
    }
}

// Main execution
async function main() {
    const args = process.argv.slice(2);

    // Parse arguments
    const options = {
        tolerance: 5.0,
        verbose: false
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
  --category, -c <name>    Test specific category (e.g., basic, flex, grid)
  --test, -t <file>        Test specific HTML file
  --pattern, -p <text>     Test files containing pattern (runs in verbose mode)
  --tolerance <pixels>     Layout difference tolerance in pixels (default: 5.0)
  --element-threshold <pct> Element match threshold percentage (default: 80.0)
  --text-threshold <pct>   Text match threshold percentage (default: 70.0)
  --verbose, -v            Show detailed output
  --radiant-exe <path>     Path to Radiant executable (default: ./radiant.exe)
  --help, -h               Show this help message

Examples:
  node test/layout/test_radiant_layout.js                              # Test all categories
  node test/layout/test_radiant_layout.js -c baseline                  # Test baseline category only
  node test/layout/test_radiant_layout.js -t baseline_801_display_block.html  # Test specific file
  node test/layout/test_radiant_layout.js -p float                     # Test all files containing "float"
  node test/layout/test_radiant_layout.js --tolerance 2.0              # Use 2px tolerance
  node test/layout/test_radiant_layout.js --element-threshold 90       # Require 90% element match
  node test/layout/test_radiant_layout.js --text-threshold 80          # Require 80% text match
  node test/layout/test_radiant_layout.js -v                           # Verbose output

Note: Run this script from the project root directory.
`);
        process.exit(0);
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
            await tester.testByPattern(pattern);

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
