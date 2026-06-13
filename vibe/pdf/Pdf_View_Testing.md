# PDF View Testing Proposal

**Date**: January 2026  
**Status**: Draft  
**Author**: Lambda Team

## Overview

This proposal outlines a comprehensive testing strategy for Radiant's PDF rendering by leveraging pdf.js's extensive test fixtures and operator list export capabilities. The goal is to establish a robust regression testing framework that compares Radiant's PDF-to-ViewTree conversion against pdf.js's parsed operator lists.

## Goals

1. **Leverage pdf.js test fixtures** - Reuse the ~900+ PDF test files from pdf.js
2. **Establish test infrastructure** - Create `./test/pdf/` following the layout test patterns
3. **Export pdf.js operator lists** - Capture rendering commands as JSON reference data
4. **Extend `lambda layout` command** - Support PDF documents with view tree export
5. **Create comparison framework** - Compare Radiant output against pdf.js references
6. **Curate starter test set** - Copy simple PDFs for initial testing

---

## 1. pdf.js Test Fixture Integration

### 1.1 Available Fixtures

The pdf.js repository at `./pdf-js/test/pdfs/` contains ~900+ PDF files:

| Category | Examples | Count |
|----------|----------|-------|
| Standard reference | `tracemonkey.pdf` | 1 |
| Bug fixes | `bug*.pdf` | ~80+ |
| Issue reproductions | `issue*.pdf` | ~150+ |
| Font tests | `standard_fonts.pdf`, `cid_cff.pdf`, `mmtype1.pdf` | ~15 |
| Color/Graphics | `colors.pdf`, `gradientfill.pdf`, `blendmode.pdf` | ~20 |
| Annotations | `annotation-*.pdf` | ~25 |
| Simple tests | `empty.pdf`, `rotated.pdf`, `sizes.pdf` | ~10 |

**Note**: Many files are `.link` references to external PDFs. We'll focus on actual PDF files.

### 1.2 pdf.js Operator List API

pdf.js exposes a `getOperatorList()` API that returns structured rendering commands:

```javascript
const opList = await page.getOperatorList();
// Returns:
// {
//   fnArray: number[],    // Operation codes (see OPS enum)
//   argsArray: any[][],   // Arguments for each operation
//   lastChunk: boolean
// }
```

**OPS enum** (from `pdf-js/src/shared/util.js`):

| Code | Operation | Description |
|------|-----------|-------------|
| 12 | `transform` | Set transformation matrix |
| 31 | `beginText` | BT operator |
| 32 | `endText` | ET operator |
| 37 | `setFont` | Tf - set font |
| 42 | `setTextMatrix` | Tm operator |
| 44 | `showText` | Tj - show text string |
| 45 | `showSpacedText` | TJ - text with kerning |
| 13-19 | Path ops | moveTo, lineTo, curveTo, rectangle |
| 20-27 | Paint ops | stroke, fill, eoFill |
| 59 | `setFillRGBColor` | rg - set fill color |
| 83-90 | Image ops | paintImageXObject, etc. |

---

## 2. Test Directory Structure

Following the existing `./test/layout/` structure:

```
test/pdf/
├── package.json                    # Dependencies (pdfjs-dist)
├── test_radiant_pdf.js            # Main test runner
├── export_pdfjs_oplist.js         # Export pdf.js operator lists to JSON
├── compare_pdf_output.js          # Compare Radiant output vs pdf.js reference
├── data/                          # Test PDF files
│   ├── basic/                     # Simple PDFs for initial testing
│   │   ├── tracemonkey.pdf
│   │   ├── standard_fonts.pdf
│   │   ├── colors.pdf
│   │   ├── empty.pdf
│   │   └── ...
│   ├── fonts/                     # Font-specific tests
│   ├── graphics/                  # Graphics/path tests
│   └── text/                      # Text positioning tests
├── reference/                     # pdf.js operator list references
│   ├── tracemonkey.json
│   ├── standard_fonts.json
│   └── ...
└── output/                        # Radiant output for comparison
```

### 2.1 package.json

