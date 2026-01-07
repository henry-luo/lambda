#!/usr/bin/env python3
"""
Extract test cases from MathLive test files for Lambda math layout testing.

Usage:
    python3 utils/extract_mathlive_tests.py

This script extracts LaTeX test cases from:
1. mathlive/test/markup.test.ts - Jest unit tests
2. mathlive/test/static/index.html - Visual test cases

Output is written to test/math/fixtures/ as JSON files.
"""

import json
import os
import re
from pathlib import Path
from typing import List, Dict, Any, Optional

# Root directory of the project
PROJECT_ROOT = Path(__file__).parent.parent

# Input paths
MATHLIVE_DIR = PROJECT_ROOT / "mathlive"
MARKUP_TEST_FILE = MATHLIVE_DIR / "test" / "markup.test.ts"
STATIC_INDEX_FILE = MATHLIVE_DIR / "test" / "static" / "index.html"
SNAPSHOT_FILE = MATHLIVE_DIR / "test" / "__snapshots__" / "markup.test.ts.snap"

# Output directory
OUTPUT_DIR = PROJECT_ROOT / "test" / "math" / "fixtures"


def extract_test_each_cases(content: str) -> Dict[str, List[str]]:
    """Extract test cases from test.each() blocks in TypeScript test files."""
    categories = {}

    # Pattern to match describe blocks with test.each
    describe_pattern = r"describe\(['\"]([^'\"]+)['\"].*?\{(.*?)\n\}\);"

    # Find all describe blocks
    for match in re.finditer(describe_pattern, content, re.DOTALL):
        category_name = match.group(1)
        block_content = match.group(2)

        # Extract test.each arrays
        test_each_pattern = r"test\.each\(\[(.*?)\]\)"
        for test_match in re.finditer(test_each_pattern, block_content, re.DOTALL):
            array_content = test_match.group(1)

            # Extract string literals (LaTeX expressions)
            latex_pattern = r"['\"]([^'\"]+)['\"]"
            latex_cases = re.findall(latex_pattern, array_content)

            # Filter out non-LaTeX strings (descriptions, etc.)
            latex_cases = [s for s in latex_cases if not s.startswith('%') and len(s) > 0]

            if latex_cases:
                if category_name not in categories:
                    categories[category_name] = []
                categories[category_name].extend(latex_cases)

    return categories


def extract_from_markup_test() -> Dict[str, List[Dict[str, Any]]]:
    """Extract LaTeX strings from markup.test.ts"""
    if not MARKUP_TEST_FILE.exists():
        print(f"Warning: {MARKUP_TEST_FILE} not found")
        return {}

    content = MARKUP_TEST_FILE.read_text(encoding='utf-8')

    # Categories we're interested in
    target_categories = {
        'FRACTIONS': 'fractions',
        'SURDS': 'radicals',
        'ACCENTS': 'accents',
        'BINARY OPERATORS': 'operators',
        'SUPERSCRIPT/SUBSCRIPT': 'subscripts',
        'LEFT/RIGHT': 'delimiters',
        'SPACING AND KERN': 'spacing',
        'ENVIRONMENTS': 'environments',
        'OVER/UNDERLINE': 'overunder',
        'NOT': 'negation',
        'BOX': 'boxes',
        'DELIMITER SIZING COMMANDS': 'delimiter_sizing',
        'SIZING COMMANDS': 'sizing',
    }

    results = {}

    # Extract test.each blocks
    raw_categories = extract_test_each_cases(content)

    for raw_name, latex_list in raw_categories.items():
        # Map to our category names
        category = target_categories.get(raw_name, raw_name.lower().replace(' ', '_'))

        tests = []
        for i, latex in enumerate(latex_list):
            # Skip empty or invalid entries
            if not latex or latex.isspace():
                continue

            tests.append({
                'id': i + 1,
                'latex': latex,
                'description': f'{raw_name} test case {i + 1}',
                'source': 'mathlive/markup.test.ts'
            })

        if tests:
            results[category] = tests

    return results


def extract_height_from_snapshot(latex: str, snapshot_content: str) -> Optional[float]:
    """Extract height value from snapshot file for a given LaTeX expression."""
    # Escape special regex characters in latex
    escaped_latex = re.escape(latex)

    # Look for the snapshot entry and extract height
    pattern = rf'height:(\d+\.?\d*)em'

    # Find the snapshot for this latex
    # Snapshots are keyed by test name which includes the latex
    if latex in snapshot_content:
        # Find nearby height value
        idx = snapshot_content.find(latex)
        nearby = snapshot_content[idx:idx+500]
        height_match = re.search(pattern, nearby)
        if height_match:
            return float(height_match.group(1))

    return None


