#!/usr/bin/env python3
"""
generate_hybrid_refs.py - Generate hybrid Lambda HTML references

This script transforms HTML output to Lambda's hybrid format (.html)
based on the mapping decisions in vibe/Latex_Html_Mapping.md.

Sources:
- *.latexjs.html (latex.js output) -> *.html
- *.latexml.html (LaTeXML output) -> *.html (if no latexjs.html exists)

Key transformations for latex.js:
- <div class="body"> -> <article class="latex-document">
- <span class="bf"> -> <strong>
- <span class="it"> -> <em>
- <span class="tt"> -> <code>
- <div class="list quote"> -> <blockquote class="quote">
- etc.

Key transformations for LaTeXML:
- Extract body content, remove ltx_ wrapper divs
- <span class="ltx_text ltx_font_bold"> -> <strong>
- <span class="ltx_text ltx_font_italic"> -> <em>
- <span class="ltx_text ltx_font_typewriter"> -> <code>
- <section class="ltx_section"> -> <section class="section">
- <blockquote class="ltx_quote"> -> <blockquote class="quote">
- etc.

Usage:
    python3 utils/generate_hybrid_refs.py [--clean] [--verbose] [--test=<pattern>]
"""

import os
import re
import sys
import argparse
from pathlib import Path


def transform_latexjs_to_hybrid(html: str) -> str:
    """Transform latex.js HTML to Lambda hybrid format."""
    result = html
    
    # Document wrapper: <div class="body"> -> <article class="latex-document">
    result = re.sub(
        r'<div class="body">',
        '<article class="latex-document">',
        result
    )
    result = re.sub(
        r'</div>\s*$',
        '</article>',
        result.rstrip()
    )
    
    # Text formatting: semantic HTML5 tags
    result = re.sub(r'<span class="bf">(.*?)</span>', r'<strong>\1</strong>', result)
    result = re.sub(r'<span class="it">(.*?)</span>', r'<em>\1</em>', result)
    result = re.sub(r'<span class="tt">(.*?)</span>', r'<code>\1</code>', result)
    result = re.sub(r'<span class="underline">(.*?)</span>', r'<u>\1</u>', result)
    result = re.sub(r'<span class="sout">(.*?)</span>', r'<s>\1</s>', result)
    
    # Quote environments: div -> blockquote
    def replace_quote_env(html: str, env_class: str) -> str:
        pattern = f'<div class="list {env_class}">'
        replacement = f'<blockquote class="{env_class}">'
        
        result = html
        while pattern in result:
            start = result.find(pattern)
            if start == -1:
                break
            
            after_tag = start + len(pattern)
            depth = 1
            pos = after_tag
            while depth > 0 and pos < len(result):
                if result[pos:pos+5] == '<div ':
                    depth += 1
                    pos += 5
                elif result[pos:pos+4] == '<div':
                    depth += 1
                    pos += 4
                elif result[pos:pos+6] == '</div>':
                    depth -= 1
                    if depth == 0:
                        result = result[:start] + replacement + result[after_tag:pos] + '</blockquote>' + result[pos+6:]
                        break
                    pos += 6
                else:
                    pos += 1
            
            if depth > 0:
                result = result[:start] + replacement + result[after_tag:]
        
        return result
    
    result = replace_quote_env(result, 'quote')
    result = replace_quote_env(result, 'quotation')
    result = replace_quote_env(result, 'verse')
    
    # Lists: update class names
    result = re.sub(r'<ul class="list">', '<ul class="itemize">', result)
    result = re.sub(r'<ol class="list">', '<ol class="enumerate">', result)
    result = re.sub(r'<dl class="list">', '<dl class="description">', result)
    
    # Simplify item labels - remove span.itemlabel wrapper
    result = re.sub(
        r'<span class="itemlabel"><span class="hbox llap">[^<]*</span></span>',
        '',
        result
    )
    result = re.sub(
        r'<span class="itemlabel"><span class="hbox">[^<]*</span></span>',
        '',
        result
    )
    
    # Logos: update class names
    result = re.sub(r'<span class="tex">', '<span class="tex-logo">', result)
    result = re.sub(r'<span class="latex">', '<span class="latex-logo">', result)
    
    # Verbatim: code class="tt" -> code (no class needed)
    result = re.sub(r'<code class="tt">', '<code>', result)
    
    # Center/alignment environments
    result = re.sub(r'<div class="list center">', '<div class="center">', result)
    result = re.sub(r'<div class="list flushleft">', '<div class="flushleft">', result)
    result = re.sub(r'<div class="list flushright">', '<div class="flushright">', result)
    
    # Clean up empty spans from hbox
    result = re.sub(r'<span class="hbox"><span></span></span>', '', result)
    result = re.sub(r'<span class="hbox llap"><span></span></span>', '', result)
    
    return result