```json
{
    "name": "pdf-test-tools",
    "version": "1.0.0",
    "description": "Automated PDF testing tools for Radiant engine",
    "main": "test_radiant_pdf.js",
    "scripts": {
        "test": "node test_radiant_pdf.js",
        "test:basic": "node test_radiant_pdf.js --suite basic",
        "export": "node export_pdfjs_oplist.js",
        "compare": "node compare_pdf_output.js"
    },
    "dependencies": {
        "pdfjs-dist": "^4.0.0",
        "glob": "^10.0.0"
    },
    "author": "Lambda Project",
    "license": "MIT"
}
```

---

## 3. Utility Script: Export pdf.js Operator List

### 3.1 export_pdfjs_oplist.js

```javascript
#!/usr/bin/env node

/**
 * Export pdf.js operator list as JSON reference
 * 
 * Usage:
 *   node export_pdfjs_oplist.js <pdf-file> [output-json]
 *   node export_pdfjs_oplist.js --all    # Export all PDFs in data/
 */

const fs = require('fs').promises;
const path = require('path');

// OPS code to name mapping (from pdf.js util.js)
const OPS_NAMES = {
    1: 'dependency',
    2: 'setLineWidth',
    3: 'setLineCap',
    4: 'setLineJoin',
    5: 'setMiterLimit',
    6: 'setDash',
    7: 'setRenderingIntent',
    8: 'setFlatness',
    9: 'setGState',
    10: 'save',
    11: 'restore',
    12: 'transform',
    13: 'moveTo',
    14: 'lineTo',
    15: 'curveTo',
    16: 'curveTo2',
    17: 'curveTo3',
    18: 'closePath',
    19: 'rectangle',
    20: 'stroke',
    21: 'closeStroke',
    22: 'fill',
    23: 'eoFill',
    24: 'fillStroke',
    25: 'eoFillStroke',
    26: 'closeFillStroke',
    27: 'closeEOFillStroke',
    28: 'endPath',
    29: 'clip',
    30: 'eoClip',
    31: 'beginText',
    32: 'endText',
    33: 'setCharSpacing',
    34: 'setWordSpacing',
    35: 'setHScale',
    36: 'setLeading',
    37: 'setFont',
    38: 'setTextRenderingMode',
    39: 'setTextRise',
    40: 'moveText',
    41: 'setLeadingMoveText',
    42: 'setTextMatrix',
    43: 'nextLine',
    44: 'showText',
    45: 'showSpacedText',
    46: 'nextLineShowText',
    47: 'nextLineSetSpacingShowText',
    48: 'setCharWidth',
    49: 'setCharWidthAndBounds',
    50: 'setStrokeColorSpace',
    51: 'setFillColorSpace',
    52: 'setStrokeColor',
    53: 'setStrokeColorN',
    54: 'setFillColor',
    55: 'setFillColorN',
    56: 'setStrokeGray',
    57: 'setFillGray',
    58: 'setStrokeRGBColor',
    59: 'setFillRGBColor',
    60: 'setStrokeCMYKColor',
    61: 'setFillCMYKColor',
    62: 'shadingFill',
    63: 'beginInlineImage',
    64: 'beginImageData',
    65: 'endInlineImage',
    66: 'paintXObject',
    67: 'markPoint',
    68: 'markPointProps',
    69: 'beginMarkedContent',
    70: 'beginMarkedContentProps',
    71: 'endMarkedContent',
    72: 'beginCompat',
    73: 'endCompat',
    74: 'paintFormXObjectBegin',
    75: 'paintFormXObjectEnd',
    76: 'beginGroup',
    77: 'endGroup',
    80: 'beginAnnotation',
    81: 'endAnnotation',
    83: 'paintImageMaskXObject',
    84: 'paintImageMaskXObjectGroup',
    85: 'paintImageXObject',
    86: 'paintInlineImageXObject',
    87: 'paintInlineImageXObjectGroup',
    88: 'paintImageXObjectRepeat',
    89: 'paintImageMaskXObjectRepeat',
    90: 'paintSolidColorImageMask',
    91: 'constructPath',
    92: 'setStrokeTransparent',
    93: 'setFillTransparent',
    94: 'rawFillPath'
};

async function exportOperatorList(pdfPath, outputPath) {
    // Dynamic import for ESM module
    const pdfjsLib = await import('pdfjs-dist/legacy/build/pdf.mjs');
    
    console.log(`📄 Loading PDF: ${pdfPath}`);
    
    const doc = await pdfjsLib.getDocument(pdfPath).promise;
    const result = {
        file: path.basename(pdfPath),
        numPages: doc.numPages,
        generator: 'pdf.js',
        exportedAt: new Date().toISOString(),
        pages: []
    };
    
    for (let pageNum = 1; pageNum <= doc.numPages; pageNum++) {
        const page = await doc.getPage(pageNum);
        const viewport = page.getViewport({ scale: 1.0 });
        const opList = await page.getOperatorList();
        
        // Get text content for reference
        const textContent = await page.getTextContent();
        
        // Convert to readable format
        const operations = opList.fnArray.map((fn, idx) => ({
            op: OPS_NAMES[fn] || `unknown_${fn}`,
            code: fn,
            args: sanitizeArgs(opList.argsArray[idx])
        }));
        
        // Extract text items with positions
        const textItems = textContent.items.map(item => ({
            str: item.str,
            transform: item.transform,
            width: item.width,
            height: item.height,
            fontName: item.fontName
        }));
        
        result.pages.push({
            pageNum,
            viewport: {
                width: viewport.width,
                height: viewport.height,
                scale: viewport.scale
            },
            operationCount: operations.length,
            operations,
            textItems
        });
        
        console.log(`   Page ${pageNum}: ${operations.length} operations, ${textItems.length} text items`);
    }
    
    // Write output
    const outputFile = outputPath || pdfPath.replace('.pdf', '_oplist.json');
    await fs.writeFile(outputFile, JSON.stringify(result, null, 2));
    console.log(`✅ Exported to: ${outputFile}`);
    
    return result;
}

// Sanitize arguments for JSON serialization
function sanitizeArgs(args) {
    if (!args) return [];
    return args.map(arg => {
        if (arg instanceof Uint8Array || arg instanceof Uint8ClampedArray) {
            return { type: 'binary', length: arg.length };
        }
        if (ArrayBuffer.isView(arg)) {
            return Array.from(arg);
        }
        if (typeof arg === 'object' && arg !== null) {
            // Handle special objects
            if (arg.name) return { name: arg.name };
            return JSON.parse(JSON.stringify(arg));
        }
        return arg;
    });
}

async function main() {
    const args = process.argv.slice(2);
    
    if (args.length === 0 || args[0] === '--help') {
        console.log(`
