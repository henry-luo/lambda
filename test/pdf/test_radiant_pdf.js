#!/usr/bin/env node

/**
 * Radiant PDF Test Runner
 * 
 * Compares Radiant's PDF rendering against pdf.js operator lists
 * 
 * Usage:
 *   node test_radiant_pdf.js                    # Run basic suite
 *   node test_radiant_pdf.js --suite=basic      # Run specific suite
 *   node test_radiant_pdf.js --test=tracemonkey # Run single test
 *   node test_radiant_pdf.js -v                 # Verbose output
 */

const fs = require('fs').promises;
const path = require('path');
const { spawn } = require('child_process');
const os = require('os');

const CURRENT_PLATFORM = os.platform();

class RadiantPdfTester {
    constructor(options = {}) {
        this.radiantExe = options.radiantExe || './lambda.exe';
        this.dataDir = path.join(__dirname, 'data');
        this.referenceDir = path.join(__dirname, 'reference');
        this.outputDir = path.join(__dirname, 'output');
        this.verbose = options.verbose || false;
        this.tolerance = options.tolerance || 2.0;  // Position tolerance in points
        this.projectRoot = options.projectRoot || process.cwd();
        this.testIdCounter = 0;
    }

    /**
     * Generate unique output file for parallel execution
     */
    getUniqueOutputFile(testName) {
        return path.join(this.outputDir, `${testName}_${++this.testIdCounter}.json`);
    }

    /**
     * Run Radiant layout on a PDF file
     */
    async runRadiantLayout(pdfFile, outputFile) {
        return new Promise((resolve, reject) => {
            const args = ['layout', pdfFile, '--view-output', outputFile];
            
            if (this.verbose) {
                console.log(`   Running: ${this.radiantExe} ${args.join(' ')}`);
            }
            
            const proc = spawn(this.radiantExe, args, { 
                cwd: this.projectRoot,
                stdio: ['ignore', 'pipe', 'pipe']
            });
            
            let stdout = '';
            let stderr = '';
            
            proc.stdout.on('data', (data) => stdout += data.toString());
            proc.stderr.on('data', (data) => stderr += data.toString());
            
            const timeout = setTimeout(() => {
                proc.kill();
                reject(new Error('Radiant execution timeout (30s)'));
            }, 30000);
            
            proc.on('close', (code) => {
                clearTimeout(timeout);
                if (code === 0) {
                    resolve({ stdout, stderr, outputFile });
                } else {
                    reject(new Error(`Radiant failed with exit code ${code}: ${stderr}`));
                }
            });
            
            proc.on('error', (error) => {
                clearTimeout(timeout);
                reject(new Error(`Failed to spawn Radiant: ${error.message}`));
            });
        });
    }

    /**
     * Load Radiant view tree output
     */
    async loadRadiantOutput(outputFile) {
        try {
            const content = await fs.readFile(outputFile, 'utf8');
            return JSON.parse(content);
        } catch (error) {
            if (error.code === 'ENOENT') {
                return null;
            }
            throw error;
        }
    }

    /**
     * Load pdf.js reference
     */
    async loadPdfjsReference(testName) {
        const refFile = path.join(this.referenceDir, `${testName}.json`);
        try {
            const content = await fs.readFile(refFile, 'utf8');
            return JSON.parse(content);
        } catch (error) {
            if (error.code === 'ENOENT') return null;
            throw error;
        }
    }

    /**
     * Extract text items from Radiant view tree
     */
    extractRadiantTextItems(viewTree) {
        const textItems = [];
        
        function traverse(node) {
            if (!node) return;
            
            // Check if this is a text node
            if (node.type === 'text' || node.text) {
                const layout = node.layout || {};
                const font = node.computed?.font || {};
                textItems.push({
                    str: node.content || node.text || '',
                    x: layout.x || node.x || 0,
                    y: layout.y || node.y || 0,
                    width: layout.width || node.width || 0,
                    height: layout.height || node.height || 0,
                    fontSize: font.size || node.fontSize || node.font_size || 12,
                    fontName: font.family || node.fontName || node.font_family || ''
                });
            }
            
            // Traverse children
            if (node.children && Array.isArray(node.children)) {
                for (const child of node.children) {
                    traverse(child);
                }
            }
        }
        
        traverse(viewTree);
        return textItems;
    }

