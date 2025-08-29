#!/usr/bin/env python3
"""
Lambda Script Automated Input/Formatter/Validator Testing System
Phase 1 & 2 Implementation: Document Discovery, Download, and Testing

This script implements automated testing of Lambda engine's input parsing, 
format conversion, and validation capabilities using real-world documents.
"""

import os
import sys
import json
import time
import requests
import subprocess
import logging
import hashlib
import csv
from pathlib import Path
from datetime import datetime
from typing import List, Dict, Any, Optional, Tuple
from urllib.parse import urlparse
import argparse

# Paths Configuration
SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent  # test/auto -> test -> project_root
TEST_OUTPUT_DIR = PROJECT_ROOT / "test_output" / "auto"
DOC_LIST_CSV = PROJECT_ROOT / "test" / "auto" / "doc_list.csv"
LAMBDA_EXECUTABLE = PROJECT_ROOT / "lambda.exe"
import csv
import json
import hashlib
import requests
import time
import subprocess
import logging
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Tuple, Optional, Any
from urllib.parse import urlparse, urljoin
from concurrent.futures import ThreadPoolExecutor, as_completed
import re

# Setup logging
def setup_logging():
    """Setup logging configuration"""
    log_file = TEST_OUTPUT_DIR / "automation.log"
    log_file.parent.mkdir(parents=True, exist_ok=True)
    
    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s - %(levelname)s - %(message)s',
        handlers=[
            logging.FileHandler(log_file),
            logging.StreamHandler(sys.stdout)
        ]
    )
    return logging.getLogger(__name__)

logger = setup_logging()

class DocumentInfo:
    """Represents a document for testing"""
    def __init__(self, url: str, format_type: str, source: str, 
                 size_bytes: int = 0, local_filename: str = "", 
                 content_hash: str = "", notes: str = ""):
        self.url = url
        self.format = format_type
        self.source = source
        self.size_bytes = size_bytes
        self.local_filename = local_filename
        self.content_hash = content_hash
        self.notes = notes
        self.discovered_date = datetime.now().strftime("%Y-%m-%d")
        self.test_status = "pending"
        self.last_tested = None
        self.issues_count = 0

