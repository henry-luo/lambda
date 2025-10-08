#!/usr/bin/env node

/**
 * Demo: Character-to-Rectangle Mapping for Multi-line Text
 *
 * This demonstrates how to map individual characters or words to their
 * corresponding rectangles when text wraps across multiple lines.
 */

const puppeteer = require('puppeteer');

async function demonstrateTextRectMapping() {
    console.log('🔍 Demonstrating character-to-rectangle mapping for wrapped text...');

    const browser = await puppeteer.launch({ headless: 'new' });
    const page = await browser.newPage();
    await page.setViewport({ width: 1200, height: 800 });

    // Create a simple test HTML with text that will wrap
    const testHTML = `
    <!DOCTYPE html>
    <html>
    <head>
        <style>
            .text-container {
                width: 400px;
                background-color: #f5f5f5;
                padding: 20px;
                font-family: Arial, sans-serif;
                font-size: 20px;
            }
        </style>
    </head>
    <body>
        <div class="text-container">
            This is some text that will <span class="larger-text">wrap across multiple lines</span> in the container.
        </div>
    </body>
    </html>
    `;

    await page.setContent(testHTML);

    // Extract character-to-rectangle mapping
    const textMapping = await page.evaluate(() => {
        // Enhanced text node analysis function
        function analyzeTextNode(textNode) {
            const text = textNode.textContent;
            const range = document.createRange();

            // Get overall rectangles for the entire text node
            range.selectNodeContents(textNode);
            const overallRects = Array.from(range.getClientRects()).map(rect => ({
                x: Math.round(rect.left * 100) / 100,
                y: Math.round(rect.top * 100) / 100,
                width: Math.round(rect.width * 100) / 100,
                height: Math.round(rect.height * 100) / 100,
                right: Math.round(rect.right * 100) / 100,
                bottom: Math.round(rect.bottom * 100) / 100
            }));

            // Method 1: Character-by-character mapping
            const characterRects = [];
            for (let i = 0; i < text.length; i++) {
                try {
                    range.setStart(textNode, i);
                    range.setEnd(textNode, i + 1);
                    const charRects = range.getClientRects();
                    if (charRects.length > 0) {
                        const rect = charRects[0];
                        characterRects.push({
                            char: text[i],
                            index: i,
                            rect: {
                                x: Math.round(rect.left * 100) / 100,
                                y: Math.round(rect.top * 100) / 100,
                                width: Math.round(rect.width * 100) / 100,
                                height: Math.round(rect.height * 100) / 100,
                                right: Math.round(rect.right * 100) / 100,
                                bottom: Math.round(rect.bottom * 100) / 100
                            }
                        });
                    }
                } catch (e) {
                    // Skip problematic characters
                }
            }

            // Method 2: Word-by-word mapping
            const words = text.split(/(\s+)/); // Split but keep whitespace
            const wordRects = [];
            let currentIndex = 0;

            for (const word of words) {
                if (word.length > 0) {
                    try {
                        range.setStart(textNode, currentIndex);
                        range.setEnd(textNode, currentIndex + word.length);
                        const wordRectsList = range.getClientRects();

                        const wordData = {
                            word: word,
                            startIndex: currentIndex,
                            endIndex: currentIndex + word.length,
                            rects: Array.from(wordRectsList).map(rect => ({
                                x: Math.round(rect.left * 100) / 100,
                                y: Math.round(rect.top * 100) / 100,
                                width: Math.round(rect.width * 100) / 100,
                                height: Math.round(rect.height * 100) / 100,
                                right: Math.round(rect.right * 100) / 100,
                                bottom: Math.round(rect.bottom * 100) / 100
                            }))
                        };
                        wordRects.push(wordData);
                    } catch (e) {
                        // Skip problematic words
                    }
                }
                currentIndex += word.length;
            }

            // Method 3: Line-by-line mapping (approximation)
            const lineRects = [];
            if (overallRects.length > 1) {
                // Group characters by their Y position to determine line breaks
                const lineGroups = new Map();
                characterRects.forEach(charData => {
                    const y = charData.rect.y;
                    if (!lineGroups.has(y)) {
                        lineGroups.set(y, []);
                    }
                    lineGroups.get(y).push(charData);
                });

                // Sort lines by Y position and extract text for each line
                const sortedLines = Array.from(lineGroups.entries()).sort((a, b) => a[0] - b[0]);
                sortedLines.forEach(([y, chars], lineIndex) => {
                    const lineText = chars.map(c => c.char).join('');
                    const startIndex = chars[0]?.index || 0;
                    const endIndex = chars[chars.length - 1]?.index + 1 || text.length;

                    lineRects.push({
                        lineIndex: lineIndex,
                        text: lineText,
                        startIndex: startIndex,
                        endIndex: endIndex,
                        rect: overallRects[lineIndex] || null,
                        characterCount: chars.length
                    });
                });
            }

            range.detach();

            return {
                fullText: text,
                overallRects: overallRects,
                characterMapping: characterRects,
                wordMapping: wordRects,
                lineMapping: lineRects
            };
        }

        // Find all text nodes in the document
        const walker = document.createTreeWalker(
            document.body,
            NodeFilter.SHOW_TEXT,
            {
                acceptNode: function(node) {
                    // Skip whitespace-only text nodes
                    return node.textContent.trim().length > 0 ?
                        NodeFilter.FILTER_ACCEPT :
                        NodeFilter.FILTER_REJECT;
                }
            }
        );

        const results = [];
        let textNode;
        while (textNode = walker.nextNode()) {
            const parentElement = textNode.parentElement;
            const elementInfo = {
                tagName: parentElement.tagName.toLowerCase(),
                className: parentElement.className,
                textContent: textNode.textContent
            };

            const mapping = analyzeTextNode(textNode);
            results.push({
                element: elementInfo,
                mapping: mapping
            });
        }

        return results;
    });

    await browser.close();

    // Display results
    console.log('\n📊 Text-to-Rectangle Mapping Results:');
    console.log('=====================================');

    textMapping.forEach((result, index) => {
        console.log(`\n📄 Text Node ${index + 1}:`);
        console.log(`   Element: <${result.element.tagName}${result.element.className ? ' class="' + result.element.className + '"' : ''}>`);
        console.log(`   Text: "${result.mapping.fullText}"`);
        console.log(`   Overall Rects: ${result.mapping.overallRects.length}`);

        if (result.mapping.lineMapping.length > 0) {
            console.log('\n   📏 Line-by-Line Breakdown:');
            result.mapping.lineMapping.forEach(line => {
                console.log(`      Line ${line.lineIndex + 1}: "${line.text}" (chars ${line.startIndex}-${line.endIndex})`);
                if (line.rect) {
                    console.log(`         Rect: x=${line.rect.x}, y=${line.rect.y}, w=${line.rect.width}, h=${line.rect.height}`);
                }
            });
        }

        if (result.mapping.wordMapping.length > 0) {
            console.log('\n   📝 Word-by-Word Breakdown:');
            result.mapping.wordMapping.slice(0, 5).forEach(word => { // Show first 5 words
                console.log(`      "${word.word}" (chars ${word.startIndex}-${word.endIndex}): ${word.rects.length} rect(s)`);
            });
            if (result.mapping.wordMapping.length > 5) {
                console.log(`      ... and ${result.mapping.wordMapping.length - 5} more words`);
            }
        }
    });

    return textMapping;
}

