#!/usr/bin/env python3
"""
Phase 3: Lambda Convert Engine Integration with Roundtrip Testing
Implements advanced conversion testing including roundtrip quality checks.
"""

import os
import sys
import json
import time
import hashlib
import difflib
import subprocess
from pathlib import Path
from typing import List, Dict, Any, Optional, Tuple
import csv
from datetime import datetime

class RoundtripTester:
    """Enhanced test runner with roundtrip conversion testing."""
    
    def __init__(self, base_dir: str = "test_output/auto"):
        self.base_dir = Path(base_dir).resolve()
        self.lambda_exe = Path("../../lambda.exe").resolve()
        self.results_dir = self.base_dir / "roundtrip"
        self.comparison_dir = self.base_dir / "comparisons"
        
        # Create directories
        self.results_dir.mkdir(parents=True, exist_ok=True)
        self.comparison_dir.mkdir(parents=True, exist_ok=True)
        
        # Format pairs for roundtrip testing (symmetric conversions)
        self.roundtrip_pairs = [
            ("markdown", "html"),
            ("json", "yaml"),
            ("xml", "json"),
            ("html", "markdown"),
            ("latex", "json"),     # LaTeX roundtrip through JSON
            ("wiki", "json"),      # Wiki roundtrip through JSON
            ("yaml", "json"),      # YAML roundtrip through JSON
            ("ini", "json"),       # INI roundtrip through JSON
            ("toml", "json"),      # TOML roundtrip through JSON
            ("rst", "html"),       # RST roundtrip through HTML
            ("css", "json")        # CSS roundtrip through JSON
            # Add more symmetric pairs as Lambda supports them
        ]
        
        # Conversion targets for each format
        self.conversion_targets = {
            "markdown": ["html", "json", "xml", "text"],
            "json": ["yaml", "xml", "csv", "text"],
            "html": ["markdown", "json", "xml", "text"],
            "xml": ["json", "yaml", "text"],
            "text": ["json"],  # Simple text to structured format
            "latex": ["json", "html", "markdown", "text"],  # LaTeX to structured formats
            "wiki": ["json", "html", "markdown", "text"],   # Wiki markup to structured formats
            "yaml": ["json", "xml", "text"],                # YAML conversion targets
            "ini": ["json", "text"],                        # INI conversion targets
            "toml": ["json", "yaml", "text"],               # TOML conversion targets  
            "rst": ["html", "json", "text"],                # reStructuredText conversion targets
            "css": ["json", "text"]                         # CSS conversion targets
        }
    
    def run_lambda_convert(self, input_file: Path, from_format: str, to_format: str, output_file: Path) -> Tuple[bool, str, float]:
        """Run lambda convert command and return success, error message, and execution time."""
        start_time = time.time()
        
        try:
            cmd = [
                str(self.lambda_exe),
                "convert",
                str(input_file),
                "-f", from_format,
                "-t", to_format,
                "-o", str(output_file)
            ]
            
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=30  # 30 second timeout
            )
            
            execution_time = time.time() - start_time
            
            if result.returncode == 0:
                return True, "", execution_time
            else:
                error_msg = result.stderr.strip() or result.stdout.strip() or "Unknown error"
                return False, error_msg, execution_time
                
        except subprocess.TimeoutExpired:
            return False, "Conversion timeout (30s)", time.time() - start_time
        except Exception as e:
            return False, f"Execution error: {str(e)}", time.time() - start_time
    
    def calculate_file_hash(self, file_path: Path) -> str:
        """Calculate SHA256 hash of file content."""
        if not file_path.exists():
            return ""
        
        hasher = hashlib.sha256()
        with open(file_path, 'rb') as f:
            for chunk in iter(lambda: f.read(4096), b""):
                hasher.update(chunk)
        return hasher.hexdigest()[:16]  # First 16 chars for readability
    
    def compare_text_files(self, file1: Path, file2: Path) -> Dict[str, Any]:
        """Compare two text files and return similarity metrics."""
        try:
            with open(file1, 'r', encoding='utf-8', errors='ignore') as f1:
                content1 = f1.readlines()
            with open(file2, 'r', encoding='utf-8', errors='ignore') as f2:
                content2 = f2.readlines()
            
            # Calculate similarity using difflib
            matcher = difflib.SequenceMatcher(None, content1, content2)
            similarity_ratio = matcher.ratio()
            
            # Count differences
            diff_lines = list(difflib.unified_diff(content1, content2, lineterm=''))
            diff_count = len([line for line in diff_lines if line.startswith(('+', '-'))])
            
            return {
                "similarity_ratio": similarity_ratio,
                "total_lines_file1": len(content1),
                "total_lines_file2": len(content2),
                "diff_lines": diff_count,
                "size_difference": abs(len(content1) - len(content2)),
                "identical": similarity_ratio == 1.0
            }
            
        except Exception as e:
            return {
                "similarity_ratio": 0.0,
                "error": str(e),
                "identical": False
            }
    
    def test_roundtrip_conversion(self, input_file: Path, source_format: str) -> Dict[str, Any]:
        """Test roundtrip conversion: source -> target -> source."""
        roundtrip_results = []
        
        # Find roundtrip pairs for this format
        applicable_pairs = [
            (f1, f2) for f1, f2 in self.roundtrip_pairs 
            if f1 == source_format or f2 == source_format
        ]
        
        for format1, format2 in applicable_pairs:
            if source_format == format1:
                intermediate_format = format2
            else:
                intermediate_format = format1
            
            # Generate output filenames
            base_name = input_file.stem
            intermediate_file = self.results_dir / f"{base_name}_to_{intermediate_format}.{intermediate_format}"
            roundtrip_file = self.results_dir / f"{base_name}_roundtrip.{source_format}"
            
            # Step 1: Convert source -> intermediate
            success1, error1, time1 = self.run_lambda_convert(
                input_file, source_format, intermediate_format, intermediate_file
            )
            
            if not success1:
                roundtrip_results.append({
                    "source_format": source_format,
                    "intermediate_format": intermediate_format,
                    "success": False,
                    "stage_failed": "forward_conversion",
                    "error_message": error1,
                    "forward_time": time1,
                    "backward_time": 0,
                    "total_time": time1
                })
                continue
            
            # Step 2: Convert intermediate -> source
            success2, error2, time2 = self.run_lambda_convert(
                intermediate_file, intermediate_format, source_format, roundtrip_file
            )
            
            if not success2:
                roundtrip_results.append({
                    "source_format": source_format,
                    "intermediate_format": intermediate_format,
                    "success": False,
                    "stage_failed": "backward_conversion",
                    "error_message": error2,
                    "forward_time": time1,
                    "backward_time": time2,
                    "total_time": time1 + time2
                })
                continue
            
            # Step 3: Compare original vs roundtrip
            comparison = self.compare_text_files(input_file, roundtrip_file)
            
            # Generate diff file for manual inspection
            diff_file = self.comparison_dir / f"{base_name}_roundtrip_diff.txt"
            self.generate_diff_file(input_file, roundtrip_file, diff_file)
            
            roundtrip_results.append({
                "source_format": source_format,
                "intermediate_format": intermediate_format,
                "success": True,
                "stage_failed": None,
                "error_message": "",
                "forward_time": time1,
                "backward_time": time2,
                "total_time": time1 + time2,
                "intermediate_file": str(intermediate_file),
                "roundtrip_file": str(roundtrip_file),
                "diff_file": str(diff_file),
                "original_hash": self.calculate_file_hash(input_file),
                "roundtrip_hash": self.calculate_file_hash(roundtrip_file),
                "files_identical": comparison.get("identical", False),
                "similarity_ratio": comparison.get("similarity_ratio", 0.0),
                "diff_lines": comparison.get("diff_lines", 0),
                "size_difference": comparison.get("size_difference", 0)
            })
        
        return {
            "roundtrip_tests": roundtrip_results,
            "total_tests": len(roundtrip_results),
            "successful_tests": sum(1 for r in roundtrip_results if r["success"]),
            "identical_roundtrips": sum(1 for r in roundtrip_results if r.get("files_identical", False)),
            "average_similarity": sum(r.get("similarity_ratio", 0) for r in roundtrip_results) / max(len(roundtrip_results), 1)
        }
    
    def test_all_conversions(self, input_file: Path, source_format: str) -> Dict[str, Any]:
        """Test all possible conversions from source format."""
        conversion_results = []
        target_formats = self.conversion_targets.get(source_format, [])
        
        for target_format in target_formats:
            base_name = input_file.stem
            output_file = self.results_dir / f"{base_name}_to_{target_format}.{target_format}"
            
            success, error_msg, exec_time = self.run_lambda_convert(
                input_file, source_format, target_format, output_file
            )
            
            # Calculate file sizes
            input_size = input_file.stat().st_size if input_file.exists() else 0
            output_size = output_file.stat().st_size if output_file.exists() else 0
            
            conversion_results.append({
                "from_format": source_format,
                "to_format": target_format,
                "success": success,
                "error_message": error_msg,
                "execution_time": exec_time,
                "input_size": input_size,
                "output_size": output_size,
                "size_ratio": output_size / max(input_size, 1),
                "output_file": str(output_file) if success else "",
                "output_hash": self.calculate_file_hash(output_file) if success else ""
            })
        
        return {
            "conversion_tests": conversion_results,
            "total_conversions": len(conversion_results),
            "successful_conversions": sum(1 for r in conversion_results if r["success"]),
            "average_execution_time": sum(r["execution_time"] for r in conversion_results) / max(len(conversion_results), 1)
        }
    
    def generate_diff_file(self, file1: Path, file2: Path, diff_file: Path) -> None:
        """Generate a unified diff file for comparison."""
        try:
            with open(file1, 'r', encoding='utf-8', errors='ignore') as f1:
                content1 = f1.readlines()
            with open(file2, 'r', encoding='utf-8', errors='ignore') as f2:
                content2 = f2.readlines()
            
            diff_lines = list(difflib.unified_diff(
                content1, content2,
                fromfile=str(file1),
                tofile=str(file2),
                lineterm=''
            ))
            
            with open(diff_file, 'w', encoding='utf-8') as df:
                df.write('\n'.join(diff_lines))
                
        except Exception as e:
            with open(diff_file, 'w', encoding='utf-8') as df:
                df.write(f"Error generating diff: {str(e)}\n")
    
    def test_document_comprehensive(self, doc_path: Path, doc_format: str) -> Dict[str, Any]:
        """Run comprehensive testing on a document."""
        start_time = time.time()
        
        # Test 1: Format detection (basic parsing)
        detection_success, detection_error, detection_time = self.run_lambda_convert(
            doc_path, doc_format, "json", 
            self.results_dir / f"{doc_path.stem}_detection_test.json"
        )
        
        # Test 2: All format conversions
        conversion_results = self.test_all_conversions(doc_path, doc_format)
        
        # Test 3: Roundtrip testing
        roundtrip_results = self.test_roundtrip_conversion(doc_path, doc_format)
        
        total_time = time.time() - start_time
        
        return {
            "document": str(doc_path),
            "format": doc_format,
            "timestamp": datetime.now().isoformat(),
            "total_test_time": total_time,
            "file_size": doc_path.stat().st_size if doc_path.exists() else 0,
            "detection_test": {
                "success": detection_success,
                "error_message": detection_error,
                "execution_time": detection_time
            },
            "conversion_results": conversion_results,
            "roundtrip_results": roundtrip_results,
            "overall_success": detection_success and (conversion_results["successful_conversions"] > 0),
            "quality_score": self.calculate_quality_score(detection_success, conversion_results, roundtrip_results)
        }
    
    def calculate_quality_score(self, detection_success: bool, conversion_results: Dict, roundtrip_results: Dict) -> float:
        """Calculate an overall quality score for the document testing."""
        score = 0.0
        
        # Detection success (20%)
        if detection_success:
            score += 20.0
        
        # Conversion success rate (40%)
        if conversion_results["total_conversions"] > 0:
            conversion_rate = conversion_results["successful_conversions"] / conversion_results["total_conversions"]
            score += 40.0 * conversion_rate
        
        # Roundtrip success rate (40%)
        if roundtrip_results["total_tests"] > 0:
            roundtrip_rate = roundtrip_results["successful_tests"] / roundtrip_results["total_tests"]
            similarity_bonus = roundtrip_results["average_similarity"]
            score += 40.0 * roundtrip_rate * similarity_bonus
        
        return min(score, 100.0)  # Cap at 100%


