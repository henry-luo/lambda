#!/usr/bin/env python3
"""
Compare latex.js and LaTeXML HTML outputs for the migrated fixtures.

This script analyzes structural differences between the two HTML outputs
to help unify the Lambda TeX pipeline output format.
"""

import os
import re
import sys
from pathlib import Path
from collections import defaultdict
from html.parser import HTMLParser

EXPECTED_DIR = Path("test/latexml/expected/latexjs")

class TagExtractor(HTMLParser):
    """Extract tags and their classes from HTML."""
    def __init__(self):
        super().__init__()
        self.tags = []
        self.classes = defaultdict(set)
        
    def handle_starttag(self, tag, attrs):
        self.tags.append(tag)
        attrs_dict = dict(attrs)
        if 'class' in attrs_dict:
            for cls in attrs_dict['class'].split():
                self.classes[tag].add(cls)
                
    def handle_endtag(self, tag):
        pass


def extract_structure(html: str) -> dict:
    """Extract structural information from HTML."""
    parser = TagExtractor()
    try:
        parser.feed(html)
    except:
        pass
    
    return {
        'tags': parser.tags,
        'tag_counts': {tag: parser.tags.count(tag) for tag in set(parser.tags)},
        'classes': {tag: list(classes) for tag, classes in parser.classes.items()},
    }


def extract_text(html: str) -> str:
    """Extract plain text from HTML."""
    # Remove tags
    text = re.sub(r'<[^>]+>', ' ', html)
    # Normalize whitespace
    text = re.sub(r'\s+', ' ', text).strip()
    return text


def compare_files(latexjs_path: Path, latexml_path: Path) -> dict:
    """Compare two HTML files and return differences."""
    latexjs_html = latexjs_path.read_text(encoding='utf-8').strip()
    latexml_html = latexml_path.read_text(encoding='utf-8').strip()
    
    latexjs_struct = extract_structure(latexjs_html)
    latexml_struct = extract_structure(latexml_html)
    
    latexjs_text = extract_text(latexjs_html)
    latexml_text = extract_text(latexml_html)
    
    return {
        'name': latexjs_path.stem.replace('.latexjs', ''),
        'latexjs': {
            'html': latexjs_html,
            'tags': latexjs_struct['tag_counts'],
            'classes': latexjs_struct['classes'],
            'text': latexjs_text,
        },
        'latexml': {
            'html': latexml_html,
            'tags': latexml_struct['tag_counts'],
            'classes': latexml_struct['classes'],
            'text': latexml_text,
        },
        'text_match': latexjs_text == latexml_text,
        'structure_match': latexjs_struct['tag_counts'] == latexml_struct['tag_counts'],
    }


def main():
    print("Comparing latex.js vs LaTeXML HTML outputs")
    print("=" * 60)
    print()
    
    # Collect all fixture pairs
    pairs = []
    
    for category_dir in sorted(EXPECTED_DIR.iterdir()):
        if not category_dir.is_dir():
            continue
            
        for latexjs_file in sorted(category_dir.glob("*.latexjs.html")):
            latexml_file = latexjs_file.with_suffix('').with_suffix('.latexml.html')
            
            if latexml_file.exists():
                pairs.append((latexjs_file, latexml_file))
    
    print(f"Found {len(pairs)} fixture pairs to compare")
    print()
    
    # Analyze differences
    text_matches = 0
    struct_matches = 0
    
    # Track class mappings
    class_mappings = defaultdict(lambda: defaultdict(int))  # latexjs_class -> latexml_class -> count
    tag_mappings = defaultdict(lambda: defaultdict(int))  # latexjs pattern -> latexml pattern -> count
    
    # Examples of differences
    diff_examples = []
    
    for latexjs_path, latexml_path in pairs:
        result = compare_files(latexjs_path, latexml_path)
        
        if result['text_match']:
            text_matches += 1
        
        if result['structure_match']:
            struct_matches += 1
        else:
            if len(diff_examples) < 10:
                diff_examples.append(result)
        
        # Track class mappings
        for tag, classes in result['latexjs']['classes'].items():
            for cls in classes:
                for ltx_cls in result['latexml']['classes'].get(tag, []):
                    class_mappings[cls][ltx_cls] += 1
    
    # Print summary
    print(f"Text content matches: {text_matches}/{len(pairs)} ({100*text_matches/len(pairs):.1f}%)")
    print(f"Structure matches:    {struct_matches}/{len(pairs)} ({100*struct_matches/len(pairs):.1f}%)")
    print()
    
    # Print common class mappings
    print("Common class mappings (latex.js -> LaTeXML):")
    print("-" * 50)
    
    for latexjs_cls, latexml_counts in sorted(class_mappings.items()):
        if sum(latexml_counts.values()) >= 3:  # Only show common ones
            top_mappings = sorted(latexml_counts.items(), key=lambda x: -x[1])[:3]
            mappings_str = ", ".join(f"{cls}({count})" for cls, count in top_mappings)
            print(f"  {latexjs_cls:20s} -> {mappings_str}")
    
    print()
    
    # Print structural difference examples
    if diff_examples:
        print("Example structural differences:")
        print("-" * 50)
        
        for i, result in enumerate(diff_examples[:5], 1):
            print(f"\n{i}. {result['name']}")
            print(f"   latex.js tags: {result['latexjs']['tags']}")
            print(f"   LaTeXML  tags: {result['latexml']['tags']}")
            print(f"   latex.js: {result['latexjs']['html'][:100]}...")
            print(f"   LaTeXML:  {result['latexml']['html'][:100]}...")
    
    print()
    print("Key observations:")
    print("-" * 50)
    print("""
1. WRAPPER STRUCTURE:
   - latex.js: <div class="body"><p>...</p></div>
   - LaTeXML:  <div class="ltx_para"><p class="ltx_p">...</p></div>
   
2. CLASS NAMING:
   - latex.js uses simple names: body, bf, it, tt, etc.
   - LaTeXML uses ltx_ prefix: ltx_para, ltx_text, ltx_font_bold, etc.
   
3. SEMANTIC DIFFERENCES:
   - latex.js: <span class="bf">
   - LaTeXML:  <span class="ltx_text ltx_font_bold">
   
4. PARAGRAPH HANDLING:
   - latex.js: <p>content</p>
   - LaTeXML:  <p class="ltx_p">content</p>

RECOMMENDATION:
Lambda should output HTML in a format that can be normalized to either:
- Use --legacy mode to match latex.js output format
- Use --semantic mode to match LaTeXML output format (with ltx_ classes)
- Use --modern mode for semantic HTML5 (<strong>, <em>, etc.)
""")


if __name__ == '__main__':
    main()
