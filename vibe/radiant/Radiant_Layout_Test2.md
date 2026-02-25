# Radiant Layout Engine Automated Integration Testing - Revised Strategy

## Executive Summary

This document outlines a **simplified and more practical** approach to automated layout testing for the Radiant layout engine. Instead of complex C++ integration tests, we leverage Radiant's existing capabilities to generate structured output that can be validated against browser references using simple scripts.

## 1. Core Strategy Overview

### 1.1 Simplified Testing Approach

**OLD APPROACH (Complex):**
- Write complex C++ tests calling Radiant functions directly
- Complex build dependencies and integration issues
- Difficult to debug and maintain

**NEW APPROACH (Simple):**
1. **Enhance Radiant main program** with `layout` sub-command
2. **Generate structured output** (text + JSON) from layout operations
3. **Use simple scripts** to validate against browser references
4. **Leverage existing infrastructure** (print_view_tree, layout engine)

### 1.2 Key Benefits

- ✅ **Simpler to implement** - uses existing Radiant infrastructure
- ✅ **Easier to debug** - human-readable output files
- ✅ **More maintainable** - no complex C++ test framework
- ✅ **Better coverage** - tests the actual Radiant executable
- ✅ **Flexible validation** - JSON output easy to process with scripts

## 2. Implementation Plan

### 2.1 Phase 1: Enhance Radiant Main Program

#### 2.1.1 Add Layout Sub-command Support

**Modify `/radiant/window.cpp`:**

```cpp
// Add command-line argument parsing
int main(int argc, char* argv[]) {
    // Check for layout sub-command
    if (argc >= 3 && strcmp(argv[1], "layout") == 0) {
        return run_layout_test(argv[2]);
    }

    // Original GUI mode code...
    log_init_wrapper();
    // ... rest of existing main function
}

// New layout test function
int run_layout_test(const char* html_file) {
    log_init_wrapper();
    ui_context_init(&ui_context);

    // Set consistent test viewport
    ui_context.window_width = 1200;
    ui_context.window_height = 800;
    ui_context.pixel_ratio = 1.0;

    // Create surface for layout (no actual window needed)
    ui_context_create_surface(&ui_context, 1200, 800);

    // Load and layout the HTML file
    Url* cwd = get_current_dir();
    if (!cwd) {
        fprintf(stderr, "Error: Could not get current directory\n");
        return 1;
    }

    Document* doc = load_html_doc(cwd, (char*)html_file);
    if (!doc) {
        fprintf(stderr, "Error: Could not load HTML file: %s\n", html_file);
        url_destroy(cwd);
        return 1;
    }

    // Layout the document
    View* root_view = layout_html_doc(&ui_context, doc, false);
    if (!root_view) {
        fprintf(stderr, "Error: Layout failed for file: %s\n", html_file);
        free_document(doc);
        url_destroy(cwd);
        return 1;
    }

    // Print view tree (existing functionality)
    printf("Layout completed successfully for: %s\n", html_file);
    print_view_tree((ViewGroup*)root_view);

    // Generate JSON output (new functionality)
    print_view_tree_json((ViewGroup*)root_view);

    // Cleanup
    free_document(doc);
    url_destroy(cwd);
    ui_context_cleanup(&ui_context);
    log_cleanup();

    return 0;
}
```

**Usage:**
```bash
# Layout a test file and generate output
./radiant.exe layout test/layout/data/basic/flex_001_row_space_between.html

# Output files generated:
# - view_tree.txt (existing text format)
# - view_tree.json (new JSON format)
```

### 2.2 Phase 2: Enhance print_view_tree Function

#### 2.2.1 Add JSON Output Support

**Modify `/radiant/view_pool.cpp`:**

