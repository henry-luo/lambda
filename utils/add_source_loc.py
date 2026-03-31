#!/usr/bin/env python3
"""
Add source_loc() prefix to layout log messages within specified functions.

Pattern: log_debug("message", args) -> log_debug("%s message", var->source_loc(), args)

Uses character-by-character parsing to handle:
- Single-line log calls
- Multi-line format strings (concatenated "..." "...")
- Multi-line argument lists
- Escaped quotes within format strings
"""
import re
import sys
import os

LOG_FUNCS = {'log_debug', 'log_info', 'log_warn', 'log_error'}

def find_function_ranges(text, func_var_map):
    """Find function definition ranges and their corresponding variable expressions."""
    lines = text.split('\n')
    ranges = []  # (start_offset, end_offset, var_expr, func_name)
    
    for func_name, var_expr in func_var_map.items():
        # Find all definition sites of this function
        # Match lines that start with a return type (at column 0) followed by the function name
        pattern = re.compile(rf'(?:^|\n)([^\n]*\b{re.escape(func_name)}\s*\([^\n]*)')
        for m in pattern.finditer(text):
            line_text = m.group(1).strip()
            # Skip forward declarations and calls (must look like a definition)
            if line_text.endswith(';') or line_text.startswith('//'):
                continue
            # Check indentation - function defs start near column 0
            prev_nl = text.rfind('\n', 0, m.start(1))
            col = m.start(1) - prev_nl - 1 if prev_nl >= 0 else m.start(1)
            if col > 4:  # Skip deeply indented matches (function calls)
                continue
            # Also skip if this looks like a function call (no return type before name)
            # A function definition has a return type like 'void', 'static', 'int', etc.
            before_func = line_text[:line_text.find(func_name)].strip()
            if not before_func:
                # No return type - this is likely a function call
                continue
            
            if '--debug' in sys.argv:
                print(f"    FOUND {func_name} at col={col}, line_text={line_text[:80]}")
            
            # Find opening brace - search from start of the match (the { may be on the same line)
            pos = m.start(1)
            while pos < len(text) and text[pos] != '{':
                if text[pos] == ';':
                    break  # forward declaration
                pos += 1
            if pos >= len(text) or text[pos] != '{':
                continue
            
            # Find matching closing brace
            brace_start = pos
            depth = 1
            pos += 1
            in_str = False
            in_char = False
            in_line_comment = False
            in_block_comment = False
            while pos < len(text) and depth > 0:
                ch = text[pos]
                if in_line_comment:
                    if ch == '\n':
                        in_line_comment = False
                elif in_block_comment:
                    if ch == '*' and pos + 1 < len(text) and text[pos+1] == '/':
                        in_block_comment = False
                        pos += 1
                elif in_str:
                    if ch == '\\':
                        pos += 1  # skip escaped char
                    elif ch == '"':
                        in_str = False
                elif in_char:
                    if ch == '\\':
                        pos += 1
                    elif ch == "'":
                        in_char = False
                else:
                    if ch == '/' and pos + 1 < len(text):
                        if text[pos+1] == '/':
                            in_line_comment = True
                            pos += 1
                        elif text[pos+1] == '*':
                            in_block_comment = True
                            pos += 1
                    elif ch == '"':
                        in_str = True
                    elif ch == "'":
                        in_char = True
                    elif ch == '{':
                        depth += 1
                    elif ch == '}':
                        depth -= 1
                pos += 1
            
            ranges.append((brace_start, pos, var_expr, func_name))
    
    return ranges


def find_log_calls(text, start, end):
    """Find all log_debug/log_info/log_warn/log_error calls within a range."""
    calls = []  # (call_start, call_end, log_func_name)
    pos = start
    while pos < end:
        # Find next log function call
        found = False
        for func in LOG_FUNCS:
            pat = func + '('
            idx = text.find(pat, pos, end)
            if idx != -1 and (not found or idx < calls[-1][0] if calls else True):
                # Verify it's a standalone call (preceded by whitespace/newline)
                if idx > 0 and text[idx-1].isalnum():
                    continue
                found = True
                # Found the start, now find the matching )
                paren_start = idx + len(func)
                paren_depth = 0
                p = paren_start
                in_str = False
                in_char = False
                in_line_comment = False
                in_block_comment = False
                while p < end:
                    ch = text[p]
                    if in_line_comment:
                        if ch == '\n':
                            in_line_comment = False
                    elif in_block_comment:
                        if ch == '*' and p + 1 < end and text[p+1] == '/':
                            in_block_comment = False
                            p += 1
                    elif in_str:
                        if ch == '\\':
                            p += 1  # skip escaped
                        elif ch == '"':
                            in_str = False
                    elif in_char:
                        if ch == '\\':
                            p += 1
                        elif ch == "'":
                            in_char = False
                    else:
                        if ch == '/' and p + 1 < end:
                            if text[p+1] == '/':
                                in_line_comment = True
                            elif text[p+1] == '*':
                                in_block_comment = True
                        elif ch == '"':
                            in_str = True
                        elif ch == "'":
                            in_char = True
                        elif ch == '(':
                            paren_depth += 1
                        elif ch == ')':
                            paren_depth -= 1
                            if paren_depth == 0:
                                calls.append((idx, p + 1, func))
                                pos = p + 1
                                break
                    p += 1
                else:
                    pos = p
                if found:
                    break
        if not found:
            pos += 1
    return calls


