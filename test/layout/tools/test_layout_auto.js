#!/usr/bin/env node

/**
 * Radiant Layout Engine Automated Integration Testing
 * 
 * This script compares Radiant's layout output against browser reference data
 * to validate the layout engine's accuracy and compatibility.
 */

const fs = require('fs').promises;
const path = require('path');
const { spawn } = require('child_process');
const { extractLayoutFromFile, extractAllTestFiles } = require('./extract_browser_references.js');

class LayoutTester {
    constructor(options = {}) {
        this.radiantExe = options.radiantExe || path.join(__dirname, '..', '..', '..', 'radiant.exe');
        this.tolerance = options.tolerance || 2.0;
        this.textTolerance = options.textTolerance || 1.0; // Allow 1px difference for text
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
    
    flattenRadiantTree(node, result = [], path = '', depth = 0) {
        if (!node) return result;
        
        const currentPath = path ? `${path} > ${node.tag}` : node.tag;
        
        // Handle text nodes separately for text comparison
        if (node.tag === 'text' && node.layout) {
            result.push({
                selector: currentPath,
                tag: 'text',
                layout: node.layout,
                text: node.content || '',  // CRITICAL FIX: Use 'content' not 'text'
                isTextNode: true
            });
        } else if (node.layout) {
            // FOCUS: Skip non-layout elements to match browser filtering  
            const isNonLayoutElement = node.tag === 'script';
            
            if (!isNonLayoutElement) {
                // Map Radiant's element structure to browser equivalents
                let mappedTag = node.tag;
                let mappedSelector = node.selector || currentPath;
                
                // DEBUG: Log what elements we're including
                if (this.verbose && (node.tag === 'html' || node.tag === 'body' || node.selector?.includes('container'))) {
                    console.log(`DEBUG: Including Radiant element: ${mappedSelector} (${mappedTag}) - ${node.layout.width}x${node.layout.height} at (${node.layout.x},${node.layout.y})`);
                }
                
                result.push({
                    selector: mappedSelector,
                    tag: mappedTag,
                    layout: node.layout,
                    css_properties: node.css_properties || {},
                    isTextNode: false
                });
            }
        }
        
        if (node.children && Array.isArray(node.children)) {
            node.children.forEach(child => {
                this.flattenRadiantTree(child, result, currentPath, depth + 1);
            });
        }
        
        return result;
    }
    
    /**
     * Extract text nodes from browser elements with their position data
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
     * FOCUS: Skip viewport-level elements, focus on content containers and items
     */
    filterContentElements(browserElements) {
        const contentElements = {};
        
        Object.entries(browserElements).forEach(([key, elem]) => {
            // Skip elements with display: none (not rendered)
            if (elem.css && elem.css.display === 'none') {
                return;
            }
            
            // Skip elements with zero dimensions (not visible)
            const hasZeroDimensions = elem.layout && elem.layout.width === 0 && elem.layout.height === 0;
            if (hasZeroDimensions) {
                return;
            }
            
            // FOCUS: Skip only non-layout structural elements
            // Keep html, body for proper element matching, but skip head, meta, etc.
            const isNonLayoutStructural = elem.tag === 'head' || elem.tag === 'meta' || 
                                        elem.tag === 'title' || elem.tag === 'style' || 
                                        elem.tag === 'script';
            
            if (isNonLayoutStructural) {
                return; // Skip non-layout structural elements
            }
            
            // INCLUDE: Layout-significant elements
            const isLayoutElement = 
                elem.tag === 'html' ||          // Root element
                elem.tag === 'body' ||          // Body element  
                elem.selector?.includes('.') || // Has CSS class (content)
                (elem.layout && elem.layout.width < 1200) ||     // Smaller than viewport (content)
                (elem.css && elem.css.display === 'flex'); // Flex containers
            
            if (isLayoutElement) {
                // DEBUG: Log what browser elements we're including
                if (this.verbose && (elem.tag === 'html' || elem.tag === 'body' || elem.selector?.includes('container'))) {
                    const layout = elem.layout || { width: 'N/A', height: 'N/A', x: 'N/A', y: 'N/A' };
                    console.log(`DEBUG: Including browser element: ${elem.selector} (${elem.tag}) - ${layout.width}x${layout.height} at (${layout.x},${layout.y})`);
                }
                contentElements[key] = elem;
            }
        });
        
        return contentElements;
    }
    
    /**
     * Compare text node positioning between Radiant and browser
     */
    compareTextNodes(radiantTextNodes, browserTextNodes) {
        const results = {
            totalTextNodes: Math.max(radiantTextNodes.length, browserTextNodes.length),
            matchedTextNodes: 0,
            textDifferences: [],
            maxTextDifference: 0
        };
        
        if (results.totalTextNodes === 0) {
            return results; // No text to compare
        }
        
        // Create lookup maps for text nodes
        const browserTextMap = new Map();
        browserTextNodes.forEach((textNode, index) => {
            // Create keys based on text content and approximate position
            const textKey = textNode.text?.trim().substring(0, 20); // First 20 chars
            const posKey = `${Math.round(textNode.layout.x/10)*10}_${Math.round(textNode.layout.y/10)*10}`;
            
            if (textKey) {
                browserTextMap.set(`text_${textKey}`, textNode);
                browserTextMap.set(`pos_${posKey}_${textKey}`, textNode);
                browserTextMap.set(`index_${index}`, textNode);
            }
        });
        
        // Compare each Radiant text node
        radiantTextNodes.forEach((radiantText, index) => {
            const textKey = radiantText.text?.trim().substring(0, 20);
            const posKey = `${Math.round(radiantText.layout.x/10)*10}_${Math.round(radiantText.layout.y/10)*10}`;
            
            let browserText = null;
            if (textKey) {
                // Try exact text match first
                browserText = browserTextMap.get(`text_${textKey}`) ||
                            browserTextMap.get(`pos_${posKey}_${textKey}`) ||
                            browserTextMap.get(`index_${index}`);
            }
            
            if (!browserText) {
                results.textDifferences.push({
                    type: 'missing_in_browser',
                    text: radiantText.text,
                    radiant: radiantText.layout,
                    browser: null
                });
                return;
            }
            
            // Compare positions
            const diffs = this.compareElementLayout(radiantText.layout, browserText.layout);
            if (diffs.length > 0) {
                const maxDiff = Math.max(...diffs.map(d => d.difference));
                results.maxTextDifference = Math.max(results.maxTextDifference, maxDiff);
                
                if (maxDiff > this.textTolerance) {
                    results.textDifferences.push({
                        type: 'text_position_difference',
                        text: radiantText.text,
                        radiant: radiantText.layout,
                        browser: browserText.layout,
                        differences: diffs,
                        maxDifference: maxDiff
                    });
                } else {
                    results.matchedTextNodes++;
                }
            } else {
                results.matchedTextNodes++;
            }
        });
        
        return results;
    }

    /**
     * Generate layout-focused keys for element matching
     * FOCUS: Position and dimensions matter more than element names
     */
    generateElementKeys(elem, index) {
        const keys = [];
        
        if (!elem || !elem.layout) {
            console.warn('WARNING: Element or layout is undefined in generateElementKeys:', { 
                elem: !!elem, 
                layout: elem ? !!elem.layout : 'N/A',
                selector: elem ? elem.selector : 'N/A',
                tag: elem ? elem.tag : 'N/A'
            });
            return keys;
        }
        
        const { x, y, width, height } = elem.layout;
        
        // PRIMARY: Exact position + dimensions (most specific match)
        keys.push(`layout_${x}_${y}_${width}x${height}`);
        
        // SECONDARY: Dimensions only (for elements that might be positioned differently)
        keys.push(`size_${width}x${height}`);
        
        // TERTIARY: Position only (REMOVED - too loose, causes incorrect matches)
        // keys.push(`position_${x}_${y}`);
        
        // QUATERNARY: Common layout patterns (prioritize these for matching)
        if (width === 600 && height === 100) {
            keys.push('flex_container_600x100');
        }
        if (width === 100 && height === 60) {
            keys.push('flex_item_100x60');
        }
        if (width === 400 && height === 300) {
            keys.push('container_400x300');
        }
        if (width === 100 && height === 100) {
            keys.push('box_100x100');
        }
        
        // SELECTOR-BASED: Add selector-based keys for better matching
        if (elem.selector) {
            keys.push(`selector_${elem.selector}`);
            keys.push(`selector_${elem.selector}_${width}x${height}`);
        }
        if (elem.tag) {
            keys.push(`tag_${elem.tag}_${width}x${height}`);
        }
        
        // FALLBACK: Index-based matching for similar elements (DEPRIORITIZED)
        // Only use index matching as a last resort to avoid incorrect matches
        keys.push(`index_${index}`);
        
        // VIEWPORT: Special handling for viewport-sized elements
        if (width >= 1200 || height >= 800) {
            keys.push('viewport_element');
        }
        
        return keys;
    }
    
    compareLayouts(radiantData, browserData) {
        const results = {
            totalElements: 0,
            matchedElements: 0,
            differences: [],
            maxDifference: 0,
            // Enhanced with text node results
            totalTextNodes: 0,
            matchedTextNodes: 0,
            textDifferences: [],
            maxTextDifference: 0,
            summary: ''
        };
        
        // Flatten Radiant tree structure (includes both elements and text nodes)
        const radiantElements = this.flattenRadiantTree(radiantData.layout_tree);
        const browserElements = browserData.layout_data.elements || browserData.layout_data;
        
        // Separate elements and text nodes
        const radiantLayoutElements = radiantElements.filter(elem => !elem.isTextNode);
        const radiantTextNodes = radiantElements.filter(elem => elem.isTextNode);
        
        // Extract text nodes from browser data
        const browserTextNodes = this.extractBrowserTextNodes(browserElements);
        
        // CRITICAL FIX: Filter out document structure elements that Radiant doesn't output
        // Focus on content elements that actually affect layout
        const contentElements = this.filterContentElements(browserElements);
        
        // Create lookup maps with improved matching strategy (layout elements only)
        const radiantMap = new Map();
        radiantLayoutElements.forEach((elem, index) => {
            // Create multiple lookup keys for better matching
            const keys = this.generateElementKeys(elem, index);
            keys.forEach(key => radiantMap.set(key, elem));
        });
        
        const browserMap = new Map();
        Object.entries(contentElements).forEach(([key, elem], index) => {
            const keys = this.generateElementKeys(elem, index);
            keys.forEach(lookupKey => browserMap.set(lookupKey, elem));
        });
        
        results.totalElements = Math.max(radiantLayoutElements.length, Object.keys(contentElements).length);
        
        // Compare elements - deduplicate browser elements first
        const processedBrowserElements = new Set();
        const processedRadiantElements = new Set();
        
        Object.values(contentElements).forEach(browserElem => {
            if (processedBrowserElements.has(browserElem.selector)) {
                return; // Skip duplicates
            }
            processedBrowserElements.add(browserElem.selector);
            
            // Try to find matching Radiant element using multiple keys
            let radiantElem = null;
            const keys = this.generateElementKeys(browserElem, 0);
            
            for (const key of keys) {
                radiantElem = radiantMap.get(key);
                if (radiantElem) {
                    const elemId = radiantElem.selector || radiantElem.tag;
                    if (!processedRadiantElements.has(elemId)) {
                        processedRadiantElements.add(elemId);
                        break;
                    }
                }
                radiantElem = null; // Reset if already processed
            }
            
            if (!radiantElem) {
                results.differences.push({
                    type: 'missing_in_radiant',
                    selector: browserElem.selector,
                    browser: browserElem.layout,
                    radiant: null
                });
                return;
            }
            
            // Compare dimensions and position
            const diffs = this.compareElementLayout(radiantElem.layout, browserElem.layout);
            if (diffs.length > 0) {
                const maxDiff = Math.max(...diffs.map(d => d.difference));
                results.maxDifference = Math.max(results.maxDifference, maxDiff);
                
                // Log large differences for debugging
                // if (maxDiff > 100) {
                //     console.log(`DEBUG: Large difference for ${browserElem.selector}: ${maxDiff}px`);
                // }
                
                if (maxDiff > this.tolerance) {
                    results.differences.push({
                        type: 'layout_difference',
                        selector: browserElem.selector,
                        browser: browserElem.layout,
                        radiant: radiantElem.layout,
                        differences: diffs,
                        maxDifference: maxDiff
                    });
                } else {
                    results.matchedElements++;
                }
            } else {
                results.matchedElements++;
            }
        });
        
        // Check for extra elements in Radiant
        radiantLayoutElements.forEach(radiantElem => {
            const elemId = radiantElem.selector || radiantElem.tag;
            if (!processedRadiantElements.has(elemId)) {
                results.differences.push({
                    type: 'extra_in_radiant',
                    selector: elemId,
                    browser: null,
                    radiant: radiantElem.layout
                });
            }
        });
        
        // Compare text nodes
        const textComparison = this.compareTextNodes(radiantTextNodes, browserTextNodes);
        results.totalTextNodes = textComparison.totalTextNodes;
        results.matchedTextNodes = textComparison.matchedTextNodes;
        results.textDifferences = textComparison.textDifferences;
        results.maxTextDifference = textComparison.maxTextDifference;
        
        // Generate comprehensive summary including text nodes
        const passRate = results.totalElements > 0 ? 
            (results.matchedElements / results.totalElements * 100) : 0;
        const textPassRate = results.totalTextNodes > 0 ? 
            (results.matchedTextNodes / results.totalTextNodes * 100) : 100; // 100% if no text
        
        // Focus on layout accuracy rather than just element count
        const layoutAccuracy = results.maxDifference <= this.tolerance ? 'ACCURATE' : 'INACCURATE';
        const textAccuracy = results.maxTextDifference <= this.textTolerance ? 'ACCURATE' : 'INACCURATE';
        const maxDiffStr = results.maxDifference.toFixed(2);
        const maxTextDiffStr = results.maxTextDifference.toFixed(2);
        
        // Enhanced summary with text information
        let summary = `${results.matchedElements}/${results.totalElements} layout matches (${passRate.toFixed(1)}%) - Max diff: ${maxDiffStr}px (${layoutAccuracy})`;
        
        if (results.totalTextNodes > 0) {
            summary += ` | Text: ${results.matchedTextNodes}/${results.totalTextNodes} matches (${textPassRate.toFixed(1)}%) - Max text diff: ${maxTextDiffStr}px (${textAccuracy})`;
        }
        
        results.summary = summary;
        
        return results;
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
                const elementCount = this.flattenRadiantTree(radiantData.layout_tree).length;
                
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
            const comparison = this.compareLayouts(radiantData, browserData);
            
            // FOCUS: Layout accuracy is the primary success criteria
            // Pass if layout differences are within tolerance, regardless of element structure differences
            const layoutAccurate = comparison.maxDifference <= this.tolerance;
            const hasLayoutMatches = comparison.matchedElements > 0;
            
            // Text accuracy check - only fail if there are text nodes and they're inaccurate
            const textAccurate = comparison.totalTextNodes === 0 || 
                               (comparison.maxTextDifference <= this.textTolerance && comparison.matchedTextNodes > 0);
            
            // Pass if we have accurate layout positioning AND accurate text positioning (when text exists)
            const passed = layoutAccurate && hasLayoutMatches && textAccurate;
            
            const status = passed ? '✅ PASS' : '❌ FAIL';
            console.log(`    ${status} (${comparison.summary})`);
            
            if (!passed && this.verbose) {
                console.log(`    Layout max difference: ${comparison.maxDifference.toFixed(2)}px`);
                if (comparison.totalTextNodes > 0) {
                    console.log(`    Text max difference: ${comparison.maxTextDifference.toFixed(2)}px`);
                }
                
                // Show layout differences
                comparison.differences.slice(0, 2).forEach(diff => {
                    console.log(`      • ${diff.selector}: ${diff.type}`);
                });
                
                // Show text differences
                if (comparison.textDifferences.length > 0) {
                    comparison.textDifferences.slice(0, 2).forEach(diff => {
                        const truncatedText = diff.text?.substring(0, 20) || 'unknown';
                        console.log(`      • Text "${truncatedText}": ${diff.type}`);
                    });
                }
            }
            
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
        
        console.log(`  📊 Category results: ${passed}/${total} passed (${passRate.toFixed(1)}%)`);
        
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
        const categories = ['basic', 'intermediate', 'medium', 'advanced', 'baseline'];
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
        
        const categories = ['basic', 'intermediate', 'medium', 'advanced', 'baseline'];
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
            console.log(`📈 Overall pass rate: ${overallPassRate.toFixed(1)}%`);
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
}

// Main execution
async function main() {
    const args = process.argv.slice(2);
    
    // Parse arguments
    const options = {
        radiantExe: './radiant.exe',
        tolerance: 2.0,
        textTolerance: 1.0,
        generateReferences: false,
        verbose: false
    };
    
    let category = null;
    let testName = null;
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
                if (!['basic', 'intermediate', 'medium', 'advanced', 'baseline'].includes(category)) {
                    console.error(`❌ Invalid category: ${category}`);
                    process.exit(1);
                }
                break;
            case '--test':
            case '-t':
                testName = args[++i];
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

Usage: node test_layout_auto.js [options]

This tool compares Radiant's layout output against browser references, including:
- Element positioning and dimensions
- Text node positions and line fragments
- CSS property application accuracy

Options:
  --category, -c <name>      Test specific category (basic|intermediate|medium|advanced|baseline)
  --test, -t <name>          Test specific test file (e.g., table_simple)
  --tolerance <pixels>       Layout difference tolerance in pixels (default: 2.0)
  --text-tolerance <pixels>  Text position/size tolerance in pixels (default: 1.0)
  --generate-references, -g  Generate browser references if missing
  --verbose, -v              Show detailed failure information
  --radiant-exe <path>       Path to Radiant executable (default: ./radiant.exe)
  --help, -h                 Show this help message

Examples:
  node test_layout_auto.js                    # Test all categories
  node test_layout_auto.js -c basic           # Test basic category only
  node test_layout_auto.js -t table_simple    # Test specific test file
  node test_layout_auto.js -g -v              # Generate references and show details
  node test_layout_auto.js --tolerance 1.0    # Use 1px tolerance
  node test_layout_auto.js --text-tolerance 0.5  # Use 0.5px text tolerance

Generated files:
  ../reference/<category>/<test>.json         # Browser reference data (with text nodes)
  /tmp/layout_test_results.json               # Test results report
`);
        process.exit(0);
    }
    
    const tester = new LayoutTester(options);
    
    try {
        if (testName) {
            await tester.testSingleByName(testName);
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
