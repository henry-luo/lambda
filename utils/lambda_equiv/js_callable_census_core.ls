// Native source census for utils/js_callable_census.py.
import .js_callable_lex
import .js_callable_catalog
import .path_utils

let callable_limits = {
    dispatch_declarations: 0, dispatch_references: 0,
    builtin_semantic_cases: 0, ambiguous_factory_references: 0,
    raw_native_factory_casts: 0, pending_new_target_references: 0,
    special_constructor_references: 0, constructor_name_comparisons: 0,
    receiver_name_dispatch_references: 0, host_name_dispatch_references: 0,
    property_miss_synthesis_references: 0, call_migration_flag_references: 0,
    array_receiver_state_references: 0, legacy_invoke_wrapper_references: 0,
    direct_global_catalog_shortcuts: 0, direct_global_identity_loads: 1,
    legacy_class_map_bridge_definitions: 1,
    legacy_class_map_bridge_references: 2, semantic_catalog_id_reads: 0,
    catalog_validation_errors: 0
}

let callable_names = [
    "ambiguous_factory_references", "array_receiver_state_references",
    "builtin_semantic_cases", "call_migration_flag_references",
    "catalog_validation_errors", "constructor_name_comparisons",
    "direct_global_catalog_shortcuts", "direct_global_identity_loads",
    "dispatch_declarations", "dispatch_references",
    "host_name_dispatch_references", "legacy_class_map_bridge_definitions",
    "legacy_class_map_bridge_references", "legacy_invoke_wrapper_references",
    "pending_new_target_references", "property_miss_synthesis_references",
    "raw_native_factory_casts", "receiver_name_dispatch_references",
    "semantic_catalog_id_reads", "special_constructor_references"
]

fn callable_source(name: string) bool =>
    ends_with(name, ".c") or ends_with(name, ".cpp") or
    ends_with(name, ".h") or ends_with(name, ".hpp") or ends_with(name, ".def")

pn callable_source_paths() {
    var paths = []
    for (path in \.lambda.js.**) {
        if (path.is_file and callable_source(path.name) and not has_link_ancestor(path)) {
            paths = paths ++ [path]
        }
    }
    for (path in \.lambda.jube.**) {
        if (path.is_file and callable_source(path.name) and not has_link_ancestor(path)) {
            paths = paths ++ [path]
        }
    }
    for (path in \.lambda.module.radiant.**) {
        if (path.is_file and callable_source(path.name) and not has_link_ancestor(path)) {
            paths = paths ++ [path]
        }
    }
    for (path in \.radiant.**) {
        if (path.is_file and callable_source(path.name) and not has_link_ancestor(path)) {
            paths = paths ++ [path]
        }
    }
    return sort(paths)
}

pn callable_extract_function(source: string, name: string) string {
    for (at in callable_word_positions(source, name)) {
        var cursor = callable_skip_space(source, at + len(name))
        if (slice(source, cursor, cursor + 1) != "(") { continue }
        cursor = cursor + 1
        while (cursor < len(source) and slice(source, cursor, cursor + 1) != ")" and
               slice(source, cursor, cursor + 1) != ";") { cursor = cursor + 1 }
        if (slice(source, cursor, cursor + 1) != ")") { continue }
        cursor = callable_skip_space(source, cursor + 1)
        if (slice(source, cursor, cursor + 1) != "{") { continue }
        var depth = 0
        while (cursor < len(source)) {
            let ch = slice(source, cursor, cursor + 1)
            if (ch == "{") { depth = depth + 1 }
            else if (ch == "}") {
                depth = depth - 1
                if (depth == 0) { return slice(source, at, cursor + 1) }
            }
            cursor = cursor + 1
        }
        return slice(source, at)
    }
    return ""
}

