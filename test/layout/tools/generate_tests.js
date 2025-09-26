#!/usr/bin/env node

/**
 * Test Case Generator for Radiant Layout Testing
 * 
 * Generates systematic HTML/CSS test cases for comprehensive layout validation
 */

const fs = require('fs').promises;
const path = require('path');

class TestGenerator {
    constructor() {
        // Feature definitions for systematic test generation
        this.features = {
            flexbox: {
                directions: ['row', 'column', 'row-reverse', 'column-reverse'],
                wraps: ['nowrap', 'wrap', 'wrap-reverse'],
                justifyContent: ['flex-start', 'flex-end', 'center', 'space-between', 'space-around', 'space-evenly'],
                alignItems: ['stretch', 'flex-start', 'flex-end', 'center', 'baseline'],
                alignContent: ['stretch', 'flex-start', 'flex-end', 'center', 'space-between', 'space-around']
            },
            block: {
                margins: ['0', '10px', 'auto', '10px 20px', '5px 10px 15px 20px'],
                paddings: ['0', '10px', '5px 10px', '5px 10px 15px 20px'],
                widths: ['auto', '100px', '50%', 'max-content', 'min-content'],
                heights: ['auto', '50px', '100px'],
                displays: ['block', 'inline-block']
            },
            typography: {
                fontSizes: ['12px', '16px', '20px', '24px'],
                lineHeights: ['normal', '1.2', '1.5', '2.0'],
                fontWeights: ['normal', 'bold', '300', '700'],
                textAligns: ['left', 'center', 'right', 'justify']
            }
        };
        
        this.testCounter = 0;
    }

    // Generate HTML template with inline CSS
    generateHtmlTemplate(testId, category, features, description, cssRules, htmlContent, specReferences = []) {
        return `<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Test: ${testId}</title>
    <style>
        /* Reset for consistency */
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: 'Arial', sans-serif; font-size: 16px; }
        
        /* Test-specific CSS */
        ${cssRules}
    </style>
</head>
<body>
    ${htmlContent}
    
    <!-- Test metadata (hidden) -->
    <script type="application/json" id="test-metadata">
    {
        "test_id": "${testId}",
        "category": "${category}",
        "features": ${JSON.stringify(features)},
        "description": "${description}",
        "spec_references": ${JSON.stringify(specReferences)},
        "complexity": "${category}"
    }
    </script>
</body>
</html>`;
    }

    // Generate basic flexbox tests
    generateFlexboxTests() {
        const tests = [];
        let testNum = 1;

        // Single property tests
        for (const direction of this.features.flexbox.directions) {
            for (const justify of this.features.flexbox.justifyContent) {
                const testId = `flex_${testNum.toString().padStart(3, '0')}_${direction}_${justify.replace('-', '_')}`;
                
                const cssRules = `
        .container {
            display: flex;
            flex-direction: ${direction};
            justify-content: ${justify};
            width: 600px;
            height: 120px;
            background: #f0f0f0;
            padding: 10px;
        }
        .item {
            width: 80px;
            height: 60px;
            background: #4CAF50;
            color: white;
            display: flex;
            align-items: center;
            justify-content: center;
            border-radius: 4px;
            flex-shrink: 0;
        }`;
                
                const htmlContent = `
    <div class="container">
        <div class="item" id="item1">1</div>
        <div class="item" id="item2">2</div>
        <div class="item" id="item3">3</div>
    </div>`;

                tests.push({
                    filename: `${testId}.html`,
                    content: this.generateHtmlTemplate(
                        testId,
                        'basic',
                        ['flexbox', 'flex-direction', 'justify-content'],
                        `Flexbox with direction: ${direction}, justify-content: ${justify}`,
                        cssRules,
                        htmlContent,
                        ['https://www.w3.org/TR/css-flexbox-1/#flex-direction-property']
                    )
                });
                testNum++;
            }
        }

        // Flex wrap tests
        for (const wrap of this.features.flexbox.wraps) {
            for (const alignContent of this.features.flexbox.alignContent) {
                const testId = `flex_${testNum.toString().padStart(3, '0')}_wrap_${wrap}_${alignContent.replace('-', '_')}`;
                
                const cssRules = `
        .container {
            display: flex;
            flex-wrap: ${wrap};
            align-content: ${alignContent};
            width: 300px;
            height: 200px;
            background: #e3f2fd;
            padding: 10px;
        }
        .item {
            width: 100px;
            height: 60px;
            background: #2196f3;
            color: white;
            display: flex;
            align-items: center;
            justify-content: center;
            border-radius: 4px;
            margin: 2px;
        }`;
                
                const htmlContent = `
    <div class="container">
        <div class="item" id="item1">1</div>
        <div class="item" id="item2">2</div>
        <div class="item" id="item3">3</div>
        <div class="item" id="item4">4</div>
    </div>`;

                tests.push({
                    filename: `${testId}.html`,
                    content: this.generateHtmlTemplate(
                        testId,
                        'basic',
                        ['flexbox', 'flex-wrap', 'align-content'],
                        `Flexbox with wrap: ${wrap}, align-content: ${alignContent}`,
                        cssRules,
                        htmlContent,
                        ['https://www.w3.org/TR/css-flexbox-1/#flex-wrap-property']
                    )
                });
                testNum++;
            }
        }

        return tests;
    }