class DocumentDownloader:
    """Handles document discovery and download with safe filename generation"""
    
    def __init__(self, base_dir: Path):
        self.base_dir = Path(base_dir)
        self.format_counters = {}
        self.session = requests.Session()
        self.session.headers.update({
            'User-Agent': 'Lambda-Script-Test-Bot/1.0 (+https://github.com/henry-luo/lambda)'
        })
        
        # Create format subdirectories
        self.format_dirs = {
            'markdown': self.base_dir / 'markdown',
            'json': self.base_dir / 'json', 
            'html': self.base_dir / 'html',
            'xml': self.base_dir / 'xml',
            'yaml': self.base_dir / 'yaml',
            'csv': self.base_dir / 'csv',
            'latex': self.base_dir / 'latex',
            'pdf': self.base_dir / 'pdf',
            'text': self.base_dir / 'text',
            'ini': self.base_dir / 'ini',
            'toml': self.base_dir / 'toml',
            'results': self.base_dir / 'results'
        }
        
        for format_dir in self.format_dirs.values():
            format_dir.mkdir(parents=True, exist_ok=True)
    
    def sanitize_filename(self, filename: str) -> str:
        """Remove unsafe characters and limit length"""
        # Remove unsafe characters
        safe = re.sub(r'[<>:"/\\|?*]', '_', filename)
        # Remove multiple underscores
        safe = re.sub(r'_+', '_', safe)
        # Limit length and clean up
        return safe[:100].strip('_')
    
    def extract_domain(self, url: str) -> str:
        """Extract domain name for filename"""
        domain = urlparse(url).netloc
        return domain.replace('.', '_').replace('-', '_')[:20]
    
    def extract_identifier(self, url: str) -> str:
        """Extract meaningful identifier from URL"""
        path = urlparse(url).path
        parts = [p for p in path.split('/') if p and p not in ['blob', 'raw', 'main', 'master']]
        if parts:
            # Use last meaningful part, remove extension
            identifier = parts[-1]
            if '.' in identifier:
                identifier = identifier.rsplit('.', 1)[0]
            return self.sanitize_filename(identifier)[:30]
        return "document"
    
    def generate_safe_filename(self, url: str, format_type: str, source: str) -> str:
        """Generate safe, structured local filename"""
        # Initialize/increment counter for format
        if format_type not in self.format_counters:
            self.format_counters[format_type] = 1
        else:
            self.format_counters[format_type] += 1
        
        # Extract meaningful parts from URL
        domain = self.extract_domain(url)
        identifier = self.extract_identifier(url)
        
        # Generate structured filename
        counter = str(self.format_counters[format_type]).zfill(3)
        base_filename = f"{format_type}_{counter}_{source}_{domain}_{identifier}"
        
        # Sanitize and add extension
        safe_filename = self.sanitize_filename(base_filename)
        extension = self.get_format_extension(format_type)
        
        return f"{safe_filename}.{extension}"
    
    def get_format_extension(self, format_type: str) -> str:
        """Get appropriate file extension for format"""
        extensions = {
            'markdown': 'md',
            'json': 'json',
            'html': 'html',
            'xml': 'xml',
            'yaml': 'yaml',
            'csv': 'csv',
            'latex': 'tex',
            'pdf': 'pdf',
            'text': 'txt',
            'ini': 'ini',
            'toml': 'toml'
        }
        return extensions.get(format_type, 'txt')
    
    def calculate_content_hash(self, content: bytes) -> str:
        """Calculate SHA256 hash of content"""
        return f"sha256:{hashlib.sha256(content).hexdigest()[:16]}"
    
    def download_document(self, doc: DocumentInfo) -> Tuple[bool, str, Path]:
        """Download document and return success status, message, and local path"""
        try:
            logger.info(f"Downloading {doc.url}")
            
            # Generate safe filename
            if not doc.local_filename:
                doc.local_filename = self.generate_safe_filename(doc.url, doc.format, doc.source)
            
            # Determine target directory and file path
            format_dir = self.format_dirs.get(doc.format, self.base_dir)
            local_path = format_dir / doc.local_filename
            
            # Skip if file already exists and has content
            if local_path.exists() and local_path.stat().st_size > 0:
                logger.info(f"File already exists: {local_path}")
                return True, "Already exists", local_path
            
            # Download with timeout and size limits
            response = self.session.get(doc.url, timeout=30, stream=True)
            response.raise_for_status()
            
            # Check content size (limit to 10MB)
            content_length = response.headers.get('content-length')
            if content_length and int(content_length) > 10 * 1024 * 1024:
                return False, "Content too large (>10MB)", local_path
            
            # Download content
            content = b""
            for chunk in response.iter_content(chunk_size=8192):
                content += chunk
                if len(content) > 10 * 1024 * 1024:  # 10MB limit
                    return False, "Content too large (>10MB)", local_path
            
            # Calculate content hash
            doc.content_hash = self.calculate_content_hash(content)
            doc.size_bytes = len(content)
            
            # Write to file
            with open(local_path, 'wb') as f:
                f.write(content)
            
            logger.info(f"Downloaded {len(content)} bytes to {local_path}")
            return True, "Download successful", local_path
            
        except requests.RequestException as e:
            error_msg = f"Download failed: {str(e)}"
            logger.error(f"{doc.url}: {error_msg}")
            return False, error_msg, local_path
        except Exception as e:
            error_msg = f"Unexpected error: {str(e)}"
            logger.error(f"{doc.url}: {error_msg}")
            return False, error_msg, local_path

