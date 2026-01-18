#!/usr/bin/env python3
"""
Migrate latex.js fixtures to the latexml test structure.

This script reads latex.js fixture files (with embedded expected HTML)
and splits them into individual .tex and .html files for use with
the latexml comparison test framework.

Format of latex.js fixtures:
** header line
.
LaTeX source
.
Expected HTML
.

Output structure:
test/latexml/fixtures/latexjs/<filename>/<id>_<slug>.tex
test/latexml/expected/latexjs/<filename>/<id>_<slug>.html
"""

import os
import re
import sys
from pathlib import Path

# Source and destination directories
LATEXJS_FIXTURES_DIR = Path("test/latex_js/fixtures")
DEST_FIXTURES_DIR = Path("test/latexml/fixtures/latexjs")
DEST_EXPECTED_DIR = Path("test/latexml/expected/latexjs")

# Fixture files to migrate (subset that passes in baseline)
BASELINE_FILES = [
    "basic_test.tex",
    "text.tex",
    "environments.tex",
    "sectioning.tex",
    "whitespace.tex",
    "formatting.tex",
    "symbols.tex",
]

# Files to skip entirely
SKIP_FILES = [
    "math.tex",
    "math.tex.skip",
    "picture.tex",
    "picture.tex.skip",
]


def slugify(text: str) -> str:
    """Convert header text to a filesystem-safe slug."""
    # Convert to lowercase and replace non-alphanumeric with underscore
    slug = re.sub(r'[^a-zA-Z0-9]+', '_', text.lower())
    # Remove leading/trailing underscores
    slug = slug.strip('_')
    # Truncate to reasonable length
    return slug[:40]


def parse_fixtures(content: str, filename: str) -> list:
    """Parse latex.js fixture format into list of fixtures."""
    fixtures = []
    
    # Split by lines that are just "."
    parts = []
    current_part = []
    
    for line in content.split('\n'):
        if line.strip() == '.':
            parts.append('\n'.join(current_part))
            current_part = []
        else:
            current_part.append(line)
    
    if current_part:
        parts.append('\n'.join(current_part))
    
    # Process fixtures in groups of 3: header, source, expected
    fixture_id = 1
    i = 0
    while i + 2 < len(parts):
        header = parts[i].strip()
        source = parts[i + 1]
        expected = parts[i + 2]
        
        # Skip empty fixtures
        if not source.strip() or not expected.strip():
            i += 3
            continue
        
        # Parse header flags
        skip_test = False
        screenshot_test = False
        
        if header.startswith('!'):
            skip_test = True
            header = header[1:]
        if header.startswith('s'):
            screenshot_test = True
            header = header[1:]
        if header.startswith('** '):
            header = header[3:]
        
        fixtures.append({
            'id': fixture_id,
            'header': header.strip(),
            'slug': slugify(header.strip()),
            'source': source,
            'expected': expected,
            'skip': skip_test,
            'screenshot': screenshot_test,
            'filename': filename,
        })
        
        fixture_id += 1
        i += 3
    
    return fixtures


def migrate_file(src_path: Path, verbose: bool = True) -> int:
    """Migrate a single latex.js fixture file. Returns number of fixtures created."""
    filename = src_path.stem  # e.g., "basic_test"
    
    # Read source file
    content = src_path.read_text(encoding='utf-8')
    
    # Parse fixtures
    fixtures = parse_fixtures(content, filename)
    
    if verbose:
        print(f"  {src_path.name}: {len(fixtures)} fixtures")
    
    # Create output directories
    tex_dir = DEST_FIXTURES_DIR / filename
    html_dir = DEST_EXPECTED_DIR / filename
    tex_dir.mkdir(parents=True, exist_ok=True)
    html_dir.mkdir(parents=True, exist_ok=True)
    
    # Write each fixture
    for fixture in fixtures:
        # File naming: id_slug.tex and id_slug.html
        base_name = f"{fixture['id']:02d}_{fixture['slug']}"
        
        tex_path = tex_dir / f"{base_name}.tex"
        html_path = html_dir / f"{base_name}.html"
        
        # Write .tex file with header comment
        tex_content = f"% {fixture['header']}\n"
        if fixture['skip']:
            tex_content += "% SKIP: marked as skip in latex.js\n"
        if fixture['screenshot']:
            tex_content += "% SCREENSHOT: marked as screenshot test in latex.js\n"
        tex_content += fixture['source']
        
        tex_path.write_text(tex_content, encoding='utf-8')
        
        # Write .html file (expected output)
        html_path.write_text(fixture['expected'], encoding='utf-8')
        
        if verbose and len(fixtures) <= 5:
            print(f"    -> {base_name}")
    
    return len(fixtures)


def main():
    """Main entry point."""
    import argparse
    
    parser = argparse.ArgumentParser(description='Migrate latex.js fixtures to latexml structure')
    parser.add_argument('--all', action='store_true', help='Migrate all fixture files, not just baseline')
    parser.add_argument('--clean', action='store_true', help='Remove existing output directories first')
    parser.add_argument('--verbose', '-v', action='store_true', help='Verbose output')
    args = parser.parse_args()
    
    print("Migrating latex.js fixtures to latexml structure")
    print("=" * 50)
    
    # Clean if requested
    if args.clean:
        import shutil
        if DEST_FIXTURES_DIR.exists():
            print(f"Removing {DEST_FIXTURES_DIR}")
            shutil.rmtree(DEST_FIXTURES_DIR)
        if DEST_EXPECTED_DIR.exists():
            print(f"Removing {DEST_EXPECTED_DIR}")
            shutil.rmtree(DEST_EXPECTED_DIR)
    
    # Create output directories
    DEST_FIXTURES_DIR.mkdir(parents=True, exist_ok=True)
    DEST_EXPECTED_DIR.mkdir(parents=True, exist_ok=True)
    
    # Get list of files to migrate
    if args.all:
        files_to_migrate = [
            f for f in LATEXJS_FIXTURES_DIR.iterdir()
            if f.is_file() and f.suffix == '.tex' and f.name not in SKIP_FILES
        ]
    else:
        files_to_migrate = [
            LATEXJS_FIXTURES_DIR / f for f in BASELINE_FILES
            if (LATEXJS_FIXTURES_DIR / f).exists()
        ]
    
    print(f"\nSource: {LATEXJS_FIXTURES_DIR}")
    print(f"Fixtures output: {DEST_FIXTURES_DIR}")
    print(f"Expected output: {DEST_EXPECTED_DIR}")
    print(f"\nMigrating {len(files_to_migrate)} fixture files:")
    
    total_fixtures = 0
    for src_path in sorted(files_to_migrate):
        count = migrate_file(src_path, verbose=args.verbose)
        total_fixtures += count
    
    print(f"\n✓ Migrated {total_fixtures} fixtures from {len(files_to_migrate)} files")
    print(f"\nTexture files: {DEST_FIXTURES_DIR}")
    print(f"Expected HTML: {DEST_EXPECTED_DIR}")


if __name__ == '__main__':
    main()
