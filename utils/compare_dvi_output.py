#!/usr/bin/env python3
"""
compare_dvi_output.py - Compare Lambda typesetting output against reference DVI files.

This script parses 'dvitype' output from reference TeX-generated DVI files and compares
glyph positions against Lambda's JSON output to validate typesetting accuracy.

Usage:
    python3 compare_dvi_output.py <lambda_output.json> <reference.dvi> [options]

Options:
    --tolerance <pt>    Position tolerance in points (default: 1.0)
    --verbose           Show detailed comparison output
    --page <n>          Compare only page n (1-indexed)
    --json              Output result as JSON
"""

import argparse
import json
import subprocess
import sys
import os
import re
from dataclasses import dataclass, field
from typing import List, Optional, Tuple


# ============================================================================
# Data structures
# ============================================================================

@dataclass
class Glyph:
    codepoint: int
    x: float  # in points
    y: float  # in points
    font: str = ""


@dataclass
class Rule:
    x: float
    y: float
    width: float
    height: float


@dataclass
class Page:
    number: int
    width: float
    height: float
    glyphs: List[Glyph] = field(default_factory=list)
    rules: List[Rule] = field(default_factory=list)


@dataclass
class ComparisonResult:
    passed: bool = False
    total_glyphs: int = 0
    matching_glyphs: int = 0
    mismatched_glyphs: int = 0
    missing_glyphs: int = 0
    extra_glyphs: int = 0
    max_h_error: float = 0.0
    max_v_error: float = 0.0
    avg_h_error: float = 0.0
    avg_v_error: float = 0.0
    mismatches: List[dict] = field(default_factory=list)


# ============================================================================
# DVI parsing (via dvitype)
# ============================================================================

# Scaled points to points conversion
SP_PER_PT = 65536.0


def sp_to_pt(sp: int) -> float:
    """Convert scaled points to points."""
    return sp / SP_PER_PT


def parse_dvitype_output(dvitype_output: str) -> List[Page]:
    """
    Parse output from 'dvitype' command to extract glyph positions.

    The dvitype output format includes:
    - 'setchar<n>' or 'set<n>' for setting characters
    - 'setrule height <h>, width <w>' for rules
    - 'h:=<n>' and 'v:=<n>' for position updates
    - 'push' and 'pop' for state stack
    - 'right<n>' and 'down<n>' for relative movements
    """
    pages = []
    current_page = None
    h, v = 0, 0
    stack = []

    # position regex patterns
    pos_h_re = re.compile(r'h:=(\-?\d+)')
    pos_v_re = re.compile(r'v:=(\-?\d+)')
    right_re = re.compile(r'right(\d)?\s+(\-?\d+)')
    down_re = re.compile(r'down(\d)?\s+(\-?\d+)')
    setchar_re = re.compile(r'setchar(\d+)')
    set_re = re.compile(r'set(\d)\s+(\d+)')
    setrule_re = re.compile(r'setrule height (\-?\d+), width (\-?\d+)')
    font_re = re.compile(r'selectfont (\S+)')
    bop_re = re.compile(r'\[(\d+)\]')

    current_font = ""

    for line in dvitype_output.splitlines():
        line = line.strip()

        # beginning of page
        if line.startswith('['):
            match = bop_re.search(line)
            if match:
                page_num = int(match.group(1))
                current_page = Page(number=page_num, width=0, height=0)
                pages.append(current_page)
                h, v = 0, 0
                stack = []

        # end of page
        elif line.startswith('eop'):
            current_page = None
            h, v = 0, 0

        elif current_page is None:
            continue

        # position updates
        elif match := pos_h_re.search(line):
            h = int(match.group(1))

        elif match := pos_v_re.search(line):
            v = int(match.group(1))

        elif match := right_re.search(line):
            h += int(match.group(2))

        elif match := down_re.search(line):
            v += int(match.group(2))

        # push/pop
        elif 'push' in line:
            stack.append((h, v))

        elif 'pop' in line:
            if stack:
                h, v = stack.pop()

        # font selection
        elif match := font_re.search(line):
            current_font = match.group(1)

        # set character
        elif match := setchar_re.match(line):
            cp = int(match.group(1))
            glyph = Glyph(codepoint=cp, x=sp_to_pt(h), y=sp_to_pt(v), font=current_font)
            current_page.glyphs.append(glyph)

        elif match := set_re.match(line):
            cp = int(match.group(2))
            glyph = Glyph(codepoint=cp, x=sp_to_pt(h), y=sp_to_pt(v), font=current_font)
            current_page.glyphs.append(glyph)

        # set rule
        elif match := setrule_re.search(line):
            height = sp_to_pt(int(match.group(1)))
            width = sp_to_pt(int(match.group(2)))
            rule = Rule(x=sp_to_pt(h), y=sp_to_pt(v), width=width, height=height)
            current_page.rules.append(rule)

    return pages