def transform_latexml_to_hybrid(html: str) -> str:
    """Transform LaTeXML HTML to Lambda hybrid format."""
    result = html
    
    # Extract body content if full document
    body_match = re.search(r'<body[^>]*>(.*)</body>', result, re.DOTALL)
    if body_match:
        result = body_match.group(1).strip()
    
    # Remove page wrapper divs
    result = re.sub(r'<div class="ltx_page_main">\s*', '', result)
    result = re.sub(r'<div class="ltx_page_content">\s*', '', result)
    result = re.sub(r'</div>\s*</div>\s*$', '', result)
    
    # Document wrapper: <article class="ltx_document..."> -> <article class="latex-document">
    result = re.sub(
        r'<article class="ltx_document[^"]*">',
        '<article class="latex-document">',
        result
    )
    
    # Remove ltx_para wrapper divs, keep just the content
    result = re.sub(r'<div\s+class="ltx_para">\s*', '', result)
    # Carefully remove closing </div> that belonged to ltx_para
    # This is tricky - for now we handle simple cases
    
    # Section classes: ltx_section -> section, ltx_subsection -> subsection, etc.
    result = re.sub(r'<section\s+class="ltx_chapter">', '<section class="chapter">', result)
    result = re.sub(r'<section\s+class="ltx_section">', '<section class="section">', result)
    result = re.sub(r'<section\s+class="ltx_subsection">', '<section class="subsection">', result)
    result = re.sub(r'<section\s+class="ltx_subsubsection">', '<section class="subsubsection">', result)
    
    # Headers: simplify ltx_title classes
    result = re.sub(r'<h1 class="ltx_title ltx_title_document">', '<h1 class="document-title">', result)
    result = re.sub(r'<h2 class="ltx_title ltx_title_section">', '<h2 class="section-title">', result)
    result = re.sub(r'<h3 class="ltx_title ltx_title_subsection">', '<h3 class="subsection-title">', result)
    result = re.sub(r'<h4 class="ltx_title ltx_title_subsubsection">', '<h4 class="subsubsection-title">', result)
    
    # Section number tags
    result = re.sub(r'<span class="ltx_tag ltx_tag_section">', '<span class="section-number">', result)
    result = re.sub(r'<span class="ltx_tag ltx_tag_subsection">', '<span class="subsection-number">', result)
    
    # Text formatting: semantic HTML5 tags
    result = re.sub(r'<span class="ltx_text ltx_font_bold">([^<]*)</span>', r'<strong>\1</strong>', result)
    result = re.sub(r'<span class="ltx_text ltx_font_italic">([^<]*)</span>', r'<em>\1</em>', result)
    result = re.sub(r'<span class="ltx_text ltx_font_typewriter">([^<]*)</span>', r'<code>\1</code>', result)
    
    # Paragraphs: <p class="ltx_p"> -> <p>
    result = re.sub(r'<p class="ltx_p">', '<p>', result)
    result = re.sub(r'<p class="ltx_p ([^"]+)">', r'<p class="\1">', result)
    
    # Quote environments
    result = re.sub(r'<blockquote class="ltx_quote">', '<blockquote class="quote">', result)
    result = re.sub(r'<blockquote class="ltx_quote ltx_role_verse">', '<blockquote class="verse">', result)
    
    # Lists
    result = re.sub(r'<ul class="ltx_itemize">', '<ul class="itemize">', result)
    result = re.sub(r'<ol class="ltx_enumerate">', '<ol class="enumerate">', result)
    result = re.sub(r'<dl class="ltx_description">', '<dl class="description">', result)
    result = re.sub(r'<li class="ltx_item"[^>]*>', '<li>', result)
    
    # Remove ltx_tag spans (item labels handled by CSS)
    result = re.sub(r'<span class="ltx_tag[^"]*">[^<]*</span>\s*', '', result)
    
    # Verbatim/code
    result = re.sub(r'<code class="ltx_verbatim ltx_font_typewriter">', '<code>', result)
    result = re.sub(r'<pre class="ltx_verbatim">', '<pre class="verbatim">', result)
    
    # Line breaks
    result = re.sub(r'<br class="ltx_break">', '<br>', result)
    
    # Links: simplify class
    result = re.sub(r'<a href="([^"]*)" class="ltx_ref[^"]*">', r'<a href="\1">', result)
    result = re.sub(r'<a href="([^"]*)" title="[^"]*" class="ltx_ref[^"]*">', r'<a href="\1">', result)
    
    # Remove dates div
    result = re.sub(r'<div class="ltx_dates">[^<]*</div>\s*', '', result)
    
    # Clean up remaining ltx_ classes (generic cleanup)
    result = re.sub(r' class="ltx_[^"]*"', '', result)
    
    # Clean up extra whitespace
    result = re.sub(r'\n\s*\n', '\n', result)
    result = result.strip()
    
    return result


