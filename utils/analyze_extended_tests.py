#!/usr/bin/env python3
"""
Simple Extended Test Analyzer - Structured diff analysis

This script runs extended tests individually, captures each failure,
and performs structured analysis to identify major issue categories.
"""

import subprocess
import re
import json
from collections import defaultdict, Counter
from typing import Dict, List, Tuple
import sys
import os
from pathlib import Path
import difflib

def get_test_list() -> List[str]:
    """Get list of all extended tests"""
    result = subprocess.run(
        ['./test/test_latex_html_extended.exe', '--gtest_list_tests'],
        cwd='/Users/henryluo/Projects/Jubily',
        capture_output=True,
        text=True
    )
    
    tests = []
    suite = None
    for line in result.stdout.split('\n'):
        if line and not line.startswith(' '):
            suite = line.strip()
        elif line.startswith('  '):
            test_name = line.strip()
            # Strip the "# GetParam() = ..." part for parameterized tests
            if ' #' in test_name:
                test_name = test_name.split(' #')[0].strip()
            if suite and test_name:
                tests.append(f"{suite}{test_name}")
    
    return tests

def run_single_test(test_name: str) -> str:
    """Run a single test and capture output"""
    result = subprocess.run(
        ['./test/test_latex_html_extended.exe', f'--gtest_filter={test_name}'],
        cwd='/Users/henryluo/Projects/Jubily',
        capture_output=True,
        text=False
    )
    return result.stdout.decode('utf-8', errors='replace') + result.stderr.decode('utf-8', errors='replace')

def extract_test_info(output: str) -> dict:
    """Extract test information from output"""
    info = {
        'passed': '[  FAILED  ]' not in output and 'FAILED' not in output,
        'latex': '',
        'expected': '',
        'actual': '',
        'test_id': '',
        'category': ''
    }
    
    # Extract test name/category
    test_match = re.search(r'RUN\s+\]\s+\S+/(\w+)', output)
    if test_match:
        full_name = test_match.group(1)
        info['test_name'] = full_name
        # Extract category (e.g., whitespace_tex_1 -> whitespace)
        cat_match = re.search(r'(\w+)_tex_\d+', full_name)
        if cat_match:
            info['category'] = cat_match.group(1)
    
    # Extract test ID
    id_match = re.search(r'ID:\s*(\d+)', output)
    if id_match:
        info['test_id'] = id_match.group(1)
    
    # Extract LaTeX
    latex_match = re.search(r'LaTeX Source:\s*-+\s*(.+?)\s*Expected HTML:', output, re.DOTALL)
    if latex_match:
        info['latex'] = latex_match.group(1).strip()
    
    # Extract expected HTML
    exp_match = re.search(r'Expected HTML:\s*-+\s*(.+?)\s*Actual HTML:', output, re.DOTALL)
    if exp_match:
        info['expected'] = exp_match.group(1).strip()
    
    # Extract actual HTML
    act_match = re.search(r'Actual HTML:\s*-+\s*(.+?)\s*(?:Differences:|$)', output, re.DOTALL)
    if act_match:
        info['actual'] = act_match.group(1).strip()
    
    return info

def analyze_html_diff(expected: str, actual: str) -> Tuple[List[dict], str]:
    """Simple diff analysis - returns issues and diff text"""
    issues = []
    
    # Generate unified diff
    diff_lines = list(difflib.unified_diff(
        expected.splitlines(keepends=True),
        actual.splitlines(keepends=True),
        fromfile='expected',
        tofile='actual',
        lineterm=''
    ))
    diff_text = ''.join(diff_lines) if diff_lines else ''
    
    # Normalize for comparison
    exp_clean = re.sub(r'\s+', ' ', expected).strip().lower()
    act_clean = re.sub(r'\s+', ' ', actual).strip().lower()
    
    # Check for missing/extra tags
    exp_tags = re.findall(r'</?(\w+)[^>]*>', exp_clean)
    act_tags = re.findall(r'</?(\w+)[^>]*>', act_clean)
    
    exp_tag_counts = Counter(exp_tags)
    act_tag_counts = Counter(act_tags)
    
    for tag, exp_count in exp_tag_counts.items():
        act_count = act_tag_counts.get(tag, 0)
        if act_count < exp_count:
            issues.append({
                'type': 'missing_tag',
                'tag': tag,
                'count': exp_count - act_count
            })
        elif act_count > exp_count:
            issues.append({
                'type': 'extra_tag',
                'tag': tag,
                'count': act_count - exp_count
            })
    
    for tag in act_tag_counts:
        if tag not in exp_tag_counts:
            issues.append({
                'type': 'unexpected_tag',
                'tag': tag,
                'count': act_tag_counts[tag]
            })
    
    # Check for HTML entities
    exp_entities = re.findall(r'&(\w+);', expected)
    act_entities = re.findall(r'&(\w+);', actual)
    
    exp_entity_counts = Counter(exp_entities)
    act_entity_counts = Counter(act_entities)
    
    for entity, exp_count in exp_entity_counts.items():
        act_count = act_entity_counts.get(entity, 0)
        if act_count < exp_count:
            issues.append({
                'type': 'missing_entity',
                'entity': entity,
                'count': exp_count - act_count
            })
    
    # Check for specific patterns
    if '&nbsp;' in expected and '&nbsp;' not in actual:
        issues.append({'type': 'nbsp_not_converted', 'pattern': '~ or space'})
    
    if '<a' in exp_clean and '<a' not in act_clean:
        issues.append({'type': 'missing_links', 'pattern': '<a> tags'})
    
    if 'class=' in exp_clean and 'class=' not in act_clean:
        issues.append({'type': 'missing_classes', 'pattern': 'CSS classes'})
    
    # Check content differences
    if exp_clean != act_clean:
        # Extract text content only
        exp_text = re.sub(r'<[^>]+>', '', exp_clean)
        act_text = re.sub(r'<[^>]+>', '', act_clean)
        
        if exp_text != act_text:
            issues.append({
                'type': 'content_diff',
                'expected_text': exp_text[:100],
                'actual_text': act_text[:100]
            })
    
    return issues, diff_text