def run_dvitype(dvi_path: str) -> str:
    """Run dvitype on a DVI file and return its output."""
    try:
        result = subprocess.run(
            ['dvitype', dvi_path],
            capture_output=True,
            text=True,
            check=True
        )
        return result.stdout
    except FileNotFoundError:
        print("Error: 'dvitype' not found. Install TeX distribution.", file=sys.stderr)
        sys.exit(1)
    except subprocess.CalledProcessError as e:
        print(f"Error running dvitype: {e.stderr}", file=sys.stderr)
        sys.exit(1)


def parse_dvi_file(dvi_path: str) -> List[Page]:
    """Parse a DVI file by running dvitype."""
    if not os.path.exists(dvi_path):
        print(f"Error: DVI file not found: {dvi_path}", file=sys.stderr)
        sys.exit(1)

    output = run_dvitype(dvi_path)
    return parse_dvitype_output(output)


# ============================================================================
# Lambda JSON parsing
# ============================================================================

def parse_lambda_json(json_path: str) -> List[Page]:
    """Parse Lambda's JSON output file."""
    if not os.path.exists(json_path):
        print(f"Error: JSON file not found: {json_path}", file=sys.stderr)
        sys.exit(1)

    with open(json_path, 'r') as f:
        data = json.load(f)

    pages = []

    # handle both single page and multi-page formats
    if isinstance(data, dict):
        if 'pages' in data:
            # multi-page format from typeset_result_to_json
            for page_data in data['pages']:
                page = parse_page_data(page_data)
                pages.append(page)
        elif 'glyphs' in data:
            # single page flat format
            page = parse_flat_page(data)
            pages.append(page)
        else:
            # TexBox tree format - need to extract glyphs
            page = extract_glyphs_from_box_tree(data)
            pages.append(page)

    return pages


def parse_page_data(data: dict) -> Page:
    """Parse a page from the multi-page format."""
    page = Page(
        number=data.get('page_number', 1),
        width=data.get('width', 0),
        height=data.get('height', 0)
    )

    if 'content' in data:
        # has TexBox tree content
        extract_glyphs_recursive(data['content'], 0, 0, page)

    return page


def parse_flat_page(data: dict) -> Page:
    """Parse a page from flat format with glyphs array."""
    page = Page(
        number=1,
        width=data.get('width', 0),
        height=data.get('height', 0)
    )

    for g in data.get('glyphs', []):
        glyph = Glyph(
            codepoint=g.get('c', g.get('codepoint', 0)),
            x=g.get('x', 0),
            y=g.get('y', 0)
        )
        page.glyphs.append(glyph)

    for r in data.get('rules', []):
        rule = Rule(
            x=r.get('x', 0),
            y=r.get('y', 0),
            width=r.get('w', r.get('width', 0)),
            height=r.get('h', r.get('height', 0))
        )
        page.rules.append(rule)

    return page


def extract_glyphs_from_box_tree(data: dict) -> Page:
    """Extract glyphs from a TexBox tree JSON."""
    page = Page(number=1, width=0, height=0)
    extract_glyphs_recursive(data, 0, 0, page)
    return page