class LambdaTestRunner:
    """Handles Lambda engine testing and format conversion"""
    
    def __init__(self, lambda_exe: Path, output_dir: Path):
        self.lambda_exe = Path(lambda_exe)
        self.output_dir = Path(output_dir)
        self.results_dir = output_dir / "results"
        self.results_dir.mkdir(parents=True, exist_ok=True)
        
        # Verify Lambda executable exists
        if not self.lambda_exe.exists():
            raise FileNotFoundError(f"Lambda executable not found: {self.lambda_exe}")
    
    def run_lambda_command(self, args: List[str], timeout: int = 60) -> Tuple[bool, str, str]:
        """Run Lambda command and return success, stdout, stderr"""
        try:
            cmd = [str(self.lambda_exe)] + args
            logger.debug(f"Running: {' '.join(cmd)}")
            
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=timeout,
                cwd=str(self.lambda_exe.parent)
            )
            
            success = result.returncode == 0
            return success, result.stdout, result.stderr
            
        except subprocess.TimeoutExpired:
            return False, "", f"Command timed out after {timeout} seconds"
        except Exception as e:
            return False, "", f"Command execution failed: {str(e)}"
    
    def test_format_detection(self, file_path: Path, expected_format: str) -> Dict[str, Any]:
        """Test if Lambda can detect and parse the file format"""
        test_result = {
            'test_type': 'format_detection',
            'input_file': str(file_path),
            'expected_format': expected_format,
            'success': False,
            'detected_format': None,
            'error_message': '',
            'execution_time': 0,
            'file_size': 0
        }
        
        if not file_path.exists():
            test_result['error_message'] = "Input file does not exist"
            return test_result
        
        test_result['file_size'] = file_path.stat().st_size
        
        # For now, use convert command to test parsing by converting to same format
        output_file = self.results_dir / f"detection_test_{file_path.stem}.{expected_format}"
        
        start_time = time.time()
        success, stdout, stderr = self.run_lambda_command([
            'convert', str(file_path), 
            '-f', expected_format,
            '-t', expected_format, 
            '-o', str(output_file)
        ])
        test_result['execution_time'] = time.time() - start_time
        
        if success and output_file.exists():
            test_result['success'] = True
            test_result['detected_format'] = expected_format
            # Clean up temp file
            output_file.unlink(missing_ok=True)
        else:
            test_result['error_message'] = stderr or "Unknown parsing error"
        
        return test_result
    
    def test_conversion(self, input_file: Path, from_format: str, to_format: str) -> Dict[str, Any]:
        """Test conversion between two formats"""
        test_result = {
            'test_type': 'conversion',
            'input_file': str(input_file),
            'from_format': from_format,
            'to_format': to_format,
            'success': False,
            'output_file': '',
            'error_message': '',
            'execution_time': 0,
            'input_size': 0,
            'output_size': 0
        }
        
        if not input_file.exists():
            test_result['error_message'] = "Input file does not exist"
            return test_result
        
        test_result['input_size'] = input_file.stat().st_size
        
        # Generate output filename
        output_file = self.results_dir / f"{input_file.stem}_to_{to_format}.{self.get_format_extension(to_format)}"
        test_result['output_file'] = str(output_file)
        
        start_time = time.time()
        success, stdout, stderr = self.run_lambda_command([
            'convert', str(input_file),
            '-f', from_format,
            '-t', to_format,
            '-o', str(output_file)
        ])
        test_result['execution_time'] = time.time() - start_time
        
        if success and output_file.exists():
            test_result['success'] = True
            test_result['output_size'] = output_file.stat().st_size
        else:
            test_result['error_message'] = stderr or "Unknown conversion error"
        
        return test_result
    
    def get_format_extension(self, format_type: str) -> str:
        """Get appropriate file extension for format"""
        extensions = {
            'markdown': 'md', 'json': 'json', 'html': 'html', 'xml': 'xml',
            'yaml': 'yaml', 'csv': 'csv', 'latex': 'tex', 'pdf': 'pdf',
            'text': 'txt', 'ini': 'ini', 'toml': 'toml'
        }
        return extensions.get(format_type, 'txt')
    
    def get_target_formats(self, source_format: str) -> List[str]:
        """Get list of target formats to test conversion to"""
        # Common conversion targets for each format
        conversions = {
            'markdown': ['html', 'json', 'xml'],
            'json': ['yaml', 'xml', 'csv'],
            'html': ['markdown', 'xml', 'json'],
            'xml': ['json', 'yaml'],
            'yaml': ['json', 'xml'],
            'csv': ['json', 'xml'],
            'latex': ['html', 'xml'],
            'text': ['json', 'xml']
        }
        return conversions.get(source_format, ['json'])
    
    def test_document_complete(self, file_path: Path, format_type: str) -> Dict[str, Any]:
        """Run complete test suite on a document"""
        test_results = {
            'document': str(file_path),
            'format': format_type,
            'timestamp': datetime.now().isoformat(),
            'detection_test': None,
            'conversion_tests': [],
            'overall_success': False,
            'total_execution_time': 0
        }
        
        start_time = time.time()
        
        # Test 1: Format detection and parsing
        logger.info(f"Testing format detection for {file_path}")
        detection_result = self.test_format_detection(file_path, format_type)
        test_results['detection_test'] = detection_result
        
        # Test 2: Format conversions (only if detection succeeded)
        if detection_result['success']:
            target_formats = self.get_target_formats(format_type)
            logger.info(f"Testing conversions from {format_type} to {target_formats}")
            
            for target_format in target_formats:
                conversion_result = self.test_conversion(file_path, format_type, target_format)
                test_results['conversion_tests'].append(conversion_result)
        
        test_results['total_execution_time'] = time.time() - start_time
        
        # Determine overall success
        detection_success = detection_result['success']
        conversion_success = all(t['success'] for t in test_results['conversion_tests'])
        test_results['overall_success'] = detection_success and (not test_results['conversion_tests'] or conversion_success)
        
        return test_results