def main():
    # Create output directory
    output_dir = Path('/Users/henryluo/Projects/Jubily/test_output')
    output_dir.mkdir(exist_ok=True)
    
    print("Getting test list...")
    tests = get_test_list()
    print(f"Found {len(tests)} tests")
    
    failures = []
    passed_tests = []
    issue_summary = defaultdict(int)
    category_summary = defaultdict(list)
    
    print("\nRunning tests individually...")
    for i, test in enumerate(tests, 1):
        print(f"  [{i}/{len(tests)}] {test}...", end=' ', flush=True)
        output = run_single_test(test)
        info = extract_test_info(output)
        
        if not info['passed']:
            print("FAIL")
            issues, diff_text = analyze_html_diff(info['expected'], info['actual'])
            info['issues'] = issues
            info['diff'] = diff_text
            failures.append(info)
            
            # Update summaries
            if info.get('category'):
                category_summary[info['category']].append(info)
            
            for issue in issues:
                issue_type = issue['type']
                issue_summary[issue_type] += 1
        else:
            print("PASS")
            # Store passed test info too (with diff showing they're identical)
            _, diff_text = analyze_html_diff(info['expected'], info['actual'])
            info['diff'] = diff_text if diff_text else "No differences (test passed)"
            passed_tests.append(info)
    
    # Generate report
    print("\n" + "="*80)
    print("EXTENDED TEST ANALYSIS REPORT")
    print("="*80)
    print(f"\nTotal tests: {len(tests)}")
    print(f"Failed: {len(failures)}")
    print(f"Passed: {len(tests) - len(failures)}")
    
    print("\n" + "-"*80)
    print("FAILURES BY CATEGORY")
    print("-"*80)
    for category in sorted(category_summary.keys()):
        count = len(category_summary[category])
        print(f"  {category:20s}: {count:3d} failures")
    
    print("\n" + "-"*80)
    print("ISSUE TYPE FREQUENCY")
    print("-"*80)
    for issue_type in sorted(issue_summary.keys(), key=issue_summary.get, reverse=True):
        count = issue_summary[issue_type]
        print(f"  {issue_type:30s}: {count:3d} occurrences")
    
    print("\n" + "-"*80)
    print("DETAILED ISSUE PATTERNS")
    print("-"*80)
    
    # Analyze specific patterns
    missing_tags = defaultdict(int)
    missing_entities = defaultdict(int)
    
    for failure in failures:
        for issue in failure.get('issues', []):
            if issue['type'] == 'missing_tag':
                missing_tags[issue['tag']] += issue.get('count', 1)
            elif issue['type'] == 'missing_entity':
                missing_entities[issue['entity']] += issue.get('count', 1)
    
    if missing_tags:
        print("\nMost commonly missing tags:")
        for tag, count in sorted(missing_tags.items(), key=lambda x: x[1], reverse=True)[:10]:
            print(f"  <{tag}>: {count} occurrences")
    
    if missing_entities:
        print("\nMost commonly missing entities:")
        for entity, count in sorted(missing_entities.items(), key=lambda x: x[1], reverse=True)[:10]:
            print(f"  &{entity};: {count} occurrences")
    
    # Show sample failures by category
    print("\n" + "-"*80)
    print("SAMPLE FAILURES BY CATEGORY")
    print("-"*80)
    
    for category in sorted(category_summary.keys())[:5]:
        failures_in_cat = category_summary[category]
        print(f"\n{category.upper()} (showing first 2 of {len(failures_in_cat)}):")
        for failure in failures_in_cat[:2]:
            print(f"\n  Test: {failure.get('test_name', 'unknown')}")
            print(f"  LaTeX: {failure.get('latex', '')[:70]}...")
            print(f"  Issues: {len(failure.get('issues', []))}")
            for issue in failure.get('issues', [])[:3]:
                print(f"    - {issue['type']}: {issue}")
    
    # Generate recommendations
    print("\n" + "="*80)
    print("RECOMMENDED PRIORITIES")
    print("="*80)
    
    priorities = []
    
    nbsp_count = issue_summary.get('nbsp_not_converted', 0)
    link_count = issue_summary.get('missing_links', 0)
    tag_count = sum(v for k, v in issue_summary.items() if 'tag' in k)
    entity_count = sum(v for k, v in issue_summary.items() if 'entity' in k)
    
    if nbsp_count > 0:
        priorities.append((nbsp_count, "Priority 1", 
                         "Implement bare tilde (~) to &nbsp; conversion",
                         "Quick fix, affects many tests"))
    
    if link_count > 0:
        priorities.append((link_count, "Priority 2",
                         "Implement <a> tag generation for \\ref{}",
                         "Label/reference system enhancement"))
    
    if entity_count > 10:
        priorities.append((entity_count, "Priority 3",
                         f"Fix HTML entity handling ({sum(missing_entities.values())} missing entities)",
                         "Affects special characters and symbols"))
    
    if tag_count > 10:
        priorities.append((tag_count, "Priority 4",
                         f"Fix HTML structure issues ({len(missing_tags)} tag types)",
                         "Various formatting commands"))
    
    print()
    for count, priority, description, notes in sorted(priorities, key=lambda x: x[0], reverse=True):
        print(f"{priority}: {description}")
        print(f"  → Affected: {count} test failures")
        print(f"  → Notes: {notes}\n")
    
    # Save detailed JSON with all test results
    json_file = output_dir / 'extended_test_analysis.json'
    with open(json_file, 'w') as f:
        json.dump({
            'total': len(tests),
            'passed': len(passed_tests),
            'failed': len(failures),
            'failures': failures,
            'passed_tests': passed_tests,
            'category_summary': {k: len(v) for k, v in category_summary.items()},
            'issue_summary': dict(issue_summary)
        }, f, indent=2)
    
    print(f"\nJSON results saved to: {json_file}")
    
    # Generate detailed text report with diffs for each test
    report_file = output_dir / 'detailed_test_report.txt'
    with open(report_file, 'w') as f:
        f.write("="*80 + "\n")
        f.write("EXTENDED TEST ANALYSIS - DETAILED REPORT\n")
        f.write("="*80 + "\n\n")
        f.write(f"Total tests: {len(tests)}\n")
        f.write(f"Passed: {len(passed_tests)}\n")
        f.write(f"Failed: {len(failures)}\n\n")
        
        # Write failed tests with full diffs
        if failures:
            f.write("="*80 + "\n")
            f.write("FAILED TESTS (with diffs)\n")
            f.write("="*80 + "\n\n")
            
            for i, failure in enumerate(failures, 1):
                f.write(f"\n{'='*80}\n")
                f.write(f"FAILURE #{i}: {failure.get('test_name', 'unknown')}\n")
                f.write(f"{'='*80}\n")
                f.write(f"Category: {failure.get('category', 'unknown')}\n")
                f.write(f"Test ID: {failure.get('test_id', 'unknown')}\n\n")
                
                f.write("LaTeX Source:\n")
                f.write("-" * 80 + "\n")
                f.write(failure.get('latex', 'N/A') + "\n\n")
                
                f.write("Expected HTML:\n")
                f.write("-" * 80 + "\n")
                f.write(failure.get('expected', 'N/A') + "\n\n")
                
                f.write("Actual HTML (Lambda):\n")
                f.write("-" * 80 + "\n")
                f.write(failure.get('actual', 'N/A') + "\n\n")
                
                f.write("Unified Diff:\n")
                f.write("-" * 80 + "\n")
                if failure.get('diff'):
                    f.write(failure['diff'] + "\n")
                else:
                    f.write("(No diff generated)\n")
                f.write("\n")
                
                f.write("Issues Detected:\n")
                f.write("-" * 80 + "\n")
                for issue in failure.get('issues', []):
                    f.write(f"  - {issue.get('type', 'unknown')}: {issue}\n")
                f.write("\n")
        
        # Write passed tests
        if passed_tests:
            f.write("\n" + "="*80 + "\n")
            f.write("PASSED TESTS\n")
            f.write("="*80 + "\n\n")
            
            for i, test in enumerate(passed_tests, 1):
                f.write(f"\n{'-'*80}\n")
                f.write(f"PASS #{i}: {test.get('test_name', 'unknown')}\n")
                f.write(f"{'-'*80}\n")
                f.write(f"Category: {test.get('category', 'unknown')}\n\n")
                
                f.write("LaTeX Source:\n")
                f.write(test.get('latex', 'N/A') + "\n\n")
                
                f.write("HTML Output:\n")
                f.write(test.get('expected', 'N/A') + "\n\n")
    
    print(f"Detailed text report saved to: {report_file}")
    print(f"\nAll outputs saved to: {output_dir}/")

if __name__ == '__main__':
    main()
