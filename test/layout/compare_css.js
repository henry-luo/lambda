#!/usr/bin/env node

/**
 * CSS Property Comparison Script
 *
 * Compares CSS properties between Lambda CSS output and browser reference data.
 * This script focuses on verifying that the CSS cascade and property resolution
 * is working correctly by comparing resolved properties.
 */

const fs = require('fs').promises;
const path = require('path');

class CSSComparator {
    constructor(options = {}) {
        this.tolerance = options.tolerance || 1.0; // 1px tolerance for numeric values
        this.verbose = options.verbose || false;
        this.referenceDir = path.join(__dirname, 'reference', 'baseline');
    }

    /**
     * Property mapping from browser (camelCase) to Lambda CSS cascade output
     * Lambda cascade output now uses same camelCase format as browser
     */
    getBrowserToLambdaMapping() {
        return {
            // Box model - now direct properties in css_properties
            'marginTop': 'marginTop',
            'marginRight': 'marginRight',
            'marginBottom': 'marginBottom',
            'marginLeft': 'marginLeft',
            'paddingTop': 'paddingTop',
            'paddingRight': 'paddingRight',
            'paddingBottom': 'paddingBottom',
            'paddingLeft': 'paddingLeft',
            'borderTopWidth': 'borderTopWidth',
            'borderRightWidth': 'borderRightWidth',
            'borderBottomWidth': 'borderBottomWidth',
            'borderLeftWidth': 'borderLeftWidth',
            'borderTopColor': 'borderTopColor',
            'borderRightColor': 'borderRightColor',
            'borderBottomColor': 'borderBottomColor',
            'borderLeftColor': 'borderLeftColor',
            'borderTopLeftRadius': 'borderTopLeftRadius',
            'borderTopRightRadius': 'borderTopRightRadius',
            'borderBottomRightRadius': 'borderBottomRightRadius',
            'borderBottomLeftRadius': 'borderBottomLeftRadius',
            'backgroundColor': 'backgroundColor',

            // Display and positioning
            'display': 'display',
            'position': 'position',
            'top': 'top',
            'right': 'right',
            'bottom': 'bottom',
            'left': 'left',
            'zIndex': 'zIndex',
            'float': 'float',
            'clear': 'clear',

            // Typography
            'fontSize': 'fontSize',
            'fontFamily': 'fontFamily',
            'fontWeight': 'fontWeight',
            'fontStyle': 'fontStyle',
            'lineHeight': 'lineHeight',
            'textAlign': 'textAlign',
            'color': 'color',

            // Flexbox
            'flexDirection': 'flexDirection',
            'flexWrap': 'flexWrap',
            'justifyContent': 'justifyContent',
            'alignItems': 'alignItems',
            'alignContent': 'alignContent',

            // Sizing
            'width': 'width',
            'height': 'height',

            // Other
            'opacity': 'opacity',
            'overflow': 'overflow',
            'visibility': 'visibility'
        };
    }

    /**
     * CSS default/initial values for common properties
     * If Lambda doesn't output a property but browser shows this value,
     * we consider it a pass (Lambda is just optimizing by not outputting defaults)
     */
    getCSSDefaultValues() {
        return {
            // Box model - default is 0
            'marginTop': '0',
            'marginRight': '0',
            'marginBottom': '0',
            'marginLeft': '0',
            'paddingTop': '0',
            'paddingRight': '0',
            'paddingBottom': '0',
            'paddingLeft': '0',
            'borderTopWidth': '0',
            'borderRightWidth': '0',
            'borderBottomWidth': '0',
            'borderLeftWidth': '0',
            'borderTopLeftRadius': '0',
            'borderTopRightRadius': '0',
            'borderBottomRightRadius': '0',
            'borderBottomLeftRadius': '0',

            // Positioning - defaults
            'position': 'static',
            'top': 'auto',
            'right': 'auto',
            'bottom': 'auto',
            'left': 'auto',
            'zIndex': 'auto',
            'float': 'none',
            'clear': 'none',

            // Display and overflow
            'overflow': 'visible',
            'overflowX': 'visible',
            'overflowY': 'visible',
            'visibility': 'visible',

            // Typography - initial values
            'lineHeight': 'normal',
            'textAlign': 'start',
            'verticalAlign': 'baseline',

            // Flexbox - defaults
            'flexDirection': 'row',
            'flexWrap': 'nowrap',
            'justifyContent': 'normal',
            'alignItems': 'normal',
            'alignContent': 'normal',
            'alignSelf': 'auto',
            'flexGrow': '0',
            'flexShrink': '1',
            'flexBasis': 'auto',
            'order': '0',
            'gap': '0',

            // Other
            'opacity': '1'
        };
    }