    /**
     * Compare text items between Radiant and pdf.js
     */
    compareTextItems(radiantItems, pdfjsItems) {
        const results = {
            matched: 0,
            mismatched: 0,
            missing: 0,
            extra: 0,
            details: []
        };
        
        if (!pdfjsItems || pdfjsItems.length === 0) {
            results.extra = radiantItems.length;
            return results;
        }
        
        if (!radiantItems || radiantItems.length === 0) {
            results.missing = pdfjsItems.length;
            return results;
        }
        
        // Create lookup map for pdf.js items by text
        const pdfjsMap = new Map();
        for (const item of pdfjsItems) {
            const key = (item.str || '').trim();
            if (!key) continue;
            if (!pdfjsMap.has(key)) pdfjsMap.set(key, []);
            pdfjsMap.get(key).push(item);
        }
        
        // Compare each Radiant item
        for (const radiantItem of radiantItems) {
            const key = (radiantItem.str || radiantItem.text || '').trim();
            if (!key) continue;
            
            const pdfjsCandidates = pdfjsMap.get(key);
            if (!pdfjsCandidates || pdfjsCandidates.length === 0) {
                results.extra++;
                if (this.verbose) {
                    results.details.push({
                        type: 'extra',
                        text: key,
                        radiant: radiantItem
                    });
                }
                continue;
            }
            
            // Find best position match
            const pdfjsItem = pdfjsCandidates[0];
            const [a, b, c, d, tx, ty] = pdfjsItem.transform || [1, 0, 0, 1, 0, 0];
            
            // PDF coordinates: origin at bottom-left, y increases upward
            // Need to compare appropriately based on page height
            const xDiff = Math.abs((radiantItem.x || 0) - tx);
            // Note: y comparison may need adjustment for coordinate system
            
            if (xDiff <= this.tolerance) {
                results.matched++;
                pdfjsCandidates.shift();  // Remove matched item
            } else {
                results.mismatched++;
                if (this.verbose) {
                    results.details.push({
                        type: 'position_mismatch',
                        text: key,
                        radiant: { x: radiantItem.x, y: radiantItem.y },
                        pdfjs: { x: tx, y: ty },
                        diff: { x: xDiff }
                    });
                }
            }
        }
        
        // Count remaining pdf.js items as missing
        for (const [key, items] of pdfjsMap) {
            results.missing += items.length;
            if (this.verbose) {
                for (const item of items) {
                    results.details.push({
                        type: 'missing',
                        text: key,
                        pdfjs: item
                    });
                }
            }
        }
        
        return results;
    }

    /**
     * Compare operation counts as a sanity check
     */
    compareOperationCounts(radiantOutput, pdfjsRef) {
        const results = {
            pdfjs: {
                totalOps: 0,
                textOps: 0,
                pathOps: 0,
                textItems: 0
            },
            radiant: {
                textItems: 0,
                totalNodes: 0
            }
        };
        
        if (pdfjsRef && pdfjsRef.pages && pdfjsRef.pages[0]) {
            const page = pdfjsRef.pages[0];
            results.pdfjs.totalOps = page.operationCount || 0;
            results.pdfjs.textItems = page.textItemCount || page.textItems?.length || 0;
            
            if (page.operationSummary) {
                results.pdfjs.textOps = (page.operationSummary.showText || 0) + 
                                        (page.operationSummary.showSpacedText || 0);
                results.pdfjs.pathOps = (page.operationSummary.stroke || 0) + 
                                        (page.operationSummary.fill || 0);
            }
        }
        
        if (radiantOutput) {
            const textItems = this.extractRadiantTextItems(radiantOutput);
            results.radiant.textItems = textItems.length;
            
            function countNodes(node) {
                if (!node) return 0;
                let count = 1;
                if (node.children && Array.isArray(node.children)) {
                    for (const child of node.children) {
                        count += countNodes(child);
                    }
                }
                return count;
            }
            results.radiant.totalNodes = countNodes(radiantOutput);
        }
        
        return results;
    }

