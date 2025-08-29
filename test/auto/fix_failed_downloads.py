#!/usr/bin/env python3
"""
Download specific failed documents with corrected URLs.
"""

import urllib.request
from pathlib import Path

def download_file(url, output_path):
    """Download a single file."""
    try:
        print(f"Downloading: {url}")
        req = urllib.request.Request(
            url,
            headers={
                'User-Agent': 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36'
            }
        )
        
        with urllib.request.urlopen(req, timeout=30) as response:
            content = response.read()
        
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with open(output_path, 'wb') as f:
            f.write(content)
        
        print(f"✅ Saved {len(content)} bytes to: {output_path}")
        return True
        
    except Exception as e:
        print(f"❌ Failed: {str(e)}")
        return False

# Download Linux README with corrected URL
base_dir = Path("../../test_output/auto")
success = download_file(
    "https://raw.githubusercontent.com/torvalds/linux/master/README",
    base_dir / "markdown" / "md_001_github_linux_readme.md"
)

# Try alternative RSS feed
if not download_file(
    "https://www.oreilly.com/radar/feed/",
    base_dir / "xml" / "xml_001_oreilly_rss_feed.xml"
):
    # Try another tech RSS feed as fallback
    download_file(
        "https://feeds.simplecast.com/54nAGcIl", 
        base_dir / "xml" / "xml_001_oreilly_rss_feed.xml"
    )

# Try alternative Express README
download_file(
    "https://raw.githubusercontent.com/expressjs/express/master/Readme.md",
    base_dir / "markdown" / "md_007_express_readme.md"
)

print("Manual download attempts completed!")