def extract_glyphs_recursive(box: dict, x: float, y: float, page: Page):
    """Recursively extract glyphs from a TexBox JSON structure."""
    if not box or not isinstance(box, dict):
        return

    # adjust position
    x += box.get('x', 0)
    y += box.get('y', 0)

    box_type = box.get('type', '')

    if box_type == 'char':
        cp = box.get('codepoint', 0)
        glyph = Glyph(codepoint=cp, x=x, y=y)
        page.glyphs.append(glyph)

    elif box_type == 'rule':
        width = box.get('width', 0)
        height = box.get('height', 0) + box.get('depth', 0)
        rule = Rule(x=x, y=y, width=width, height=height)
        page.rules.append(rule)

    elif box_type in ('hbox', 'vbox'):
        children = box.get('children', [])
        if box_type == 'hbox':
            cx = 0
            for child in children:
                if child:
                    extract_glyphs_recursive(child, x + cx, y, page)
                    cx += child.get('width', 0)
        else:  # vbox
            cy = 0
            for child in children:
                if child:
                    cy += child.get('height', 0)
                    extract_glyphs_recursive(child, x, y + cy, page)
                    cy += child.get('depth', 0)

    elif box_type == 'fraction':
        if 'numerator' in box:
            num_shift = box.get('num_shift', 0)
            extract_glyphs_recursive(box['numerator'], x, y - num_shift, page)
        if 'denominator' in box:
            denom_shift = box.get('denom_shift', 0)
            extract_glyphs_recursive(box['denominator'], x, y + denom_shift, page)

    elif box_type == 'radical':
        if 'radicand' in box:
            extract_glyphs_recursive(box['radicand'], x, y, page)
        if 'index' in box:
            extract_glyphs_recursive(box['index'], x, y, page)


# ============================================================================
# Comparison logic
# ============================================================================

def compare_pages(
    lambda_page: Page,
    dvi_page: Page,
    tolerance: float = 1.0
) -> ComparisonResult:
    """Compare a Lambda output page against a DVI reference page."""
    result = ComparisonResult()

    # sort glyphs by position
    lambda_glyphs = sorted(lambda_page.glyphs, key=lambda g: (g.y, g.x))
    dvi_glyphs = sorted(dvi_page.glyphs, key=lambda g: (g.y, g.x))

    lambda_idx = 0
    dvi_idx = 0

    total_h_error = 0.0
    total_v_error = 0.0

    while lambda_idx < len(lambda_glyphs) and dvi_idx < len(dvi_glyphs):
        lg = lambda_glyphs[lambda_idx]
        dg = dvi_glyphs[dvi_idx]

        if lg.codepoint == dg.codepoint:
            h_err = abs(lg.x - dg.x)
            v_err = abs(lg.y - dg.y)

            if h_err <= tolerance and v_err <= tolerance:
                result.matching_glyphs += 1
            else:
                result.mismatched_glyphs += 1
                result.mismatches.append({
                    'index': result.total_glyphs,
                    'codepoint': lg.codepoint,
                    'char': chr(lg.codepoint) if 32 <= lg.codepoint < 127 else '?',
                    'ref_x': dg.x,
                    'ref_y': dg.y,
                    'out_x': lg.x,
                    'out_y': lg.y,
                    'h_err': h_err,
                    'v_err': v_err
                })

            total_h_error += h_err
            total_v_error += v_err
            result.max_h_error = max(result.max_h_error, h_err)
            result.max_v_error = max(result.max_v_error, v_err)

            lambda_idx += 1
            dvi_idx += 1

        else:
            # different codepoints - try to align
            # advance whichever has smaller position
            if (lg.y, lg.x) < (dg.y, dg.x):
                result.extra_glyphs += 1
                lambda_idx += 1
            else:
                result.missing_glyphs += 1
                dvi_idx += 1

        result.total_glyphs += 1

    # remaining glyphs
    result.extra_glyphs += len(lambda_glyphs) - lambda_idx
    result.missing_glyphs += len(dvi_glyphs) - dvi_idx
    result.total_glyphs = max(len(lambda_glyphs), len(dvi_glyphs))

    # compute averages
    compared = result.matching_glyphs + result.mismatched_glyphs
    if compared > 0:
        result.avg_h_error = total_h_error / compared
        result.avg_v_error = total_v_error / compared

    # determine pass/fail
    result.passed = (
        result.missing_glyphs == 0 and
        result.extra_glyphs == 0 and
        result.mismatched_glyphs == 0
    )

    return result


# ============================================================================
# Output formatting
# ============================================================================