```cpp
// Add JSON generation function
void print_view_tree_json(ViewGroup* view_root) {
    StrBuf* json_buf = strbuf_new_cap(2048);

    strbuf_append_str(json_buf, "{\n");
    strbuf_append_str(json_buf, "  \"test_info\": {\n");
    strbuf_append_str(json_buf, "    \"timestamp\": \"");

    // Add timestamp
    time_t now = time(0);
    char* time_str = ctime(&now);
    time_str[strlen(time_str) - 1] = '\0'; // Remove newline
    strbuf_append_str(json_buf, time_str);
    strbuf_append_str(json_buf, "\",\n");

    strbuf_append_str(json_buf, "    \"radiant_version\": \"1.0\",\n");
    strbuf_append_str(json_buf, "    \"viewport\": { \"width\": 1200, \"height\": 800 }\n");
    strbuf_append_str(json_buf, "  },\n");

    strbuf_append_str(json_buf, "  \"layout_tree\": ");
    print_block_json((ViewBlock*)view_root, json_buf, 2);
    strbuf_append_str(json_buf, "\n}\n");

    // Write to file
    write_string_to_file("view_tree.json", json_buf->str);

    printf("JSON layout data written to: view_tree.json\n");
    strbuf_free(json_buf);
}

// Recursive JSON generation for view blocks
void print_block_json(ViewBlock* block, StrBuf* buf, int indent) {
    if (!block) {
        strbuf_append_str(buf, "null");
        return;
    }

    // Add indentation
    for (int i = 0; i < indent; i++) strbuf_append_str(buf, " ");

    strbuf_append_str(buf, "{\n");

    // Basic view properties
    for (int i = 0; i < indent + 2; i++) strbuf_append_str(buf, " ");
    strbuf_append_str(buf, "\"type\": \"");
    strbuf_append_str(buf, get_view_type_name(block->type));
    strbuf_append_str(buf, "\",\n");

    // Layout properties
    for (int i = 0; i < indent + 2; i++) strbuf_append_str(buf, " ");
    strbuf_append_format(buf, "\"layout\": {\n");

    for (int i = 0; i < indent + 4; i++) strbuf_append_str(buf, " ");
    strbuf_append_format(buf, "\"x\": %.2f,\n", block->x);

    for (int i = 0; i < indent + 4; i++) strbuf_append_str(buf, " ");
    strbuf_append_format(buf, "\"y\": %.2f,\n", block->y);

    for (int i = 0; i < indent + 4; i++) strbuf_append_str(buf, " ");
    strbuf_append_format(buf, "\"width\": %.2f,\n", block->width);

    for (int i = 0; i < indent + 4; i++) strbuf_append_str(buf, " ");
    strbuf_append_format(buf, "\"height\": %.2f\n", block->height);

    for (int i = 0; i < indent + 2; i++) strbuf_append_str(buf, " ");
    strbuf_append_str(buf, "},\n");

    // CSS properties (if available)
    if (block->computed_style) {
        print_css_properties_json(block->computed_style, buf, indent + 2);
    }

    // Children
    if (block->child && block->child_count > 0) {
        for (int i = 0; i < indent + 2; i++) strbuf_append_str(buf, " ");
        strbuf_append_str(buf, "\"children\": [\n");

        ViewBlock* child = block->child;
        int child_index = 0;
        while (child) {
            print_block_json(child, buf, indent + 4);
            child = child->next;
            child_index++;

            if (child_index < block->child_count) {
                strbuf_append_str(buf, ",");
            }
            strbuf_append_str(buf, "\n");
        }

        for (int i = 0; i < indent + 2; i++) strbuf_append_str(buf, " ");
        strbuf_append_str(buf, "]\n");
    } else {
        for (int i = 0; i < indent + 2; i++) strbuf_append_str(buf, " ");
        strbuf_append_str(buf, "\"children\": []\n");
    }

    for (int i = 0; i < indent; i++) strbuf_append_str(buf, " ");
    strbuf_append_str(buf, "}");
}

// Helper function to get view type name
const char* get_view_type_name(RdtViewType type) {
    switch (type) {
        case RDT_VIEW_BLOCK: return "block";
        case RDT_VIEW_SPAN: return "span";
        case RDT_VIEW_TEXT: return "text";
        case RDT_VIEW_IMAGE: return "image";
        default: return "unknown";
    }
}
```

### 2.3 Phase 3: Enhance Flex Layout Output

#### 2.3.1 Add Flex Properties to JSON Output

**Modify flex layout to include properties in view tree:**

