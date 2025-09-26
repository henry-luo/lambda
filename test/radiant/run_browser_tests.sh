#!/bin/bash

# Radiant Browser Layout Testing Workflow
# 
# This script demonstrates the complete workflow for using browser-generated
# layout data to test the Radiant layout engine.

set -e

echo "🚀 Radiant Browser Layout Testing Workflow"
echo "=========================================="

# Configuration
TOOLS_DIR="$(dirname "$0")/tools"
TEST_DATA_DIR="$(dirname "$0")/data"
TEST_CASES_DIR="$(dirname "$0")/test_cases"
REPORTS_DIR="$(dirname "$0")/reports"

# Create directories if they don't exist
mkdir -p "$TEST_DATA_DIR" "$TEST_CASES_DIR" "$REPORTS_DIR"

# Function to check if Node.js and npm are available
check_node_setup() {
    if ! command -v node &> /dev/null; then
        echo "❌ Node.js is required but not installed."
        echo "Please install Node.js 16+ from https://nodejs.org/"
        exit 1
    fi
    
    if ! command -v npm &> /dev/null; then
        echo "❌ npm is required but not installed."
        exit 1
    fi
    
    echo "✓ Node.js $(node --version) detected"
}

# Function to setup the browser extractor
setup_extractor() {
    echo ""
    echo "📦 Setting up browser layout extractor..."
    
    cd "$TOOLS_DIR"
    
    if [ ! -f "node_modules/.bin/puppeteer" ]; then
        echo "Installing npm dependencies..."
        npm install
    else
        echo "✓ Dependencies already installed"
    fi
    
    echo "✓ Browser extractor ready"
}

# Function to generate sample test cases
generate_sample_cases() {
    echo ""
    echo "📝 Generating sample test cases..."
    
    # Create sample HTML/CSS test cases
    cat > "$TEST_CASES_DIR/flexbox_basic.html" << 'EOF'
<div class="container">
    <div class="item item1">Item 1</div>
    <div class="item item2">Item 2</div>
    <div class="item item3">Item 3</div>
</div>
EOF

    cat > "$TEST_CASES_DIR/flexbox_basic.css" << 'EOF'
.container {
    display: flex;
    width: 600px;
    height: 100px;
    justify-content: space-between;
    align-items: center;
    background: #f0f0f0;
    padding: 10px;
}
.item {
    width: 120px;
    height: 60px;
    background: #4CAF50;
    color: white;
    display: flex;
    align-items: center;
    justify-content: center;
    border-radius: 4px;
}
EOF

    cat > "$TEST_CASES_DIR/block_margins.html" << 'EOF'
<div class="parent">
    <div class="child1">Child with margins</div>
    <div class="child2">Centered child</div>
    <div class="child3">Another block</div>
</div>
EOF

    cat > "$TEST_CASES_DIR/block_margins.css" << 'EOF'
.parent {
    width: 500px;
    padding: 20px;
    background: #e3f2fd;
}
.child1 {
    width: 200px;
    height: 80px;
    margin: 15px 20px;
    background: #ff5722;
    color: white;
    text-align: center;
    line-height: 80px;
}
.child2 {
    width: 180px;
    height: 60px;
    margin: 10px auto;
    background: #2196f3;
    color: white;
    text-align: center;
    line-height: 60px;
}
.child3 {
    width: 150px;
    height: 50px;
    margin: 20px 0;
    background: #9c27b0;
    color: white;
    text-align: center;
    line-height: 50px;
}
EOF

    cat > "$TEST_CASES_DIR/nested_flex.html" << 'EOF'
<div class="outer">
    <div class="flex-container">
        <div class="flex-item">
            <div class="nested-block">Nested Content</div>
        </div>
        <div class="flex-item">
            <div class="nested-flex">
                <span class="inline-item">A</span>
                <span class="inline-item">B</span>
            </div>
        </div>
    </div>
</div>
EOF

    cat > "$TEST_CASES_DIR/nested_flex.css" << 'EOF'
.outer {
    width: 800px;
    padding: 25px;
    background: #f5f5f5;
}
.flex-container {
    display: flex;
    gap: 30px;
    background: white;
    padding: 20px;
    border-radius: 8px;
    box-shadow: 0 2px 4px rgba(0,0,0,0.1);
}
.flex-item {
    flex: 1;
    background: #ffecb3;
    padding: 15px;
    border-radius: 4px;
}
.nested-block {
    width: 100%;
    height: 80px;
    background: #3f51b5;
    color: white;
    text-align: center;
    line-height: 80px;
    border-radius: 4px;
}
.nested-flex {
    display: flex;
    gap: 10px;
    background: #fff;
    padding: 10px;
    border-radius: 4px;
}
.inline-item {
    padding: 8px 16px;
    background: #ff9800;
    color: white;
    border-radius: 20px;
    font-weight: bold;
}
EOF

    echo "✓ Generated sample test cases:"
    echo "  - flexbox_basic: Basic flexbox layout with space-between"
    echo "  - block_margins: Block elements with various margin configurations" 
    echo "  - nested_flex: Complex nested layout with flex and block elements"
}