def main():
    """Run Phase 3 comprehensive testing."""
    print("🚀 Starting Phase 3: Lambda Convert Engine Integration with Roundtrip Testing")
    
    tester = RoundtripTester("../../test_output/auto")
    
    # Read document list
    doc_list_file = Path("doc_list.csv")
    if not doc_list_file.exists():
        print(f"❌ Document list not found: {doc_list_file}")
        return 1
    
    # Load documents from CSV
    test_results = []
    with open(doc_list_file, 'r', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        documents = [row for row in reader if not row['url'].startswith('#')]
    
    print(f"📋 Found {len(documents)} documents to test")
    
    # Test each document
    for i, doc in enumerate(documents, 1):
        # Only skip documents that failed to download (download_failed)
        if doc['test_status'] == 'download_failed':
            print(f"⏩ Skipping {doc['local_filename']} (status: {doc['test_status']})")
            continue
        
        doc_path = Path(f"../../test_output/auto/{doc['format']}/{doc['local_filename']}").resolve()
        if not doc_path.exists():
            print(f"⏩ Skipping {doc['local_filename']} (file not found)")
            continue
        
        print(f"🧪 Testing {i}/{len(documents)}: {doc['local_filename']} ({doc['format']})")
        
        try:
            result = tester.test_document_comprehensive(doc_path, doc['format'])
            test_results.append(result)
            
            # Print summary
            conv_success = result['conversion_results']['successful_conversions']
            conv_total = result['conversion_results']['total_conversions']
            round_success = result['roundtrip_results']['successful_tests']
            round_total = result['roundtrip_results']['total_tests']
            quality = result['quality_score']
            
            print(f"   ✅ Conversions: {conv_success}/{conv_total}")
            print(f"   🔄 Roundtrips: {round_success}/{round_total}")
            print(f"   🎯 Quality Score: {quality:.1f}%")
            
        except Exception as e:
            print(f"   ❌ Error testing {doc['local_filename']}: {str(e)}")
            continue
    
    # Save comprehensive results
    results_file = Path("../../test_output/auto/phase3_comprehensive_results.json")
    results_file.parent.mkdir(parents=True, exist_ok=True)
    
    results_data = {
        "timestamp": datetime.now().isoformat(),
        "phase": "3_comprehensive_testing",
        "total_documents": len(test_results),
        "test_results": test_results,
        "summary": {
            "total_tests": len(test_results),
            "average_quality_score": sum(r["quality_score"] for r in test_results) / max(len(test_results), 1),
            "total_conversions": sum(r["conversion_results"]["total_conversions"] for r in test_results),
            "successful_conversions": sum(r["conversion_results"]["successful_conversions"] for r in test_results),
            "total_roundtrips": sum(r["roundtrip_results"]["total_tests"] for r in test_results),
            "successful_roundtrips": sum(r["roundtrip_results"]["successful_tests"] for r in test_results),
            "identical_roundtrips": sum(r["roundtrip_results"]["identical_roundtrips"] for r in test_results)
        }
    }
    
    with open(results_file, 'w', encoding='utf-8') as f:
        json.dump(results_data, f, indent=2)
    
    print(f"\n✅ Phase 3 testing complete! Results saved to {results_file}")
    print(f"📊 Summary:")
    print(f"   Documents tested: {len(test_results)}")
    total_conversions = sum(r["conversion_results"]["total_conversions"] for r in test_results)
    successful_conversions = sum(r["conversion_results"]["successful_conversions"] for r in test_results)
    total_roundtrips = sum(r["roundtrip_results"]["total_tests"] for r in test_results)
    successful_roundtrips = sum(r["roundtrip_results"]["successful_tests"] for r in test_results)
    
    print(f"   Conversions: {successful_conversions}/{total_conversions} ({successful_conversions/max(total_conversions,1)*100:.1f}%)")
    print(f"   Roundtrips: {successful_roundtrips}/{total_roundtrips} ({successful_roundtrips/max(total_roundtrips,1)*100:.1f}%)")
    
    return 0


if __name__ == "__main__":
    sys.exit(main())
