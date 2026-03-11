#ifndef LAMBDA_INPUT_GRAPH_H
#define LAMBDA_INPUT_GRAPH_H

#include "input.hpp"

#ifdef __cplusplus

#include "source_tracker.hpp"
#include "input-context.hpp"
#include <cctype>
#include <cstring>

// Shared helper: skip to end of current line (used by all graph parsers).
static inline void skip_to_eol(lambda::SourceTracker& tracker) {
    while (!tracker.atEnd() && tracker.current() != '\n')
        tracker.advance();
}

// Shared helper: skip whitespace, optional line comments, and optional C-style
// block comments.  Call sites pass the parser-specific comment markers.
//
//   line_comment1 / line_comment2 — single-line comment prefixes (e.g. "//",
//     "#", "%%").  Pass nullptr for unused slots.
//   block_comments — true enables C-style /* ... */ skipping (DOT only).
//
// Examples:
//   skip_wsc(tracker, "//", "#", true);   // DOT
//   skip_wsc(tracker, "#",  nullptr, false); // D2
//   skip_wsc(tracker, "%%", nullptr, false); // Mermaid
static inline void skip_wsc(lambda::SourceTracker& tracker,
                             const char* line_comment1,
                             const char* line_comment2,
                             bool block_comments) {
    while (!tracker.atEnd()) {
        char c = tracker.current();
        if (isspace(c)) { tracker.advance(); continue; }
        if (block_comments && c == '/' && tracker.peek(1) == '*') {
            tracker.advance(); tracker.advance(); // skip /*
            while (!tracker.atEnd() &&
                   !(tracker.current() == '*' && tracker.peek(1) == '/'))
                tracker.advance();
            if (!tracker.atEnd()) { tracker.advance(); tracker.advance(); } // skip */
            continue;
        }
        if (line_comment1 && tracker.match(line_comment1)) { skip_to_eol(tracker); continue; }
        if (line_comment2 && tracker.match(line_comment2)) { skip_to_eol(tracker); continue; }
        break;
    }
}

// Shared identifier reader for graph parsers.
//
//   extra            — additional chars (beyond isalnum + '_') valid anywhere
//                      in the identifier (e.g. "-." for D2, "-" for Mermaid).
//   require_alpha_start — if true the first char must be isalpha or '_';
//                         if false any char in the allowed set is accepted.
//
// Returns nullptr if no identifier chars are found.
static inline String* read_graph_identifier(lambda::InputContext& ctx,
                                            const char* extra,
                                            bool require_alpha_start) {
    lambda::SourceTracker& tracker = ctx.tracker;
    if (tracker.atEnd()) return nullptr;
    char c = tracker.current();
    bool start_ok = (isalpha(c) || c == '_') ||
                    (!require_alpha_start && (isdigit(c) || (extra && strchr(extra, c))));
    if (!start_ok) return nullptr;
    const char* start = tracker.rest();
    size_t len = 0;
    while (!tracker.atEnd()) {
        c = tracker.current();
        if (isalnum(c) || c == '_' || (extra && strchr(extra, c))) {
            tracker.advance(); len++;
        } else {
            break;
        }
    }
    if (len == 0) return nullptr;
    return ctx.builder.createString(start, len);
}

extern "C" {
#endif

// Graph parsing functions
void parse_graph(Input* input, const char* graph_string, const char* flavor);
void parse_graph_dot(Input* input, const char* dot_string);
void parse_graph_mermaid(Input* input, const char* mermaid_string);
void parse_graph_d2(Input* input, const char* d2_string);

// Helper functions for graph construction
Element* create_graph_element(Input* input, const char* type, const char* layout, const char* flavor);
Element* create_node_element(Input* input, const char* id, const char* label, const char* shape = nullptr);
Element* create_edge_element(Input* input, const char* from, const char* to, const char* label,
                             const char* style = nullptr, const char* arrow_start = nullptr,
                             const char* arrow_end = nullptr);
Element* create_cluster_element(Input* input, const char* id, const char* label);

// Attribute management
void add_graph_attribute(Input* input, Element* element, const char* name, const char* value);
void add_node_attributes(Input* input, Element* node, const char* attr_string);
void add_edge_attributes(Input* input, Element* edge, const char* attr_string);

// Graph structure manipulation
void add_node_to_graph(Input* input, Element* graph, Element* node);
void add_edge_to_graph(Input* input, Element* graph, Element* edge);
void add_cluster_to_graph(Input* input, Element* graph, Element* cluster);

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_INPUT_GRAPH_H
