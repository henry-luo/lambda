#!/usr/bin/env python3
"""
Lambda Script Test Report Generator
Phase 3: Structured Test Reporting and Analysis

Generates comprehensive HTML and markdown reports from the automated test results.
"""

import json
import csv
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Any
import os

# Configuration
SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent
TEST_OUTPUT_DIR = PROJECT_ROOT / "test_output" / "auto"
DOC_LIST_CSV = PROJECT_ROOT / "test" / "auto" / "doc_list.csv"
RESULTS_JSON = TEST_OUTPUT_DIR / "test_results.json"

class TestReportGenerator:
    """Generates comprehensive test reports from automation results"""
    
    def __init__(self, results_file: Path, doc_list_file: Path):
        self.results_file = Path(results_file)
        self.doc_list_file = Path(doc_list_file)
        self.results_data = None
        self.doc_data = []
        self.load_data()
    
    def load_data(self):
        """Load test results and document data"""
        # Load test results JSON
        if self.results_file.exists():
            with open(self.results_file, 'r') as f:
                self.results_data = json.load(f)
        else:
            raise FileNotFoundError(f"Results file not found: {self.results_file}")
        
        # Load document CSV
        if self.doc_list_file.exists():
            with open(self.doc_list_file, 'r', newline='', encoding='utf-8') as f:
                reader = csv.DictReader(f)
                for row in reader:
                    if not row['url'].startswith('#'):  # Skip comments
                        self.doc_data.append(row)
    
    def generate_summary_stats(self) -> Dict[str, Any]:
        """Generate summary statistics"""
        stats = self.results_data.get('statistics', {})
        
        # Calculate additional metrics
        total_tests = len(self.results_data.get('test_results', []))
        successful_conversions = 0
        failed_conversions = 0
        total_execution_time = 0
        
        for test_result in self.results_data.get('test_results', []):
            total_execution_time += test_result.get('total_execution_time', 0)
            
            for conv_test in test_result.get('conversion_tests', []):
                if conv_test.get('success'):
                    successful_conversions += 1
                else:
                    failed_conversions += 1
        
        # Format by format breakdown
        format_stats = {}
        for doc in self.doc_data:
            fmt = doc['format']
            if fmt not in format_stats:
                format_stats[fmt] = {
                    'total': 0, 'downloaded': 0, 'tested': 0, 
                    'passed': 0, 'failed': 0
                }
            format_stats[fmt]['total'] += 1
            
            status = doc.get('test_status', 'pending')
            if status in ['downloaded', 'passed', 'failed']:
                format_stats[fmt]['downloaded'] += 1
            if status in ['passed', 'failed']:
                format_stats[fmt]['tested'] += 1
            if status == 'passed':
                format_stats[fmt]['passed'] += 1
            elif status == 'failed':
                format_stats[fmt]['failed'] += 1
        
        return {
            'overview': stats,
            'conversions': {
                'successful': successful_conversions,
                'failed': failed_conversions,
                'total': successful_conversions + failed_conversions
            },
            'performance': {
                'total_execution_time': total_execution_time,
                'average_test_time': total_execution_time / max(total_tests, 1)
            },
            'by_format': format_stats
        }
    
    def generate_html_report(self, output_file: Path):
        """Generate comprehensive HTML report"""
        stats = self.generate_summary_stats()
        timestamp = self.results_data.get('timestamp', datetime.now().isoformat())
        
        html_content = f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Lambda Script Automated Test Report</title>
    <style>
        body {{ font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 40px; background: #f5f5f5; }}
        .container {{ max-width: 1200px; margin: 0 auto; background: white; padding: 40px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }}
        h1 {{ color: #2c3e50; margin-bottom: 30px; }}
        h2 {{ color: #34495e; border-bottom: 2px solid #3498db; padding-bottom: 10px; }}
        .stats-grid {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 20px; margin: 30px 0; }}
        .stat-card {{ background: #f8f9fa; padding: 20px; border-radius: 6px; text-align: center; border-left: 4px solid #3498db; }}
        .stat-number {{ font-size: 2em; font-weight: bold; color: #2c3e50; }}
        .stat-label {{ color: #7f8c8d; margin-top: 5px; }}
        .success {{ color: #27ae60; }}
        .failure {{ color: #e74c3c; }}
        .warning {{ color: #f39c12; }}
        table {{ width: 100%; border-collapse: collapse; margin: 20px 0; }}
        th, td {{ padding: 12px; text-align: left; border-bottom: 1px solid #ddd; }}
        th {{ background-color: #f8f9fa; font-weight: 600; }}
        tr:hover {{ background-color: #f5f5f5; }}
        .status-badge {{ padding: 4px 8px; border-radius: 4px; font-size: 0.8em; font-weight: bold; }}
        .status-passed {{ background: #d4edda; color: #155724; }}
        .status-failed {{ background: #f8d7da; color: #721c24; }}
        .status-pending {{ background: #fff3cd; color: #856404; }}
        .status-download-failed {{ background: #f8d7da; color: #721c24; }}
        .conversion-matrix {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 20px; }}
        .conversion-card {{ background: #f8f9fa; padding: 15px; border-radius: 6px; }}
        .progress-bar {{ width: 100%; height: 20px; background: #ecf0f1; border-radius: 10px; overflow: hidden; }}
        .progress-fill {{ height: 100%; background: linear-gradient(90deg, #3498db, #2ecc71); transition: width 0.3s ease; }}
    </style>
</head>
<body>
    <div class="container">
        <h1>🔬 Lambda Script Automated Test Report</h1>
        <p><strong>Generated:</strong> {datetime.fromisoformat(timestamp.replace('T', ' ').replace('Z', '')).strftime('%Y-%m-%d %H:%M:%S')}</p>
        
        <h2>📊 Overview Statistics</h2>
        <div class="stats-grid">
            <div class="stat-card">
                <div class="stat-number">{stats['overview']['total_documents']}</div>
                <div class="stat-label">Total Documents</div>
            </div>
            <div class="stat-card">
                <div class="stat-number success">{stats['overview']['downloaded']}</div>
                <div class="stat-label">Successfully Downloaded</div>
            </div>
            <div class="stat-card">
                <div class="stat-number">{stats['overview']['tested']}</div>
                <div class="stat-label">Documents Tested</div>
            </div>
            <div class="stat-card">
                <div class="stat-number success">{stats['overview']['test_passed']}</div>
                <div class="stat-label">Tests Passed</div>
            </div>
            <div class="stat-card">
                <div class="stat-number failure">{stats['overview']['test_failed']}</div>
                <div class="stat-label">Tests Failed</div>
            </div>
            <div class="stat-card">
                <div class="stat-number">{stats['conversions']['total']}</div>
                <div class="stat-label">Total Conversions</div>
            </div>
        </div>
        
        <h2>🔄 Format Conversion Results</h2>
        <div class="stats-grid">
            <div class="stat-card">
                <div class="stat-number success">{stats['conversions']['successful']}</div>
                <div class="stat-label">Successful Conversions</div>
            </div>
            <div class="stat-card">
                <div class="stat-number failure">{stats['conversions']['failed']}</div>
                <div class="stat-label">Failed Conversions</div>
            </div>
            <div class="stat-card">
                <div class="stat-number">{stats['performance']['total_execution_time']:.2f}s</div>
                <div class="stat-label">Total Execution Time</div>
            </div>
            <div class="stat-card">
                <div class="stat-number">{stats['performance']['average_test_time']:.2f}s</div>
                <div class="stat-label">Average Test Time</div>
            </div>
        </div>
        
        <h2>📄 Format Breakdown</h2>
        <table>
            <thead>
                <tr>
                    <th>Format</th>
                    <th>Total Documents</th>
                    <th>Downloaded</th>
                    <th>Tested</th>
                    <th>Passed</th>
                    <th>Failed</th>
                    <th>Success Rate</th>
                </tr>
            </thead>
            <tbody>"""
        
        for fmt, data in stats['by_format'].items():
            success_rate = (data['passed'] / max(data['tested'], 1)) * 100 if data['tested'] > 0 else 0
            html_content += f"""
                <tr>
                    <td><strong>{fmt}</strong></td>
                    <td>{data['total']}</td>
                    <td>{data['downloaded']}</td>
                    <td>{data['tested']}</td>
                    <td class="success">{data['passed']}</td>
                    <td class="failure">{data['failed']}</td>
                    <td>
                        <div class="progress-bar">
                            <div class="progress-fill" style="width: {success_rate}%"></div>
                        </div>
                        {success_rate:.1f}%
                    </td>
                </tr>"""
        
        html_content += """
            </tbody>
        </table>
        
        <h2>📋 Document Test Results</h2>
        <table>
            <thead>
                <tr>
                    <th>Document</th>
                    <th>Format</th>
                    <th>Source</th>
                    <th>Status</th>
                    <th>File Size</th>
                    <th>Test Time</th>
                    <th>Conversions</th>
                    <th>Issues</th>
                </tr>
            </thead>
            <tbody>"""
        
        # Add document results
        test_results_by_file = {}
        for test_result in self.results_data.get('test_results', []):
            filename = Path(test_result['document']).name
            test_results_by_file[filename] = test_result
        
        for doc in self.doc_data:
            status = doc.get('test_status', 'pending')
            status_class = f"status-{status.replace('_', '-')}"
            
            filename = doc.get('local_filename', '')
            test_result = test_results_by_file.get(filename)
            
            test_time = ""
            conversions_info = "N/A"
            if test_result:
                test_time = f"{test_result.get('total_execution_time', 0):.2f}s"
                
                conv_tests = test_result.get('conversion_tests', [])
                if conv_tests:
                    successful = sum(1 for t in conv_tests if t.get('success'))
                    total = len(conv_tests)
                    conversions_info = f"{successful}/{total}"
            
            size_mb = int(doc.get('size_bytes', 0)) / 1024 / 1024
            size_display = f"{size_mb:.1f} MB" if size_mb >= 1 else f"{int(doc.get('size_bytes', 0)) / 1024:.1f} KB"
            
            html_content += f"""
                <tr>
                    <td><a href="{doc['url']}" target="_blank">{filename or 'N/A'}</a></td>
                    <td>{doc['format']}</td>
                    <td>{doc['source']}</td>
                    <td><span class="status-badge {status_class}">{status.replace('_', ' ').title()}</span></td>
                    <td>{size_display}</td>
                    <td>{test_time}</td>
                    <td>{conversions_info}</td>
                    <td>{doc.get('issues_count', 0)}</td>
                </tr>"""
        
        html_content += """
            </tbody>
        </table>
        
        <h2>🚀 Next Steps & Recommendations</h2>
        <div style="background: #e8f4f8; padding: 20px; border-radius: 6px; border-left: 4px solid #3498db;">
            <h3>Immediate Actions</h3>
            <ul>"""
        
        # Generate recommendations based on results
        if stats['overview']['download_failed'] > 0:
            html_content += f"<li><strong>Fix Download Issues:</strong> {stats['overview']['download_failed']} documents failed to download. Check URLs and network connectivity.</li>"
        
        if stats['overview']['test_failed'] > 0:
            html_content += f"<li><strong>Address Test Failures:</strong> {stats['overview']['test_failed']} tests failed. Review error logs for specific issues.</li>"
        
        if stats['conversions']['failed'] > 0:
            html_content += f"<li><strong>Fix Conversion Issues:</strong> {stats['conversions']['failed']} conversions failed. Focus on format-specific parsers.</li>"
        
        html_content += """
            </ul>
            
            <h3>Performance Optimization</h3>
            <ul>
                <li><strong>Parallel Processing:</strong> Implement parallel document processing to reduce total execution time.</li>
                <li><strong>Caching:</strong> Cache downloaded documents to avoid re-downloading during subsequent test runs.</li>
                <li><strong>Incremental Testing:</strong> Only test changed or new documents to speed up regression testing.</li>
            </ul>
            
            <h3>Coverage Expansion</h3>
            <ul>
                <li><strong>More Formats:</strong> Add support for additional document formats (RTF, ODT, etc.).</li>
                <li><strong>Edge Cases:</strong> Include malformed and edge-case documents for robustness testing.</li>
                <li><strong>Large Documents:</strong> Test with larger documents to identify scalability issues.</li>
            </ul>
        </div>
        
        <hr style="margin: 40px 0;">
        <p style="text-align: center; color: #7f8c8d;">
            Generated by Lambda Script Automated Testing System • 
            <a href="https://github.com/henry-luo/lambda">Lambda Script Engine</a>
        </p>
    </div>
</body>
</html>"""
        
        # Write HTML report
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(html_content)
        
        print(f"HTML report generated: {output_file}")
    
    def generate_markdown_report(self, output_file: Path):
        """Generate markdown report for documentation"""
        stats = self.generate_summary_stats()
        timestamp = self.results_data.get('timestamp', datetime.now().isoformat())
        
        md_content = f"""# Lambda Script Automated Test Report

**Generated:** {datetime.fromisoformat(timestamp.replace('T', ' ').replace('Z', '')).strftime('%Y-%m-%d %H:%M:%S')}

## Overview Statistics

| Metric | Value |
|--------|-------|
| Total Documents | {stats['overview']['total_documents']} |
| Successfully Downloaded | {stats['overview']['downloaded']} |
| Documents Tested | {stats['overview']['tested']} |
| Tests Passed | {stats['overview']['test_passed']} ✅ |
| Tests Failed | {stats['overview']['test_failed']} ❌ |
| Total Conversions | {stats['conversions']['total']} |
| Successful Conversions | {stats['conversions']['successful']} ✅ |
| Failed Conversions | {stats['conversions']['failed']} ❌ |

## Performance Metrics

- **Total Execution Time:** {stats['performance']['total_execution_time']:.2f} seconds
- **Average Test Time:** {stats['performance']['average_test_time']:.2f} seconds per document

## Format Breakdown

| Format | Total | Downloaded | Tested | Passed | Failed | Success Rate |
|--------|-------|------------|--------|--------|--------|--------------|"""
        
        for fmt, data in stats['by_format'].items():
            success_rate = (data['passed'] / max(data['tested'], 1)) * 100 if data['tested'] > 0 else 0
            md_content += f"""
| {fmt} | {data['total']} | {data['downloaded']} | {data['tested']} | {data['passed']} | {data['failed']} | {success_rate:.1f}% |"""
        
        md_content += f"""

## Document Test Results

| Document | Format | Status | Size | Test Time | Conversions | Issues |
|----------|--------|--------|------|-----------|-------------|--------|"""
        
        # Add document results
        test_results_by_file = {}
        for test_result in self.results_data.get('test_results', []):
            filename = Path(test_result['document']).name
            test_results_by_file[filename] = test_result
        
        for doc in self.doc_data:
            status = doc.get('test_status', 'pending')
            status_emoji = {'passed': '✅', 'failed': '❌', 'pending': '⏳', 'download_failed': '🚫'}.get(status, '❓')
            
            filename = doc.get('local_filename', 'N/A')
            test_result = test_results_by_file.get(filename)
            
            test_time = "N/A"
            conversions_info = "N/A"
            if test_result:
                test_time = f"{test_result.get('total_execution_time', 0):.2f}s"
                
                conv_tests = test_result.get('conversion_tests', [])
                if conv_tests:
                    successful = sum(1 for t in conv_tests if t.get('success'))
                    total = len(conv_tests)
                    conversions_info = f"{successful}/{total}"
            
            size_mb = int(doc.get('size_bytes', 0)) / 1024 / 1024
            size_display = f"{size_mb:.1f}MB" if size_mb >= 1 else f"{int(doc.get('size_bytes', 0)) / 1024:.1f}KB"
            
            md_content += f"""
| [{filename}]({doc['url']}) | {doc['format']} | {status_emoji} {status} | {size_display} | {test_time} | {conversions_info} | {doc.get('issues_count', 0)} |"""
        
        md_content += f"""

## Recommendations

### Immediate Actions

"""
        
        # Generate recommendations
        if stats['overview']['download_failed'] > 0:
            md_content += f"- **Fix Download Issues:** {stats['overview']['download_failed']} documents failed to download. Check URLs and network connectivity.\n"
        
        if stats['overview']['test_failed'] > 0:
            md_content += f"- **Address Test Failures:** {stats['overview']['test_failed']} tests failed. Review error logs for specific issues.\n"
        
        if stats['conversions']['failed'] > 0:
            md_content += f"- **Fix Conversion Issues:** {stats['conversions']['failed']} conversions failed. Focus on format-specific parsers.\n"
        
        md_content += """
### Performance Optimization

- **Parallel Processing:** Implement parallel document processing to reduce total execution time.
- **Caching:** Cache downloaded documents to avoid re-downloading during subsequent test runs.
- **Incremental Testing:** Only test changed or new documents to speed up regression testing.

### Coverage Expansion

- **More Formats:** Add support for additional document formats (RTF, ODT, etc.).
- **Edge Cases:** Include malformed and edge-case documents for robustness testing.
- **Large Documents:** Test with larger documents to identify scalability issues.

---

*Generated by Lambda Script Automated Testing System*
"""
        
        # Write markdown report
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(md_content)
        
        print(f"Markdown report generated: {output_file}")

def main():
    """Generate test reports"""
    print("=== Lambda Script Test Report Generator ===")
    
    if not RESULTS_JSON.exists():
        print(f"Error: Results file not found: {RESULTS_JSON}")
        print("Please run the automated tests first using: ./test/run_auto_tests.sh")
        return 1
    
    # Initialize report generator
    generator = TestReportGenerator(RESULTS_JSON, DOC_LIST_CSV)
    
    # Generate reports
    html_report = TEST_OUTPUT_DIR / "test_report.html"
    md_report = TEST_OUTPUT_DIR / "test_report.md"
    
    print("Generating reports...")
    generator.generate_html_report(html_report)
    generator.generate_markdown_report(md_report)
    
    print(f"\n✅ Reports generated successfully!")
    print(f"📊 HTML Report: {html_report}")
    print(f"📝 Markdown Report: {md_report}")
    print(f"\nOpen the HTML report in your browser to view the interactive report.")
    
    return 0

if __name__ == "__main__":
    exit_code = main()
    exit(exit_code)