pn callable_file_counts(source: string) {
    var counts = {}
    counts.dispatch_declarations = callable_count_decl(source, "js_dispatch_builtin")
    counts.dispatch_references = callable_count_call(source, "js_dispatch_builtin")
    counts.builtin_semantic_cases = callable_count_builtin_cases(source)
    counts.ambiguous_factory_references = callable_count_call(source, "js_new_function")
    counts.raw_native_factory_casts = callable_count_factory_casts(source)
    counts.pending_new_target_references = callable_count_union(
        source, ["js_pending_new_target", "js_has_pending_new_target"], false)
    counts.special_constructor_references = callable_count_union(
        source, ["special_ctor", "special_ctor_kind", "special_ctor_name_id"], false)
    counts.receiver_name_dispatch_references = callable_count_union(source,
        ["js_string_method", "js_number_method", "js_map_method", "js_array_method",
         "js_array_method_direct"], true)
    counts.host_name_dispatch_references = callable_count_union(source,
        ["js_dom_element_method", "js_document_method", "js_document_proxy_method",
         "js_css_namespace_method", "js_canvas_method_dispatch", "js_classlist_method",
         "js_dom_implementation_method", "jube_member_call"], true)
    counts.property_miss_synthesis_references = callable_count_union(source,
        ["js_get_or_create_builtin", "js_lookup_builtin_method_spec"], true)
    counts.call_migration_flag_references = callable_count_union(source,
        ["JS_CALL_STATS", "JS_CALL_FORCE_GENERIC", "JS_CALL_LANE_CHECK"], false)
    counts.array_receiver_state_references = callable_count_word(source, "js_array_method_real_this")
    counts.legacy_invoke_wrapper_references = callable_count_call(source, "js_invoke_fn")
    counts.legacy_class_map_bridge_definitions = callable_count_static_decl(
        source, "js_construct_entry_legacy_class_map")
    counts.legacy_class_map_bridge_references = callable_count_call(
        source, "js_construct_entry_legacy_class_map")
    counts.semantic_catalog_id_reads = callable_count_catalog_reads(source)
    var nonzero = {}
    for (name in callable_names) {
        if (counts[name] != null and counts[name] > 0) { nonzero[name] = counts[name] }
    }
    return nonzero
}

pub pn callable_census() any^ {
    let paths = callable_source_paths()
    var counts = {}
    for (name in callable_names) { counts[name] = 0 }
    var by_file = {}
    var runtime_text = ""
    var lowering_text = ""
    var catalog_text = ""
    for (path in paths) {
        let label = relative_path(path)
        let source = input(path, "text")^
        let file_counts = callable_file_counts(source)
        if (len(file_counts) > 0) {
            by_file[label] = file_counts
            for (name in callable_names) {
                if (file_counts[name] != null) { counts[name] = counts[name] + file_counts[name] }
            }
        }
        if (label == "lambda/js/js_runtime.cpp") { runtime_text = source }
        if (label == "lambda/js/js_mir_expression_lowering.cpp") { lowering_text = source }
        if (label == "lambda/js/js_builtin_catalog.def") { catalog_text = source }
    }
    counts.builtin_semantic_cases = callable_count_builtin_cases(runtime_text)
    var constructor_source = callable_extract_function(runtime_text, "js_construct_legacy_algorithm")
    if (constructor_source == "") {
        constructor_source = callable_extract_function(runtime_text, "js_new_from_class_object")
    }
    counts.constructor_name_comparisons = callable_count_constructor_names(constructor_source)
    counts.direct_global_catalog_shortcuts = callable_count_call(
        lowering_text, "js_builtin_global_find")
    counts.direct_global_identity_loads = callable_count_word(
        lowering_text, "js_get_global_builtin_fn_by_id")
    let catalog_errors = callable_catalog_errors(catalog_text)
    counts.catalog_validation_errors = len(catalog_errors)
    return {schema: 1,
            roots: ["lambda/js", "lambda/jube", "lambda/module/radiant", "radiant"],
            counts: counts, ratchet_max: callable_limits, by_file: by_file,
            catalog_errors: catalog_errors}
}

pub pn callable_census_print(result) {
    print("JS Tune4 callable census\n")
    var index = 0
    for (name in callable_names) {
        let last_line = index == len(callable_names) - 1 and len(result.catalog_errors) == 0
        print(name ++ ": " ++ string(result.counts[name]) ++ " (max " ++
              string(result.ratchet_max[name]) ++ ")" ++ (if (last_line) "" else "\n"))
        index = index + 1
    }
    index = 0
    for (entry in result.catalog_errors) {
        let last_line = index == len(result.catalog_errors) - 1
        print("catalog error: " ++ entry ++ (if (last_line) "" else "\n"))
        index = index + 1
    }
}

pub pn callable_census_check(result) {
    var failures = []
    for (name in callable_names) {
        if (result.counts[name] > result.ratchet_max[name]) {
            failures = failures ++ [name ++ ": " ++ string(result.counts[name]) ++
                                    " > " ++ string(result.ratchet_max[name])]
        }
    }
    if (len(failures) > 0) {
        print("callable census ratchet failures:\n")
        for (failure in failures) { print("  " ++ failure ++ "\n") }
        raise error("callable census ratchet failures")
    }
}
