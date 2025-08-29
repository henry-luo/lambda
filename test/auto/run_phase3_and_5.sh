#!/bin/bash
# Phase 3 & 5 Combined Runner
# Runs comprehensive roundtrip testing and advanced reporting

set -e  # Exit on any error

echo "🚀 Running Phase 3 & 5: Comprehensive Testing and Advanced Reporting"
echo "============================================================================"

# Get script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"

# Change to project root
cd "$PROJECT_ROOT"

# Check if we're in the right directory
if [ ! -f "lambda.exe" ]; then
    echo "❌ lambda.exe not found. Please run from the project root directory."
    exit 1
fi

# Check if test data exists
if [ ! -f "test/auto/doc_list.csv" ]; then
    echo "❌ Document list not found. Please run Phase 1 & 2 first."
    exit 1
fi

# Ensure we're in the Python virtual environment
if [ -z "$VIRTUAL_ENV" ]; then
    echo "🐍 Activating Python virtual environment..."
    source test/venv/bin/activate
fi

# Phase 3: Comprehensive Roundtrip Testing
echo ""
echo "🧪 Phase 3: Running comprehensive roundtrip testing..."
echo "------------------------------------------------------"

cd test/auto
python3 phase3_roundtrip_tester.py
cd "$PROJECT_ROOT"

# Check if Phase 3 results were generated
if [ ! -f "test_output/auto/phase3_comprehensive_results.json" ]; then
    echo "❌ Phase 3 results not generated. Check for errors above."
    exit 1
fi

echo "✅ Phase 3 complete!"

# Phase 5: Advanced Reporting
echo ""
echo "📊 Phase 5: Generating advanced reports..."
echo "-------------------------------------------"

cd test/auto
python3 phase5_advanced_reporting.py
cd "$PROJECT_ROOT"

# Check if reports were generated
if [ ! -f "test_output/auto/reports/comprehensive_report.html" ]; then
    echo "❌ Phase 5 reports not generated. Check for errors above."
    exit 1
fi

echo "✅ Phase 5 complete!"

# Summary
echo ""
echo "🎉 Phase 3 & 5 Complete!"
echo "========================="
echo ""
echo "📊 Generated Reports:"
echo "  📝 Executive Summary:    test_output/auto/reports/executive_summary.md"
echo "  🌐 HTML Dashboard:       test_output/auto/reports/comprehensive_report.html"
echo "  📋 Quality Metrics:      test_output/auto/reports/quality_metrics.json"
echo "  📊 CSV Summary:          test_output/auto/reports/test_summary.csv"
echo ""
echo "🔍 Detailed Results:"
echo "  🧪 Phase 3 Results:      test_output/auto/phase3_comprehensive_results.json"
echo "  🔄 Roundtrip Tests:       test_output/auto/roundtrip/"
echo "  📊 Comparisons:           test_output/auto/comparisons/"
echo "  📈 Historical Data:       test_output/auto/history/"
echo ""
echo "💡 Next Steps:"
echo "  1. Open the HTML report for interactive analysis"
echo "  2. Review the executive summary for key findings"
echo "  3. Check roundtrip diff files for quality issues"
echo "  4. Address any regressions found in the comparison"
echo ""
echo "🌐 To view the HTML report:"
echo "  open test_output/auto/reports/comprehensive_report.html"
echo ""
