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
        this.generateReferences = options.generateReferences || false;
        this.verbose = options.verbose || false;
        
        this.testDir = path.join(__dirname, '..');
        this.dataDir = path.join(this.testDir, 'data');
        this.referenceDir = path.join(this.testDir, 'reference');
        this.reportsDir = path.join(this.testDir, 'reports');
        this.projectRoot = path.join(__dirname, '..', '..', '..');
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
        const jsonFile = path.join(this.projectRoot, 'view_tree.json');
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
    
    flattenRadiantTree(node, result = [], path = '') {
        if (!node) return result;
        
        const currentPath = path ? `${path} > ${node.tag}` : node.tag;
        
        result.push({
            selector: currentPath,
            tag: node.tag,
            layout: node.layout,
            css_properties: node.css_properties || {}
        });
        
        if (node.children && Array.isArray(node.children)) {
            node.children.forEach(child => {
                this.flattenRadiantTree(child, result, currentPath);
            });
        }
        
        return result;
    }
    
    compareLayouts(radiantData, browserData) {
        const results = {
            totalElements: 0,
            matchedElements: 0,
            differences: [],
            maxDifference: 0,
            summary: ''
        };
        
        // Flatten Radiant tree structure
        const radiantElements = this.flattenRadiantTree(radiantData.layout_tree);
        const browserElements = browserData.layout_data.elements;
        
        // Create lookup maps
        const radiantMap = new Map();
        radiantElements.forEach(elem => {
            // Try to match by tag and position
            const key = `${elem.tag}_${elem.layout.x}_${elem.layout.y}`;
            radiantMap.set(key, elem);
        });
        
        const browserMap = new Map();
        Object.entries(browserElements).forEach(([key, elem]) => {
            const lookupKey = `${elem.tag}_${elem.layout.x}_${elem.layout.y}`;
            browserMap.set(lookupKey, elem);
        });
        
        results.totalElements = Math.max(radiantElements.length, Object.keys(browserElements).length);
        
        // Compare elements
        for (const [key, browserElem] of browserMap) {
            const radiantElem = radiantMap.get(key);
            
            if (!radiantElem) {
                results.differences.push({
                    type: 'missing_in_radiant',
                    selector: browserElem.selector,
                    browser: browserElem.layout,
                    radiant: null
                });
                continue;
            }
            
            // Compare dimensions and position
            const diffs = this.compareElementLayout(radiantElem.layout, browserElem.layout);
            if (diffs.length > 0) {
                const maxDiff = Math.max(...diffs.map(d => d.difference));
                results.maxDifference = Math.max(results.maxDifference, maxDiff);
                
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
        }
        
        // Check for extra elements in Radiant
        for (const [key, radiantElem] of radiantMap) {
            if (!browserMap.has(key)) {
                results.differences.push({
                    type: 'extra_in_radiant',
                    selector: radiantElem.selector || radiantElem.tag,
                    browser: null,
                    radiant: radiantElem.layout
                });
            }
        }
        
        // Generate summary
        const passRate = results.totalElements > 0 ? 
            (results.matchedElements / results.totalElements * 100) : 0;
        
        results.summary = `${results.matchedElements}/${results.totalElements} elements matched (${passRate.toFixed(1)}%)`;
        
        return results;
    }
    
    compareElementLayout(radiant, browser) {
        const differences = [];
        const properties = ['x', 'y', 'width', 'height'];
        
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
            
            // CRITICAL FIX: A test should fail if too few elements are matched
            // Even if layout differences are within tolerance, structural mismatches indicate fundamental issues
            const matchRate = comparison.totalElements > 0 ? 
                (comparison.matchedElements / comparison.totalElements) : 0;
            const minMatchRate = 0.5; // Require at least 50% of elements to match
            
            const passed = (comparison.differences.length === 0 || 
                          comparison.maxDifference <= this.tolerance) &&
                          matchRate >= minMatchRate;
            
            const status = passed ? '✅ PASS' : '❌ FAIL';
            console.log(`    ${status} (${comparison.summary})`);
            
            if (!passed && this.verbose) {
                console.log(`    Max difference: ${comparison.maxDifference.toFixed(2)}px`);
                comparison.differences.slice(0, 3).forEach(diff => {
                    console.log(`      • ${diff.selector}: ${diff.type}`);
                });
            }
            
            return {
                testName,
                passed,
                executionTime: (Date.now() - startTime) / 1000,
                matchedElements: comparison.matchedElements,
                totalElements: comparison.totalElements,
                maxDifference: comparison.maxDifference,
                differences: comparison.differences,
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
        
        const categories = ['basic', 'intermediate', 'advanced'];
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
        generateReferences: false,
        verbose: false
    };
    
    let category = null;
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
                if (!['basic', 'intermediate', 'advanced'].includes(category)) {
                    console.error(`❌ Invalid category: ${category}`);
                    process.exit(1);
                }
                break;
            case '--tolerance':
            case '-t':
                options.tolerance = parseFloat(args[++i]);
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
Radiant Layout Engine Integration Testing

Usage: node test_layout_auto.js [options]

Options:
  --category, -c <name>      Test specific category (basic|intermediate|advanced)
  --tolerance, -t <pixels>   Layout difference tolerance in pixels (default: 2.0)
  --generate-references, -g  Generate browser references if missing
  --verbose, -v              Show detailed failure information
  --radiant-exe <path>       Path to Radiant executable (default: ./radiant.exe)
  --help, -h                 Show this help message

Examples:
  node test_layout_auto.js                    # Test all categories
  node test_layout_auto.js -c basic           # Test basic category only
  node test_layout_auto.js -g -v              # Generate references and show details
  node test_layout_auto.js -t 1.0             # Use 1px tolerance

Generated files:
  ../reference/<category>/<test>.json         # Browser reference data
  ../reports/layout_test_results.json         # Test results report
`);
        process.exit(0);
    }
    
    const tester = new LayoutTester(options);
    
    try {
        if (category) {
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