Usage:
  node export_pdfjs_oplist.js <pdf-file> [output-json]
  node export_pdfjs_oplist.js --all
  node export_pdfjs_oplist.js --suite <suite-name>

Options:
  --all         Export all PDFs in data/ directory
  --suite NAME  Export PDFs in data/<NAME>/ directory
`);
        process.exit(0);
    }
    
    if (args[0] === '--all') {
        const glob = require('glob');
        const files = glob.sync('data/**/*.pdf');
        for (const file of files) {
            const outputFile = path.join('reference', 
                path.basename(file).replace('.pdf', '.json'));
            await exportOperatorList(file, outputFile);
        }
    } else if (args[0] === '--suite') {
        const suite = args[1] || 'basic';
        const glob = require('glob');
        const files = glob.sync(`data/${suite}/*.pdf`);
        for (const file of files) {
            const outputFile = path.join('reference', 
                path.basename(file).replace('.pdf', '.json'));
            await exportOperatorList(file, outputFile);
        }
    } else {
        await exportOperatorList(args[0], args[1]);
    }
}

main().catch(err => {
    console.error('Error:', err);
    process.exit(1);
});
```

---

## 4. Lambda Layout Command Enhancement

### 4.1 Current State

The `lambda layout` command already supports PDF files via `load_pdf_doc()`:

```cpp
// In cmd_layout.cpp
DomDocument* load_pdf_doc(Url* pdf_url, int viewport_width, int viewport_height, 
                          Pool* pool, float pixel_ratio);
```

This creates a `DomDocument` with a pre-built `ViewTree` from the PDF content.

### 4.2 Proposed Enhancement

Add JSON output for PDF view tree to enable comparison:

```bash
# Current usage (text output)
./lambda.exe layout test.pdf

# Enhanced usage (JSON output for testing)
./lambda.exe layout test.pdf --format json --view-output /tmp/pdf_view.json

# Proposed new option for operator-level output
./lambda.exe layout test.pdf --format pdf-ops -o output.json
```

### 4.3 Implementation: PDF Operator Export

Add to `cmd_layout.cpp` or create new `pdf_ops_export.cpp`:

```cpp
/**
 * Export PDF parsing results as JSON for comparison with pdf.js
 * 
 * Output format matches pdf.js operator list structure:
 * {
 *   "file": "test.pdf",
 *   "numPages": 1,
 *   "pages": [{
 *     "pageNum": 1,
 *     "viewport": { "width": 612, "height": 792 },
 *     "textItems": [
 *       { "str": "Hello", "x": 72, "y": 700, "fontSize": 12, "fontName": "F1" }
 *     ],
 *     "graphics": [
 *       { "type": "path", "d": "M 0 0 L 100 100", "stroke": "#000000" }
 *     ]
 *   }]
 * }
 */