def extract_from_static_html() -> Dict[str, List[Dict[str, Any]]]:
    """Extract test cases from static/index.html"""
    if not STATIC_INDEX_FILE.exists():
        print(f"Warning: {STATIC_INDEX_FILE} not found")
        return {}

    content = STATIC_INDEX_FILE.read_text(encoding='utf-8')

    # Find the TESTING_SAMPLES JavaScript object
    # Pattern to match the JavaScript object structure
    samples_pattern = r"const TESTING_SAMPLES = \{(.*?)\};\s*(?://|$|\n\s*\n)"

    results = {}

    # Extract category blocks
    # Format: 'Category Name': [ { title: '...', latex: '...' }, ... ]
    category_pattern = r"['\"]([^'\"]+)['\"]:\s*\[(.*?)\](?=,\s*['\"]|\s*\})"

    for cat_match in re.finditer(category_pattern, content, re.DOTALL):
        category_name = cat_match.group(1)
        items_content = cat_match.group(2)

        # Extract individual test items
        item_pattern = r"\{([^}]+)\}"
        tests = []
        test_id = 1

        for item_match in re.finditer(item_pattern, items_content, re.DOTALL):
            item_content = item_match.group(1)

            # Extract latex field
            latex_match = re.search(r"latex:\s*['\"`]([^'\"`]+)['\"`]", item_content)
            if not latex_match:
                # Try template literal with backticks
                latex_match = re.search(r"latex:\s*`([^`]+)`", item_content)

            if latex_match:
                latex = latex_match.group(1)

                # Extract title if available
                title_match = re.search(r"title:\s*['\"]([^'\"]+)['\"]", item_content)
                title = title_match.group(1) if title_match else f"Test {test_id}"

                # Extract pic reference if available
                pic_match = re.search(r"pic:\s*['\"]([^'\"]+)['\"]", item_content)
                pic = pic_match.group(1) if pic_match else None

                tests.append({
                    'id': test_id,
                    'latex': latex,
                    'description': title,
                    'source': 'mathlive/static/index.html',
                    'reference_image': pic
                })
                test_id += 1

        if tests:
            # Normalize category name
            category_key = category_name.lower().replace(' ', '_').replace('/', '_')
            results[category_key] = tests

    return results


def merge_fixtures(markup_fixtures: Dict, static_fixtures: Dict) -> Dict[str, List[Dict]]:
    """Merge fixtures from different sources, removing duplicates."""
    merged = {}

    # Category mapping to consolidate similar categories
    category_mapping = {
        'generalized_fraction': 'fractions',
        'math_styles': 'styles',
        'layout_(spacing)': 'spacing',
        'stacks': 'stacks',
        'styling': 'styling',
        'text': 'text',
        'multi-line': 'multiline',
        'serialization': 'serialization',
        'phantom_atom': 'phantom',
    }

    def normalize_category(cat: str) -> str:
        cat_lower = cat.lower().replace(' ', '_')
        return category_mapping.get(cat_lower, cat_lower)

    # Add markup fixtures
    for category, tests in markup_fixtures.items():
        norm_cat = normalize_category(category)
        if norm_cat not in merged:
            merged[norm_cat] = []
        merged[norm_cat].extend(tests)

    # Add static fixtures
    for category, tests in static_fixtures.items():
        norm_cat = normalize_category(category)
        if norm_cat not in merged:
            merged[norm_cat] = []

        # Check for duplicates by latex string
        existing_latex = {t['latex'] for t in merged[norm_cat]}
        for test in tests:
            if test['latex'] not in existing_latex:
                merged[norm_cat].append(test)
                existing_latex.add(test['latex'])

    return merged


def write_fixture_file(category: str, tests: List[Dict], output_dir: Path):
    """Write a fixture file for a category."""
    fixture = {
        'category': category,
        'source': 'mathlive',
        'tests': tests
    }

    output_file = output_dir / f"{category}.json"
    with open(output_file, 'w', encoding='utf-8') as f:
        json.dump(fixture, f, indent=2, ensure_ascii=False)

    print(f"  Wrote {len(tests)} tests to {output_file.name}")


def write_all_fixtures_combined(all_fixtures: Dict[str, List[Dict]], output_dir: Path):
    """Write a combined fixture file with all tests."""
    combined = {
        'source': 'mathlive',
        'categories': all_fixtures
    }

    output_file = output_dir / "all_tests.json"
    total_tests = sum(len(tests) for tests in all_fixtures.values())

    with open(output_file, 'w', encoding='utf-8') as f:
        json.dump(combined, f, indent=2, ensure_ascii=False)

    print(f"  Wrote combined file with {total_tests} total tests")


def main():
    print("Extracting MathLive test fixtures...")
    print(f"  Project root: {PROJECT_ROOT}")
    print(f"  MathLive dir: {MATHLIVE_DIR}")

    # Create output directory
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    print(f"  Output dir: {OUTPUT_DIR}")

    # Extract from markup.test.ts
    print("\nExtracting from markup.test.ts...")
    markup_fixtures = extract_from_markup_test()
    print(f"  Found {len(markup_fixtures)} categories")

    # Extract from static/index.html
    print("\nExtracting from static/index.html...")
    static_fixtures = extract_from_static_html()
    print(f"  Found {len(static_fixtures)} categories")

    # Merge fixtures
    print("\nMerging fixtures...")
    all_fixtures = merge_fixtures(markup_fixtures, static_fixtures)
    print(f"  Total categories: {len(all_fixtures)}")

    # Write individual fixture files
    print("\nWriting fixture files...")
    for category, tests in sorted(all_fixtures.items()):
        write_fixture_file(category, tests, OUTPUT_DIR)

    # Write combined file
    write_all_fixtures_combined(all_fixtures, OUTPUT_DIR)

    # Print summary
    print("\n" + "="*50)
    print("Summary:")
    print("="*50)
    total = 0
    for category, tests in sorted(all_fixtures.items()):
        print(f"  {category}: {len(tests)} tests")
        total += len(tests)
    print(f"\nTotal: {total} test cases extracted")


if __name__ == '__main__':
    main()
