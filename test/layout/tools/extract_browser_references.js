#!/usr/bin/env node

/**
 * Simple Browser Layout Extractor
 * Extracts layout data from a single HTML file using Puppeteer
 */

const puppeteer = require('puppeteer');
const fs = require('fs').promises;
const path = require('path');

async function extractLayoutFromFile(htmlFilePath) {
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
                '--disable-dev-shm-usage',
                '--disable-gpu'
            ]
        });
        
        const page = await browser.newPage();
        await page.setViewport({ width: 1200, height: 800 });
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
            const elements = document.querySelectorAll('*');
            const results = {};
            
            elements.forEach((element, index) => {
                const rect = element.getBoundingClientRect();
                const computed = window.getComputedStyle(element);
                
                // Generate simple selector
                let selector = element.tagName.toLowerCase();
                if (element.id) {
                    selector = `#${element.id}`;
                } else if (element.className) {
                    const classes = element.className.split(' ').filter(c => c.trim());
                    if (classes.length > 0) {
                        selector += '.' + classes.join('.');
                    }
                }
                
                // Add index to make unique
                const key = `${selector}_${index}`;
                
                results[key] = {
                    tag: element.tagName.toLowerCase(),
                    selector: selector,
                    layout: {
                        x: Math.round(rect.left * 100) / 100,
                        y: Math.round(rect.top * 100) / 100,
                        width: Math.round(rect.width * 100) / 100,
                        height: Math.round(rect.height * 100) / 100
                    },
                    css: {
                        display: computed.display,
                        position: computed.position,
                        flexDirection: computed.flexDirection,
                        justifyContent: computed.justifyContent,
                        alignItems: computed.alignItems,
                        flexGrow: parseFloat(computed.flexGrow) || 0,
                        flexShrink: parseFloat(computed.flexShrink) || 1,
                        flexBasis: computed.flexBasis,
                        marginTop: parseFloat(computed.marginTop) || 0,
                        marginRight: parseFloat(computed.marginRight) || 0,
                        marginBottom: parseFloat(computed.marginBottom) || 0,
                        marginLeft: parseFloat(computed.marginLeft) || 0,
                        paddingTop: parseFloat(computed.paddingTop) || 0,
                        paddingRight: parseFloat(computed.paddingRight) || 0,
                        paddingBottom: parseFloat(computed.paddingBottom) || 0,
                        paddingLeft: parseFloat(computed.paddingLeft) || 0
                    }
                };
            });
            
            return {
                viewport: {
                    width: window.innerWidth,
                    height: window.innerHeight
                },
                elements: results
            };
        });
        
        console.log('✅ Layout data extracted');
        console.log(`📈 Found ${Object.keys(layoutData.elements).length} elements`);
        
        // Create reference JSON
        const reference = {
            test_file: path.basename(htmlFilePath),
            extraction_date: new Date().toISOString(),
            browser_info: {
                userAgent: await page.evaluate(() => navigator.userAgent),
                viewport: { width: 1200, height: 800 }
            },
            layout_data: layoutData
        };
        
        // Save to reference directory
        const baseName = path.basename(htmlFilePath, '.html');
        const outputDir = path.join(path.dirname(htmlFilePath), '..', 'reference', 'basic');
        const outputFile = path.join(outputDir, `${baseName}.json`);
        
        // Ensure output directory exists
        await fs.mkdir(outputDir, { recursive: true });
        
        // Write JSON file
        await fs.writeFile(outputFile, JSON.stringify(reference, null, 2));
        
        console.log(`💾 Reference saved to: ${outputFile}`);
        
        // Show sample of extracted data
        console.log('\n📋 Sample extracted data:');
        const sampleElements = Object.entries(layoutData.elements).slice(0, 3);
        sampleElements.forEach(([key, data]) => {
            console.log(`  ${key}: ${data.layout.width}x${data.layout.height} at (${data.layout.x}, ${data.layout.y})`);
        });
        
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

async function extractAllTestFiles(category = null) {
    console.log('🔍 Scanning for test HTML files...');
    
    const dataDir = path.join(__dirname, '..', 'data');
    const categories = category ? [category] : ['basic', 'intermediate', 'advanced'];
    
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
    const results = [];
    
    for (const fileInfo of allFiles) {
        console.log(`\n📄 Processing: ${fileInfo.category}/${fileInfo.file}`);
        
        try {
            const result = await extractLayoutFromFile(fileInfo.path);
            results.push({
                ...fileInfo,
                success: true,
                elementCount: Object.keys(result.layout_data.elements).length,
                result: result
            });
            successCount++;
            console.log(`✅ Success: ${Object.keys(result.layout_data.elements).length} elements extracted`);
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
    console.log(`✅ Successful: ${successCount}`);
    console.log(`❌ Failed: ${failCount}`);
    console.log(`📈 Success rate: ${Math.round(successCount / totalFiles * 100)}%`);
    
    // Show detailed results
    console.log('\n📋 Detailed Results:');
    results.forEach(result => {
        const status = result.success ? '✅' : '❌';
        const details = result.success 
            ? `${result.elementCount} elements`
            : `Error: ${result.error}`;
        console.log(`  ${status} ${result.category}/${result.file} - ${details}`);
    });
    
    // Save summary to file
    const summaryFile = path.join(__dirname, '..', 'reports', 'extraction_summary.json');
    await fs.mkdir(path.dirname(summaryFile), { recursive: true });
    
    const summary = {
        extraction_date: new Date().toISOString(),
        total_files: totalFiles,
        successful: successCount,
        failed: failCount,
        success_rate: Math.round(successCount / totalFiles * 100),
        results: results.map(r => ({
            category: r.category,
            file: r.file,
            success: r.success,
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
    
    for (let i = 0; i < args.length; i++) {
        const arg = args[i];
        if (arg === '--help' || arg === '-h') {
            showHelp = true;
        } else if (arg === '--category' || arg === '-c') {
            category = args[++i];
            if (!['basic', 'intermediate', 'advanced'].includes(category)) {
                console.error(`❌ Invalid category: ${category}. Must be basic, intermediate, or advanced`);
                process.exit(1);
            }
        } else if (arg.endsWith('.html')) {
            singleFile = arg;
        } else {
            console.error(`❌ Unknown argument: ${arg}`);
            showHelp = true;
        }
    }
    
    if (showHelp) {
        console.log(`
Usage: node simple_extract.js [options] [html_file]

Options:
  --category, -c <name>   Extract only from specific category (basic|intermediate|advanced)
  --help, -h              Show this help message

Examples:
  node simple_extract.js                                    # Extract all test files
  node simple_extract.js --category basic                   # Extract only basic tests
  node simple_extract.js ../data/basic/flex_001.html        # Extract single file

Generated files:
  ../reference/<category>/<test_name>.json                  # Individual reference files
  ../reports/extraction_summary.json                        # Summary report
`);
        process.exit(0);
    }
    
    try {
        if (singleFile) {
            // Single file mode
            await fs.access(singleFile);
            const result = await extractLayoutFromFile(singleFile);
            console.log(`\n🎉 Extraction completed successfully!`);
            console.log(`✅ Reference JSON created with ${Object.keys(result.layout_data.elements).length} elements`);
        } else {
            // Batch mode
            await extractAllTestFiles(category);
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