    // Generate basic block layout tests
    generateBlockTests() {
        const tests = [];
        let testNum = 1;

        // Margin tests
        for (const margin of this.features.block.margins) {
            for (const width of this.features.block.widths.slice(0, 3)) { // Limit combinations
                const testId = `block_${testNum.toString().padStart(3, '0')}_margin_${margin.replace(/\s+/g, '_').replace(/px/g, 'px')}_width_${width.replace(/\s+/g, '_').replace(/px/g, 'px').replace(/%/g, 'pct')}`;
                
                const cssRules = `
        .parent {
            width: 500px;
            height: 300px;
            background: #fff3e0;
            padding: 20px;
        }
        .child {
            width: ${width};
            height: 80px;
            margin: ${margin};
            background: #ff9800;
            color: white;
            display: flex;
            align-items: center;
            justify-content: center;
            border-radius: 4px;
        }`;
                
                const htmlContent = `
    <div class="parent">
        <div class="child" id="child1">Child 1</div>
        <div class="child" id="child2">Child 2</div>
    </div>`;

                tests.push({
                    filename: `${testId}.html`,
                    content: this.generateHtmlTemplate(
                        testId,
                        'basic',
                        ['block', 'margin', 'width'],
                        `Block layout with margin: ${margin}, width: ${width}`,
                        cssRules,
                        htmlContent,
                        ['https://www.w3.org/TR/CSS2/box.html#margin-properties']
                    )
                });
                testNum++;
            }
        }

        // Padding tests
        for (const padding of this.features.block.paddings.slice(0, 3)) {
            const testId = `block_${testNum.toString().padStart(3, '0')}_padding_${padding.replace(/\s+/g, '_').replace(/px/g, 'px')}`;
            
            const cssRules = `
        .container {
            width: 400px;
            background: #f3e5f5;
            border: 2px solid #9c27b0;
        }
        .padded-box {
            padding: ${padding};
            background: #e1bee7;
            border: 1px solid #9c27b0;
        }
        .content {
            background: #9c27b0;
            color: white;
            height: 60px;
            display: flex;
            align-items: center;
            justify-content: center;
        }`;
            
            const htmlContent = `
    <div class="container">
        <div class="padded-box" id="padded">
            <div class="content" id="content">Content with padding: ${padding}</div>
        </div>
    </div>`;

            tests.push({
                filename: `${testId}.html`,
                content: this.generateHtmlTemplate(
                    testId,
                    'basic',
                    ['block', 'padding'],
                    `Block layout with padding: ${padding}`,
                    cssRules,
                    htmlContent,
                    ['https://www.w3.org/TR/CSS2/box.html#padding-properties']
                )
            });
            testNum++;
        }

        return tests;
    }