# Function to extract layout data from browser
extract_browser_data() {
    echo ""
    echo "🌐 Extracting layout data from browser..."
    
    cd "$TOOLS_DIR"
    
    # Extract layout data for each test case
    for html_file in "$TEST_CASES_DIR"/*.html; do
        if [ -f "$html_file" ]; then
            base_name=$(basename "$html_file" .html)
            css_file="$TEST_CASES_DIR/${base_name}.css"
            output_file="$TEST_DATA_DIR/${base_name}.json"
            
            if [ -f "$css_file" ]; then
                echo "Extracting: $base_name"
                node layout_extractor.js extract-single "$html_file" "$css_file" "$output_file"
            else
                echo "⚠️  No CSS file found for $base_name, skipping"
            fi
        fi
    done
    
    echo "✓ Browser layout data extracted to $TEST_DATA_DIR/"
}

# Function to run C++ tests with extracted data
run_cpp_tests() {
    echo ""
    echo "🧪 Running C++ integration tests..."
    
    # This would compile and run the C++ test suite
    # For now, we'll just show what would happen
    
    echo "Would run:"
    echo "  1. Compile browser_layout_validator.cpp with JSON-C library"
    echo "  2. Compile test_browser_integration_gtest.cpp with Criterion"
    echo "  3. Execute tests against extracted JSON data"
    echo "  4. Generate HTML test report"
    
    echo ""
    echo "Sample make targets to add to main Makefile:"
    echo ""
    cat << 'EOF'
# Add to Makefile
build-browser-tests: build
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -I./test/radiant \
		test/radiant/browser_layout_validator.cpp \
		test/radiant/test_browser_integration_gtest.cpp \
		-ljsoncpp -lcriterion -o test_browser_integration

run-browser-tests: build-browser-tests
	./test_browser_integration
	
browser-test-report: run-browser-tests
	./test_browser_integration --html-report=test/radiant/reports/results.html
EOF
}

# Function to generate documentation
generate_docs() {
    echo ""
    echo "📚 Generating documentation..."
    
    cat > "$REPORTS_DIR/README.md" << 'EOF'
# Radiant Browser Layout Testing

This directory contains automated layout testing using real browser engines as reference.

## How It Works

1. **Test Case Creation**: HTML/CSS test cases are created in `test_cases/`
2. **Browser Extraction**: Puppeteer loads each test case in a headless browser and extracts computed layout properties  
3. **Reference Data**: Layout data is saved as JSON in `data/` for use as reference
4. **Radiant Validation**: C++ tests load the JSON data and compare against Radiant's layout engine output
5. **Reporting**: HTML reports show differences and compliance metrics

## Generated Files

### Test Data (`data/`)
- `*.json` - Browser-extracted layout reference data
- Each file contains element positions, dimensions, and computed CSS properties

### Test Reports (`reports/`)
- `results.html` - HTML test report with pass/fail status and difference details
- `compliance_matrix.html` - CSS feature compliance tracking
- Automated visual diff images (if enabled)

## Usage

```bash
# Full workflow
./run_browser_tests.sh

# Individual steps
./run_browser_tests.sh --extract-only    # Just extract browser data
./run_browser_tests.sh --test-only       # Just run C++ tests
./run_browser_tests.sh --report-only     # Just generate reports
```

## Benefits

- **Spec Compliance**: Tests directly against browser behavior
- **Regression Prevention**: Automated detection of layout changes
- **Cross-Platform**: Consistent reference across different systems
- **Documentation**: Test cases serve as layout feature examples
EOF

    echo "✓ Documentation generated in $REPORTS_DIR/README.md"
}

# Function to show final summary
show_summary() {
    echo ""
    echo "🎉 Browser Layout Testing Setup Complete!"
    echo "========================================"
    echo ""
    echo "What was created:"
    echo "📁 $TOOLS_DIR/ - Browser layout extraction tools"
    echo "📁 $TEST_CASES_DIR/ - Sample HTML/CSS test cases"  
    echo "📁 $TEST_DATA_DIR/ - Browser-extracted reference data"
    echo "📁 $REPORTS_DIR/ - Test reports and documentation"
    echo ""
    echo "Next steps:"
    echo "1. Add more test cases to $TEST_CASES_DIR/"
    echo "2. Run './run_browser_tests.sh' to extract fresh browser data"
    echo "3. Integrate C++ tests into main build system"
    echo "4. Set up CI/CD to run tests automatically"
    echo ""
    echo "Key files to examine:"
    echo "- $TEST_DATA_DIR/*.json (browser reference data)"
    echo "- test/radiant/browser_layout_validator.hpp (C++ integration)"
    echo "- $REPORTS_DIR/README.md (detailed documentation)"
}

# Main execution
main() {
    case "${1:-all}" in
        "extract-only")
            check_node_setup
            setup_extractor
            extract_browser_data
            ;;
        "test-only")
            run_cpp_tests
            ;;
        "report-only")
            generate_docs
            ;;
        "all"|"")
            check_node_setup
            setup_extractor
            generate_sample_cases
            extract_browser_data
            run_cpp_tests
            generate_docs
            show_summary
            ;;
        *)
            echo "Usage: $0 [extract-only|test-only|report-only|all]"
            exit 1
            ;;
    esac
}

# Run main function with all arguments
main "$@"
