/**
 * markup_parser.cpp - Core markup parser implementation
 *
 * Implements the MarkupParser class methods for construction, configuration,
 * and error reporting. The actual block/inline parsing is delegated to
 * the shared parsers in block/*.cpp and inline/*.cpp.
 *
 * This file also provides the entry point that bridges the old API
 * (input-markup.cpp) to the new modular architecture.
 */
#include "markup_parser.hpp"
#include "format_adapter.hpp"
#include "lib/log.h"
#include <cstring>
#include <cstdlib>

namespace lambda {
namespace markup {

// ============================================================================
// Construction / Destruction
// ============================================================================

MarkupParser::MarkupParser(Input* input, const ParseConfig& cfg)
    : InputContext(input)
    , config(cfg)
    , adapter_(nullptr)
    , lines(nullptr)
    , line_count(0)
    , current_line(0)
    , link_def_count_(0)
{
    // Get format adapter
    if (config.format == Format::AUTO_DETECT) {
        // Will be set during parseContent based on content/filename
        adapter_ = FormatRegistry::getAdapter(Format::MARKDOWN);
    } else {
        adapter_ = FormatRegistry::getAdapter(config.format);
    }

    resetState();
}

MarkupParser::~MarkupParser() {
    freeLines();
}

void MarkupParser::resetState() {
    state.reset();
    current_line = 0;
}

// ============================================================================
// Line Management
// ============================================================================

void MarkupParser::splitLines(const char* content) {
    freeLines();

    if (!content || !*content) {
        lines = nullptr;
        line_count = 0;
        return;
    }

    // Count lines
    int count = 1;
    for (const char* p = content; *p; p++) {
        if (*p == '\n') count++;
    }

    // Allocate line array
    lines = (char**)calloc(count + 1, sizeof(char*));
    if (!lines) {
        log_error("markup_parser: failed to allocate line array");
        line_count = 0;
        return;
    }

    // Split into lines
    line_count = 0;
    const char* line_start = content;

    for (const char* p = content; ; p++) {
        if (*p == '\n' || *p == '\0') {
            size_t len = p - line_start;

            // Remove trailing \r if present
            if (len > 0 && line_start[len - 1] == '\r') {
                len--;
            }

            // Allocate and copy line
            char* line = (char*)malloc(len + 1);
            if (line) {
                memcpy(line, line_start, len);
                line[len] = '\0';
                lines[line_count++] = line;
            }

            if (*p == '\0') break;
            line_start = p + 1;
        }
    }
}

void MarkupParser::freeLines() {
    if (lines) {
        for (int i = 0; i < line_count; i++) {
            free(lines[i]);
        }
        free(lines);
        lines = nullptr;
    }
    line_count = 0;
    current_line = 0;
}

// ============================================================================
// Parsing
// ============================================================================

Item MarkupParser::parseContent(const char* content) {
    if (!content) {
        log_error("markup_parser: null content");
        return Item{.item = ITEM_NULL};
    }

    // Auto-detect format if needed
    if (config.format == Format::AUTO_DETECT) {
        const char* filename = nullptr;
        if (input() && input()->path) {
            filename = (const char*)input()->path;
        }
        adapter_ = FormatRegistry::detectAdapter(content, filename);
        log_debug("markup_parser: auto-detected format '%s'", adapter_->name());
    }

    // Split content into lines
    splitLines(content);

    if (line_count == 0) {
        log_debug("markup_parser: empty content");
        // Return empty document
        Element* doc = builder.element("doc").final().element;
        return Item{.item = (uint64_t)doc};
    }

    // Reset state
    resetState();

    // Parse document using modular block parsers
    log_debug("markup_parser: parsing %d lines with format '%s'",
              line_count, adapter_->name());

    return parse_document(this);
}

// ============================================================================
// Error Reporting
// ============================================================================

static const char* severity_name(ParseErrorSeverity sev) {
    switch (sev) {
        case ParseErrorSeverity::ERROR: return "error";
        case ParseErrorSeverity::WARNING: return "warning";
        case ParseErrorSeverity::NOTE: return "note";
        default: return "unknown";
    }
}

void MarkupParser::addMarkupError(MarkupErrorCategory category,
                                   const std::string& message,
                                   const std::string& hint) {
    // Get current source location
    SourceLocation loc = tracker.location();

    // Get context line
    std::string context;
    if (current_line >= 0 && current_line < line_count && lines[current_line]) {
        context = lines[current_line];
    }

    // Determine severity based on category
    ParseErrorSeverity severity;
    switch (category) {
        case MarkupErrorCategory::ENCODING:
            severity = ParseErrorSeverity::ERROR;
            break;
        case MarkupErrorCategory::UNEXPECTED:
        case MarkupErrorCategory::DEPRECATED:
            severity = ParseErrorSeverity::NOTE;
            break;
        default:
            severity = ParseErrorSeverity::WARNING;
    }

    // Create and add error
    ParseError err(loc, severity, message, context, hint);
    errors().addError(err);

    // Log for debugging
    log_debug("markup_parser: [%s] %s at line %zu: %s",
              category_name(category), severity_name(severity),
              loc.line, message.c_str());
}

void MarkupParser::warnUnclosed(const char* delimiter, size_t start_line) {
    std::string msg = "Unclosed ";
    msg += delimiter;
    msg += " (opened at line ";
    msg += std::to_string(start_line);
    msg += ")";

    std::string hint = "Add closing ";
    hint += delimiter;

    addMarkupError(MarkupErrorCategory::UNCLOSED, msg, hint);
}

void MarkupParser::warnInvalidSyntax(const char* construct, const char* expected) {
    std::string msg = "Invalid ";
    msg += construct;
    msg += " syntax";

    std::string hint = "Expected: ";
    hint += expected;

    addMarkupError(MarkupErrorCategory::SYNTAX, msg, hint);
}

void MarkupParser::noteUnresolvedReference(const char* ref_type, const char* ref_id) {
    std::string msg = "Unresolved ";
    msg += ref_type;
    msg += " reference: ";
    msg += ref_id;

    addMarkupError(MarkupErrorCategory::REFERENCE, msg,
                   "Define the reference or check spelling");
}

// ============================================================================
// Link Reference Definition Management
// ============================================================================

void MarkupParser::normalizeLabel(const char* label, size_t len, char* out, size_t out_size) {
    if (!label || !out || out_size == 0) return;

    size_t out_pos = 0;
    bool in_whitespace = true; // start true to skip leading whitespace

    for (size_t i = 0; i < len && out_pos < out_size - 1; i++) {
        char c = label[i];

        // Check for whitespace (space, tab, newline)
        bool is_ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r');

        if (is_ws) {
            if (!in_whitespace && out_pos < out_size - 1) {
                // collapse whitespace to single space
                out[out_pos++] = ' ';
            }
            in_whitespace = true;
        } else {
            // convert to lowercase
            if (c >= 'A' && c <= 'Z') {
                c = c - 'A' + 'a';
            }
            out[out_pos++] = c;
            in_whitespace = false;
        }
    }

    // trim trailing space
    if (out_pos > 0 && out[out_pos - 1] == ' ') {
        out_pos--;
    }

    out[out_pos] = '\0';
}

bool MarkupParser::addLinkDefinition(const char* label, size_t label_len,
                                      const char* url, size_t url_len,
                                      const char* title, size_t title_len) {
    if (!label || label_len == 0 || !url || url_len == 0) {
        return false;
    }

    if (link_def_count_ >= MAX_LINK_DEFINITIONS) {
        log_debug("markup_parser: link definition limit reached (%d)", MAX_LINK_DEFINITIONS);
        return false;
    }

    // normalize the label
    char normalized[256];
    normalizeLabel(label, label_len, normalized, sizeof(normalized));

    // check for duplicate (first definition wins per CommonMark)
    for (int i = 0; i < link_def_count_; i++) {
        if (strcmp(link_defs_[i].label, normalized) == 0) {
            // duplicate, ignore
            return false;
        }
    }

    // add new definition
    LinkDefinition& def = link_defs_[link_def_count_];

    strncpy(def.label, normalized, sizeof(def.label) - 1);
    def.label[sizeof(def.label) - 1] = '\0';

    size_t copy_len = (url_len < sizeof(def.url) - 1) ? url_len : sizeof(def.url) - 1;
    memcpy(def.url, url, copy_len);
    def.url[copy_len] = '\0';

    if (title && title_len > 0) {
        copy_len = (title_len < sizeof(def.title) - 1) ? title_len : sizeof(def.title) - 1;
        memcpy(def.title, title, copy_len);
        def.title[copy_len] = '\0';
        def.has_title = true;
    } else {
        def.title[0] = '\0';
        def.has_title = false;
    }

    link_def_count_++;
    log_debug("markup_parser: added link definition [%s] -> %s", def.label, def.url);
    return true;
}

const LinkDefinition* MarkupParser::getLinkDefinition(const char* label, size_t label_len) const {
    if (!label || label_len == 0) {
        return nullptr;
    }

    // normalize the label for lookup
    char normalized[256];
    normalizeLabel(label, label_len, normalized, sizeof(normalized));

    for (int i = 0; i < link_def_count_; i++) {
        if (strcmp(link_defs_[i].label, normalized) == 0) {
            return &link_defs_[i];
        }
    }

    return nullptr;
}

} // namespace markup
} // namespace lambda
// ============================================================================
// Bridge Functions - Connect new modular parser to old API
// ============================================================================

using namespace lambda;
using namespace lambda::markup;

/**
 * input_markup_modular - Entry point for modular markup parser
 *
 * This function provides a bridge from the input system to the new
 * modular parser architecture.
 */
extern "C" Item input_markup_modular(Input* input, const char* content) {
    if (!input || !content) {
        log_error("input_markup_modular: null input or content");
        return Item{.item = ITEM_ERROR};
    }

    // Create parser config
    ParseConfig cfg;
    cfg.format = Format::AUTO_DETECT;
    cfg.strict_mode = false;
    cfg.collect_metadata = true;
    cfg.resolve_refs = true;

    // Create and run parser
    MarkupParser parser(input, cfg);
    Item result = parser.parseContent(content);

    if (result.item == ITEM_ERROR) {
        log_error("input_markup_modular: parsing failed");
    }

    return result;
}
