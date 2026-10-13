// latex/macros.ls — the table of document macro definitions
// The TeX engine expands every macro invocation before the direct parser runs
// (D7.2.7) and echoes the \newcommand-family definitions into its output, so
// the AST still holds <newcommand "\\name" ...> elements whose argument_groups
// keep each group's raw source. This module collects those definitions as data
// for the readers that need them: TikZ islands (tikz_bridge.ls), bussproofs
// labels, and \familydefault. It never substitutes macros itself.

import util: .util

// ============================================================
// Public API
// ============================================================

// extract macro definitions from the AST
// returns an array of {name: "...", params: N, body: <curly_group ...>}
pub fn get_defs(ast) {
    if (ast == null) { [] }
    else if (ast is element) { collect_all_defs(ast, 0, len(ast), []) }
    else { [] }
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
    let cmd_name = util.strip_backslash(raw_name)
    // find param count, optional default, and body
    let pb = find_param_and_body(nc_el, 1, nc_n)
    let raw_body = util.raw_argument(nc_el, "required", 1)
    if (pb.body != null and pb.default_arg != null) {
        make_def_with_default(cmd_name, pb.params, pb.body, pb.default_arg,
            raw_body)
    }
    else if (pb.body != null) { make_def(cmd_name, pb.params, pb.body, raw_body) }
    else { null }
}

fn make_def(cmd_name, params, body, raw_body) {
    {name: cmd_name, params: params, body: body,
     default_arg: null, source: raw_body}
}

fn make_def_with_default(cmd_name, params, body, default_arg, raw_body) {
    {name: cmd_name, params: params, body: body,
     default_arg: default_arg, source: raw_body}
}

fn get_cmd_name(node) {
    if (node is string) node
    else if (node is element) util.text_of(node)
    else string(node)
}

// find brack_group (param count) and curly_group (body) from children starting at j
fn find_param_and_body(nc_el, j, nc_n) {
    if (j >= nc_n) make_pb(0, null)
    else check_param_or_body(nc_el, j, nc_n)
}

fn check_param_or_body(nc_el, j, nc_n) {
    let child = nc_el[j]
    if (child is element and string(name(child)) == "brack_group") find_default_or_body_after_params(nc_el, j, nc_n)
    else if (child is element and string(name(child)) == "curly_group") make_pb(0, child)
    else find_param_and_body(nc_el, j + 1, nc_n)
}

fn find_default_or_body_after_params(nc_el, j, nc_n) {
    let param_text = util.text_of(nc_el[j])
    let param_count = int(trim(param_text))
    let pc = if (param_count != null) { param_count } else { 0 }
    // look for optional default [value] or body {curly_group} after param count
    find_default_or_body(nc_el, j + 1, nc_n, pc)
}

fn find_default_or_body(nc_el, j, nc_n, pc) {
    if (j >= nc_n) { make_pb(pc, null) }
    else {
        let child = nc_el[j]
        if (child is element and string(name(child)) == "brack_group") {
            // this is the default value for the optional argument
            let default_text = util.text_of(child)
            find_body_with_default(nc_el, j + 1, nc_n, pc, default_text)
        }
        else if (child is element and string(name(child)) == "curly_group") {
            make_pb(pc, child)
        }
        else { find_default_or_body(nc_el, j + 1, nc_n, pc) }
    }
}

fn find_body_with_default(nc_el, j, nc_n, pc, default_text) {
    if (j >= nc_n) { make_pb_with_default(pc, null, default_text) }
    else {
        let child = nc_el[j]
        if (child is element and string(name(child)) == "curly_group") {
            make_pb_with_default(pc, child, default_text)
        }
        else { find_body_with_default(nc_el, j + 1, nc_n, pc, default_text) }
    }
}

fn make_pb(params, body) {
    {params: params, body: body, default_arg: null}
}

fn make_pb_with_default(params, body, default_text) {
    {params: params, body: body, default_arg: default_text}
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
