// Native Lambda port of test/fuzzy/lambda/feature_inventory.py (D1.10).
import .sha256
import .c_text

fn word_char(ch: string) bool =>
    len(ch) == 1 and contains("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_", ch)
fn enum_char(ch: string) bool =>
    len(ch) == 1 and contains("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_", ch)
fn digit(ch: string) bool => len(ch) == 1 and contains("0123456789", ch)
fn space(ch: string) bool =>
    len(ch) == 1 and (contains(" \t\r\n", ch) or ch == chr(12) or ch == chr(11))

pn skip_space(source: string, start: int) {
    var pos = start
    while (pos < len(source) and space(slice(source, pos, pos + 1))) { pos = pos + 1 }
    return pos
}

pn enum_values(path: string, declaration: string, prefix: string) string[]^ {
    let source = input(path, "text")^
    let found = index_of(source, declaration)
    if (found == null) { raise error("missing declaration " ++ declaration ++ " in " ++ path) }
    let stripped = strip_c_comments(slice(source, found))
    let close = index_of(stripped, "}")
    if (close == null) { raise error("unterminated declaration " ++ declaration ++ " in " ++ path) }
    let block = slice(stripped, 0, close)
    var values = []
    var pos = 0
    while (pos < len(block)) {
        if (word_char(slice(block, pos, pos + 1))) {
            let start = pos
            while (pos < len(block) and word_char(slice(block, pos, pos + 1))) {
                pos = pos + 1
            }
            let token = slice(block, start, pos)
            if (starts_with(token, prefix) and len(token) > len(prefix)) {
                let suffix = slice(token, len(prefix))
                var valid = true
                var i = 0
                while (i < len(suffix)) {
                    if (not enum_char(slice(suffix, i, i + 1))) { valid = false }
                    i = i + 1
                }
                if (valid) { values = values ++ [token] }
            }
        } else {
            pos = pos + 1
        }
    }
    return sort(unique(values))
}

pn system_function_rows(path: string) string[]^ {
    let source = input(path, "text")^
    var rows = []
    var offset = 0
    while (offset < len(source)) {
        let hit = index_of(slice(source, offset), "{SYSFUNC_")
        if (hit == null) { break }
        let start = offset + hit + 1
        offset = start + len("SYSFUNC_")
        var pos = start
        while (pos < len(source) and enum_char(slice(source, pos, pos + 1))) { pos = pos + 1 }
        let identifier = slice(source, start, pos)
        if (slice(source, pos, pos + 1) == ",") {
            pos = skip_space(source, pos + 1)
            if (slice(source, pos, pos + 1) == "\"") {
                let name_start = pos + 1
                let end_quote = index_of(slice(source, name_start), "\"")
                if (end_quote != null) {
                    let name = slice(source, name_start, name_start + end_quote)
                    pos = skip_space(source, name_start + end_quote + 1)
                    if (slice(source, pos, pos + 1) == ",") {
                        pos = skip_space(source, pos + 1)
                        let arity_start = pos
                        if (slice(source, pos, pos + 1) == "-") { pos = pos + 1 }
                        let digits_start = pos
                        while (pos < len(source) and digit(slice(source, pos, pos + 1))) {
                            pos = pos + 1
                        }
                        if (pos > digits_start) {
                            rows = rows ++ [identifier ++ ":" ++ name ++ "/" ++
                                             slice(source, arity_start, pos)]
                        }
                    }
                }
            }
        }
    }
    if (len(rows) == 0) { raise error("no SysFuncInfo rows found in " ++ path) }
    return sort(unique(rows))
}

pub pn inventory_rows() string[]^ {
    let parser = "lambda/runtime/parser/lambda_rd_parser.h"
    let ast = "lambda/runtime/ast-core.hpp"
    let registry = "lambda/runtime/sys_func_registry.c"
    var rows = []
    for (value in enum_values(parser, "typedef enum LambdaTokenKind", "LAMBDA_TOK_")^) {
        rows = rows ++ ["token\t" ++ value]
    }
    for (value in enum_values(parser, "typedef enum LambdaReductionKind", "LAMBDA_REDUCE_")^) {
        rows = rows ++ ["reduction-kind\t" ++ value]
    }
    for (value in enum_values(parser, "typedef enum LambdaReductionForm", "LAMBDA_REDUCTION_FORM_")^) {
        rows = rows ++ ["reduction-form\t" ++ value]
    }
    for (value in enum_values(ast, "typedef enum AstNodeType", "AST_")^) {
        rows = rows ++ ["ast\t" ++ value]
    }
    for (value in system_function_rows(registry)^) {
        rows = rows ++ ["sysfunc\t" ++ value]
    }
    return sort(rows)
}