def extract_format_end(text, fmt_start):
    """
    Given the position of the first " of the format string,
    find where the format string ends (handling concatenated "..." "..." segments).
    Returns (end_pos, has_args):
      - end_pos: position after the last closing " of the format string
      - has_args: True if there's a comma after the format string (more args follow)
    """
    pos = fmt_start
    while True:
        # We should be at the start of a "..."
        assert text[pos] == '"', f"Expected '\"' at pos {pos}, got '{text[pos]}'"
        pos += 1  # skip opening "
        # Find closing "
        while pos < len(text):
            if text[pos] == '\\':
                pos += 2
                continue
            if text[pos] == '"':
                pos += 1  # skip closing "
                break
            pos += 1
        
        # After closing ", skip whitespace/newlines
        saved_pos = pos
        while pos < len(text) and text[pos] in ' \t\n\r':
            pos += 1
        
        if pos < len(text) and text[pos] == '"':
            # Concatenated string literal, continue
            continue
        elif pos < len(text) and text[pos] == ',':
            return saved_pos, True
        else:
            return saved_pos, False


def transform_log_call(text, call_start, call_end, func_name, var_expr):
    """
    Transform a single log call to add source_loc() prefix.
    Returns the new text for this call, or None if no change needed.
    """
    call_text = text[call_start:call_end]
    
    # Skip if already has source_loc()
    if 'source_loc()' in call_text:
        return None
    
    # Find the opening ( and then the format string
    paren_pos = call_text.find('(')
    if paren_pos == -1:
        return None
    
    # Find the first " after (
    pos = paren_pos + 1
    while pos < len(call_text) and call_text[pos] in ' \t\n\r':
        pos += 1
    
    if pos >= len(call_text) or call_text[pos] != '"':
        return None  # No format string (maybe a variable?)
    
    fmt_start = pos
    
    # Skip separator/ruler lines
    content_start = fmt_start + 1
    remaining = call_text[content_start:content_start + 10]
    if remaining.startswith('===') or remaining.startswith('---') or remaining.startswith('###'):
        return None
    
    # Find end of format string (handling concatenated segments)
    abs_fmt_start = call_start + fmt_start
    abs_fmt_end, has_args = extract_format_end(text, abs_fmt_start)
    rel_fmt_end = abs_fmt_end - call_start
    
    # Build the new call:
    # 1. Insert "%s " at start of format string content
    # 2. Insert var_expr as first argument
    
    # Part 1: The log function name and opening paren
    prefix = call_text[:fmt_start + 1]  # up to and including the first "
    
    # Part 2: "%s " + rest of format string
    fmt_content = call_text[fmt_start + 1:rel_fmt_end]  # from after first " to end of format
    new_fmt = '%s ' + fmt_content
    
    # Part 3: Insert var_expr
    rest = call_text[rel_fmt_end:]  # everything after format string
    
    if has_args:
        # There's a comma and more args: "fmt", arg1, ... -> "%s fmt", var, arg1, ...
        # rest starts with some whitespace then ","
        # Find the comma
        comma_idx = rest.find(',')
        after_comma = rest[comma_idx + 1:]
        new_call = prefix + new_fmt + ', ' + var_expr + ',' + after_comma
    else:
        # No args: "fmt") -> "%s fmt", var)
        # rest should be something like ")"  or ") // comment" etc.
        close_paren_idx = rest.find(')')
        new_call = prefix + new_fmt + ', ' + var_expr + rest[close_paren_idx:]
    
    return new_call


def process_file(filepath, func_var_map, dry_run=False):
    """Process a single file, transforming log calls in the specified functions."""
    with open(filepath, 'r') as f:
        text = f.read()
    
    original = text
    
    # Find function ranges
    func_ranges = find_function_ranges(text, func_var_map)
    
    if not func_ranges:
        print(f"  {os.path.basename(filepath)}: no matching functions found")
        return 0
    
    # Collect all log calls across all functions
    all_transforms = []  # (call_start, call_end, new_text)
    
    for fstart, fend, var_expr, fname in func_ranges:
        log_calls = find_log_calls(text, fstart, fend)
        for call_start, call_end, log_func in log_calls:
            new_text = transform_log_call(text, call_start, call_end, log_func, var_expr)
            if new_text is not None:
                all_transforms.append((call_start, call_end, new_text))
    
    if not all_transforms:
        print(f"  {os.path.basename(filepath)}: no transforms needed")
        return 0
    
    # Apply transforms in reverse order (so offsets stay valid)
    all_transforms.sort(key=lambda x: x[0], reverse=True)
    
    for call_start, call_end, new_text in all_transforms:
        text = text[:call_start] + new_text + text[call_end:]
    
    changes = len(all_transforms)
    print(f"  {os.path.basename(filepath)}: {changes} log calls transformed")
    
    if dry_run:
        # Show a sample diff
        orig_lines = original.split('\n')
        new_lines = text.split('\n')
        for i, (ol, nl) in enumerate(zip(orig_lines, new_lines)):
            if ol != nl:
                print(f"    L{i+1}: {ol.strip()}")
                print(f"       -> {nl.strip()}")
                if i > 5:
                    print(f"    ... and more")
                    break
    else:
        with open(filepath, 'w') as f:
            f.write(text)
    
    return changes