    /**
     * Check if a browser value matches the CSS default/initial value
     */
    isDefaultValue(property, value) {
        const defaults = this.getCSSDefaultValues();
        if (!(property in defaults)) {
            return false;
        }

        const normalizedValue = this.normalizeValue(value, property);
        const normalizedDefault = this.normalizeValue(defaults[property], property);

        return normalizedValue === normalizedDefault;
    }

    /**
     * Get nested property value from object using dot notation
     */
    getNestedValue(obj, path) {
        if (!obj) return undefined;
        return path.split('.').reduce((current, key) => {
            return current && current[key] !== undefined ? current[key] : undefined;
        }, obj);
    }

    /**
     * Normalize CSS values for comparison
     */
    normalizeValue(value, property) {
        if (value === undefined || value === null) return null;

        // Convert to string for comparison
        if (typeof value === 'number') {
            return value.toString();
        }

        if (typeof value === 'string') {
            // Normalize color values
            if (property.includes('color') || property.includes('Color') || property === 'backgroundColor') {
                return this.normalizeColor(value);
            }

            // Normalize font family (remove quotes, normalize whitespace)
            if (property === 'fontFamily' || property === 'font.family') {
                return value.replace(/["']/g, '').replace(/,\s*/g, ', ').trim().toLowerCase();
            }

            // Normalize font weight
            if (property === 'fontWeight' || property === 'font.weight') {
                const weightMap = {
                    '400': 'normal',
                    '700': 'bold',
                    'normal': 'normal',
                    'bold': 'bold'
                };
                return weightMap[value] || value;
            }

            // Normalize auto/0 values
            if (value === 'auto' || value === '0' || value === '0px') {
                return '0';
            }

            // Remove 'px' suffix for numeric comparisons
            if (value.endsWith('px')) {
                return value.slice(0, -2);
            }

            return value.toString().toLowerCase();
        }

        return value.toString();
    }

    /**
     * Normalize color values for comparison
     */
    normalizeColor(color) {
        if (!color) return null;

        // Convert rgba(r, g, b, a) format
        const rgbaMatch = color.match(/rgba?\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*(?:,\s*([\d.]+))?\s*\)/);
        if (rgbaMatch) {
            const r = parseInt(rgbaMatch[1]);
            const g = parseInt(rgbaMatch[2]);
            const b = parseInt(rgbaMatch[3]);
            const a = rgbaMatch[4] !== undefined ? parseFloat(rgbaMatch[4]) : 1.0;

            // If alpha is 0 or very close, treat as transparent
            if (a < 0.01) return 'transparent';

            // If alpha is 1, use rgb format
            if (a >= 0.99) {
                return `rgb(${r}, ${g}, ${b})`;
            }

            return `rgba(${r}, ${g}, ${b}, ${a.toFixed(2)})`;
        }

        // Convert hex colors to rgb
        const hexMatch = color.match(/^#([0-9a-fA-F]{6})([0-9a-fA-F]{2})?$/);
        if (hexMatch) {
            const r = parseInt(hexMatch[1].substr(0, 2), 16);
            const g = parseInt(hexMatch[1].substr(2, 2), 16);
            const b = parseInt(hexMatch[1].substr(4, 2), 16);

            if (hexMatch[2]) {
                const a = parseInt(hexMatch[2], 16) / 255;
                if (a < 0.01) return 'transparent';
                return `rgba(${r}, ${g}, ${b}, ${a.toFixed(2)})`;
            }

            return `rgb(${r}, ${g}, ${b})`;
        }

        // Handle named colors
        if (color === 'transparent') return 'transparent';

        return color.toLowerCase();
    }

    /**
     * Compare two numeric values with tolerance
     */
    compareNumeric(value1, value2, tolerance) {
        const num1 = parseFloat(value1);
        const num2 = parseFloat(value2);

        if (isNaN(num1) || isNaN(num2)) {
            return { match: false, difference: null };
        }

        const diff = Math.abs(num1 - num2);
        return {
            match: diff <= tolerance,
            difference: diff
        };
    }

    /**
     * Compare CSS properties for a single element
     */
    compareElementProperties(lambdaComputed, browserComputed, elementPath) {
        const mapping = this.getBrowserToLambdaMapping();
        const results = {
            totalProperties: 0,
            matches: 0,
            differences: [],
            missing: []
        };

        // Compare each mapped property
        for (const [browserProp, lambdaPath] of Object.entries(mapping)) {
            // Skip if browser doesn't have this property
            if (!(browserProp in browserComputed)) {
                continue;
            }

            results.totalProperties++;

            const browserValue = browserComputed[browserProp];
            const lambdaValue = this.getNestedValue(lambdaComputed, lambdaPath);

            // Check if Lambda CSS has the property
            if (lambdaValue === undefined || lambdaValue === null) {
                // If browser value is the default/initial value, consider it a match
                // (Lambda may be optimizing by not outputting default values)
                if (this.isDefaultValue(browserProp, browserValue)) {
                    results.matches++;
                    continue;
                }

                results.missing.push({
                    property: browserProp,
                    lambdaPath: lambdaPath,
                    browserValue: browserValue,
                    elementPath: elementPath
                });
                continue;
            }

            // Normalize values
            const normalizedBrowser = this.normalizeValue(browserValue, browserProp);
            const normalizedLambda = this.normalizeValue(lambdaValue, browserProp);

            // Try numeric comparison first
            const numericResult = this.compareNumeric(normalizedBrowser, normalizedLambda, this.tolerance);

            let match;
            let difference = null;

            if (numericResult.difference !== null) {
                match = numericResult.match;
                difference = numericResult.difference;
            } else {
                // String comparison
                match = normalizedBrowser === normalizedLambda;
            }

            if (match) {
                results.matches++;
            } else {
                results.differences.push({
                    property: browserProp,
                    lambdaPath: lambdaPath,
                    browser: browserValue,
                    lambda: lambdaValue,
                    normalizedBrowser: normalizedBrowser,
                    normalizedLambda: normalizedLambda,
                    difference: difference,
                    elementPath: elementPath
                });
            }
        }

        return results;
    }

    /**
     * Recursively compare all elements in the tree
     */
    compareElements(lambdaNode, browserNode, path = 'root', results = null) {
        if (!results) {
            results = {
                totalElements: 0,
                comparedElements: 0,
                totalProperties: 0,
                totalMatches: 0,
                differences: [],
                missing: [],
                unmatchedElements: []
            };
        }

        if (!lambdaNode || !browserNode) {
            if (lambdaNode || browserNode) {
                results.unmatchedElements.push({
                    path: path,
                    lambda: lambdaNode ? lambdaNode.tag : null,
                    browser: browserNode ? browserNode.tag : null
                });
            }
            return results;
        }

        results.totalElements++;

        // Skip elements with display: none in browser reference
        // These don't participate in layout and Lambda may not include them
        if (browserNode.computed && browserNode.computed.display === 'none') {
            // Skip this element and its children
            return results;
        }

        // Compare properties if both have computed styles
        // Handle both old format (lambdaNode.computed) and new cascade format (lambdaNode.css_properties)
        const lambdaComputed = lambdaNode.css_properties || lambdaNode.computed;
        const browserComputed = browserNode.computed;

        if (lambdaComputed && browserComputed) {
            results.comparedElements++;
            const elementPath = `${path} (${browserNode.selector || browserNode.tag})`;
            const propResults = this.compareElementProperties(
                lambdaComputed,
                browserComputed,
                elementPath
            );

            results.totalProperties += propResults.totalProperties;
            results.totalMatches += propResults.matches;
            results.differences.push(...propResults.differences);
            results.missing.push(...propResults.missing);
        }

        // Recursively compare children
        const lambdaChildren = lambdaNode.children || [];
        const browserChildren = browserNode.children || [];

        // Filter out display:none elements from browser children
        // Lambda may not include these in the layout tree
        const visibleBrowserChildren = browserChildren.filter(child => {
            return !child.computed || child.computed.display !== 'none';
        });

        const maxChildren = Math.max(lambdaChildren.length, visibleBrowserChildren.length);
        for (let i = 0; i < maxChildren; i++) {
            const childPath = `${path}/${i}`;
            this.compareElements(
                lambdaChildren[i],
                visibleBrowserChildren[i],
                childPath,
                results
            );
        }

        return results;
    }

    /**
     * Load Lambda CSS output (from /tmp/css_cascade.json)
     */
    async loadLambdaOutput() {
        try {
            const content = await fs.readFile('/tmp/css_cascade.json', 'utf8');
            return JSON.parse(content);
        } catch (error) {
            throw new Error(`Failed to load Lambda CSS output: ${error.message}`);
        }
    }

    /**
     * Load browser reference data
     */
    async loadBrowserReference(testName) {
        try {
            const refFile = path.join(this.referenceDir, `${testName}.json`);
            const content = await fs.readFile(refFile, 'utf8');
            return JSON.parse(content);
        } catch (error) {
            throw new Error(`Failed to load browser reference: ${error.message}`);
        }
    }

    /**
     * Generate comparison report
     */
    generateReport(results, testName) {
        const matchRate = results.totalProperties > 0
            ? (results.totalMatches / results.totalProperties * 100).toFixed(2)
            : 0;

        let report = '\n';
        report += '='.repeat(80) + '\n';
        report += `CSS Property Comparison Report: ${testName}\n`;
        report += '='.repeat(80) + '\n\n';

        report += `Elements compared: ${results.comparedElements} / ${results.totalElements}\n`;
        report += `Properties compared: ${results.totalProperties}\n`;
        report += `Matches: ${results.totalMatches}\n`;
        report += `Differences: ${results.differences.length}\n`;
        report += `Missing in Lambda: ${results.missing.length}\n`;
        report += `Match rate: ${matchRate}%\n\n`;

        if (results.differences.length > 0) {
            report += '─'.repeat(80) + '\n';
            report += 'Property Differences:\n';
            report += '─'.repeat(80) + '\n';

            for (const diff of results.differences) {
                report += `\n${diff.elementPath}\n`;
                report += `  Property: ${diff.property} (${diff.lambdaPath})\n`;
                report += `  Browser:  ${diff.browser} → ${diff.normalizedBrowser}\n`;
                report += `  Lambda:   ${diff.lambda} → ${diff.normalizedLambda}\n`;
                if (diff.difference !== null) {
                    report += `  Difference: ${diff.difference.toFixed(2)}px\n`;
                }
            }
        }

        if (results.missing.length > 0) {
            report += '\n' + '─'.repeat(80) + '\n';
            report += 'Missing Properties in Lambda CSS:\n';
            report += '─'.repeat(80) + '\n';

            for (const missing of results.missing) {
                report += `\n${missing.elementPath}\n`;
                report += `  Property: ${missing.property} (${missing.lambdaPath})\n`;
                report += `  Browser value: ${missing.browserValue}\n`;
            }
        }

        if (results.unmatchedElements.length > 0) {
            report += '\n' + '─'.repeat(80) + '\n';
            report += 'Unmatched Elements:\n';
            report += '─'.repeat(80) + '\n';

            for (const unmatched of results.unmatchedElements) {
                report += `\n${unmatched.path}\n`;
                report += `  Lambda: ${unmatched.lambda || '(missing)'}\n`;
                report += `  Browser: ${unmatched.browser || '(missing)'}\n`;
            }
        }

        report += '\n' + '='.repeat(80) + '\n';

        return report;
    }

    /**
     * Main comparison function
     */
    async compare(testName) {
        console.log(`\nComparing CSS properties for test: ${testName}`);
        console.log('Loading Lambda CSS output from /tmp/css_cascade.json...');

        const lambdaData = await this.loadLambdaOutput();

        console.log(`Loading browser reference: ${testName}.json...`);
        const browserData = await this.loadBrowserReference(testName);

        console.log('Comparing CSS properties...\n');

        const results = this.compareElements(
            lambdaData.layout_tree,
            browserData.layout_tree
        );

        const report = this.generateReport(results, testName);
        console.log(report);

        return results;
    }
}

// Main execution
async function main() {
    const args = process.argv.slice(2);

    if (args.length === 0) {
        console.error('Usage: node compare_css.js <test_name>');
        console.error('Example: node compare_css.js baseline_803_basic_margin');
        process.exit(1);
    }

    const testName = args[0];
    const verbose = args.includes('--verbose') || args.includes('-v');

    const comparator = new CSSComparator({ verbose });

    try {
        const results = await comparator.compare(testName);

        // Exit with error code if there are differences
        if (results.differences.length > 0 || results.missing.length > 0) {
            process.exit(1);
        }

        process.exit(0);
    } catch (error) {
        console.error(`Error: ${error.message}`);
        process.exit(1);
    }
}

if (require.main === module) {
    main();
}

module.exports = { CSSComparator };