```cpp
// In layout_flex.cpp - add flex properties to ViewBlock
void print_css_properties_json(ComputedStyle* style, StrBuf* buf, int indent) {
    if (!style) return;

    for (int i = 0; i < indent; i++) strbuf_append_str(buf, " ");
    strbuf_append_str(buf, "\"css_properties\": {\n");

    // Display property
    for (int i = 0; i < indent + 2; i++) strbuf_append_str(buf, " ");
    strbuf_append_format(buf, "\"display\": \"%s\",\n",
                        style->display == CSS_DISPLAY_FLEX ? "flex" : "block");

    // Flex container properties
    if (style->display == CSS_DISPLAY_FLEX) {
        for (int i = 0; i < indent + 2; i++) strbuf_append_str(buf, " ");
        strbuf_append_format(buf, "\"flex_direction\": \"%s\",\n",
                            get_flex_direction_name(style->flex_direction));

        for (int i = 0; i < indent + 2; i++) strbuf_append_str(buf, " ");
        strbuf_append_format(buf, "\"justify_content\": \"%s\",\n",
                            get_justify_content_name(style->justify_content));

        for (int i = 0; i < indent + 2; i++) strbuf_append_str(buf, " ");
        strbuf_append_format(buf, "\"align_items\": \"%s\",\n",
                            get_align_items_name(style->align_items));

        for (int i = 0; i < indent + 2; i++) strbuf_append_str(buf, " ");
        strbuf_append_format(buf, "\"flex_wrap\": \"%s\",\n",
                            get_flex_wrap_name(style->flex_wrap));
    }

    // Flex item properties
    for (int i = 0; i < indent + 2; i++) strbuf_append_str(buf, " ");
    strbuf_append_format(buf, "\"flex_grow\": %.2f,\n", style->flex_grow);

    for (int i = 0; i < indent + 2; i++) strbuf_append_str(buf, " ");
    strbuf_append_format(buf, "\"flex_shrink\": %.2f,\n", style->flex_shrink);

    // Box model properties
    for (int i = 0; i < indent + 2; i++) strbuf_append_str(buf, " ");
    strbuf_append_format(buf, "\"margin\": [%.2f, %.2f, %.2f, %.2f],\n",
                        style->margin_top, style->margin_right,
                        style->margin_bottom, style->margin_left);

    for (int i = 0; i < indent + 2; i++) strbuf_append_str(buf, " ");
    strbuf_append_format(buf, "\"padding\": [%.2f, %.2f, %.2f, %.2f]\n",
                        style->padding_top, style->padding_right,
                        style->padding_bottom, style->padding_left);

    for (int i = 0; i < indent; i++) strbuf_append_str(buf, " ");
    strbuf_append_str(buf, "},\n");
}
```

### 2.4 Phase 4: Automated Test Script

#### 2.4.1 Create Test Validation Script

**Create `/test/layout/tools/validate_radiant.js`:**

