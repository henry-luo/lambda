#!/usr/bin/env python3
"""
Radiant Layout Engine Automated Testing

This script provides automated layout validation for the Radiant layout engine
by running layout tests via CLI and comparing results against browser references.
"""

import os
import sys
import json
import subprocess
import time
import argparse
from pathlib import Path
from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass, asdict


@dataclass
class TestResult:
    """Result of a single layout test"""
    test_name: str
    passed: bool
    error_message: Optional[str] = None
    execution_time: float = 0.0
    matched_elements: int = 0
    total_elements: int = 0
    max_difference: float = 0.0
    differences: List[Dict] = None
    
    def __post_init__(self):
        if self.differences is None:
            self.differences = []


@dataclass
class TestSummary:
    """Summary of all test results"""
    total_tests: int = 0
    passed_tests: int = 0
    failed_tests: int = 0
    error_tests: int = 0
    total_time: float = 0.0
    results: List[TestResult] = None
    
    def __post_init__(self):
        if self.results is None:
            self.results = []
    
    @property
    def pass_rate(self) -> float:
        return (self.passed_tests / self.total_tests * 100) if self.total_tests > 0 else 0.0


class RadiantLayoutTester:
    """Main class for running Radiant layout tests"""
    
    def __init__(self, radiant_exe: str = "./radiant.exe", tolerance: float = 2.0):
        self.radiant_exe = radiant_exe
        self.tolerance = tolerance
        self.test_dir = Path("test/layout")
        self.data_dir = self.test_dir / "data"
        self.reference_dir = self.test_dir / "reference"
        self.reports_dir = self.test_dir / "reports"
        
        # Ensure directories exist
        self.reports_dir.mkdir(parents=True, exist_ok=True)
        
    def discover_tests(self, category: Optional[str] = None) -> List[Path]:
        """Discover available test HTML files"""
        test_files = []
        
        if category:
            categories = [category]
        else:
            categories = ["basic", "intermediate", "advanced"]
        
        for cat in categories:
            cat_dir = self.data_dir / cat
            if cat_dir.exists():
                html_files = list(cat_dir.glob("*.html"))
                test_files.extend(html_files)
        
        return sorted(test_files)
    
    def run_single_test(self, html_file: Path) -> TestResult:
        """Run a single layout test"""
        test_name = html_file.stem
        category = html_file.parent.name
        
        print(f"  🧪 Testing: {test_name}")
        
        start_time = time.time()
        
        try:
            # Run Radiant layout command
            result = subprocess.run(
                [self.radiant_exe, "layout", str(html_file)],
                capture_output=True,
                text=True,
                timeout=30,  # 30 second timeout
                cwd=Path.cwd()
            )
            
            execution_time = time.time() - start_time
            
            if result.returncode != 0:
                return TestResult(
                    test_name=test_name,
                    passed=False,
                    error_message=f"Radiant execution failed (exit code {result.returncode}): {result.stderr}",
                    execution_time=execution_time
                )
            
            # Check if output files were generated
            view_tree_json = Path("view_tree.json")
            view_tree_txt = Path("view_tree.txt")
            
            if not view_tree_json.exists():
                return TestResult(
                    test_name=test_name,
                    passed=False,
                    error_message="view_tree.json not generated",
                    execution_time=execution_time
                )
            
            # Load Radiant output
            try:
                with open(view_tree_json, 'r') as f:
                    radiant_data = json.load(f)
            except json.JSONDecodeError as e:
                return TestResult(
                    test_name=test_name,
                    passed=False,
                    error_message=f"Invalid JSON in view_tree.json: {e}",
                    execution_time=execution_time
                )
            
            # Load browser reference (if available)
            reference_file = self.reference_dir / category / f"{test_name}.json"
            if reference_file.exists():
                try:
                    with open(reference_file, 'r') as f:
                        reference_data = json.load(f)
                    
                    # Compare layouts
                    comparison = self.compare_layouts(radiant_data, reference_data)
                    
                    test_result = TestResult(
                        test_name=test_name,
                        passed=comparison["max_difference"] <= self.tolerance,
                        execution_time=execution_time,
                        matched_elements=comparison["matched_elements"],
                        total_elements=comparison["total_elements"],
                        max_difference=comparison["max_difference"],
                        differences=comparison["differences"]
                    )
                    
                except json.JSONDecodeError as e:
                    test_result = TestResult(
                        test_name=test_name,
                        passed=False,
                        error_message=f"Invalid reference JSON: {e}",
                        execution_time=execution_time
                    )
            else:
                # No reference data - just validate that Radiant produced output
                elements_count = self.count_elements(radiant_data.get("layout_tree", {}))
                test_result = TestResult(
                    test_name=test_name,
                    passed=True,
                    execution_time=execution_time,
                    matched_elements=elements_count,
                    total_elements=elements_count,
                    max_difference=0.0
                )
                print(f"    ⚠️  No reference data - validated Radiant execution only")
            
            # Cleanup output files
            if view_tree_json.exists():
                view_tree_json.unlink()
            if view_tree_txt.exists():
                view_tree_txt.unlink()
            
            # Print result
            if test_result.passed:
                print(f"    ✅ PASS ({test_result.matched_elements}/{test_result.total_elements} elements)")
            else:
                print(f"    ❌ FAIL ({test_result.matched_elements}/{test_result.total_elements} elements)")
                if test_result.max_difference > 0:
                    print(f"       Max difference: {test_result.max_difference:.2f}px")
            
            return test_result
            
        except subprocess.TimeoutExpired:
            return TestResult(
                test_name=test_name,
                passed=False,
                error_message="Test timed out after 30 seconds",
                execution_time=time.time() - start_time
            )
        except Exception as e:
            return TestResult(
                test_name=test_name,
                passed=False,
                error_message=f"Unexpected error: {str(e)}",
                execution_time=time.time() - start_time
            )
    
    def count_elements(self, layout_tree: Dict) -> int:
        """Count elements in layout tree"""
        if not layout_tree:
            return 0
        
        count = 1  # Count this element
        children = layout_tree.get("children", [])
        for child in children:
            count += self.count_elements(child)
        
        return count
    
    def compare_layouts(self, radiant_data: Dict, reference_data: Dict) -> Dict:
        """Compare Radiant layout with browser reference"""
        differences = []
        max_difference = 0.0
        matched_elements = 0
        total_elements = 0
        
        # Extract elements from both layouts
        radiant_elements = self.extract_elements(radiant_data.get("layout_tree", {}))
        reference_elements = reference_data.get("layout_data", {})
        
        # Compare each element
        for selector, radiant_element in radiant_elements.items():
            total_elements += 1
            
            if selector in reference_elements:
                ref_element = reference_elements[selector]
                element_diff = self.compare_element(radiant_element, ref_element)
                
                if element_diff["max_diff"] <= self.tolerance:
                    matched_elements += 1
                else:
                    differences.append({
                        "selector": selector,
                        "differences": element_diff["differences"],
                        "max_diff": element_diff["max_diff"]
                    })
                
                max_difference = max(max_difference, element_diff["max_diff"])
        
        return {
            "matched_elements": matched_elements,
            "total_elements": total_elements,
            "max_difference": max_difference,
            "differences": differences
        }
    
    def extract_elements(self, layout_tree: Dict, path: str = "", elements: Dict = None) -> Dict:
        """Extract elements from layout tree with CSS-like selectors"""
        if elements is None:
            elements = {}
        
        if not layout_tree:
            return elements
        
        # Generate selector for this element
        selector = path or "root"
        
        elements[selector] = {
            "layout": layout_tree.get("layout", {}),
            "css_properties": layout_tree.get("css_properties", {}),
            "type": layout_tree.get("type", "unknown"),
            "tag": layout_tree.get("tag", "unknown")
        }
        
        # Process children
        children = layout_tree.get("children", [])
        for index, child in enumerate(children):
            child_path = f"{selector} > :nth-child({index + 1})"
            self.extract_elements(child, child_path, elements)
        
        return elements
    
    def compare_element(self, radiant_element: Dict, reference_element: Dict) -> Dict:
        """Compare a single element between Radiant and reference"""
        differences = []
        max_diff = 0.0
        
        # Compare layout properties
        layout_props = ["x", "y", "width", "height"]
        radiant_layout = radiant_element.get("layout", {})
        ref_layout = reference_element.get("layout", {})
        
        for prop in layout_props:
            radiant_value = radiant_layout.get(prop, 0)
            ref_value = ref_layout.get(prop, 0)
            diff = abs(radiant_value - ref_value)
            
            if diff > 0.1:  # Ignore tiny differences
                differences.append({
                    "property": prop,
                    "radiant": radiant_value,
                    "reference": ref_value,
                    "difference": diff
                })
                max_diff = max(max_diff, diff)
        
        return {"differences": differences, "max_diff": max_diff}
    
    def run_category(self, category: str) -> TestSummary:
        """Run all tests in a category"""
        print(f"📂 Processing {category} tests...")
        
        test_files = self.discover_tests(category)
        if not test_files:
            print(f"  ⚠️  No test files found in {category}")
            return TestSummary()
        
        summary = TestSummary()
        start_time = time.time()
        
        for test_file in test_files:
            result = self.run_single_test(test_file)
            summary.results.append(result)
            summary.total_tests += 1
            
            if result.passed:
                summary.passed_tests += 1
            elif result.error_message:
                summary.error_tests += 1
            else:
                summary.failed_tests += 1
        
        summary.total_time = time.time() - start_time
        
        print(f"  📊 Category results: {summary.passed_tests}/{summary.total_tests} passed ({summary.pass_rate:.1f}%)")
        print()
        
        return summary
    
    def run_all_tests(self) -> TestSummary:
        """Run all available tests"""
        print("🎨 Radiant Layout Validation Suite")
        print("===================================\n")
        
        # Check if Radiant executable exists
        if not Path(self.radiant_exe).exists():
            print(f"❌ Error: Radiant executable not found: {self.radiant_exe}")
            print("Please build Radiant first with: make build-radiant")
            sys.exit(1)
        
        categories = ["basic", "intermediate", "advanced"]
        overall_summary = TestSummary()
        
        for category in categories:
            if (self.data_dir / category).exists():
                category_summary = self.run_category(category)
                
                # Merge results
                overall_summary.total_tests += category_summary.total_tests
                overall_summary.passed_tests += category_summary.passed_tests
                overall_summary.failed_tests += category_summary.failed_tests
                overall_summary.error_tests += category_summary.error_tests
                overall_summary.total_time += category_summary.total_time
                overall_summary.results.extend(category_summary.results)
        
        self.print_summary(overall_summary)
        self.save_report(overall_summary)
        
        return overall_summary
    
    def print_summary(self, summary: TestSummary):
        """Print test results summary"""
        print("📊 Test Results Summary")
        print("=======================")
        print(f"Total tests: {summary.total_tests}")
        print(f"✅ Passed: {summary.passed_tests}")
        print(f"❌ Failed: {summary.failed_tests}")
        print(f"💥 Errors: {summary.error_tests}")
        print(f"⏱️  Total time: {summary.total_time:.2f}s")
        
        if summary.total_tests > 0:
            print(f"📈 Pass rate: {summary.pass_rate:.1f}%")
        
        # Show failed tests
        failed_results = [r for r in summary.results if not r.passed]
        if failed_results:
            print(f"\n❌ Failed Tests ({len(failed_results)}):")
            for result in failed_results[:10]:  # Show first 10 failures
                print(f"  • {result.test_name}: {result.error_message or 'Layout differences'}")
                if result.max_difference > 0:
                    print(f"    Max difference: {result.max_difference:.2f}px")
            
            if len(failed_results) > 10:
                print(f"  ... and {len(failed_results) - 10} more failures")
    
    def save_report(self, summary: TestSummary):
        """Save detailed test report"""
        timestamp = int(time.time())
        report_file = self.reports_dir / f"radiant_validation_{timestamp}.json"
        
        report_data = {
            "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
            "radiant_exe": self.radiant_exe,
            "tolerance": self.tolerance,
            "summary": asdict(summary)
        }
        
        with open(report_file, 'w') as f:
            json.dump(report_data, f, indent=2)
        
        print(f"📄 Detailed report saved: {report_file}")


