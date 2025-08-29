#!/usr/bin/env python3
"""
Enhanced document processor that handles both downloaded and local files.
- Downloads missing external documents
- Copies local files from test/input to test_output/auto
- Updates CSV with proper metadata
"""

import csv
import urllib.request
import urllib.error
import shutil
import time
from pathlib import Path
import hashlib
import sys

class EnhancedDocumentProcessor:
    def __init__(self, csv_file: str = "doc_list.csv", 
                 base_dir: str = "../../test_output/auto",
                 source_dir: str = "../../test/input"):
        self.csv_file = Path(csv_file)
        self.base_dir = Path(base_dir).resolve()
        self.source_dir = Path(source_dir).resolve()
        
        self.downloaded = 0
        self.copied = 0
        self.failed = 0
        self.updated_docs = []

    def calculate_content_hash(self, content: bytes) -> str:
        """Calculate SHA256 hash of content."""
        return hashlib.sha256(content).hexdigest()[:16]

    def download_url(self, url: str, timeout: int = 30) -> tuple[bool, bytes, str]:
        """Download content from URL with error handling."""
        try:
            print(f"  📥 Downloading: {url}")
            
            req = urllib.request.Request(
                url,
                headers={
                    'User-Agent': 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36'
                }
            )
            
            with urllib.request.urlopen(req, timeout=timeout) as response:
                content = response.read()
                content_type = response.headers.get('content-type', '').lower()
            
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

    def copy_local_file(self, source_path: Path, dest_path: Path) -> tuple[bool, bytes, str]:
        """Copy local file from source to destination."""
        try:
            if not source_path.exists():
                error_msg = f"Source file not found: {source_path}"
                print(f"  ❌ {error_msg}")
                return False, b"", error_msg
            
            print(f"  📁 Copying: {source_path} -> {dest_path}")
            
            # Read source content
            with open(source_path, 'rb') as f:
                content = f.read()
            
            # Create destination directory
            dest_path.parent.mkdir(parents=True, exist_ok=True)
            
            # Copy file
            shutil.copy2(source_path, dest_path)
            
            print(f"  ✅ Copied {len(content)} bytes")
            return True, content, ""
            
        except Exception as e:
            error_msg = f"Copy failed: {str(e)}"
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

    def process_document(self, doc: dict) -> bool:
        """Process a single document (download or copy local file)."""
        url = doc['url']
        local_filename = doc['local_filename']
        format_type = doc['format']
        
        print(f"\n🔄 Processing: {local_filename}")
        
        # Create file path
        file_path = self.base_dir / format_type / local_filename
        
        # Check if file already exists and is not a failed download/pending
        if file_path.exists() and doc['test_status'] not in ['download_failed', 'pending']:
            print(f"  ✅ File already exists and status is {doc['test_status']}")
            return True
        
        success = False
        content = b""
        error_msg = ""
        
        # Handle local files
        if url.startswith('local://'):
            # Extract the relative path from local:// URL
            relative_path = url[8:]  # Remove 'local://' prefix
            
            # Build path relative to project root
            project_root = self.source_dir.parent.parent  # Go up from test/input to project root
            source_path = project_root / relative_path
            
            success, content, error_msg = self.copy_local_file(source_path, file_path)
            if success:
                self.copied += 1
        else:
            # Handle external URLs
            success, content, error_msg = self.download_url(url)
            if success:
                if self.save_content(content, file_path):
                    self.downloaded += 1
                else:
                    success = False
                    error_msg = "File save failed"
        
        # Update document metadata
        if success:
            content_hash = self.calculate_content_hash(content)
            doc['test_status'] = 'passed'
            doc['content_hash'] = f"sha256:{content_hash}"
            doc['size_bytes'] = str(len(content))
            doc['issues_count'] = '0'
            doc['notes'] = f"Processed successfully on {time.strftime('%Y-%m-%d')}"
            self.updated_docs.append(doc)
        else:
            self.failed += 1
            doc['test_status'] = 'download_failed' if not url.startswith('local://') else 'failed'
            doc['issues_count'] = str(int(doc.get('issues_count', '0')) + 1)
            doc['notes'] = error_msg
        
        # Add small delay to be respectful
        if not url.startswith('local://'):
            time.sleep(1)
        
        return success

    def update_csv(self):
        """Update the CSV file with new processing status."""
        print(f"\n📝 Updating CSV file...")
        
        # Read all documents
        documents = []
        with open(self.csv_file, 'r', encoding='utf-8') as f:
            # Skip comment lines
            lines = []
            for line in f:
                if not line.strip().startswith('#'):
                    lines.append(line)
            
            # Parse CSV from filtered lines
            from io import StringIO
            csv_content = ''.join(lines)
            csv_reader = csv.DictReader(StringIO(csv_content))
            all_docs = [row for row in csv_reader]
        
        # Create updated docs lookup
        updated_lookup = {doc['local_filename']: doc for doc in self.updated_docs}
        
        # Update documents
        for doc in all_docs:
            if doc['local_filename'] in updated_lookup:
                doc.update(updated_lookup[doc['local_filename']])
        
        # Write updated CSV
        with open(self.csv_file, 'r', encoding='utf-8') as f:
            original_content = f.read()
        
        # Extract comment lines
        comment_lines = [line for line in original_content.split('\n') if line.startswith('#')]
        
        with open(self.csv_file, 'w', encoding='utf-8', newline='') as f:
            # Write header comments
            for comment in comment_lines:
                f.write(comment + '\n')
            
            # Write CSV data
            if all_docs:
                fieldnames = all_docs[0].keys()
                writer = csv.DictWriter(f, fieldnames=fieldnames)
                writer.writeheader()
                writer.writerows(all_docs)
        
        print(f"  ✅ Updated {len(self.updated_docs)} document records")

    def run(self):
        """Main processing workflow."""
        print("🚀 Starting Enhanced Document Processing")
        print(f"📁 Source directory: {self.source_dir}")
        print(f"📁 Output directory: {self.base_dir}")
        
        # Load documents from CSV
        documents = []
        with open(self.csv_file, 'r', encoding='utf-8') as f:
            # Skip comment lines
            lines = []
            for line in f:
                if not line.strip().startswith('#'):
                    lines.append(line)
            
            # Parse CSV from filtered lines
            from io import StringIO
            csv_content = ''.join(lines)
            csv_reader = csv.DictReader(StringIO(csv_content))
            documents = [row for row in csv_reader]
        
        # Filter for documents that need processing
        pending_docs = []
        for doc in documents:
            file_path = self.base_dir / doc['format'] / doc['local_filename']
            
            # Include if file doesn't exist OR if status indicates failure/pending
            if (not file_path.exists()) or \
               doc['test_status'] in ['download_failed', 'pending', 'failed']:
                pending_docs.append(doc)
        
        print(f"📋 Found {len(pending_docs)} documents to process")
        
        if not pending_docs:
            print("✅ No documents need processing!")
            return 0
        
        # Process each document
        for i, doc in enumerate(pending_docs, 1):
            print(f"\n📋 [{i}/{len(pending_docs)}] Processing: {doc['local_filename']}")
            self.process_document(doc)
        
        # Update CSV with results
        if self.updated_docs:
            self.update_csv()
        
        # Print summary
        print(f"\n📊 Processing Summary:")
        print(f"  📥 Downloads: {self.downloaded}")
        print(f"  📁 Local copies: {self.copied}")
        print(f"  ❌ Failed: {self.failed}")
        print(f"  📝 Updated CSV records: {len(self.updated_docs)}")
        
        if self.failed > 0:
            print(f"\n⚠️  Some processing failed. Check the CSV for error details.")
            return 1
        else:
            print(f"\n🎉 All documents processed successfully!")
            return 0


if __name__ == "__main__":
    processor = EnhancedDocumentProcessor()
    exit_code = processor.run()
    sys.exit(exit_code)