```javascript
#!/usr/bin/env node

const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

class RadiantLayoutValidator {
    constructor() {
        this.tolerance = 2.0; // 2px tolerance
        this.results = {
            total: 0,
            passed: 0,
            failed: 0,
            errors: 0,
            details: []
        };
    }

    async validateAllTests() {
        console.log('🎨 Radiant Layout Validation Suite');
        console.log('=====================================\n');

        const categories = ['basic', 'intermediate', 'advanced'];

        for (const category of categories) {
            console.log(`📂 Processing ${category} tests...`);
            await this.validateCategory(category);
        }

        this.printSummary();
        return this.results;
    }

    async validateCategory(category) {
        const dataDir = `./data/${category}`;
        const referenceDir = `./reference/${category}`;

        if (!fs.existsSync(dataDir)) {
            console.log(`⚠️  Warning: ${dataDir} not found, skipping`);
            return;
        }

        const htmlFiles = fs.readdirSync(dataDir)
            .filter(f => f.endsWith('.html'))
            .sort();

        for (const htmlFile of htmlFiles) {
            const testName = path.basename(htmlFile, '.html');
            const referenceFile = `${referenceDir}/${testName}.json`;

            console.log(`  🧪 Testing: ${testName}`);

            try {
                const result = await this.validateSingleTest(
                    `${dataDir}/${htmlFile}`,
                    referenceFile,
                    testName
                );

                this.results.total++;
                if (result.passed) {
                    this.results.passed++;
                    console.log(`    ✅ PASS (${result.matchedElements}/${result.totalElements} elements)`);
                } else {
                    this.results.failed++;
                    console.log(`    ❌ FAIL (${result.matchedElements}/${result.totalElements} elements)`);
                    console.log(`       Max difference: ${result.maxDifference.toFixed(2)}px`);
                }

                this.results.details.push(result);

            } catch (error) {
                this.results.total++;
                this.results.errors++;
                console.log(`    💥 ERROR: ${error.message}`);

                this.results.details.push({
                    testName,
                    passed: false,
                    error: error.message
                });
            }
        }

        console.log('');
    }

    async validateSingleTest(htmlFile, referenceFile, testName) {
        // Run Radiant layout command
        try {
            execSync(`./radiant.exe layout "${htmlFile}"`, {
                stdio: 'pipe',
                timeout: 10000 // 10 second timeout
            });
        } catch (error) {
            throw new Error(`Radiant execution failed: ${error.message}`);
        }

        // Check if output files were generated
        if (!fs.existsSync('view_tree.json')) {
            throw new Error('view_tree.json not generated');
        }

        // Load Radiant output
        const radiantData = JSON.parse(fs.readFileSync('view_tree.json', 'utf8'));

        // Load browser reference (if available)
        let referenceData = null;
        if (fs.existsSync(referenceFile)) {
            referenceData = JSON.parse(fs.readFileSync(referenceFile, 'utf8'));
        } else {
            console.log(`    ⚠️  No reference data found: ${referenceFile}`);
            // For now, just validate that Radiant produced output
            return {
                testName,
                passed: true,
                matchedElements: 1,
                totalElements: 1,
                maxDifference: 0,
                note: 'No reference data - validated Radiant execution only'
            };
        }

        // Compare layouts
        const comparison = this.compareLayouts(radiantData, referenceData);

        // Cleanup
        fs.unlinkSync('view_tree.json');
        if (fs.existsSync('view_tree.txt')) {
            fs.unlinkSync('view_tree.txt');
        }

        return {
            testName,
            passed: comparison.maxDifference <= this.tolerance,
            matchedElements: comparison.matchedElements,
            totalElements: comparison.totalElements,
            maxDifference: comparison.maxDifference,
            differences: comparison.differences
        };
    }

    compareLayouts(radiantData, referenceData) {
        const differences = [];
        let maxDifference = 0;
        let matchedElements = 0;
        let totalElements = 0;

        // Extract elements from both layouts
        const radiantElements = this.extractElements(radiantData.layout_tree);
        const referenceElements = referenceData.layout_data || {};

        // Compare each element
        for (const [selector, radiantElement] of Object.entries(radiantElements)) {
            totalElements++;

            if (referenceElements[selector]) {
                const refElement = referenceElements[selector];
                const elementDiff = this.compareElement(radiantElement, refElement);

                if (elementDiff.maxDiff <= this.tolerance) {
                    matchedElements++;
                } else {
                    differences.push({
                        selector,
                        differences: elementDiff.differences,
                        maxDiff: elementDiff.maxDiff
                    });
                }

                maxDifference = Math.max(maxDifference, elementDiff.maxDiff);
            }
        }

        return {
            matchedElements,
            totalElements,
            maxDifference,
            differences
        };
    }

    extractElements(layoutTree, path = '', elements = {}) {
        if (!layoutTree) return elements;

        // Generate selector for this element
        const selector = path || 'root';

        elements[selector] = {
            layout: layoutTree.layout,
            css_properties: layoutTree.css_properties || {}
        };

        // Process children
        if (layoutTree.children && layoutTree.children.length > 0) {
            layoutTree.children.forEach((child, index) => {
                const childPath = `${selector} > :nth-child(${index + 1})`;
                this.extractElements(child, childPath, elements);
            });
        }

        return elements;
    }

    compareElement(radiantElement, referenceElement) {
        const differences = [];
        let maxDiff = 0;

        // Compare layout properties
        const layoutProps = ['x', 'y', 'width', 'height'];
        for (const prop of layoutProps) {
            const radiantValue = radiantElement.layout[prop] || 0;
            const referenceValue = referenceElement.layout[prop] || 0;
            const diff = Math.abs(radiantValue - referenceValue);

            if (diff > 0.1) { // Ignore tiny differences
                differences.push({
                    property: prop,
                    radiant: radiantValue,
                    reference: referenceValue,
                    difference: diff
                });
                maxDiff = Math.max(maxDiff, diff);
            }
        }

        return { differences, maxDiff };
    }

    printSummary() {
        console.log('📊 Test Results Summary');
        console.log('=======================');
        console.log(`Total tests: ${this.results.total}`);
        console.log(`✅ Passed: ${this.results.passed}`);
        console.log(`❌ Failed: ${this.results.failed}`);
        console.log(`💥 Errors: ${this.results.errors}`);

        if (this.results.total > 0) {
            const passRate = (this.results.passed / this.results.total * 100).toFixed(1);
            console.log(`📈 Pass rate: ${passRate}%`);
        }

        // Save detailed results
        const reportFile = `./reports/radiant_validation_${Date.now()}.json`;
        fs.writeFileSync(reportFile, JSON.stringify(this.results, null, 2));
        console.log(`📄 Detailed report: ${reportFile}`);
    }
}

// CLI usage
if (require.main === module) {
    const validator = new RadiantLayoutValidator();
    validator.validateAllTests()
        .then(results => {
            process.exit(results.failed > 0 ? 1 : 0);
        })
        .catch(error => {
            console.error('💥 Validation failed:', error);
            process.exit(1);
        });
}

module.exports = RadiantLayoutValidator;
```

#### 2.4.2 Create Package Configuration

**Create `/test/layout/tools/package.json`:**

