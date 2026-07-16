#include "../lib/log.h"
#include "../lib/file.h"
#include "../lib/memtrack.h"
#include "../lambda/input/input.hpp"

#include <cstdio>
#include <cstring>

static char* graph_bridge_escape_lambda_string(const char* value) {
    if (!value) return nullptr;
    size_t out_len = 0;
    for (const char* cursor = value; *cursor; cursor++) {
        char ch = *cursor;
        out_len += (ch == '\\' || ch == '"' || ch == '\n' || ch == '\r' || ch == '\t') ? 2 : 1;
    }

    char* out = (char*)mem_alloc(out_len + 1, MEM_CAT_TEMP);
    if (!out) return nullptr;
    size_t pos = 0;
    for (const char* cursor = value; *cursor; cursor++) {
        char ch = *cursor;
        if (ch == '\\' || ch == '"') {
            out[pos++] = '\\';
            out[pos++] = ch;
        } else if (ch == '\n') {
            out[pos++] = '\\'; out[pos++] = 'n';
        } else if (ch == '\r') {
            out[pos++] = '\\'; out[pos++] = 'r';
        } else if (ch == '\t') {
            out[pos++] = '\\'; out[pos++] = 't';
        } else {
            out[pos++] = ch;
        }
    }
    out[pos] = '\0';
    return out;
}

const char* graph_bridge_flavor_for_path(const char* graph_file) {
    const char* ext = graph_file ? strrchr(graph_file, '.') : nullptr;
    if (ext && strcmp(ext, ".mmd") == 0) return "mermaid";
    if (ext && strcmp(ext, ".d2") == 0) return "d2";
    if (ext && (strcmp(ext, ".dsl") == 0 || strcmp(ext, ".structurizr") == 0))
        return "structurizr";
    return "dot";
}

bool graph_bridge_path_is_graph(const char* graph_file) {
    const char* ext = graph_file ? strrchr(graph_file, '.') : nullptr;
    if (!ext) return false;
    if (strcmp(ext, ".mmd") == 0 || strcmp(ext, ".d2") == 0 ||
            strcmp(ext, ".dot") == 0 || strcmp(ext, ".gv") == 0 ||
            strcmp(ext, ".structurizr") == 0) return true;
    if (strcmp(ext, ".dsl") != 0) return false;
    char* source = read_text_file(graph_file);
    if (!source) return false;
    // Share auto-input's boundary-safe detector so arbitrary DSL files remain ordinary text.
    bool structurizr = input_detect_structurizr_flavor(
        graph_file, source, strlen(source)) != nullptr;
    mem_free(source);
    return structurizr;
}

char* build_graph_to_html_bridge_script(const char* graph_file, const char* theme_name,
                                        const char* view_key, const char* log_prefix) {
    char* escaped_file = graph_bridge_escape_lambda_string(graph_file);
    char* escaped_theme = theme_name ? graph_bridge_escape_lambda_string(theme_name) : nullptr;
    char* escaped_view = view_key ? graph_bridge_escape_lambda_string(view_key) : nullptr;
    if (!escaped_file || (theme_name && !escaped_theme) || (view_key && !escaped_view)) {
        if (escaped_file) mem_free(escaped_file);
        if (escaped_theme) mem_free(escaped_theme);
        if (escaped_view) mem_free(escaped_view);
        log_error("[%s] GRAPH_BRIDGE_ESCAPE: failed to escape graph bridge arguments",
                  log_prefix ? log_prefix : "graph");
        return nullptr;
    }

    const char* flavor = graph_bridge_flavor_for_path(graph_file);
    const char* opts_prefix = escaped_theme ? "{theme: \"" : "null";
    const char* opts_theme = escaped_theme ? escaped_theme : "";
    const char* opts_suffix = escaped_theme ? "\"}" : "";
    const char* graph_source_format =
        "import graph_transform: lambda.package.graph.transform\n"
        "let graph^err = input(\"%s\", {type: \"graph\", flavor: \"%s\"})\n"
        "let installed = graph_transform.install()\n"
        "graph_transform.to_html(graph, %s%s%s)\n";
    const char* structurizr_source_format =
        "import graph_transform: lambda.package.graph.transform\n"
        "import structurizr: lambda.package.graph.structurizr.structurizr\n"
        "let source^err = input(\"%s\", {type: \"graph\", flavor: \"structurizr\"})\n"
        "let workspace = structurizr.normalize(source)\n"
        "let installed = graph_transform.install()\n"
        "structurizr.to_html(workspace, %s%s%s, %s%s%s)\n";
    bool is_structurizr = strcmp(flavor, "structurizr") == 0;
    const char* view_expr_prefix = escaped_view ? "\"" : "null";
    const char* view_expr_value = escaped_view ? escaped_view : "";
    const char* view_expr_suffix = escaped_view ? "\"" : "";
    int needed = is_structurizr
        ? snprintf(nullptr, 0, structurizr_source_format, escaped_file,
            view_expr_prefix, view_expr_value, view_expr_suffix,
            opts_prefix, opts_theme, opts_suffix)
        : snprintf(nullptr, 0, graph_source_format, escaped_file, flavor,
            opts_prefix, opts_theme, opts_suffix);
    if (needed <= 0) {
        mem_free(escaped_file);
        if (escaped_theme) mem_free(escaped_theme);
        if (escaped_view) mem_free(escaped_view);
        log_error("[%s] GRAPH_BRIDGE_SIZE: failed to size graph bridge script",
                  log_prefix ? log_prefix : "graph");
        return nullptr;
    }

    char* script = (char*)mem_alloc((size_t)needed + 1, MEM_CAT_TEMP);
    if (script) {
        if (is_structurizr) {
            snprintf(script, (size_t)needed + 1, structurizr_source_format, escaped_file,
                view_expr_prefix, view_expr_value, view_expr_suffix,
                opts_prefix, opts_theme, opts_suffix);
        } else {
            snprintf(script, (size_t)needed + 1, graph_source_format, escaped_file, flavor,
                opts_prefix, opts_theme, opts_suffix);
        }
    } else {
        log_error("[%s] GRAPH_BRIDGE_ALLOC: failed to allocate graph bridge script",
                  log_prefix ? log_prefix : "graph");
    }
    mem_free(escaped_file);
    if (escaped_theme) mem_free(escaped_theme);
    if (escaped_view) mem_free(escaped_view);
    return script;
}
