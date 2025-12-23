#!/usr/bin/env python3
"""
Extract WPT HTML test cases from JavaScript-embedded test files.

Parses html5lib_*.html files from WPT suite, extracts test data from JavaScript,
and outputs JSON test fixtures for C++ GTest consumption.

Usage:
    python3 utils/extract_wpt_tests.py test/wpt/html/syntax/parsing/ test/html/wpt/
"""

import sys
import os
import json
import glob
import re
import urllib.parse
from pathlib import Path


def url_decode(encoded_str):
    """Decode URL-encoded string (handles %3C, %20, etc.)."""
    return urllib.parse.unquote(encoded_str)


def extract_js_tests_object(content):
    """
    Extract the JavaScript 'tests' object from HTML file content.

    The tests object has format:
    var tests = {
        "hash_id": [async_test(...), "encoded_input", "encoded_expected"],
        ...
    }
    """
    # Find the 'var tests = {...}' block - need to handle multiline with nested braces
    match = re.search(r'var tests\s*=\s*\{(.+?)\s*\}\s*;?\s*init_tests', content, re.DOTALL)
    if not match:
        return None

    tests_content = match.group(1)

    # Parse test entries - each is: "hash":[async_test(...), "encoded_input", "encoded_expected"]
    test_entries = {}

    # Match each test entry - the strings can contain escaped characters
    # Use a more flexible pattern that handles line breaks in strings
    pattern = r'"([a-f0-9]+)"\s*:\s*\[\s*async_test\([^)]+\)\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\]'

    for match in re.finditer(pattern, tests_content, re.DOTALL):
        test_id = match.group(1)
        encoded_input = match.group(2)
        encoded_expected = match.group(3)

        test_entries[test_id] = {
            'input': url_decode(encoded_input),
            'expected': url_decode(encoded_expected)
        }

    return test_entries


def parse_wpt_file(filepath):
    """
    Parse a single WPT HTML test file and extract all test cases.

    Returns list of test case dictionaries.
    """
    filename = os.path.basename(filepath)

    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
    except Exception as e:
        print(f"Error reading {filepath}: {e}", file=sys.stderr)
        return []

    tests_obj = extract_js_tests_object(content)
    if not tests_obj:
        print(f"Warning: No tests found in {filename}", file=sys.stderr)
        return []

    test_cases = []
    for test_id, test_data in tests_obj.items():
        test_cases.append({
            'test_id': test_id,
            'file': filename,
            'input': test_data['input'],
            'expected': test_data['expected']
        })

    return test_cases


def extract_wpt_tests(input_dir, output_dir):
    """
    Extract all test cases from WPT HTML files in input_dir.

    Creates one JSON file per source HTML file in output_dir.
    """
    input_path = Path(input_dir)
    output_path = Path(output_dir)

    # Create output directory if it doesn't exist
    output_path.mkdir(parents=True, exist_ok=True)

    # Find all html5lib test files
    test_files = sorted(input_path.glob('html5lib_*.html'))

    if not test_files:
        print(f"No html5lib_*.html files found in {input_dir}", file=sys.stderr)
        return

    total_tests = 0

    for test_file in test_files:
        print(f"Processing {test_file.name}...", file=sys.stderr)

        test_cases = parse_wpt_file(test_file)

        if test_cases:
            # Create output JSON file
            output_file = output_path / f"{test_file.stem}.json"

            with open(output_file, 'w', encoding='utf-8') as f:
                json.dump(test_cases, f, indent=2, ensure_ascii=False)

            print(f"  → Extracted {len(test_cases)} tests to {output_file.name}",
                  file=sys.stderr)
            total_tests += len(test_cases)

    print(f"\nTotal: {total_tests} test cases from {len(test_files)} files",
          file=sys.stderr)


def main():
    if len(sys.argv) != 3:
        print("Usage: python3 extract_wpt_tests.py <input_dir> <output_dir>",
              file=sys.stderr)
        print("Example: python3 extract_wpt_tests.py test/wpt/html/syntax/parsing/ test/html/wpt/",
              file=sys.stderr)
        sys.exit(1)

    input_dir = sys.argv[1]
    output_dir = sys.argv[2]

    if not os.path.isdir(input_dir):
        print(f"Error: Input directory not found: {input_dir}", file=sys.stderr)
        sys.exit(1)

    extract_wpt_tests(input_dir, output_dir)


if __name__ == '__main__':
    main()
