const { LayoutExtractor } = require('./extract_layout.js');
const path = require('path');

async function testTextExtraction() {
    const extractor = new LayoutExtractor();
    
    try {
        console.log('Testing enhanced text node extraction...');
        await extractor.initialize();
        
        const testFile = path.join(__dirname, 'test_text_extraction.html');
        const layoutData = await extractor.extractCompleteLayout(testFile);
        
        console.log('\n=== Text Node Extraction Results ===\n');
        
        // Print text nodes found in each element
        Object.entries(layoutData).forEach(([selector, data]) => {
            if (data.textNodes && data.textNodes.length > 0) {
                console.log(`Element: ${selector}`);
                console.log(`  Tag: ${data.tag || 'N/A'}`);
                console.log(`  Text Nodes (${data.textNodes.length}):`);
                
                data.textNodes.forEach((textNode, i) => {
                    console.log(`    [${i}] "${textNode.text}"`);
                    console.log(`        Length: ${textNode.length}`);
                    console.log(`        Parent: ${textNode.parentElement || 'N/A'}`);
                    console.log(`        Rects: ${textNode.rects.length}`);
                    
                    textNode.rects.forEach((rect, j) => {
                        console.log(`          Rect ${j}: x=${rect.x}, y=${rect.y}, w=${rect.width}, h=${rect.height}`);
                    });
                });
                console.log('');
            }
        });
        
        // Save complete results to file for inspection
        const fs = require('fs').promises;
        await fs.writeFile(
            path.join(__dirname, 'text_extraction_test_result.json'),
            JSON.stringify(layoutData, null, 2)
        );
        
        console.log('✅ Test completed. Full results saved to text_extraction_test_result.json');
        
    } catch (error) {
        console.error('❌ Test failed:', error);
    } finally {
        await extractor.close();
    }
}

if (require.main === module) {
    testTextExtraction().catch(console.error);
}