def process_file(source_path: Path, output_path: Path, is_latexjs: bool, verbose: bool = False) -> bool:
    """Process a single file, transforming to hybrid format."""
    try:
        with open(source_path, 'r', encoding='utf-8') as f:
            source_html = f.read()
        
        if is_latexjs:
            hybrid_html = transform_latexjs_to_hybrid(source_html)
        else:
            hybrid_html = transform_latexml_to_hybrid(source_html)
        
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with open(output_path, 'w', encoding='utf-8') as f:
            f.write(hybrid_html)
        
        if verbose:
            src_type = "latexjs" if is_latexjs else "latexml"
            print(f"  [{src_type}] {output_path}")
        
        return True
    except Exception as e:
        print(f"  ERROR: {source_path}: {e}")
        return False


def main():
    parser = argparse.ArgumentParser(description='Generate hybrid Lambda HTML references')
    parser.add_argument('--clean', action='store_true', help='Remove existing .html files first')
    parser.add_argument('--verbose', action='store_true', help='Show detailed output')
    parser.add_argument('--test', type=str, default='', help='Only process files matching pattern')
    parser.add_argument('--latexjs-only', action='store_true', help='Only process latexjs directory')
    args = parser.parse_args()
    
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    expected_dir = project_root / 'test' / 'latexml' / 'expected'
    
    if not expected_dir.exists():
        print(f"Error: Expected directory not found: {expected_dir}")
        sys.exit(1)
    
    print("Lambda Hybrid HTML Reference Generator")
    print("======================================")
    print(f"Source: {expected_dir}")
    print()
    
    # Find all source files
    latexjs_files = list(expected_dir.rglob('*.latexjs.html'))
    latexml_files = list(expected_dir.rglob('*.latexml.html'))
    
    if args.test:
        latexjs_files = [f for f in latexjs_files if args.test in str(f)]
        latexml_files = [f for f in latexml_files if args.test in str(f)]
    
    if args.latexjs_only:
        latexml_files = [f for f in latexml_files if '/latexjs/' in str(f)]
    
    print(f"Found {len(latexjs_files)} latex.js files")
    print(f"Found {len(latexml_files)} LaTeXML files")
    print()
    
    # Clean existing .html files if requested
    if args.clean:
        print("Cleaning existing hybrid .html files...")
        for f in expected_dir.rglob('*.html'):
            if not str(f).endswith('.latexjs.html') and not str(f).endswith('.latexml.html'):
                f.unlink()
                if args.verbose:
                    print(f"  Removed: {f}")
        print()
    
    # Build set of stems that have latexjs.html (prefer latexjs over latexml)
    latexjs_stems = set()
    for f in latexjs_files:
        stem = str(f).replace('.latexjs.html', '')
        latexjs_stems.add(stem)
    
    success = 0
    failed = 0
    skipped = 0
    
    # Process latex.js files
    print("Processing latex.js files...")
    for source_path in sorted(latexjs_files):
        stem = str(source_path).replace('.latexjs.html', '')
        output_path = Path(stem + '.html')
        
        if process_file(source_path, output_path, is_latexjs=True, verbose=args.verbose):
            success += 1
        else:
            failed += 1
    
    # Process LaTeXML files (only if no latexjs.html exists for same stem)
    print("Processing LaTeXML files...")
    for source_path in sorted(latexml_files):
        stem = str(source_path).replace('.latexml.html', '')
        
        # Skip if latexjs.html exists (prefer latexjs)
        if stem in latexjs_stems:
            skipped += 1
            if args.verbose:
                print(f"  [skip] {source_path.name} (latexjs preferred)")
            continue
        
        output_path = Path(stem + '.html')
        
        if process_file(source_path, output_path, is_latexjs=False, verbose=args.verbose):
            success += 1
        else:
            failed += 1
    
    print()
    print("Summary")
    print("-------")
    print(f"Processed: {success + failed}")
    print(f"Success:   {success}")
    print(f"Failed:    {failed}")
    print(f"Skipped:   {skipped} (latexjs preferred over latexml)")


if __name__ == '__main__':
    main()