class DocumentManager:
    """Manages the document list CSV and coordinates testing"""
    
    def __init__(self, csv_file: Path):
        self.csv_file = Path(csv_file)
        self.documents = []
        self.load_documents()
    
    def load_documents(self):
        """Load documents from CSV file"""
        if not self.csv_file.exists():
            logger.warning(f"CSV file not found: {self.csv_file}")
            return
        
        with open(self.csv_file, 'r', newline='', encoding='utf-8') as f:
            reader = csv.DictReader(f)
            for row in reader:
                if row['url'].startswith('#'):  # Skip comments
                    continue
                doc = DocumentInfo(
                    url=row['url'],
                    format_type=row['format'],
                    source=row['source'],
                    size_bytes=int(row['size_bytes']) if row['size_bytes'].isdigit() else 0,
                    local_filename=row['local_filename'],
                    content_hash=row['content_hash'],
                    notes=row.get('notes', '')
                )
                doc.test_status = row.get('test_status', 'pending')
                doc.last_tested = row.get('last_tested')
                doc.issues_count = int(row.get('issues_count', 0))
                self.documents.append(doc)
        
        logger.info(f"Loaded {len(self.documents)} documents from {self.csv_file}")
    
    def save_documents(self):
        """Save documents back to CSV file"""
        fieldnames = [
            'url', 'format', 'source', 'size_bytes', 'discovered_date',
            'test_status', 'last_tested', 'issues_count', 'local_filename',
            'content_hash', 'notes'
        ]
        
        # Create backup
        backup_file = self.csv_file.with_suffix('.csv.backup')
        if self.csv_file.exists():
            self.csv_file.rename(backup_file)
        
        with open(self.csv_file, 'w', newline='', encoding='utf-8') as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            
            # Write comment
            f.write("# Automated document testing index - managed by test automation system\n")
            
            for doc in self.documents:
                writer.writerow({
                    'url': doc.url,
                    'format': doc.format,
                    'source': doc.source,
                    'size_bytes': doc.size_bytes,
                    'discovered_date': doc.discovered_date,
                    'test_status': doc.test_status,
                    'last_tested': doc.last_tested or 'null',
                    'issues_count': doc.issues_count,
                    'local_filename': doc.local_filename,
                    'content_hash': doc.content_hash,
                    'notes': doc.notes
                })
        
        logger.info(f"Saved {len(self.documents)} documents to {self.csv_file}")