void export_pdf_view_as_json(ViewTree* view_tree, const char* output_path);
```

---

## 5. Test Comparison Framework

### 5.1 test_radiant_pdf.js

```javascript
#!/usr/bin/env node

/**
 * Radiant PDF Test Runner
 * 
 * Compares Radiant's PDF rendering against pdf.js operator lists
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
        this.tolerance = options.tolerance || 1.0;  // Position tolerance in points
        this.projectRoot = options.projectRoot || process.cwd();
    }

    /**
     * Run Radiant layout on a PDF file
     */
    async runRadiantLayout(pdfFile) {
        const outputFile = path.join(this.outputDir, 
            path.basename(pdfFile).replace('.pdf', '_view.json'));
        
        return new Promise((resolve, reject) => {
            const args = ['layout', pdfFile, '--format', 'json', 
                         '--view-output', outputFile];
            
            const proc = spawn(this.radiantExe, args, { cwd: this.projectRoot });
            
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
        });
    }

    /**
     * Load Radiant output
     */
    async loadRadiantOutput(outputFile) {
        const content = await fs.readFile(outputFile, 'utf8');
        return JSON.parse(content);
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
        
        // Create lookup map for pdf.js items
        const pdfjsMap = new Map();
        for (const item of pdfjsItems) {
            const key = item.str.trim();
            if (!pdfjsMap.has(key)) pdfjsMap.set(key, []);
            pdfjsMap.get(key).push(item);
        }
        
        // Compare each Radiant item
        for (const radiantItem of radiantItems) {
            const key = radiantItem.text?.trim() || radiantItem.str?.trim();
            if (!key) continue;
            
            const pdfjsCandidates = pdfjsMap.get(key);
            if (!pdfjsCandidates || pdfjsCandidates.length === 0) {
                results.extra++;
                results.details.push({
                    type: 'extra',
                    text: key,
                    radiant: radiantItem
                });
                continue;
            }
            
            // Find best position match
            const pdfjsItem = pdfjsCandidates[0];
            const [a, b, c, d, tx, ty] = pdfjsItem.transform || [1, 0, 0, 1, 0, 0];
            
            const xDiff = Math.abs((radiantItem.x || 0) - tx);
            const yDiff = Math.abs((radiantItem.y || 0) - ty);
            
            if (xDiff <= this.tolerance && yDiff <= this.tolerance) {
                results.matched++;
                pdfjsCandidates.shift();  // Remove matched item
            } else {
                results.mismatched++;
                results.details.push({
                    type: 'position_mismatch',
                    text: key,
                    radiant: { x: radiantItem.x, y: radiantItem.y },
                    pdfjs: { x: tx, y: ty },
                    diff: { x: xDiff, y: yDiff }
                });
            }
        }
        
        // Count remaining pdf.js items as missing
        for (const [key, items] of pdfjsMap) {
            results.missing += items.length;
            for (const item of items) {
                results.details.push({
                    type: 'missing',
                    text: key,
                    pdfjs: item
                });
            }
        }
        
        return results;
    }

    /**
     * Run a single test
     */
    async runTest(testName, category = 'basic') {
        const pdfFile = path.join(this.dataDir, category, `${testName}.pdf`);
        
        console.log(`\n📄 Testing: ${testName}`);
        
        // Run Radiant layout
        let radiantResult;
        try {
            radiantResult = await this.runRadiantLayout(pdfFile);
        } catch (error) {
            console.log(`   ❌ Radiant failed: ${error.message}`);
            return { status: 'error', error: error.message };
        }
        
        // Load outputs
        const radiantOutput = await this.loadRadiantOutput(radiantResult.outputFile);
        const pdfjsRef = await this.loadPdfjsReference(testName);
        
        if (!pdfjsRef) {
            console.log(`   ⚠️  No pdf.js reference found`);
            return { status: 'no_reference' };
        }
        
        // Compare
        const comparison = this.compareTextItems(
            radiantOutput.textItems || [],
            pdfjsRef.pages?.[0]?.textItems || []
        );
        
        const passRate = comparison.matched / 
            (comparison.matched + comparison.mismatched + comparison.missing) * 100;
        
        if (passRate >= 90) {
            console.log(`   ✅ PASS: ${passRate.toFixed(1)}% match`);
        } else if (passRate >= 70) {
            console.log(`   ⚠️  PARTIAL: ${passRate.toFixed(1)}% match`);
        } else {
            console.log(`   ❌ FAIL: ${passRate.toFixed(1)}% match`);
        }
        
        if (this.verbose) {
            console.log(`      Matched: ${comparison.matched}`);
            console.log(`      Mismatched: ${comparison.mismatched}`);
            console.log(`      Missing: ${comparison.missing}`);
            console.log(`      Extra: ${comparison.extra}`);
        }
        
        return { status: 'tested', passRate, comparison };
    }

    /**
     * Run all tests in a suite
     */
    async runSuite(suiteName = 'basic') {
        const glob = require('glob');
        const suiteDir = path.join(this.dataDir, suiteName);
        const pdfFiles = glob.sync('*.pdf', { cwd: suiteDir });
        
        console.log(`\n🧪 Running PDF test suite: ${suiteName}`);
        console.log(`   Found ${pdfFiles.length} PDF files\n`);
        
        const results = {
            total: pdfFiles.length,
            passed: 0,
            failed: 0,
            partial: 0,
            errors: 0,
            noReference: 0
        };
        
        for (const pdfFile of pdfFiles) {
            const testName = path.basename(pdfFile, '.pdf');
            const result = await this.runTest(testName, suiteName);
            
            if (result.status === 'tested') {
                if (result.passRate >= 90) results.passed++;
                else if (result.passRate >= 70) results.partial++;
                else results.failed++;
            } else if (result.status === 'error') {
                results.errors++;
            } else {
                results.noReference++;
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
        
        return results;
    }
}

// CLI
async function main() {
    const args = process.argv.slice(2);
    const options = {
        verbose: args.includes('-v') || args.includes('--verbose'),
        projectRoot: process.cwd()
    };
    
    // Navigate to project root
    while (!require('fs').existsSync(path.join(options.projectRoot, 'lambda.exe'))) {
        options.projectRoot = path.dirname(options.projectRoot);
        if (options.projectRoot === '/') {
            console.error('Could not find project root (lambda.exe)');
            process.exit(1);
        }
    }
    
    const tester = new RadiantPdfTester(options);
    
    // Ensure output directory exists
    await fs.mkdir(tester.outputDir, { recursive: true });
    
    const suiteArg = args.find(a => a.startsWith('--suite='));
    const suite = suiteArg ? suiteArg.split('=')[1] : 'basic';
    
    await tester.runSuite(suite);
}

main().catch(console.error);
```

---

## 6. Starter Test PDFs

### 6.1 Initial PDF Selection

Copy these PDFs from `./pdf-js/test/pdfs/` to `./test/pdf/data/basic/`:

| File | Purpose | Complexity |
|------|---------|------------|
| `tracemonkey.pdf` | Standard multi-page reference | Medium |
| `standard_fonts.pdf` | Test 14 standard PDF fonts | Low |
| `colors.pdf` | Color handling | Low |
| `empty.pdf` | Edge case - empty document | Minimal |
| `rotated.pdf` | Page rotation | Low |
| `rotation.pdf` | Content rotation | Low |
| `transparency.pdf` | Alpha/transparency | Medium |
| `simpletype3font.pdf` | Type 3 fonts | Medium |
| `basicapi.pdf` | Basic API test | Low |
| `canvas.pdf` | Simple graphics | Low |

### 6.2 Setup Script

```bash
#!/bin/bash
# setup_pdf_test_fixtures.sh

# Create directory structure
mkdir -p test/pdf/{data/basic,reference,output}

# Copy basic test PDFs from pdf.js
PDFJS_DIR="./pdf-js/test/pdfs"
TARGET_DIR="./test/pdf/data/basic"

BASIC_PDFS=(
    "tracemonkey.pdf"
    "standard_fonts.pdf"
    "colors.pdf"
    "empty.pdf"
    "rotated.pdf"
    "rotation.pdf"
    "transparency.pdf"
    "simpletype3font.pdf"
    "basicapi.pdf"
    "canvas.pdf"
)

for pdf in "${BASIC_PDFS[@]}"; do
    if [ -f "$PDFJS_DIR/$pdf" ]; then
        cp "$PDFJS_DIR/$pdf" "$TARGET_DIR/"
        echo "Copied: $pdf"
    else
        echo "Skipped (not found): $pdf"
    fi
done

# Initialize npm project
cd test/pdf
npm init -y
npm install pdfjs-dist glob

echo "✅ PDF test fixtures setup complete"
```

---

## 7. Makefile Integration

Add to main `Makefile`:

```makefile
# PDF Testing
.PHONY: test-pdf test-pdf-basic test-pdf-export

test-pdf:
	cd test/pdf && npm test

test-pdf-basic:
	cd test/pdf && npm run test -- --suite=basic

test-pdf-export:
	cd test/pdf && npm run export -- --suite=basic

# Setup
setup-pdf-tests:
	./test/pdf/setup_pdf_test_fixtures.sh
```

---

## 8. Implementation Timeline

| Phase | Task | Effort |
|-------|------|--------|
| 1 | Create `test/pdf/` directory structure | 0.5 day |
| 2 | Implement `export_pdfjs_oplist.js` | 1 day |
| 3 | Copy and verify starter PDFs | 0.5 day |
| 4 | Generate initial pdf.js references | 0.5 day |
| 5 | Implement `test_radiant_pdf.js` | 2 days |
| 6 | Enhance `lambda layout` for JSON output | 1 day |
| 7 | Test and debug comparison logic | 2 days |
| 8 | Add Makefile targets and CI integration | 0.5 day |

**Total Estimated Effort**: ~8 days

---

## 9. Success Criteria

1. **Export utility works**: Can export pdf.js operator lists for any PDF
2. **Reference coverage**: At least 10 PDFs have generated references
3. **Radiant layout works**: `lambda layout *.pdf` produces valid JSON output
4. **Comparison runs**: Test runner can compare outputs and report pass/fail
5. **Basic suite passes**: ≥70% of basic PDFs pass at ≥90% match rate
6. **CI integration**: Tests run automatically on PR/push

---

## 10. Future Enhancements

1. **Visual regression testing**: Compare rendered PNG output (like pdf.js test_manifest)
2. **Operator-level comparison**: Compare raw PDF operators, not just text
3. **Font substitution validation**: Verify correct font mapping
4. **Multi-page support**: Test pagination and page navigation
5. **Performance benchmarks**: Track parsing and rendering time
6. **Annotation testing**: Extend to annotation rendering

---

## Appendix A: pdf.js OPS Full Reference

See `pdf-js/src/shared/util.js` lines 250-345 for the complete OPS enum.

## Appendix B: Related Files

- `radiant/cmd_layout.cpp` - Layout command implementation
- `radiant/pdf/pdf_to_view.cpp` - PDF to ViewTree conversion
- `radiant/view_pool.cpp` - View tree JSON export
- `test/layout/test_radiant_layout.js` - Layout test runner (reference)
- `test/layout/extract_browser_references.js` - Reference extraction (reference)
