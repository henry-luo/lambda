#!/usr/bin/env node

/**
 * Export pdf.js operator list as JSON reference
 * 
 * Usage:
 *   node export_pdfjs_oplist.js <pdf-file> [output-json]
 *   node export_pdfjs_oplist.js --all              # Export all PDFs in data/
 *   node export_pdfjs_oplist.js --suite=basic      # Export PDFs in data/basic/
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
            try {
                return JSON.parse(JSON.stringify(arg));
            } catch (e) {
                return { type: 'object', keys: Object.keys(arg) };
            }
        }
        return arg;
    });
}

async function exportOperatorList(pdfPath, outputPath) {
    // Dynamic import for ESM module
    const pdfjsLib = await import('pdfjs-dist/legacy/build/pdf.mjs');
    
    console.log(`📄 Loading PDF: ${pdfPath}`);
    
    let doc;
    try {
        doc = await pdfjsLib.getDocument(pdfPath).promise;
    } catch (error) {
        console.error(`   ❌ Failed to load PDF: ${error.message}`);
        return null;
    }
    
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
        const textItems = textContent.items
            .filter(item => item.str && item.str.trim())
            .map(item => ({
                str: item.str,
                transform: item.transform,
                width: item.width,
                height: item.height,
                fontName: item.fontName
            }));
        
        // Summary statistics
        const opSummary = {};
        for (const op of operations) {
            opSummary[op.op] = (opSummary[op.op] || 0) + 1;
        }
        
        result.pages.push({
            pageNum,
            viewport: {
                width: viewport.width,
                height: viewport.height,
                scale: viewport.scale
            },
            operationCount: operations.length,
            operationSummary: opSummary,
            operations,
            textItemCount: textItems.length,
            textItems
        });
        
        console.log(`   Page ${pageNum}: ${operations.length} ops, ${textItems.length} text items`);
    }
    
    // Write output
    const outputFile = outputPath || path.join(__dirname, 'reference', 
        path.basename(pdfPath).replace('.pdf', '.json'));
    
    await fs.mkdir(path.dirname(outputFile), { recursive: true });
    await fs.writeFile(outputFile, JSON.stringify(result, null, 2));
    console.log(`   ✅ Exported to: ${outputFile}`);
    
    return result;
}

async function exportSuite(suiteName) {
    const { glob } = await import('glob');
    const suiteDir = path.join(__dirname, 'data', suiteName);
    
    let pdfFiles;
    try {
        pdfFiles = await glob('*.pdf', { cwd: suiteDir });
    } catch (error) {
        console.error(`Failed to find PDFs in ${suiteDir}: ${error.message}`);
        return;
    }
    
    if (pdfFiles.length === 0) {
        console.log(`⚠️  No PDF files found in ${suiteDir}`);
        console.log(`   Run setup script first to copy test PDFs`);
        return;
    }
    
    console.log(`\n📦 Exporting suite: ${suiteName}`);
    console.log(`   Found ${pdfFiles.length} PDF files\n`);
    
    let exported = 0;
    let failed = 0;
    
    for (const pdfFile of pdfFiles) {
        const pdfPath = path.join(suiteDir, pdfFile);
        const outputPath = path.join(__dirname, 'reference', 
            pdfFile.replace('.pdf', '.json'));
        
        const result = await exportOperatorList(pdfPath, outputPath);
        if (result) {
            exported++;
        } else {
            failed++;
        }
    }
    
    console.log(`\n📊 Export Summary: ${exported} succeeded, ${failed} failed`);
}

async function exportAll() {
    const { glob } = await import('glob');
    const dataDir = path.join(__dirname, 'data');
    
    const pdfFiles = await glob('**/*.pdf', { cwd: dataDir });
    
    console.log(`\n📦 Exporting all PDFs`);
    console.log(`   Found ${pdfFiles.length} PDF files\n`);
    
    let exported = 0;
    let failed = 0;
    
    for (const pdfFile of pdfFiles) {
        const pdfPath = path.join(dataDir, pdfFile);
        const outputPath = path.join(__dirname, 'reference', 
            path.basename(pdfFile).replace('.pdf', '.json'));
        
        const result = await exportOperatorList(pdfPath, outputPath);
        if (result) {
            exported++;
        } else {
            failed++;
        }
    }
    
    console.log(`\n📊 Export Summary: ${exported} succeeded, ${failed} failed`);
}

async function main() {
    const args = process.argv.slice(2);
    
    if (args.length === 0 || args.includes('--help') || args.includes('-h')) {
        console.log(`
pdf.js Operator List Exporter

Usage:
  node export_pdfjs_oplist.js <pdf-file> [output-json]
  node export_pdfjs_oplist.js --suite=<name>
  node export_pdfjs_oplist.js --all

Options:
  <pdf-file>       Path to a single PDF file to export
  --suite=<name>   Export all PDFs in data/<name>/ directory
  --all            Export all PDFs in data/ directory
  -h, --help       Show this help message

Examples:
  node export_pdfjs_oplist.js data/basic/tracemonkey.pdf
  node export_pdfjs_oplist.js --suite=basic
  node export_pdfjs_oplist.js --all
`);
        process.exit(0);
    }
    
    // Check for --suite argument
    const suiteArg = args.find(a => a.startsWith('--suite='));
    if (suiteArg) {
        const suite = suiteArg.split('=')[1];
        await exportSuite(suite);
        return;
    }
    
    // Check for --all argument
    if (args.includes('--all')) {
        await exportAll();
        return;
    }
    
    // Single file export
    const pdfPath = args[0];
    const outputPath = args[1];
    
    if (!pdfPath) {
        console.error('Error: No PDF file specified');
        process.exit(1);
    }
    
    await exportOperatorList(pdfPath, outputPath);
}

main().catch(err => {
    console.error('Error:', err);
    process.exit(1);
});