def main():
    """Main automation entry point"""
    print("=== Lambda Script Automated Testing System ===")
    print("Phase 1 & 2: Document Download and Testing")
    print()
    
    # Initialize components
    doc_manager = DocumentManager(DOC_LIST_CSV)
    downloader = DocumentDownloader(TEST_OUTPUT_DIR)
    test_runner = LambdaTestRunner(LAMBDA_EXECUTABLE, TEST_OUTPUT_DIR)
    
    # Create test results file
    results_file = TEST_OUTPUT_DIR / "test_results.json"
    all_results = []
    
    # Statistics tracking
    stats = {
        'total_documents': len(doc_manager.documents),
        'downloaded': 0,
        'download_failed': 0,
        'tested': 0,
        'test_passed': 0,
        'test_failed': 0
    }
    
    print(f"Found {stats['total_documents']} documents to process")
    print(f"Output directory: {TEST_OUTPUT_DIR}")
    print(f"Lambda executable: {LAMBDA_EXECUTABLE}")
    print()
    
    # Phase 1: Download documents
    print("=== Phase 1: Document Download ===")
    for i, doc in enumerate(doc_manager.documents, 1):
        print(f"[{i}/{stats['total_documents']}] Processing: {doc.url}")
        
        success, message, local_path = downloader.download_document(doc)
        if success:
            stats['downloaded'] += 1
            doc.test_status = 'downloaded'
            print(f"  ✓ Downloaded: {local_path}")
        else:
            stats['download_failed'] += 1
            doc.test_status = 'download_failed'
            doc.issues_count += 1
            print(f"  ✗ Failed: {message}")
        
        # Rate limiting
        time.sleep(1)
    
    print(f"\nDownload Summary: {stats['downloaded']} successful, {stats['download_failed']} failed")
    
    # Update CSV with download results
    doc_manager.save_documents()
    
    # Phase 2: Test documents
    print("\n=== Phase 2: Lambda Engine Testing ===")
    for i, doc in enumerate(doc_manager.documents, 1):
        if doc.test_status != 'downloaded':
            continue
        
        local_path = TEST_OUTPUT_DIR / doc.format / doc.local_filename
        if not local_path.exists():
            continue
        
        print(f"[{i}/{stats['total_documents']}] Testing: {local_path}")
        
        try:
            test_result = test_runner.test_document_complete(local_path, doc.format)
            all_results.append(test_result)
            
            if test_result['overall_success']:
                stats['test_passed'] += 1
                doc.test_status = 'passed'
                print(f"  ✓ All tests passed ({test_result['total_execution_time']:.2f}s)")
            else:
                stats['test_failed'] += 1
                doc.test_status = 'failed' 
                doc.issues_count += 1
                print(f"  ✗ Tests failed ({test_result['total_execution_time']:.2f}s)")
                
                # Show specific failures
                if not test_result['detection_test']['success']:
                    print(f"    - Detection failed: {test_result['detection_test']['error_message']}")
                for conv_test in test_result['conversion_tests']:
                    if not conv_test['success']:
                        print(f"    - Conversion {conv_test['from_format']}→{conv_test['to_format']} failed: {conv_test['error_message']}")
                        
            doc.last_tested = datetime.now().isoformat()
            stats['tested'] += 1
            
        except Exception as e:
            stats['test_failed'] += 1
            doc.test_status = 'error'
            doc.issues_count += 1
            print(f"  ✗ Test error: {str(e)}")
    
    # Save final results
    doc_manager.save_documents()
    
    with open(results_file, 'w') as f:
        json.dump({
            'timestamp': datetime.now().isoformat(),
            'statistics': stats,
            'test_results': all_results
        }, f, indent=2)
    
    # Print final summary
    print("\n=== Final Summary ===")
    print(f"Total Documents: {stats['total_documents']}")
    print(f"Downloaded: {stats['downloaded']} ({stats['download_failed']} failed)")
    print(f"Tested: {stats['tested']} ({stats['test_passed']} passed, {stats['test_failed']} failed)")
    print(f"Results saved to: {results_file}")
    print(f"Logs saved to: {TEST_OUTPUT_DIR / 'automation.log'}")
    
    return 0 if stats['test_failed'] == 0 else 1

if __name__ == "__main__":
    try:
        exit_code = main()
        sys.exit(exit_code)
    except KeyboardInterrupt:
        print("\n\nOperation cancelled by user")
        sys.exit(1)
    except Exception as e:
        logger.error(f"Fatal error: {str(e)}", exc_info=True)
        sys.exit(1)