// Export the analysis function for use in other scripts
function createEnhancedTextNodeExtractor() {
    return `
        // Enhanced text node analysis function (for injection into browser context)
        function analyzeTextNodeWithMapping(textNode) {
            const text = textNode.textContent;
            const range = document.createRange();

            // Get overall rectangles
            range.selectNodeContents(textNode);
            const overallRects = Array.from(range.getClientRects()).map(rect => ({
                x: Math.round(rect.left * 100) / 100,
                y: Math.round(rect.top * 100) / 100,
                width: Math.round(rect.width * 100) / 100,
                height: Math.round(rect.height * 100) / 100,
                right: Math.round(rect.right * 100) / 100,
                bottom: Math.round(rect.bottom * 100) / 100
            }));

            // Word-level mapping for line detection
            const words = text.split(/(\\s+)/);
            const wordRects = [];
            let currentIndex = 0;

            for (const word of words) {
                if (word.length > 0) {
                    try {
                        range.setStart(textNode, currentIndex);
                        range.setEnd(textNode, currentIndex + word.length);
                        const wordRectsList = range.getClientRects();

                        wordRects.push({
                            word: word,
                            startIndex: currentIndex,
                            endIndex: currentIndex + word.length,
                            rects: Array.from(wordRectsList).map(rect => ({
                                x: Math.round(rect.left * 100) / 100,
                                y: Math.round(rect.top * 100) / 100,
                                width: Math.round(rect.width * 100) / 100,
                                height: Math.round(rect.height * 100) / 100,
                                right: Math.round(rect.right * 100) / 100,
                                bottom: Math.round(rect.bottom * 100) / 100
                            }))
                        });
                    } catch (e) {
                        // Skip problematic words
                    }
                }
                currentIndex += word.length;
            }

            // Derive line breaks by analyzing word positions
            const lineSegments = [];
            if (overallRects.length > 1) {
                // Group words by Y position to determine which words are on which lines
                const tolerance = 2; // Allow small Y variations
                const lines = [];

                wordRects.forEach(wordData => {
                    if (wordData.rects.length > 0) {
                        const wordY = wordData.rects[0].y;
                        let foundLine = lines.find(line => Math.abs(line.y - wordY) <= tolerance);

                        if (!foundLine) {
                            foundLine = { y: wordY, words: [] };
                            lines.push(foundLine);
                        }
                        foundLine.words.push(wordData);
                    }
                });

                // Sort lines by Y position and extract text segments
                lines.sort((a, b) => a.y - b.y);
                lines.forEach((line, lineIndex) => {
                    if (line.words.length > 0) {
                        const firstWord = line.words[0];
                        const lastWord = line.words[line.words.length - 1];
                        const lineText = text.substring(firstWord.startIndex, lastWord.endIndex);

                        lineSegments.push({
                            lineIndex: lineIndex,
                            text: lineText,
                            startIndex: firstWord.startIndex,
                            endIndex: lastWord.endIndex,
                            rect: overallRects[lineIndex] || null
                        });
                    }
                });
            } else if (overallRects.length === 1) {
                // Single line
                lineSegments.push({
                    lineIndex: 0,
                    text: text,
                    startIndex: 0,
                    endIndex: text.length,
                    rect: overallRects[0]
                });
            }

            range.detach();

            return {
                nodeType: 'text',
                text: text,
                length: text.length,
                isWhitespaceOnly: !text.trim(),
                layout: {
                    rects: overallRects,
                    lineSegments: lineSegments, // NEW: Line-by-line breakdown
                    hasLayout: overallRects.length > 0,
                    // ... other layout properties
                }
            };
        }
    `;
}

// Run the demo if called directly
if (require.main === module) {
    demonstrateTextRectMapping().catch(console.error);
}

module.exports = { demonstrateTextRectMapping, createEnhancedTextNodeExtractor };