pub pn expected_lock(rows) {
    return "version\t1\ncount\t" ++ string(len(rows)) ++ "\nsha256\t" ++
           sha256_hex(join(rows, "\n") ++ "\n") ++ "\n"
}

pn campaign_statuses(path: string) any^ {
    if (not exists(path)) { raise error("missing feature campaign map: " ++ path) }
    let source = input(path, "text")^
    var statuses = {}
    var names = []
    var line_number = 1
    for (raw_line in split(source, "\n")) {
        let line = trim(raw_line)
        if (line != "" and not starts_with(line, "#")) {
            let fields = split(line, "\t")
            if (len(fields) != 4) {
                raise error(path ++ ":" ++ string(line_number) ++ ": expected 4 tab-separated fields")
            }
            let surface = fields[0]
            let status = fields[1]
            if (status != "covered" and status != "unsupported-with-reason") {
                raise error(path ++ ":" ++ string(line_number) ++ ": invalid status " ++ status)
            }
            if (fields[2] == "" or fields[3] == "") {
                raise error(path ++ ":" ++ string(line_number) ++ ": evidence and references are required")
            }
            if (statuses[surface] != null) {
                raise error(path ++ ":" ++ string(line_number) ++ ": duplicate surface " ++ surface)
            }
            statuses[surface] = {status: status, evidence: fields[2], refs: fields[3]}
            names = names ++ [surface]
        }
        line_number = line_number + 1
    }
    return {statuses: statuses, names: names}
}

pub pn check_inventory() string^ {
    let rows = inventory_rows()^
    var surface_names = []
    for (row in rows) {
        let surface = split(row, "\t")[0]
        if (not contains(surface_names, surface)) { surface_names = surface_names ++ [surface] }
    }
    let surfaces = sort(surface_names)
    let campaigns = campaign_statuses("test/fuzzy/lambda/feature_campaigns.tsv")^
    var missing = []
    var stale = []
    for (surface in surfaces) {
        if (campaigns.statuses[surface] == null) { missing = missing ++ [surface] }
    }
    for (surface in campaigns.names) {
        if (not contains(surfaces, surface)) { stale = stale ++ [surface] }
    }
    if (len(missing) > 0 or len(stale) > 0) {
        raise error("feature campaign map mismatch: missing=" ++ string(sort(missing)) ++
                    " stale=" ++ string(sort(stale)))
    }

    let lock_path = "test/fuzzy/lambda/feature_inventory.lock"
    if (not exists(lock_path)) { raise error("missing feature inventory lock: " ++ lock_path) }
    let actual = input(lock_path, "text")^
    if (actual != expected_lock(rows)) {
        raise error("feature inventory changed; review campaign coverage and update " ++ lock_path)
    }

    let semantic_names = ["syntax-parser", "values-types-representation",
                          "operators-equality-order", "errors-functions-resources",
                          "mutation-lifetime", "concurrency", "data-processing", "modules"]
    let semantics = campaign_statuses("test/fuzzy/lambda/semantic_campaigns.tsv")^
    missing = []
    stale = []
    for (name in semantic_names) {
        if (semantics.statuses[name] == null) { missing = missing ++ [name] }
    }
    for (name in semantics.names) {
        if (not contains(semantic_names, name)) { stale = stale ++ [name] }
    }
    if (len(missing) > 0 or len(stale) > 0) {
        raise error("semantic campaign map mismatch: missing=" ++ string(sort(missing)) ++
                    " stale=" ++ string(sort(stale)))
    }

    var summary_parts = []
    for (surface in surfaces) {
        var count = 0
        for (row in rows) {
            if (starts_with(row, surface ++ "\t")) { count = count + 1 }
        }
        summary_parts = summary_parts ++ [surface ++ "=" ++ string(count)]
    }
    return "feature inventory verified: " ++ join(summary_parts, ", ") ++
           "; campaigns=" ++ string(len(semantics.names))
}
