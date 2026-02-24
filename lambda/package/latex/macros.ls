// latex/macros.ls — Basic \newcommand macro expansion
// Pre-processes the AST to extract macro definitions and expand usages.
// Supports \newcommand{\name}{body} and \newcommand{\name}[N]{body}
// with parameter substitution (#1, #2, etc.)

import util: .lambda.package.latex.util

// ============================================================
// Public API
// ============================================================

// expand macros in the AST:
// 1. scan for \newcommand definitions, build macro table
// 2. remove the definition nodes from the AST
// 3. replace macro invocations with expanded bodies
pub fn expand(ast) {
    if (ast == null) null
    else if (ast is element) try_expand(ast)
    else ast
}

fn try_expand(el) {
    // only expand if there are macro definitions in the top-level children
    let n = len(el)
    let defs = collect_defs(el, 0, n, [])
    if (len(defs) == 0) el
    else expand_with_defs(el, defs)
}

fn expand_with_defs(el, defs) {
    let n = len(el)
    let new_kids = expand_children(el, 0, n, defs, [])
    util.rebuild_with_children(el, new_kids)
}

// ============================================================
// Definition collection
// ============================================================

// scan children for "ewcommand" text nodes to find macro definitions
fn collect_defs(el, i, n, acc) {
    if (i >= n) acc
    else collect_def_at(el, i, n, acc)
}

fn collect_def_at(el, i, n, acc) {
    let child = el[i]
    if ((child is string) and is_newcommand(child)) parse_def(el, i, n, acc)
    else collect_defs(el, i + 1, n, acc)
}

fn is_newcommand(text) {
    let t = trim(text)
    t == "ewcommand" or t == "renewcommand" or t == "providecommand"
}

// parse the newcommand definition starting at position i
// pattern: "ewcommand" <curly_group; <name>> [optional "[N]"] <curly_group; body>
fn parse_def(el, i, n, acc) {
    // i is the "ewcommand" text
    // i+1 should be curly_group with command name
    // i+2 could be "[N]" or curly_group body
    if (i + 2 >= n) collect_defs(el, i + 1, n, acc)
    else parse_def_parts(el, i, n, acc)
}

fn parse_def_parts(el, i, n, acc) {
    let name_group = el[i + 1]
    if ((name_group is element) and name(name_group) == 'curly_group') extract_def(el, i, n, acc, name_group)
    else collect_defs(el, i + 1, n, acc)
}

fn extract_def(el, i, n, acc, name_group) {
    // name is the first child of the curly_group
    let cmd_el = name_group[0]
    let cmd_name = if (cmd_el is element) name(cmd_el) else string(cmd_el)
    // check for param count [N]
    let next = el[i + 2]
    let param_info = parse_param_count(next)
    let param_count = param_info.count
    let body_idx = if (param_info.consumed) i + 3 else i + 2
    // body is the next curly_group
    if (body_idx >= n) collect_defs(el, i + 1, n, acc)
    else extract_body(el, i, n, acc, cmd_name, param_count, body_idx)
}

fn extract_body(el, i, n, acc, cmd_name, param_count, body_idx) {
    let body_group = el[body_idx]
    if ((body_group is element) and name(body_group) == 'curly_group') add_def(el, i, n, acc, cmd_name, param_count, body_idx, body_group)
    else collect_defs(el, i + 1, n, acc)
}

fn add_def(el, i, n, acc, cmd_name, param_count, body_idx, body_group) {
    let def = {name: cmd_name, params: param_count, body: body_group, start: i, end: body_idx}
    collect_defs(el, body_idx + 1, n, acc ++ [def])
}

// parse "[N]" parameter count from a text node
fn parse_param_count(node) {
    if ((node is string) and starts_with(trim(node), "[")) parse_bracket_count(node)
    else make_param_info(0, false)
}

fn parse_bracket_count(node) {
    let t = trim(node)
    let inner = slice(t, 1, len(t) - 1)
    let n = int(trim(inner))
    let result = if (n != null) make_param_info(n, true) else make_param_info(0, false)
    result
}

fn make_param_info(count, consumed) {
    {count: count, consumed: consumed}
}

// ============================================================
// Macro table
// ============================================================

// ============================================================
// Expansion
// ============================================================

// look up a macro definition by command name
fn find_macro(defs, cmd_name, i) {
    if (i >= len(defs)) null
    else check_macro(defs, cmd_name, i)
}

fn check_macro(defs, cmd_name, i) {
    let d = defs[i]
    if (d.name == cmd_name) d
    else find_macro(defs, cmd_name, i + 1)
}

// rebuild children, skipping definition nodes and expanding macro invocations
fn expand_children(el, i, n, defs, acc) {
    if (i >= n) acc
    else expand_child_at(el, i, n, defs, acc)
}

