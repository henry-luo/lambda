#!/usr/bin/env node

/**
 * Test the layout extractor with sample HTML/CSS
 */

const LayoutExtractor = require('./layout_extractor');
const fs = require('fs').promises;

async function runTests() {
    const extractor = new LayoutExtractor();
    await extractor.initialize();

    console.log('🧪 Testing Layout Extractor...\n');

    try {
        // Test 1: Basic Flexbox Layout
        console.log('Test 1: Basic Flexbox Layout');
        const flexHtml = `
<div class="container">
    <div class="item item1">Item 1</div>
    <div class="item item2">Item 2</div>
    <div class="item item3">Item 3</div>
</div>`;

        const flexCss = `
.container {
    display: flex;
    width: 600px;
    height: 200px;
    justify-content: space-between;
    align-items: center;
    background: lightgray;
}
.item {
    width: 100px;
    height: 50px;
    background: blue;
    color: white;
    display: flex;
    align-items: center;
    justify-content: center;
}`;

        const flexResult = await extractor.generateTestDescriptor('flexbox_basic', flexHtml, flexCss, {
            description: 'Basic flexbox with space-between and center alignment',
            propertiesToTest: ['position', 'dimensions', 'flex_properties'],
            specReference: 'https://www.w3.org/TR/css-flexbox-1/#justify-content-property'
        });

        console.log('✓ Flexbox test generated');
        await fs.writeFile('./sample_flexbox_test.json', JSON.stringify(flexResult, null, 2));

        // Test 2: Block Layout with Margins
        console.log('\nTest 2: Block Layout with Margins');
        const blockHtml = `
<div class="parent">
    <div class="child1">Child 1</div>
    <div class="child2">Child 2</div>
</div>`;

        const blockCss = `
.parent {
    width: 400px;
    padding: 20px;
    background: lightblue;
}
.child1 {
    width: 200px;
    height: 100px;
    margin: 10px 20px;
    background: red;
}
.child2 {
    width: 150px;
    height: 80px;
    margin: 15px auto;
    background: green;
}`;

        const blockResult = await extractor.generateTestDescriptor('block_margins', blockHtml, blockCss, {
            description: 'Block layout with various margin configurations'
        });

        console.log('✓ Block layout test generated');
        await fs.writeFile('./sample_block_test.json', JSON.stringify(blockResult, null, 2));

        // Test 3: Complex Nested Layout
        console.log('\nTest 3: Complex Nested Layout');
        const nestedHtml = `
<div class="outer-container">
    <div class="flex-container">
        <div class="flex-item">
            <div class="nested-block">Nested Block</div>
        </div>
        <div class="flex-item">
            <div class="inline-content">
                <span>Inline</span>
                <span>Content</span>
            </div>
        </div>
    </div>
</div>`;

        const nestedCss = `
.outer-container {
    width: 800px;
    padding: 30px;
    background: #f0f0f0;
}
.flex-container {
    display: flex;
    gap: 20px;
    background: white;
    padding: 15px;
}
.flex-item {
    flex: 1;
    background: lightcoral;
    padding: 10px;
}
.nested-block {
    width: 100%;
    height: 60px;
    background: navy;
    color: white;
    text-align: center;
    line-height: 60px;
}
.inline-content {
    background: yellow;
    padding: 5px;
}
.inline-content span {
    padding: 2px 8px;
    background: orange;
    margin: 0 2px;
    border-radius: 3px;
}`;

        const nestedResult = await extractor.generateTestDescriptor('complex_nested', nestedHtml, nestedCss, {
            description: 'Complex nested layout with flex and block elements',
            propertiesToTest: ['position', 'dimensions', 'nesting_behavior', 'gap_properties']
        });

        console.log('✓ Complex nested test generated');
        await fs.writeFile('./sample_nested_test.json', JSON.stringify(nestedResult, null, 2));

        console.log('\n🎉 All tests completed successfully!');
        console.log('Generated files:');
        console.log('  - sample_flexbox_test.json');
        console.log('  - sample_block_test.json');
        console.log('  - sample_nested_test.json');

    } catch (error) {
        console.error('❌ Test failed:', error);
    } finally {
        await extractor.close();
    }
}

// Run tests
if (require.main === module) {
    runTests();
}

module.exports = runTests;