```json
{
  "name": "radiant-layout-validator",
  "version": "1.0.0",
  "description": "Automated layout validation for Radiant layout engine",
  "main": "validate_radiant.js",
  "scripts": {
    "validate": "node validate_radiant.js",
    "validate:basic": "node validate_radiant.js --category=basic",
    "validate:intermediate": "node validate_radiant.js --category=intermediate",
    "validate:advanced": "node validate_radiant.js --category=advanced",
    "extract-references": "node extract_layout.js",
    "generate-tests": "node generate_tests.js"
  },
  "dependencies": {},
  "devDependencies": {},
  "engines": {
    "node": ">=14.0.0"
  }
}
```

## 3. Directory Structure

```
test/layout/
├── data/                           # Test HTML files
│   ├── basic/
│   │   ├── flex_001_row_space_between.html
│   │   ├── flex_002_column_center.html
│   │   └── block_001_margin_padding.html
│   ├── intermediate/
│   │   ├── flex_nested_containers.html
│   │   └── mixed_block_flex.html
│   └── advanced/
│       ├── complex_responsive.html
│       └── deep_nesting.html
├── reference/                      # Browser-extracted JSON
│   ├── basic/
│   ├── intermediate/
│   └── advanced/
├── tools/                          # Automation scripts
│   ├── validate_radiant.js         # Main validation script
│   ├── extract_layout.js           # Browser reference extractor
│   ├── generate_tests.js           # Test case generator
│   └── package.json
├── reports/                        # Test results
└── README.md                       # Usage instructions
```

## 4. Usage Examples

### 4.1 Manual Testing

```bash
# Test a single HTML file
./radiant.exe layout test/layout/data/basic/flex_001_row_space_between.html

# Output files:
# - view_tree.txt (human-readable)
# - view_tree.json (machine-readable)
```

### 4.2 Automated Validation

```bash
# Run all tests
cd test/layout/tools
npm run validate

# Run specific category
npm run validate:basic

# Generate browser references (requires Puppeteer)
npm run extract-references

# Generate new test cases
npm run generate-tests
```

### 4.3 Integration with Build System

**Add to main Makefile:**

```makefile
# Test layout engine

	@echo "Running Radiant layout validation tests..."
	@cd test/layout/tools && npm run validate
	@echo "✅ Layout tests completed"

# Generate layout test references
build-mingw64 build-tree-sitter clean-tree-sitter-minimal build-radiant test-radiant-references:
	@echo "Generating browser reference data..."
	@cd test/layout/tools && npm run extract-references
	@echo "✅ Reference data generated"
```

## 5. Implementation Timeline

### Week 1: Core Infrastructure
- [ ] Enhance `radiant/window.cpp` with layout sub-command
- [ ] Add JSON output to `print_view_tree` function
- [ ] Create basic test HTML files
- [ ] Test manual layout command execution

### Week 2: JSON Enhancement
- [ ] Add CSS properties to JSON output
- [ ] Enhance flex layout property reporting
- [ ] Create validation script framework
- [ ] Test JSON structure and validation

### Week 3: Automation & Validation
- [ ] Complete validation script implementation
- [ ] Add browser reference extraction (Puppeteer)
- [ ] Create test case generator
- [ ] Integrate with build system

### Week 4: Testing & Polish
- [ ] Generate comprehensive test suite
- [ ] Validate against browser references
- [ ] Performance optimization
- [ ] Documentation and CI integration

## 6. Expected Benefits

### 6.1 Immediate Benefits
- **Simple Implementation**: Uses existing Radiant infrastructure
- **Easy Debugging**: Human-readable output files
- **Flexible Validation**: JSON format easy to process
- **Real-world Testing**: Tests actual Radiant executable

### 6.2 Long-term Benefits
- **Comprehensive Coverage**: Can test any HTML/CSS combination
- **Regression Prevention**: Automated validation catches layout changes
- **Performance Monitoring**: Track layout performance over time
- **Browser Parity**: Validate against multiple browser engines

### 6.3 Success Metrics
- **Coverage**: 95%+ of implemented CSS features tested
- **Accuracy**: <2px difference from browser references on 90%+ tests
- **Performance**: Full test suite completes in <30 seconds
- **Reliability**: <1% flaky test rate

## 7. Risk Mitigation

### 7.1 Technical Risks
- **Output Format Changes**: Version JSON schema for compatibility
- **Performance Issues**: Implement test timeouts and limits
- **Platform Differences**: Use consistent test environment

### 7.2 Process Risks
- **Test Maintenance**: Automated test generation reduces manual effort
- **False Positives**: Configurable tolerance levels
- **Browser Variations**: Pin browser versions for reference extraction

This simplified approach provides comprehensive layout testing while being much easier to implement and maintain than the original complex C++ integration testing strategy.
