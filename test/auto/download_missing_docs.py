#!/usr/bin/env python3
"""
Download missing external documents for Lambda engine testing.
Handles different content types and includes proper error handling.
"""

import csv
import urllib.request
import urllib.error
import time
from pathlib import Path
from urllib.parse import urlparse
import hashlib
import sys

class DocumentDownloader:
    def __init__(self, csv_file: str, base_dir: str = "../../test_output/auto"):
        self.csv_file = Path(csv_file)
        self.base_dir = Path(base_dir).resolve()
        
        self.downloaded = 0
        self.failed = 0
        self.updated_docs = []

    def calculate_content_hash(self, content: bytes) -> str:
        """Calculate SHA256 hash of content."""
        return hashlib.sha256(content).hexdigest()[:16]

    def download_url(self, url: str, timeout: int = 30) -> tuple[bool, bytes, str]:
        """Download content from URL with error handling."""
        try:
            print(f"  📥 Downloading: {url}")
            
            # Create request with user agent
            req = urllib.request.Request(
                url,
                headers={
                    'User-Agent': 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36'
                }
            )
            
            with urllib.request.urlopen(req, timeout=timeout) as response:
                content = response.read()
                content_type = response.headers.get('content-type', '').lower()
            
            # For HTML pages, we might get the full page
            if 'text/html' in content_type and len(content) > 1000000:  # > 1MB
                print(f"  ⚠️  Large HTML content ({len(content)} bytes), keeping as-is")
            
            print(f"  ✅ Downloaded {len(content)} bytes")
            return True, content, ""
            
        except urllib.error.URLError as e:
            error_msg = f"Download failed: {str(e)}"
            print(f"  ❌ {error_msg}")
            return False, b"", error_msg
        except Exception as e:
            error_msg = f"Unexpected error: {str(e)}"
            print(f"  ❌ {error_msg}")
            return False, b"", error_msg

    def save_content(self, content: bytes, file_path: Path) -> bool:
        """Save content to file."""
        try:
            file_path.parent.mkdir(parents=True, exist_ok=True)
            with open(file_path, 'wb') as f:
                f.write(content)
            print(f"  💾 Saved to: {file_path}")
            return True
        except Exception as e:
            print(f"  ❌ Save failed: {str(e)}")
            return False

    def download_document(self, doc: dict) -> bool:
        """Download a single document."""
        url = doc['url']
        local_filename = doc['local_filename']
        format_type = doc['format']
        
        print(f"\n🔄 Processing: {local_filename}")
        
        # Skip local files
        if url.startswith('local://'):
            print(f"  ⏩ Skipping local file")
            return True
        
        # Create file path
        file_path = self.base_dir / format_type / local_filename
        
        # Check if file already exists and is not a failed download
        if file_path.exists() and doc['test_status'] not in ['download_failed', 'pending']:
            print(f"  ✅ File already exists and status is {doc['test_status']}")
            return True
        
        # Download content
        success, content, error_msg = self.download_url(url)
        
        if not success:
            self.failed += 1
            doc['test_status'] = 'download_failed'
            doc['issues_count'] = str(int(doc.get('issues_count', '0')) + 1)
            doc['notes'] = f"Download failed: {error_msg}"
            return False
        
        # Save content
        if not self.save_content(content, file_path):
            self.failed += 1
            doc['test_status'] = 'download_failed'
            doc['issues_count'] = str(int(doc.get('issues_count', '0')) + 1)
            doc['notes'] = "File save failed"
            return False
        
        # Update document metadata
        content_hash = self.calculate_content_hash(content)
        doc['test_status'] = 'passed'
        doc['content_hash'] = f"sha256:{content_hash}"
        doc['size_bytes'] = str(len(content))
        doc['issues_count'] = '0'
        doc['notes'] = f"Downloaded successfully on {time.strftime('%Y-%m-%d')}"
        
        self.downloaded += 1
        self.updated_docs.append(doc)
        
        # Add small delay to be respectful
        time.sleep(1)
        return True

    def update_csv(self):
        """Update the CSV file with new download status."""
        print(f"\n📝 Updating CSV file...")
        
        # Read all documents
        with open(self.csv_file, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # Parse CSV
        lines = content.strip().split('\n')
        header_lines = [line for line in lines if line.startswith('#') or line.startswith('url,')]
        
        with open(self.csv_file, 'r', encoding='utf-8') as f:
            reader = csv.DictReader(f)
            all_docs = [row for row in reader if not row['url'].startswith('#')]
        
        # Create updated docs lookup
        updated_lookup = {doc['local_filename']: doc for doc in self.updated_docs}
        
        # Update documents
        for doc in all_docs:
            if doc['local_filename'] in updated_lookup:
                doc.update(updated_lookup[doc['local_filename']])
        
        # Write updated CSV
        with open(self.csv_file, 'w', encoding='utf-8', newline='') as f:
            # Write header comments
            for line in header_lines:
                if line.startswith('#'):
                    f.write(line + '\n')
            
            # Write CSV data
            if all_docs:
                fieldnames = all_docs[0].keys()
                writer = csv.DictWriter(f, fieldnames=fieldnames)
                writer.writeheader()
                writer.writerows(all_docs)
        
        print(f"  ✅ Updated {len(self.updated_docs)} document records")

    def run(self):
        """Main download process."""
        print("🚀 Starting Missing Document Download Process")
        
        # Load documents from CSV
        with open(self.csv_file, 'r', encoding='utf-8') as f:
            reader = csv.DictReader(f)
            documents = [row for row in reader if not row['url'].startswith('#')]
        
        # Filter for missing external documents
        missing_docs = []
        for doc in documents:
            file_path = self.base_dir / doc['format'] / doc['local_filename']
            
            # Include if file doesn't exist OR if status is download_failed
            if (not file_path.exists() and not doc['url'].startswith('local://')) or \
               doc['test_status'] == 'download_failed':
                missing_docs.append(doc)
        
        print(f"📋 Found {len(missing_docs)} documents to download")
        
        if not missing_docs:
            print("✅ No missing documents to download!")
            return 0
        
        # Download each document
        for i, doc in enumerate(missing_docs, 1):
            print(f"\n📥 [{i}/{len(missing_docs)}] Downloading: {doc['local_filename']}")
            self.download_document(doc)
        
        # Update CSV with results
        if self.updated_docs:
            self.update_csv()
        
        # Print summary
        print(f"\n📊 Download Summary:")
        print(f"  ✅ Successfully downloaded: {self.downloaded}")
        print(f"  ❌ Failed downloads: {self.failed}")
        print(f"  📝 Updated CSV records: {len(self.updated_docs)}")
        
        if self.failed > 0:
            print(f"\n⚠️  Some downloads failed. Check the CSV for error details.")
            return 1
        else:
            print(f"\n🎉 All downloads completed successfully!")
            return 0


if __name__ == "__main__":
    downloader = DocumentDownloader("doc_list.csv")
    exit_code = downloader.run()
    sys.exit(exit_code)