    /**
     * Run a single test
     */
    async runTest(testName, category = 'basic') {
        const pdfFile = path.join(this.dataDir, category, `${testName}.pdf`);
        const outputFile = this.getUniqueOutputFile(testName);
        
        console.log(`\n📄 Testing: ${testName}`);
        
        // Check if PDF exists
        try {
            await fs.access(pdfFile);
        } catch (error) {
            console.log(`   ⚠️  PDF file not found: ${pdfFile}`);
            return { status: 'not_found', testName };
        }
        
        // Run Radiant layout
        let radiantResult;
        try {
            radiantResult = await this.runRadiantLayout(pdfFile, outputFile);
        } catch (error) {
            console.log(`   ❌ Radiant failed: ${error.message}`);
            return { status: 'error', testName, error: error.message };
        }
        
        // Load outputs
        const radiantOutput = await this.loadRadiantOutput(outputFile);
        const pdfjsRef = await this.loadPdfjsReference(testName);
        
        if (!pdfjsRef) {
            console.log(`   ⚠️  No pdf.js reference found - run 'npm run export' first`);
            
            // Still report basic stats
            if (radiantOutput) {
                const stats = this.compareOperationCounts(radiantOutput, null);
                console.log(`   📊 Radiant: ${stats.radiant.textItems} text items, ${stats.radiant.totalNodes} nodes`);
            }
            
            return { status: 'no_reference', testName };
        }
        
        // Compare operation counts
        const stats = this.compareOperationCounts(radiantOutput, pdfjsRef);
        
        if (this.verbose) {
            console.log(`   📊 pdf.js: ${stats.pdfjs.totalOps} ops, ${stats.pdfjs.textItems} text items`);
            console.log(`   📊 Radiant: ${stats.radiant.textItems} text items, ${stats.radiant.totalNodes} nodes`);
        }
        
        // Compare text items
        const radiantTextItems = this.extractRadiantTextItems(radiantOutput.layout_tree || radiantOutput);
        const pdfjsTextItems = pdfjsRef.pages?.[0]?.textItems || [];
        
        const comparison = this.compareTextItems(radiantTextItems, pdfjsTextItems);
        
        const total = comparison.matched + comparison.mismatched + comparison.missing;
        const passRate = total > 0 ? (comparison.matched / total) * 100 : 0;
        
        // Determine status
        let status;
        if (passRate >= 90) {
            console.log(`   ✅ PASS: ${passRate.toFixed(1)}% text match (${comparison.matched}/${total})`);
            status = 'pass';
        } else if (passRate >= 50) {
            console.log(`   ⚠️  PARTIAL: ${passRate.toFixed(1)}% text match (${comparison.matched}/${total})`);
            status = 'partial';
        } else {
            console.log(`   ❌ FAIL: ${passRate.toFixed(1)}% text match (${comparison.matched}/${total})`);
            status = 'fail';
        }
        
        if (this.verbose) {
            console.log(`      Matched: ${comparison.matched}`);
            console.log(`      Mismatched: ${comparison.mismatched}`);
            console.log(`      Missing: ${comparison.missing}`);
            console.log(`      Extra: ${comparison.extra}`);
            
            // Show first few mismatches
            const mismatches = comparison.details.filter(d => d.type !== 'extra').slice(0, 5);
            for (const m of mismatches) {
                if (m.type === 'missing') {
                    console.log(`      📭 Missing: "${m.text}"`);
                } else if (m.type === 'position_mismatch') {
                    console.log(`      📍 Position: "${m.text}" diff=${m.diff.x.toFixed(1)}`);
                }
            }
        }
        
        return { 
            status, 
            testName, 
            passRate, 
            comparison,
            stats
        };
    }