    // Generate intermediate complexity tests
    generateIntermediateTests() {
        const tests = [];
        let testNum = 1;

        // Nested flex and block
        const nestedTest1 = `flex_block_${testNum.toString().padStart(3, '0')}_nesting`;
        const cssRules1 = `
        .outer-flex {
            display: flex;
            width: 800px;
            height: 400px;
            gap: 20px;
            background: #e8f5e8;
            padding: 15px;
        }
        .flex-item {
            flex: 1;
            background: #c8e6c9;
            padding: 15px;
            border-radius: 8px;
        }
        .inner-block {
            width: 100%;
            margin-bottom: 10px;
            padding: 10px;
            background: #4caf50;
            color: white;
            border-radius: 4px;
        }
        .nested-flex {
            display: flex;
            justify-content: center;
            align-items: center;
            height: 80px;
            background: #2e7d32;
            color: white;
            border-radius: 4px;
        }`;
        
        const htmlContent1 = `
    <div class="outer-flex">
        <div class="flex-item" id="item1">
            <div class="inner-block" id="block1">Block 1</div>
            <div class="inner-block" id="block2">Block 2</div>
            <div class="nested-flex" id="nested1">
                <span>Centered Content</span>
            </div>
        </div>
        <div class="flex-item" id="item2">
            <div class="inner-block" id="block3">Block 3</div>
            <div class="nested-flex" id="nested2">
                <span>More Content</span>
            </div>
        </div>
    </div>`;

        tests.push({
            filename: `${nestedTest1}.html`,
            content: this.generateHtmlTemplate(
                nestedTest1,
                'intermediate',
                ['flexbox', 'block', 'nesting'],
                'Nested flexbox container with block children',
                cssRules1,
                htmlContent1,
                ['https://www.w3.org/TR/css-flexbox-1/', 'https://www.w3.org/TR/CSS2/visuren.html']
            )
        });

        // Complex margin collapse test
        testNum = 2;
        const marginTest = `margin_${testNum.toString().padStart(3, '0')}_collapse`;
        const cssRules2 = `
        .parent {
            width: 500px;
            background: #fff8e1;
            padding: 0;
            border: 3px solid #ffc107;
        }
        .child1 {
            margin: 20px 15px;
            padding: 10px;
            background: #ffecb3;
            border: 1px solid #ffc107;
        }
        .child2 {
            margin: 30px 15px;
            padding: 10px;
            background: #ffe082;
            border: 1px solid #ffc107;
        }
        .child3 {
            margin: 15px 15px;
            padding: 10px;
            background: #ffcc02;
            border: 1px solid #ffc107;
        }`;
        
        const htmlContent2 = `
    <div class="parent" id="parent">
        <div class="child1" id="child1">Child 1 - margin: 20px 15px</div>
        <div class="child2" id="child2">Child 2 - margin: 30px 15px</div>
        <div class="child3" id="child3">Child 3 - margin: 15px 15px</div>
    </div>`;

        tests.push({
            filename: `${marginTest}.html`,
            content: this.generateHtmlTemplate(
                marginTest,
                'intermediate',
                ['block', 'margin-collapse'],
                'Block layout with margin collapse behavior',
                cssRules2,
                htmlContent2,
                ['https://www.w3.org/TR/CSS2/box.html#collapsing-margins']
            )
        });

        return tests;
    }