fn expand_child_at(el, i, n, defs, acc) {
    let child = el[i]
    // skip "ewcommand" definition sequences
    if ((child is string) and is_newcommand(child)) skip_def(el, i, n, defs, acc)
    else if (child is element) expand_element_child(el, i, n, defs, acc, child)
    else expand_children(el, i + 1, n, defs, acc ++ [child])
}

fn skip_def(el, i, n, defs, acc) {
    // skip: "ewcommand" + curly_group + optional [N] + curly_group
    let skip_to = find_def_end(el, i, n)
    expand_children(el, skip_to, n, defs, acc)
}

fn find_def_end(el, i, n) {
    // i is "ewcommand", i+1 is name_group, i+2 might be [N] or body
    if (i + 2 >= n) i + 1
    else find_def_end_lookahead(el, i, n)
}

fn find_def_end_lookahead(el, i, n) {
    let next2 = el[i + 2]
    let has_params = ((next2 is string) and starts_with(trim(next2), "["))
    let body_idx = if (has_params) i + 3 else i + 2
    if (body_idx < n and (el[body_idx] is element) and name(el[body_idx]) == 'curly_group') body_idx + 1
    else i + 1
}

fn expand_element_child(el, i, n, defs, acc, child) {
    let tag = name(child)
    let macro_def = find_macro(defs, string(tag), 0)
    if (macro_def != null) expand_macro_invocation(el, i, n, defs, acc, child, macro_def)
    else expand_non_macro(el, i, n, defs, acc, child)
}

fn expand_macro_invocation(el, i, n, defs, acc, child, macro_def) {
    let expanded = substitute_body(macro_def.body, child, macro_def.params)
    expand_children(el, i + 1, n, defs, acc ++ expanded)
}

fn expand_non_macro(el, i, n, defs, acc, child) {
    let expanded_child = expand_in(child, defs)
    expand_children(el, i + 1, n, defs, acc ++ [expanded_child])
}

// expand macros within a child element (recursive)
fn expand_in(el, defs) {
    let n = len(el)
    let new_kids = expand_children(el, 0, n, defs, [])
    util.rebuild_with_children(el, new_kids)
}

// ============================================================
// Parameter substitution
// ============================================================

// substitute #1, #2, etc. in the body with arguments from invocation
fn substitute_body(body_group, invocation, param_count) {
    let n = len(body_group)
    substitute_items(body_group, 0, n, invocation, [])
}

fn substitute_items(body, i, n, invocation, acc) {
    if (i >= n) acc
    else substitute_item(body, i, n, invocation, acc)
}

fn substitute_item(body, i, n, invocation, acc) {
    let item = body[i]
    if (item is string) substitute_text_item(body, i, n, invocation, acc, item)
    else substitute_items(body, i + 1, n, invocation, acc ++ [item])
}

fn substitute_text_item(body, i, n, invocation, acc, text) {
    let subst = substitute_text(text, invocation)
    substitute_items(body, i + 1, n, invocation, acc ++ [subst])
}

// replace #N with the Nth child of the invocation element
fn substitute_text(text, invocation) {
    // simple replacement: look for #1 through #9
    replace_params(text, invocation, 1)
}

fn replace_params(text, invocation, param_num) {
    if (param_num > 9) text
    else do_replace_param(text, invocation, param_num)
}

fn do_replace_param(text, invocation, param_num) {
    let placeholder = "#" ++ string(param_num)
    let arg_idx = param_num - 1
    let arg = if (arg_idx < len(invocation)) util.text_of_child(invocation, arg_idx) else ""
    let replaced = replace_all_occurrences(text, placeholder, arg)
    replace_params(replaced, invocation, param_num + 1)
}

fn replace_all_occurrences(text, pattern, replacement) {
    if (len(pattern) == 0) text
    else if (len(text) < len(pattern)) text
    else try_replace_at(text, pattern, replacement, 0)
}

fn try_replace_at(text, pattern, replacement, pos) {
    if (pos + len(pattern) > len(text)) text
    else check_match_at(text, pattern, replacement, pos)
}

fn check_match_at(text, pattern, replacement, pos) {
    let candidate = slice(text, pos, pos + len(pattern))
    if (candidate == pattern) do_replace(text, pattern, replacement, pos)
    else try_replace_at(text, pattern, replacement, pos + 1)
}

fn do_replace(text, pattern, replacement, pos) {
    let before = slice(text, 0, pos)
    let after = slice(text, pos + len(pattern), len(text))
    let result = before ++ replacement ++ after
    // continue searching from after the replacement
    replace_all_occurrences(result, pattern, replacement)
}