def create_sample_test_files():
    """Create sample test HTML files for demonstration"""
    basic_dir = Path("test/layout/data/basic")
    basic_dir.mkdir(parents=True, exist_ok=True)
    
    # Basic flex test
    flex_test = """<!DOCTYPE html>
<html>
<head>
    <style>
        .container {
            display: flex;
            flex-direction: row;
            justify-content: space-between;
            width: 600px;
            height: 100px;
            background: #f0f0f0;
        }
        .item {
            width: 100px;
            height: 60px;
            background: #4CAF50;
            flex-shrink: 0;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="item"></div>
        <div class="item"></div>
        <div class="item"></div>
    </div>
</body>
</html>"""
    
    with open(basic_dir / "flex_001_row_space_between.html", 'w') as f:
        f.write(flex_test)
    
    # Basic block test
    block_test = """<!DOCTYPE html>
<html>
<head>
    <style>
        .container {
            width: 400px;
            height: 200px;
            padding: 20px;
            margin: 10px;
            background: #e3f2fd;
        }
        .block {
            width: 200px;
            height: 80px;
            margin: 10px 0;
            padding: 15px;
            background: #2196f3;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="block">Block 1</div>
        <div class="block">Block 2</div>
    </div>
</body>
</html>"""
    
    with open(basic_dir / "block_001_margin_padding.html", 'w') as f:
        f.write(block_test)
    
    print("✅ Sample test files created in test/layout/data/basic/")