    /**
     * Run all tests in a suite
     */
    async runSuite(suiteName = 'basic') {
        const { glob } = await import('glob');
        const suiteDir = path.join(this.dataDir, suiteName);
        
        let pdfFiles;
        try {
            pdfFiles = await glob('*.pdf', { cwd: suiteDir });
        } catch (error) {
            console.error(`Failed to find PDFs in ${suiteDir}`);
            pdfFiles = [];
        }
        
        if (pdfFiles.length === 0) {
            console.log(`\n⚠️  No PDF files found in ${suiteDir}`);
            console.log(`   Run the setup script first to copy test PDFs:\n`);
            console.log(`   # From project root:`);
            console.log(`   cp pdf-js/test/pdfs/tracemonkey.pdf test/pdf/data/basic/`);
            console.log(`   cp pdf-js/test/pdfs/standard_fonts.pdf test/pdf/data/basic/`);
            console.log(`   # etc.\n`);
            return { total: 0 };
        }
        
        console.log(`\n🧪 Running PDF test suite: ${suiteName}`);
        console.log(`   Found ${pdfFiles.length} PDF files`);
        
        // Ensure output directory exists
        await fs.mkdir(this.outputDir, { recursive: true });
        
        const results = {
            total: pdfFiles.length,
            passed: 0,
            failed: 0,
            partial: 0,
            errors: 0,
            noReference: 0,
            notFound: 0,
            tests: []
        };
        
        for (const pdfFile of pdfFiles.sort()) {
            const testName = path.basename(pdfFile, '.pdf');
            const result = await this.runTest(testName, suiteName);
            results.tests.push(result);
            
            switch (result.status) {
                case 'pass': results.passed++; break;
                case 'partial': results.partial++; break;
                case 'fail': results.failed++; break;
                case 'error': results.errors++; break;
                case 'no_reference': results.noReference++; break;
                case 'not_found': results.notFound++; break;
            }
        }
        
        // Print summary
        console.log('\n' + '='.repeat(50));
        console.log('📊 Test Suite Summary');
        console.log('='.repeat(50));
        console.log(`   Total:        ${results.total}`);
        console.log(`   ✅ Passed:     ${results.passed}`);
        console.log(`   ⚠️  Partial:    ${results.partial}`);
        console.log(`   ❌ Failed:     ${results.failed}`);
        console.log(`   💥 Errors:     ${results.errors}`);
        console.log(`   📭 No Ref:     ${results.noReference}`);
        
        // Calculate overall pass rate
        const tested = results.passed + results.partial + results.failed;
        if (tested > 0) {
            const overallPass = ((results.passed + results.partial * 0.5) / tested) * 100;
            console.log(`\n   Overall Score: ${overallPass.toFixed(1)}%`);
        }
        
        return results;
    }
}

// CLI
async function main() {
    const args = process.argv.slice(2);
    
    if (args.includes('--help') || args.includes('-h')) {
        console.log(`
Radiant PDF Test Runner

Usage:
  node test_radiant_pdf.js [options]

Options:
  --suite=<name>   Run tests in data/<name>/ (default: basic)
  --test=<name>    Run a single test by name
  -v, --verbose    Show detailed comparison output
  -h, --help       Show this help message

Examples:
  node test_radiant_pdf.js                     # Run basic suite
  node test_radiant_pdf.js --suite=basic -v   # Verbose basic suite
  node test_radiant_pdf.js --test=tracemonkey # Single test
`);
        process.exit(0);
    }
    
    const options = {
        verbose: args.includes('-v') || args.includes('--verbose'),
        projectRoot: __dirname
    };
    
    // Navigate to project root (find lambda.exe)
    let searchDir = __dirname;
    for (let i = 0; i < 5; i++) {
        const parentDir = path.dirname(searchDir);
        try {
            await fs.access(path.join(parentDir, 'lambda.exe'));
            options.projectRoot = parentDir;
            break;
        } catch {
            searchDir = parentDir;
        }
    }
    
    const tester = new RadiantPdfTester(options);
    
    // Check for --test argument (single test)
    const testArg = args.find(a => a.startsWith('--test='));
    if (testArg) {
        const testName = testArg.split('=')[1];
        await tester.runTest(testName, 'basic');
        return;
    }
    
    // Get suite name
    const suiteArg = args.find(a => a.startsWith('--suite='));
    const suite = suiteArg ? suiteArg.split('=')[1] : 'basic';
    
    await tester.runSuite(suite);
}

main().catch(err => {
    console.error('Error:', err);
    process.exit(1);
});