def main():
    base = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'radiant') + '/'
    dry_run = '--dry-run' in sys.argv
    
    if dry_run:
        print("DRY RUN - no files will be modified\n")
    
    total = 0
    
    # layout_block.cpp
    print("Processing layout_block.cpp...")
    total += process_file(base + 'layout_block.cpp', {
        'layout_block': 'elmt->source_loc()',
        'finalize_block_flow': 'block->source_loc()',
        'layout_block_content': 'block->source_loc()',
        'layout_block_inner_content': 'block->source_loc()',
        'layout_inline_svg': 'block->source_loc()',
        'layout_display_math_block': 'elem->source_loc()',
        'create_pseudo_element': 'parent->source_loc()',
        'layout_pseudo_element': 'pseudo_elem->source_loc()',
        'create_first_letter_pseudo': 'block->source_loc()',
        'layout_abs_block': 'elmt->source_loc()',
        'alloc_pseudo_content_prop': 'block->source_loc()',
        'adjust_abs_descendants_y': 'parent->source_loc()',
    }, dry_run)
    
    # layout_inline.cpp
    print("Processing layout_inline.cpp...")
    total += process_file(base + 'layout_inline.cpp', {
        'layout_inline': 'elmt->source_loc()',
        'layout_inline_with_block_children': 'inline_elem->source_loc()',
    }, dry_run)
    
    # layout_flex.cpp
    print("Processing layout_flex.cpp...")
    total += process_file(base + 'layout_flex.cpp', {
        'layout_flex_container': 'container->source_loc()',
        'init_flex_container': 'container->source_loc()',
        'layout_flex_item_content': 'flex_item->source_loc()',
        'collect_flex_items': 'container->source_loc()',
        'collect_and_prepare_flex_items': 'container->source_loc()',
        'calculate_flex_basis': 'item->source_loc()',
        'calculate_hypothetical_main_size': 'item->source_loc()',
        'resolve_flex_item_constraints': 'item->source_loc()',
        'apply_constraints': 'item->source_loc()',
        'reposition_baseline_items': 'flex_container->source_loc()',
        'determine_container_cross_size': 'container->source_loc()',
    }, dry_run)
    
    # layout_grid.cpp
    print("Processing layout_grid.cpp...")
    total += process_file(base + 'layout_grid.cpp', {
        'layout_grid_container': 'container->source_loc()',
        'init_grid_container': 'container->source_loc()',
        'collect_grid_items': 'container->source_loc()',
        'auto_place_grid_item': 'item->source_loc()',
    }, dry_run)
    
    # layout_table.cpp
    print("Processing layout_table.cpp...")
    total += process_file(base + 'layout_table.cpp', {
        'layout_table_content': 'tableNode->source_loc()',
        'layout_table_cell_content': 'cell->source_loc()',
        'process_table_cell': 'tcell->source_loc()',
        'build_table_tree': 'tableNode->source_loc()',
        'table_auto_layout': 'table->source_loc()',
        'measure_cell_content_height': 'tcell->source_loc()',
        'calculate_cell_height': 'tcell->source_loc()',
        'analyze_table_structure': 'table->source_loc()',
        'mark_table_node': 'node->source_loc()',
        'generate_anonymous_table_boxes': 'table->source_loc()',
        'wrap_orphaned_table_children': 'parent->source_loc()',
        'get_cell_css_width': 'tcell->source_loc()',
        'measure_cell_widths': 'cell->source_loc()',
        'layout_column_elements': 'table->source_loc()',
        'apply_row_baseline_alignment': 'trow->source_loc()',
        'apply_fixed_row_height': 'trow->source_loc()',
        'apply_cell_vertical_align': 'tcell->source_loc()',
        'apply_cell_vertical_alignment': 'tcell->source_loc()',
        'detect_anonymous_boxes': 'table->source_loc()',
        'wrap_node_in_cell_if_needed': 'node->source_loc()',
        'wrap_run_in_cells': 'parent_row->source_loc()',
    }, dry_run)
    
    # layout.cpp
    print("Processing layout.cpp...")
    total += process_file(base + 'layout.cpp', {
        'layout_flow_node': 'node->source_loc()',
        'dom_node_resolve_style': 'node->source_loc()',
        'setup_line_height': 'block->source_loc()',
        'layout_html_root': 'elmt->source_loc()',
        'merge_run_in_with_next_block': 'run_in->source_loc()',
        'resolve_run_in_display': 'node->source_loc()',
    }, dry_run)
    
    print(f"\nTotal: {total} log calls transformed across all files")

if __name__ == '__main__':
    main()
