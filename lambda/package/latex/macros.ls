// latex/macros.ls — Basic \newcommand macro expansion
// Pre-processes the AST to extract macro definitions.
// Tree-sitter parses \newcommand{\name}[N]{body} as an element:
//   <newcommand; "\\name", <brack_group; "N">, <curly_group; ...body...>>
// Macro invocations like \name{arg} become elements:
//   <name; "arg">
// Expansion happens at render time via render2.ls to avoid tree-rebuilding.

import util: .lambda.package.latex.util

// ============================================================
// Public API
// ============================================================

// extract macro definitions from the AST
// returns an array of {name: "...", params: N, body: <curly_group; ...>}
pub fn get_defs(ast) {
    if (ast == null) { [] }
    else if (ast is element) { collect_all_defs(ast, 0, len(ast), []) }
    else { [] }
}

// expand is kept for backward compat — now just returns ast unchanged
pub fn expand(ast) {
    ast
}

// ============================================================
// Definition collection
// ============================================================

// recursively scan for newcommand elements
fn collect_all_defs(el, i, n, acc) {
    if (i >= n) acc
    else collect_def_at(el, i, n, acc)
}

fn collect_def_at(el, i, n, acc) {
    let child = el[i]
    if (child is element and is_newcommand_el(child)) parse_and_continue(el, i, n, acc, child)
    else if (child is element) scan_child_for_defs(el, i, n, acc, child)
    else collect_all_defs(el, i + 1, n, acc)
}

fn is_newcommand_el(el) {
    let tag = string(name(el))
    tag == "newcommand" or tag == "renewcommand" or tag == "providecommand"
}

fn parse_and_continue(el, i, n, acc, nc_el) {
    let def = parse_newcommand(nc_el)
    let new_acc = if (def != null) acc ++ [def] else acc
    collect_all_defs(el, i + 1, n, new_acc)
}

fn scan_child_for_defs(el, i, n, acc, child) {
    let child_defs = collect_all_defs(child, 0, len(child), [])
    collect_all_defs(el, i + 1, n, acc ++ child_defs)
}

// parse a newcommand element:
//   children: ["\\name", brack_group?("N"), curly_group(body)]
fn parse_newcommand(nc_el) {
    let nc_n = len(nc_el)
    if (nc_n < 2) null
    else extract_newcommand(nc_el, nc_n)
}

fn extract_newcommand(nc_el, nc_n) {
    // first child is the command name string (e.g. "\\greeting")
    let raw_name = get_cmd_name(nc_el[0])
    let cmd_name = strip_backslash(raw_name)
    // find param count and body
    let pb = find_param_and_body(nc_el, 1, nc_n)
    if (pb.body != null) make_def(cmd_name, pb.params, pb.body)
    else null
}

fn make_def(cmd_name, params, body) {
    {name: cmd_name, params: params, body: body}
}

fn get_cmd_name(node) {
    if (node is string) node
    else if (node is element) util.text_of(node)
    else string(node)
}

fn strip_backslash(s) {
    if (len(s) > 0 and slice(s, 0, 1) == "\\") slice(s, 1, len(s))
    else s
}

// find brack_group (param count) and curly_group (body) from children starting at j
fn find_param_and_body(nc_el, j, nc_n) {
    if (j >= nc_n) make_pb(0, null)
    else check_param_or_body(nc_el, j, nc_n)
}

fn check_param_or_body(nc_el, j, nc_n) {
    let child = nc_el[j]
    if (child is element and string(name(child)) == "brack_group") find_body_after_params(nc_el, j, nc_n)
    else if (child is element and string(name(child)) == "curly_group") make_pb(0, child)
    else find_param_and_body(nc_el, j + 1, nc_n)
}

fn find_body_after_params(nc_el, j, nc_n) {
    let param_text = util.text_of(nc_el[j])
    let param_count = int(trim(param_text))
    let pc = if (param_count != null) param_count else 0
    find_body(nc_el, j + 1, nc_n, pc)
}

fn find_body(nc_el, j, nc_n, pc) {
    if (j >= nc_n) { make_pb(pc, null) }
    else {
        let child = nc_el[j]
        if (child is element and string(name(child)) == "curly_group") { make_pb(pc, child) }
        else { find_body(nc_el, j + 1, nc_n, pc) }
    }
}

fn make_pb(params, body) {
    {params: params, body: body}
}

// ============================================================
// Macro lookup (used by render pass)
// ============================================================

// look up a macro definition by command name
pub fn find_macro(defs, cmd_name) {
    find_macro_rec(defs, cmd_name, 0)
}

fn find_macro_rec(defs, cmd_name, i) {
    if (i >= len(defs)) null
    else check_macro(defs, cmd_name, i)
}

fn check_macro(defs, cmd_name, i) {
    let d = defs[i]
    if (d.name == cmd_name) d
    else find_macro_rec(defs, cmd_name, i + 1)
}

// ============================================================
// Parameter substitution (used by render pass)
// ============================================================

// substitute #1, #2 in body with arguments from invocation element
// returns array of string/element items
pub fn substitute_body(body_group, invocation) {
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
    else if (is_param_ref(item)) substitute_param_ref_item(body, i, n, invocation, acc, item)
    else if (item is element) substitute_element_item(body, i, n, invocation, acc, item)
    else substitute_items(body, i + 1, n, invocation, acc ++ [item])
}

// recursively substitute inside child elements too
fn substitute_element_item(body, i, n, invocation, acc, el) {
    let expanded = substitute_body(el, invocation)
    substitute_items(body, i + 1, n, invocation, acc ++ expanded)
}

// check if node is a parameter reference like #1, #2, etc.
// these nodes have name() returning "#1", "#2" etc but type() is null
fn is_param_ref(node) {
    let n = name(node)
    if (n == null) false
    else check_param_tag(string(n))
}

fn check_param_tag(tag) {
    if len(tag) > 1 { slice(tag, 0, 1) == "#" }
    else { false }
}

// replace a #N element with the corresponding invocation argument
fn substitute_param_ref_item(body, i, n, invocation, acc, el) {
    let tag = string(name(el))
    let num_str = slice(tag, 1, len(tag))
    let param_num = int(num_str)
    let arg_idx = if (param_num != null) param_num - 1 else -1
    let arg = if (arg_idx >= 0 and arg_idx < len(invocation)) get_invocation_arg(invocation, arg_idx) else ""
    substitute_items(body, i + 1, n, invocation, acc ++ [arg])
}

fn substitute_text_item(body, i, n, invocation, acc, text) {
    let subst = substitute_text(text, invocation)
    substitute_items(body, i + 1, n, invocation, acc ++ [subst])
}

fn substitute_text(text, invocation) {
    replace_params(text, invocation, 1)
}

fn replace_params(text, invocation, param_num) {
    if (param_num > 9) text
    else do_replace_param(text, invocation, param_num)
}

fn do_replace_param(text, invocation, param_num) {
    let placeholder = "#" ++ string(param_num)
    let arg_idx = param_num - 1
    let arg = if (arg_idx < len(invocation)) get_invocation_arg(invocation, arg_idx) else ""
    let replaced = replace_all_occurrences(text, placeholder, arg)
    replace_params(replaced, invocation, param_num + 1)
}

fn get_invocation_arg(invocation, idx) {
    let child = invocation[idx]
    if (child is string) child
    else if (child is element) util.text_of(child)
    else string(child)
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
    replace_all_occurrences(result, pattern, replacement)
}
