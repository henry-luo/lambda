#!/usr/bin/env python3
"""
Phase 5: Enhanced Structured Test Reporting
Advanced reporting with quality metrics, regression tracking, and detailed analysis.
"""

import os
import sys
import json
import time
import csv
from pathlib import Path
from typing import List, Dict, Any, Optional
from datetime import datetime, timedelta
import difflib
import hashlib

class AdvancedReportGenerator:
    """Enhanced report generator with quality metrics and comparison tracking."""
    
    def __init__(self, base_dir: str = "../../test_output/auto"):
        self.base_dir = Path(base_dir)
        self.reports_dir = self.base_dir / "reports"
        self.history_dir = self.base_dir / "history"
        
        # Create directories
        self.reports_dir.mkdir(parents=True, exist_ok=True)
        self.history_dir.mkdir(parents=True, exist_ok=True)
        
        # Load historical data
        self.load_historical_data()
    
    def load_historical_data(self) -> None:
        """Load historical test results for comparison."""
        self.historical_results = []
        
        # Load previous results
        for history_file in sorted(self.history_dir.glob("test_results_*.json")):
            try:
                with open(history_file, 'r', encoding='utf-8') as f:
                    data = json.load(f)
                    self.historical_results.append({
                        "timestamp": data.get("timestamp", ""),
                        "file": history_file.name,
                        "data": data
                    })
            except Exception:
                continue
        
        print(f"📈 Loaded {len(self.historical_results)} historical test runs")
    
    def save_current_results(self, results_data: Dict[str, Any]) -> None:
        """Save current results to history."""
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        history_file = self.history_dir / f"test_results_{timestamp}.json"
        
        with open(history_file, 'w', encoding='utf-8') as f:
            json.dump(results_data, f, indent=2)
        
        print(f"💾 Saved current results to {history_file}")
    
    def calculate_quality_metrics(self, test_results: List[Dict[str, Any]]) -> Dict[str, Any]:
        """Calculate comprehensive quality metrics."""
        if not test_results:
            return {}
        
        metrics = {
            "total_documents": len(test_results),
            "format_breakdown": {},
            "quality_distribution": {"excellent": 0, "good": 0, "fair": 0, "poor": 0},
            "conversion_matrix": {},
            "roundtrip_quality": {},
            "performance_metrics": {
                "average_test_time": 0,
                "fastest_test": float('inf'),
                "slowest_test": 0,
                "total_test_time": 0
            },
            "error_analysis": {
                "total_errors": 0,
                "error_categories": {},
                "most_common_errors": []
            }
        }
        
        total_quality = 0
        total_time = 0
        error_messages = []
        
        # Process each test result
        for result in test_results:
            doc_format = result.get("format", "unknown")
            quality_score = result.get("quality_score", 0)
            test_time = result.get("total_test_time", 0)
            
            # Format breakdown
            if doc_format not in metrics["format_breakdown"]:
                metrics["format_breakdown"][doc_format] = {
                    "count": 0,
                    "average_quality": 0,
                    "successful_tests": 0,
                    "total_conversions": 0,
                    "successful_conversions": 0,
                    "roundtrip_success_rate": 0
                }
            
            fmt_metrics = metrics["format_breakdown"][doc_format]
            fmt_metrics["count"] += 1
            fmt_metrics["average_quality"] += quality_score
            
            # Quality distribution
            if quality_score >= 90:
                metrics["quality_distribution"]["excellent"] += 1
            elif quality_score >= 75:
                metrics["quality_distribution"]["good"] += 1
            elif quality_score >= 50:
                metrics["quality_distribution"]["fair"] += 1
            else:
                metrics["quality_distribution"]["poor"] += 1
            
            # Conversion matrix
            conv_results = result.get("conversion_results", {})
            if "conversion_tests" in conv_results:
                for conv in conv_results["conversion_tests"]:
                    from_fmt = conv.get("from_format", "")
                    to_fmt = conv.get("to_format", "")
                    success = conv.get("success", False)
                    
                    if from_fmt not in metrics["conversion_matrix"]:
                        metrics["conversion_matrix"][from_fmt] = {}
                    if to_fmt not in metrics["conversion_matrix"][from_fmt]:
                        metrics["conversion_matrix"][from_fmt][to_fmt] = {"attempts": 0, "successes": 0}
                    
                    metrics["conversion_matrix"][from_fmt][to_fmt]["attempts"] += 1
                    if success:
                        metrics["conversion_matrix"][from_fmt][to_fmt]["successes"] += 1
                        fmt_metrics["successful_conversions"] += 1
                    else:
                        error_messages.append(conv.get("error_message", "Unknown error"))
                    
                    fmt_metrics["total_conversions"] += 1
            
            # Roundtrip quality
            round_results = result.get("roundtrip_results", {})
            if round_results.get("total_tests", 0) > 0:
                success_rate = round_results["successful_tests"] / round_results["total_tests"]
                similarity = round_results.get("average_similarity", 0)
                
                if doc_format not in metrics["roundtrip_quality"]:
                    metrics["roundtrip_quality"][doc_format] = {
                        "total_tests": 0,
                        "success_rate": 0,
                        "average_similarity": 0,
                        "identical_count": 0
                    }
                
                rq = metrics["roundtrip_quality"][doc_format]
                rq["total_tests"] += round_results["total_tests"]
                rq["success_rate"] += success_rate
                rq["average_similarity"] += similarity
                rq["identical_count"] += round_results.get("identical_roundtrips", 0)
                
                fmt_metrics["roundtrip_success_rate"] += success_rate
            
            # Performance metrics
            total_quality += quality_score
            total_time += test_time
            metrics["performance_metrics"]["fastest_test"] = min(
                metrics["performance_metrics"]["fastest_test"], test_time
            )
            metrics["performance_metrics"]["slowest_test"] = max(
                metrics["performance_metrics"]["slowest_test"], test_time
            )
            
            # Check for overall success
            if result.get("overall_success", False):
                fmt_metrics["successful_tests"] += 1
        
        # Finalize calculations
        for fmt_name, fmt_data in metrics["format_breakdown"].items():
            if fmt_data["count"] > 0:
                fmt_data["average_quality"] /= fmt_data["count"]
                if fmt_name in metrics["roundtrip_quality"]:
                    rq = metrics["roundtrip_quality"][fmt_name]
                    rq["success_rate"] /= fmt_data["count"]
                    rq["average_similarity"] /= fmt_data["count"]
                if fmt_data["count"] > 0:
                    fmt_data["roundtrip_success_rate"] /= fmt_data["count"]
        
        metrics["overall_quality_score"] = total_quality / len(test_results)
        metrics["performance_metrics"]["average_test_time"] = total_time / len(test_results)
        metrics["performance_metrics"]["total_test_time"] = total_time
        
        if metrics["performance_metrics"]["fastest_test"] == float('inf'):
            metrics["performance_metrics"]["fastest_test"] = 0
        
        # Error analysis
        metrics["error_analysis"]["total_errors"] = len(error_messages)
        error_counts = {}
        for error in error_messages:
            if error:
                error_counts[error] = error_counts.get(error, 0) + 1
        
        metrics["error_analysis"]["most_common_errors"] = [
            {"error": error, "count": count} 
            for error, count in sorted(error_counts.items(), key=lambda x: x[1], reverse=True)[:10]
        ]
        
        return metrics
    
    def compare_with_history(self, current_metrics: Dict[str, Any]) -> Dict[str, Any]:
        """Compare current results with historical data."""
        if not self.historical_results:
            return {"message": "No historical data available for comparison"}
        
        # Get most recent historical result
        latest_historical = self.historical_results[-1]["data"]
        if "summary" not in latest_historical:
            return {"message": "Historical data format incompatible"}
        
        prev_metrics = latest_historical.get("summary", {})
        
        comparison = {
            "comparison_date": self.historical_results[-1]["timestamp"],
            "changes": {},
            "improvements": [],
            "regressions": [],
            "new_issues": [],
            "resolved_issues": []
        }
        
        # Compare key metrics
        key_metrics = [
            ("overall_quality_score", "Overall Quality"),
            ("total_documents", "Documents Tested"),
            ("performance_metrics.average_test_time", "Average Test Time"),
        ]
        
        for metric_path, display_name in key_metrics:
            current_val = self._get_nested_value(current_metrics, metric_path)
            prev_val = self._get_nested_value(prev_metrics, metric_path)
            
            if current_val is not None and prev_val is not None:
                diff = current_val - prev_val
                percent_change = (diff / prev_val * 100) if prev_val != 0 else 0
                
                comparison["changes"][display_name] = {
                    "current": current_val,
                    "previous": prev_val,
                    "difference": diff,
                    "percent_change": percent_change
                }
                
                if display_name == "Average Test Time" and diff < -0.1:
                    comparison["improvements"].append(f"Test execution faster by {abs(diff):.2f}s ({abs(percent_change):.1f}%)")
                elif display_name != "Average Test Time" and percent_change > 5:
                    comparison["improvements"].append(f"{display_name} improved by {percent_change:.1f}%")
                elif display_name != "Average Test Time" and percent_change < -5:
                    comparison["regressions"].append(f"{display_name} decreased by {abs(percent_change):.1f}%")
        
        return comparison
    
    def _get_nested_value(self, data: Dict, path: str) -> Any:
        """Get nested dictionary value using dot notation."""
        keys = path.split('.')
        current = data
        for key in keys:
            if isinstance(current, dict) and key in current:
                current = current[key]
            else:
                return None
        return current
    
    def generate_executive_summary(self, metrics: Dict[str, Any], comparison: Dict[str, Any]) -> str:
        """Generate executive summary for management reporting."""
        quality_score = metrics.get("overall_quality_score", 0)
        total_docs = metrics.get("total_documents", 0)
        
        # Quality assessment
        if quality_score >= 85:
            quality_status = "🟢 Excellent"
        elif quality_score >= 70:
            quality_status = "🟡 Good"
        elif quality_score >= 50:
            quality_status = "🟠 Fair"
        else:
            quality_status = "🔴 Needs Attention"
        
        summary = f"""# Executive Summary: Lambda Engine Quality Report
        
## Overall Status: {quality_status}

**Quality Score:** {quality_score:.1f}/100  
**Documents Tested:** {total_docs}  
**Test Duration:** {metrics.get('performance_metrics', {}).get('total_test_time', 0):.1f} seconds

## Key Findings

### Quality Distribution
"""
        
        qual_dist = metrics.get("quality_distribution", {})
        for level, count in qual_dist.items():
            percentage = (count / total_docs * 100) if total_docs > 0 else 0
            summary += f"- **{level.title()}:** {count} documents ({percentage:.1f}%)\n"
        
        summary += "\n### Format Performance\n"
        
        format_breakdown = metrics.get("format_breakdown", {})
        for fmt, data in sorted(format_breakdown.items(), key=lambda x: x[1]["average_quality"], reverse=True):
            success_rate = (data["successful_tests"] / data["count"] * 100) if data["count"] > 0 else 0
            summary += f"- **{fmt.upper()}:** {data['average_quality']:.1f}% quality, {success_rate:.0f}% success rate\n"
        
        # Add comparison if available
        if "changes" in comparison:
            summary += "\n### Trend Analysis\n"
            
            for metric, change_data in comparison["changes"].items():
                if change_data["percent_change"] != 0:
                    direction = "↗️" if change_data["percent_change"] > 0 else "↘️"
                    summary += f"- **{metric}:** {direction} {abs(change_data['percent_change']):.1f}% change\n"
        
        # Add recommendations
        summary += "\n### Recommendations\n"
        
        if quality_score < 70:
            summary += "- 🔧 **Priority:** Focus on improving low-performing formats\n"
        
        error_analysis = metrics.get("error_analysis", {})
        if error_analysis.get("total_errors", 0) > 0:
            summary += f"- 🐛 **Debug:** {error_analysis['total_errors']} errors need investigation\n"
        
        if comparison.get("regressions"):
            summary += "- ⚠️ **Alert:** Performance regressions detected\n"
        
        return summary
    
    def generate_comprehensive_html_report(self, test_results: List[Dict], metrics: Dict, comparison: Dict) -> str:
        """Generate comprehensive HTML report with charts and detailed analysis."""
        
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        
        html = f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Lambda Engine Advanced Test Report</title>
    <style>
        body {{ font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', system-ui, sans-serif; margin: 0; padding: 20px; background: #f5f5f7; }}
        .container {{ max-width: 1200px; margin: 0 auto; background: white; border-radius: 12px; box-shadow: 0 4px 20px rgba(0,0,0,0.1); overflow: hidden; }}
        .header {{ background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; padding: 30px; text-align: center; }}
        .header h1 {{ margin: 0; font-size: 2.5em; font-weight: 300; }}
        .header p {{ margin: 10px 0 0; opacity: 0.9; }}
        .content {{ padding: 30px; }}
        .metrics-grid {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 20px; margin-bottom: 30px; }}
        .metric-card {{ background: #f8f9ff; border: 1px solid #e1e5e9; border-radius: 8px; padding: 20px; text-align: center; }}
        .metric-value {{ font-size: 2em; font-weight: bold; color: #333; }}
        .metric-label {{ color: #666; margin-top: 5px; }}
        .quality-excellent {{ color: #28a745; }}
        .quality-good {{ color: #ffc107; }}
        .quality-fair {{ color: #fd7e14; }}
        .quality-poor {{ color: #dc3545; }}
        .section {{ margin-bottom: 30px; }}
        .section h2 {{ border-bottom: 2px solid #667eea; padding-bottom: 10px; color: #333; }}
        .format-table {{ width: 100%; border-collapse: collapse; margin: 20px 0; }}
        .format-table th, .format-table td {{ border: 1px solid #ddd; padding: 12px; text-align: left; }}
        .format-table th {{ background: #f8f9ff; font-weight: 600; }}
        .status-pass {{ color: #28a745; font-weight: bold; }}
        .status-fail {{ color: #dc3545; font-weight: bold; }}
        .progress-bar {{ width: 100%; height: 8px; background: #e9ecef; border-radius: 4px; overflow: hidden; }}
        .progress-fill {{ height: 100%; background: linear-gradient(90deg, #28a745, #20c997); transition: width 0.3s ease; }}
        .comparison-section {{ background: #f8f9fa; border-radius: 8px; padding: 20px; margin: 20px 0; }}
        .improvement {{ color: #28a745; }}
        .regression {{ color: #dc3545; }}
        .error-list {{ background: #fff3cd; border: 1px solid #ffeaa7; border-radius: 6px; padding: 15px; }}
        .chart-placeholder {{ background: #f8f9ff; border: 2px dashed #667eea; border-radius: 8px; padding: 40px; text-align: center; color: #666; margin: 20px 0; }}
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>Lambda Engine Advanced Test Report</h1>
            <p>Generated on {timestamp}</p>
        </div>
        
        <div class="content">
            <!-- Key Metrics -->
            <div class="section">
                <h2>📊 Key Metrics</h2>
                <div class="metrics-grid">"""
        
        # Add key metrics cards
        quality_score = metrics.get("overall_quality_score", 0)
        quality_class = "excellent" if quality_score >= 85 else "good" if quality_score >= 70 else "fair" if quality_score >= 50 else "poor"
        
        html += f"""
                    <div class="metric-card">
                        <div class="metric-value quality-{quality_class}">{quality_score:.1f}%</div>
                        <div class="metric-label">Overall Quality Score</div>
                    </div>
                    <div class="metric-card">
                        <div class="metric-value">{metrics.get('total_documents', 0)}</div>
                        <div class="metric-label">Documents Tested</div>
                    </div>
                    <div class="metric-card">
                        <div class="metric-value">{metrics.get('performance_metrics', {}).get('total_test_time', 0):.1f}s</div>
                        <div class="metric-label">Total Test Time</div>
                    </div>
                    <div class="metric-card">
                        <div class="metric-value">{len(metrics.get('format_breakdown', {}))}</div>
                        <div class="metric-label">Formats Tested</div>
                    </div>
                </div>
            </div>"""
        
        # Quality Distribution
        html += """
            <div class="section">
                <h2>🎯 Quality Distribution</h2>
                <div class="chart-placeholder">
                    Quality distribution chart would be rendered here with a JavaScript charting library
                </div>
            </div>"""
        
        # Format Performance Table
        html += """
            <div class="section">
                <h2>📄 Format Performance</h2>
                <table class="format-table">
                    <thead>
                        <tr>
                            <th>Format</th>
                            <th>Documents</th>
                            <th>Quality Score</th>
                            <th>Success Rate</th>
                            <th>Conversions</th>
                            <th>Roundtrip Success</th>
                        </tr>
                    </thead>
                    <tbody>"""
        
        for fmt, data in sorted(metrics.get("format_breakdown", {}).items(), key=lambda x: x[1]["average_quality"], reverse=True):
            success_rate = (data["successful_tests"] / data["count"] * 100) if data["count"] > 0 else 0
            conv_rate = (data["successful_conversions"] / data["total_conversions"] * 100) if data["total_conversions"] > 0 else 0
            roundtrip_rate = data["roundtrip_success_rate"] * 100
            
            quality_class = "excellent" if data["average_quality"] >= 85 else "good" if data["average_quality"] >= 70 else "fair" if data["average_quality"] >= 50 else "poor"
            
            html += f"""
                        <tr>
                            <td><strong>{fmt.upper()}</strong></td>
                            <td>{data['count']}</td>
                            <td class="quality-{quality_class}">{data['average_quality']:.1f}%</td>
                            <td>{success_rate:.0f}%</td>
                            <td>{data['successful_conversions']}/{data['total_conversions']} ({conv_rate:.0f}%)</td>
                            <td>{roundtrip_rate:.0f}%</td>
                        </tr>"""
        
        html += """
                    </tbody>
                </table>
            </div>"""
        
        # Comparison with History
        if "changes" in comparison and comparison["changes"]:
            html += """
            <div class="section">
                <h2>📈 Trend Analysis</h2>
                <div class="comparison-section">"""
            
            for metric, change_data in comparison["changes"].items():
                direction_class = "improvement" if change_data["percent_change"] > 0 else "regression"
                direction_symbol = "↗️" if change_data["percent_change"] > 0 else "↘️"
                
                html += f"""
                    <p><strong>{metric}:</strong> 
                    <span class="{direction_class}">{direction_symbol} {abs(change_data['percent_change']):.1f}% change</span>
                    (from {change_data['previous']:.1f} to {change_data['current']:.1f})</p>"""
            
            html += "</div></div>"
        
        # Error Analysis
        error_analysis = metrics.get("error_analysis", {})
        if error_analysis.get("most_common_errors"):
            html += """
            <div class="section">
                <h2>🐛 Error Analysis</h2>
                <div class="error-list">
                    <h4>Most Common Errors:</h4>
                    <ul>"""
            
            for error_info in error_analysis["most_common_errors"][:5]:
                html += f'<li><strong>{error_info["count"]}x:</strong> {error_info["error"]}</li>'
            
            html += """
                    </ul>
                </div>
            </div>"""
        
        # Footer
        html += f"""
        </div>
    </div>
    
    <script>
        // JavaScript for interactive features would go here
        console.log('Lambda Test Report Generated: {timestamp}');
    </script>
</body>
</html>"""
        
        return html
    
    def generate_all_reports(self, phase3_results_file: Optional[str] = None, basic_results_file: Optional[str] = None) -> Dict[str, str]:
        """Generate all report formats from test results."""
        
        # Load test results
        test_results = []
        
        # Try to load Phase 3 comprehensive results first
        if phase3_results_file and Path(phase3_results_file).exists():
            with open(phase3_results_file, 'r', encoding='utf-8') as f:
                phase3_data = json.load(f)
                test_results = phase3_data.get("test_results", [])
                print(f"📊 Loaded {len(test_results)} Phase 3 test results")
        
        # Fallback to basic results if no Phase 3 data
        elif basic_results_file and Path(basic_results_file).exists():
            with open(basic_results_file, 'r', encoding='utf-8') as f:
                basic_data = json.load(f)
                test_results = basic_data.get("test_results", [])
                print(f"📊 Loaded {len(test_results)} basic test results")
        
        else:
            print("❌ No test results found to generate reports")
            return {}
        
        if not test_results:
            print("❌ No valid test results to process")
            return {}
        
        # Calculate metrics
        print("📊 Calculating quality metrics...")
        metrics = self.calculate_quality_metrics(test_results)
        
        # Compare with history
        print("📈 Comparing with historical data...")
        comparison = self.compare_with_history(metrics)
        
        # Save current results to history
        current_data = {
            "timestamp": datetime.now().isoformat(),
            "test_results": test_results,
            "summary": metrics
        }
        self.save_current_results(current_data)
        
        # Generate reports
        reports = {}
        
        # Executive Summary (Markdown)
        print("📝 Generating executive summary...")
        reports["executive_summary"] = self.generate_executive_summary(metrics, comparison)
        
        # Comprehensive HTML Report
        print("🌐 Generating comprehensive HTML report...")
        reports["html_report"] = self.generate_comprehensive_html_report(test_results, metrics, comparison)
        
        # Quality Metrics JSON
        print("📋 Generating quality metrics JSON...")
        reports["quality_metrics"] = json.dumps({
            "timestamp": datetime.now().isoformat(),
            "metrics": metrics,
            "comparison": comparison
        }, indent=2)
        
        # CSV Summary
        print("📊 Generating CSV summary...")
        reports["csv_summary"] = self.generate_csv_summary(test_results, metrics)
        
        return reports
    
    def generate_csv_summary(self, test_results: List[Dict], metrics: Dict) -> str:
        """Generate CSV summary for data analysis."""
        import io
        
        output = io.StringIO()
        writer = csv.writer(output)
        
        # Write header
        writer.writerow([
            "Document", "Format", "Quality_Score", "Test_Time", 
            "Detection_Success", "Total_Conversions", "Successful_Conversions",
            "Roundtrip_Tests", "Successful_Roundtrips", "Average_Similarity",
            "File_Size", "Overall_Success"
        ])
        
        # Write data
        for result in test_results:
            conv_results = result.get("conversion_results", {})
            round_results = result.get("roundtrip_results", {})
            
            writer.writerow([
                Path(result.get("document", "")).name,
                result.get("format", ""),
                result.get("quality_score", 0),
                result.get("total_test_time", 0),
                result.get("detection_test", {}).get("success", False),
                conv_results.get("total_conversions", 0),
                conv_results.get("successful_conversions", 0),
                round_results.get("total_tests", 0),
                round_results.get("successful_tests", 0),
                round_results.get("average_similarity", 0),
                result.get("file_size", 0),
                result.get("overall_success", False)
            ])
        
        return output.getvalue()


def main():
    """Generate Phase 5 enhanced reports."""
    print("🚀 Starting Phase 5: Enhanced Structured Test Reporting")
    
    generator = AdvancedReportGenerator()
    
    # Look for available results files
    base_dir = Path("../../test_output/auto")
    phase3_file = base_dir / "phase3_comprehensive_results.json"
    basic_file = base_dir / "test_results.json"
    
    # Generate all reports
    reports = generator.generate_all_reports(
        phase3_results_file=str(phase3_file) if phase3_file.exists() else None,
        basic_results_file=str(basic_file) if basic_file.exists() else None
    )
    
    if not reports:
        print("❌ Failed to generate reports - no valid test data found")
        return 1
    
    # Save reports to files
    reports_dir = generator.reports_dir
    
    # Executive Summary
    if "executive_summary" in reports:
        exec_file = reports_dir / "executive_summary.md"
        with open(exec_file, 'w', encoding='utf-8') as f:
            f.write(reports["executive_summary"])
        print(f"📝 Executive summary saved to {exec_file}")
    
    # HTML Report
    if "html_report" in reports:
        html_file = reports_dir / "comprehensive_report.html"
        with open(html_file, 'w', encoding='utf-8') as f:
            f.write(reports["html_report"])
        print(f"🌐 Comprehensive HTML report saved to {html_file}")
    
    # Quality Metrics JSON
    if "quality_metrics" in reports:
        metrics_file = reports_dir / "quality_metrics.json"
        with open(metrics_file, 'w', encoding='utf-8') as f:
            f.write(reports["quality_metrics"])
        print(f"📊 Quality metrics saved to {metrics_file}")
    
    # CSV Summary
    if "csv_summary" in reports:
        csv_file = reports_dir / "test_summary.csv"
        with open(csv_file, 'w', encoding='utf-8') as f:
            f.write(reports["csv_summary"])
        print(f"📋 CSV summary saved to {csv_file}")
    
    print(f"\n✅ Phase 5 enhanced reporting complete!")
    print(f"📁 All reports saved to: {reports_dir}")
    print(f"🌐 Open the HTML report for interactive analysis: {reports_dir}/comprehensive_report.html")
    
    return 0


if __name__ == "__main__":
    sys.exit(main())
