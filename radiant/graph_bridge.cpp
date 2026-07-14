#include "../lib/log.h"
#include "../lib/memtrack.h"

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
    return "dot";
}

char* build_graph_to_html_bridge_script(const char* graph_file, const char* theme_name,
                                        const char* log_prefix) {
    char* escaped_file = graph_bridge_escape_lambda_string(graph_file);
    char* escaped_theme = theme_name ? graph_bridge_escape_lambda_string(theme_name) : nullptr;
    if (!escaped_file || (theme_name && !escaped_theme)) {
        if (escaped_file) mem_free(escaped_file);
        if (escaped_theme) mem_free(escaped_theme);
        log_error("[%s] GRAPH_BRIDGE_ESCAPE: failed to escape graph bridge arguments",
                  log_prefix ? log_prefix : "graph");
        return nullptr;
    }

    const char* flavor = graph_bridge_flavor_for_path(graph_file);
    const char* opts_prefix = escaped_theme ? "{theme: \"" : "null";
    const char* opts_theme = escaped_theme ? escaped_theme : "";
    const char* opts_suffix = escaped_theme ? "\"}" : "";
    const char* source_format =
        "import graph_transform: lambda.package.graph.transform\n"
        "let graph^err = input(\"%s\", {type: \"graph\", flavor: \"%s\"})\n"
        "let installed = graph_transform.install()\n"
        "graph_transform.to_html(graph, %s%s%s)\n";
    int needed = snprintf(nullptr, 0, source_format, escaped_file, flavor,
                          opts_prefix, opts_theme, opts_suffix);
    if (needed <= 0) {
        mem_free(escaped_file);
        if (escaped_theme) mem_free(escaped_theme);
        log_error("[%s] GRAPH_BRIDGE_SIZE: failed to size graph bridge script",
                  log_prefix ? log_prefix : "graph");
        return nullptr;
    }

    char* script = (char*)mem_alloc((size_t)needed + 1, MEM_CAT_TEMP);
    if (script) {
        snprintf(script, (size_t)needed + 1, source_format, escaped_file, flavor,
                 opts_prefix, opts_theme, opts_suffix);
    } else {
        log_error("[%s] GRAPH_BRIDGE_ALLOC: failed to allocate graph bridge script",
                  log_prefix ? log_prefix : "graph");
    }
    mem_free(escaped_file);
    if (escaped_theme) mem_free(escaped_theme);
    return script;
}
