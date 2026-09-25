// Native source/config inventory for utils/check_static_module_architecture.py.
import .static_arch_scan
import .path_utils

let compatibility_forwarders = ["lib/item_tagged.hpp", "lib/lambda_typed.hpp",
                                "lib/side_stack.h"]
let public_headers = [
    {path: "lambda/lambda.h", owner: "runtime-candidate"},
    {path: "lambda/lambda.hpp", owner: "mixed-active-core-io-rt"},
    {path: "lambda/lambda-data.hpp", owner: "mixed-active-core-rt"},
    {path: "lambda/core/mark_reader.hpp", owner: "core-candidate"},
    {path: "lambda/io/mark_builder.hpp", owner: "io-candidate"},
    {path: "lambda/io/mark_editor.hpp", owner: "io-candidate"},
    {path: "lambda/input/input.hpp", owner: "io-candidate"},
    {path: "lambda/format/format.h", owner: "io-candidate"},
    {path: "lambda/validator/validator.hpp", owner: "rt-candidate"},
    {path: "lambda/runtime/runtime-state.h", owner: "rt-provider"},
    {path: "radiant/radiant.hpp", owner: "radiant-candidate"}
]
let runtime_native_io = [
    {path: "lambda/js/js_fs.cpp", classification: "rt-native: Node fs handles and callbacks"},
    {path: "lambda/js/js_net.cpp", classification: "rt-native: Node sockets and libuv handles"},
    {path: "lambda/js/js_tls.cpp", classification: "rt-native: Node TLS handles and promises"},
    {path: "lambda/dom/dom_fetch.cpp", classification:
     "review: JS promise binding stays rt; reusable curl worker is io candidate"}
]

pn collect_source(path, var targets) {
    if (path.is_file and c_source(path.name) and not has_link_ancestor(path)) {
        targets = targets ++ [path]
    }
}

pub pn source_paths() {
    var targets = []
    for (path in \.lib.**) {
        collect_source(path, targets)
    }
    for (path in \.lambda.**) {
        collect_source(path, targets)
    }
    for (path in \.radiant.**) {
        collect_source(path, targets)
    }
    return sort(targets)
}

pn add_include_edges(path: string, source: string, target: string,
                     resolution: string, var entries) {
    for (hit in include_matches(source, target)) {
        entries = entries ++ [{path: path, line: hit.line,
                               token: hit.token, resolution: resolution}]
    }
}

pn include_inventory(paths) any^ {
    var lib_to_lambda = []
    var upper_to_radiant = []
    for (target_path in paths) {
        let path = relative_path(target_path)
        if (starts_with(path, "lib/") and not starts_with(path, "lib/gc/") and
            path != "lib/side_stack.c" and not contains(compatibility_forwarders, path)) {
            let source = input(target_path, "text")^
            add_include_edges(path, source, "lambda",
                              "move caller upward or replace with lib-neutral API",
                              lib_to_lambda)
        }
        if (starts_with(path, "lambda/input/") or starts_with(path, "lambda/network/") or
            starts_with(path, "lambda/js/") or starts_with(path, "lambda/module/")) {
            let source = input(target_path, "text")^
            add_include_edges(path, source, "radiant", "P3/P4 move, split, or lower-owned hook",
                              upper_to_radiant)
        }
    }
    return {lib_to_lambda: lib_to_lambda, upper_to_radiant: upper_to_radiant}
}

pn header_inventory() any^ {
    var entries = []
    for (item in public_headers) {
        let source = input(item.path, "text")^
        let runtime = identifier_matches(source, "runtime")
        let io = identifier_matches(source, "io")
        entries = entries ++ [{path: item.path, planned_owner: item.owner, frozen: false,
                               runtime_markers: runtime, io_markers: io,
                               outcome: if (len(runtime) > 0 and len(io) > 0)
                                   "split by provider before enforcement"
                               else "candidate for provider probe after relocation"}]
    }
    return entries
}

pn io_inventory() any^ {
    var entries = []
    for (item in runtime_native_io) {
        if (exists(item.path)) {
            let source = input(item.path, "text")^
            entries = entries ++ [{path: item.path, classification: item.classification,
                                   direct_io_calls: identifier_matches(source, "direct_io")}]
        }
    }
    return entries
}

pn validation_scaffold() any^ {
    let config = input("build_lambda_config.json", "json")^
    let scaffold = config.static_module_validation
    var declared = []
    for (target in scaffold.dsos) {
        if (target.module != null and not contains(declared, target.module)) {
            declared = declared ++ [target.module]
        }
    }
    let expected = ["lib", "core", "io", "rt", "radiant"]
    var missing = []
    var unexpected = []
    for (name in expected) {
        if (not contains(declared, name)) { missing = missing ++ [name] }
    }
    for (name in declared) {
        if (not contains(expected, name)) { unexpected = unexpected ++ [name] }
    }
    return {declared: sort(declared), missing: sort(missing),
            unexpected: sort(unexpected), mode: scaffold.mode}
}

pn boundary_failure_baseline(paths) any^ {
    var upward_externs = []
    var weak_providers = []
    for (target_path in paths) {
        let path = relative_path(target_path)
        if (starts_with(path, "lambda/") or starts_with(path, "radiant/")) {
            let source = input(target_path, "text")^
            if (contains(source, "extern")) {
                for (hit in upward_extern_matches(source)) {
                    upward_externs = upward_externs ++ [{path: path, line: hit.line,
                        declaration: hit.declaration,
                        resolution: "replace with lower-owned provider or registered hook"}]
                }
            }
            if (contains(source, "__attribute__")) {
                for (hit in weak_matches(source)) {
                    weak_providers = weak_providers ++ [{path: path, line: hit.line,
                        token: hit.token,
                        resolution: "P1b removes weak runtime fallbacks before DSO enforcement"}]
                }
            }
        }
    }
    return {upward_externs: upward_externs, weak_providers: weak_providers}
}

pn macro_hygiene_inventory() any^ {
    var entries = []
    for (item in public_headers) {
        let source = input(item.path, "text")^
        for (hit in static_selector_matches(source)) {
            entries = entries ++ [{path: item.path, line: hit.line,
                                   token: hit.token,
                                   resolution: "remove public-header ABI selector in P1a"}]
        }
    }
    return entries
}

pub pn static_module_inventory() any^ {
    let paths = source_paths()
    let headers = header_inventory()^
    let includes = include_inventory(paths)^
    let io = io_inventory()^
    let scaffold = validation_scaffold()^
    let baseline = boundary_failure_baseline(paths)^
    let hygiene = macro_hygiene_inventory()^
    return {schema_version: 1, mode: "report-only",
            public_header_providers: headers, include_edges: includes,
            runtime_native_io: io, validation_dso_scaffold: scaffold,
            boundary_failure_baseline: baseline,
            public_header_macro_hygiene: hygiene}
}