    // Generate advanced complexity tests
    generateAdvancedTests() {
        const tests = [];

        // Complex responsive layout
        const complexTest = 'complex_001_responsive_layout';
        const cssRules = `
        .layout-container {
            display: flex;
            flex-direction: column;
            width: 1000px;
            min-height: 600px;
            background: #fafafa;
        }
        .header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 20px;
            background: #3f51b5;
            color: white;
        }
        .logo {
            font-size: 24px;
            font-weight: bold;
        }
        .nav {
            display: flex;
            gap: 20px;
        }
        .nav-item {
            padding: 8px 16px;
            background: rgba(255,255,255,0.1);
            border-radius: 4px;
        }
        .main-content {
            display: flex;
            flex: 1;
            gap: 30px;
            padding: 30px;
        }
        .sidebar {
            width: 250px;
            background: #e8eaf6;
            padding: 20px;
            border-radius: 8px;
        }
        .content-area {
            flex: 1;
            background: white;
            padding: 30px;
            border-radius: 8px;
            box-shadow: 0 2px 8px rgba(0,0,0,0.1);
        }
        .content-grid {
            display: flex;
            flex-wrap: wrap;
            gap: 20px;
            margin-top: 20px;
        }
        .grid-item {
            flex: 1;
            min-width: 200px;
            height: 150px;
            background: #f5f5f5;
            border: 1px solid #ddd;
            border-radius: 8px;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        .footer {
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 20px;
            background: #424242;
            color: white;
        }`;
        
        const htmlContent = `
    <div class="layout-container">
        <header class="header" id="header">
            <div class="logo" id="logo">RadiantApp</div>
            <nav class="nav" id="nav">
                <div class="nav-item" id="nav1">Home</div>
                <div class="nav-item" id="nav2">About</div>
                <div class="nav-item" id="nav3">Contact</div>
            </nav>
        </header>
        
        <div class="main-content" id="main">
            <aside class="sidebar" id="sidebar">
                <h3>Sidebar</h3>
                <p>Navigation links and additional content</p>
            </aside>
            
            <main class="content-area" id="content">
                <h1>Main Content Area</h1>
                <p>This is a complex layout testing various CSS features.</p>
                
                <div class="content-grid" id="grid">
                    <div class="grid-item" id="grid1">Item 1</div>
                    <div class="grid-item" id="grid2">Item 2</div>
                    <div class="grid-item" id="grid3">Item 3</div>
                    <div class="grid-item" id="grid4">Item 4</div>
                </div>
            </main>
        </div>
        
        <footer class="footer" id="footer">
            <p>&copy; 2025 RadiantApp. All rights reserved.</p>
        </footer>
    </div>`;

        tests.push({
            filename: `${complexTest}.html`,
            content: this.generateHtmlTemplate(
                complexTest,
                'advanced',
                ['flexbox', 'complex-layout', 'responsive', 'multi-level-nesting'],
                'Complex responsive layout with header, sidebar, main content, and footer',
                cssRules,
                htmlContent,
                [
                    'https://www.w3.org/TR/css-flexbox-1/',
                    'https://www.w3.org/TR/CSS2/visuren.html',
                    'https://www.w3.org/TR/css-sizing-3/'
                ]
            )
        });

        return tests;
    }

    // Generate all test categories
    async generateAllTests() {
        console.log('🏗️  Generating test cases...');
        
        const allTests = {
            basic: [
                ...this.generateFlexboxTests(),
                ...this.generateBlockTests()
            ],
            intermediate: this.generateIntermediateTests(),
            advanced: this.generateAdvancedTests()
        };

        // Write test files
        for (const [category, tests] of Object.entries(allTests)) {
            const categoryDir = `./data/${category}`;
            console.log(`📁 Creating ${tests.length} ${category} tests...`);
            
            for (const test of tests) {
                const filePath = path.join(categoryDir, test.filename);
                await fs.writeFile(filePath, test.content);
            }
        }

        // Generate summary
        const summary = {
            generated_at: new Date().toISOString(),
            categories: Object.fromEntries(
                Object.entries(allTests).map(([cat, tests]) => [cat, tests.length])
            ),
            total_tests: Object.values(allTests).flat().length
        };

        await fs.writeFile('./reports/test_generation_summary.json', JSON.stringify(summary, null, 2));
        
        console.log('✅ Test generation complete!');
        console.log(`📊 Generated ${summary.total_tests} tests:`);
        Object.entries(summary.categories).forEach(([cat, count]) => {
            console.log(`   ${cat}: ${count} tests`);
        });

        return summary;
    }
}

// CLI execution
async function main() {
    const generator = new TestGenerator();
    await generator.generateAllTests();
}

if (require.main === module) {
    main().catch(console.error);
}

module.exports = TestGenerator;