def print_result(result: ComparisonResult, verbose: bool = False):
    """Print comparison result to stdout."""
    status = "✓ PASSED" if result.passed else "✗ FAILED"
    print(f"\n{status}")
    print(f"  Total glyphs:   {result.total_glyphs}")
    print(f"  Matching:       {result.matching_glyphs} ({100 * result.matching_glyphs / max(1, result.total_glyphs):.1f}%)")
    print(f"  Mismatched:     {result.mismatched_glyphs}")
    print(f"  Missing:        {result.missing_glyphs}")
    print(f"  Extra:          {result.extra_glyphs}")
    print(f"  Max H error:    {result.max_h_error:.2f} pt")
    print(f"  Max V error:    {result.max_v_error:.2f} pt")
    print(f"  Avg H error:    {result.avg_h_error:.2f} pt")
    print(f"  Avg V error:    {result.avg_v_error:.2f} pt")

    if verbose and result.mismatches:
        print(f"\nFirst {min(10, len(result.mismatches))} mismatches:")
        for m in result.mismatches[:10]:
            print(f"  [{m['index']}] char={m['codepoint']} '{m['char']}': "
                  f"ref=({m['ref_x']:.2f},{m['ref_y']:.2f}) "
                  f"out=({m['out_x']:.2f},{m['out_y']:.2f}) "
                  f"err=({m['h_err']:.2f},{m['v_err']:.2f})")


def result_to_json(result: ComparisonResult) -> str:
    """Convert comparison result to JSON string."""
    return json.dumps({
        'passed': result.passed,
        'total_glyphs': result.total_glyphs,
        'matching_glyphs': result.matching_glyphs,
        'mismatched_glyphs': result.mismatched_glyphs,
        'missing_glyphs': result.missing_glyphs,
        'extra_glyphs': result.extra_glyphs,
        'max_h_error': result.max_h_error,
        'max_v_error': result.max_v_error,
        'avg_h_error': result.avg_h_error,
        'avg_v_error': result.avg_v_error,
        'mismatches': result.mismatches[:100]  # limit mismatches
    }, indent=2)


# ============================================================================
# Main
# ============================================================================

def main():
    parser = argparse.ArgumentParser(
        description='Compare Lambda typesetting output against reference DVI files.'
    )
    parser.add_argument('lambda_json', help='Lambda output JSON file')
    parser.add_argument('reference_dvi', help='Reference DVI file from TeX')
    parser.add_argument('--tolerance', '-t', type=float, default=1.0,
                        help='Position tolerance in points (default: 1.0)')
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Show detailed comparison output')
    parser.add_argument('--page', '-p', type=int, default=1,
                        help='Page number to compare (1-indexed, default: 1)')
    parser.add_argument('--json', '-j', action='store_true',
                        help='Output result as JSON')

    args = parser.parse_args()

    # parse input files
    lambda_pages = parse_lambda_json(args.lambda_json)
    dvi_pages = parse_dvi_file(args.reference_dvi)

    if not lambda_pages:
        print("Error: No pages found in Lambda output", file=sys.stderr)
        sys.exit(1)

    if not dvi_pages:
        print("Error: No pages found in DVI file", file=sys.stderr)
        sys.exit(1)

    # get requested page
    page_idx = args.page - 1  # convert to 0-indexed
    if page_idx < 0 or page_idx >= len(lambda_pages):
        print(f"Error: Page {args.page} not found in Lambda output (has {len(lambda_pages)} pages)",
              file=sys.stderr)
        sys.exit(1)

    if page_idx >= len(dvi_pages):
        print(f"Error: Page {args.page} not found in DVI file (has {len(dvi_pages)} pages)",
              file=sys.stderr)
        sys.exit(1)

    # compare
    result = compare_pages(
        lambda_pages[page_idx],
        dvi_pages[page_idx],
        args.tolerance
    )

    # output
    if args.json:
        print(result_to_json(result))
    else:
        print(f"Comparing: {args.lambda_json} vs {args.reference_dvi}")
        print(f"Page: {args.page}, Tolerance: {args.tolerance} pt")
        print_result(result, args.verbose)

    # exit with appropriate code
    sys.exit(0 if result.passed else 1)


if __name__ == '__main__':
    main()