def main():
    parser = argparse.ArgumentParser(description="Radiant Layout Engine Automated Testing")
    parser.add_argument("--category", choices=["basic", "intermediate", "advanced"],
                       help="Run tests for specific category only")
    parser.add_argument("--tolerance", type=float, default=2.0,
                       help="Tolerance in pixels for layout differences (default: 2.0)")
    parser.add_argument("--radiant-exe", default="./radiant.exe",
                       help="Path to Radiant executable (default: ./radiant.exe)")
    parser.add_argument("--create-samples", action="store_true",
                       help="Create sample test files")
    parser.add_argument("--verbose", "-v", action="store_true",
                       help="Enable verbose output")
    
    args = parser.parse_args()
    
    if args.create_samples:
        create_sample_test_files()
        return
    
    # Change to project root directory
    if Path("radiant").exists() and Path("test").exists():
        # We're already in the right directory
        pass
    elif Path("../radiant").exists():
        # We're in a subdirectory, go up one level
        os.chdir("..")
    elif Path("../../radiant").exists():
        # We're in test/layout/tools, go up three levels
        os.chdir("../../..")
    elif Path("../../../radiant").exists():
        # We're in a deeper subdirectory
        os.chdir("../../../..")
    else:
        print("❌ Error: Please run from the Lambda project root directory")
        print(f"Current directory: {Path.cwd()}")
        print("Expected to find 'radiant' and 'test' directories")
        sys.exit(1)
    
    tester = RadiantLayoutTester(
        radiant_exe=args.radiant_exe,
        tolerance=args.tolerance
    )
    
    if args.category:
        summary = tester.run_category(args.category)
    else:
        summary = tester.run_all_tests()
    
    # Exit with error code if tests failed
    if summary.failed_tests > 0 or summary.error_tests > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
