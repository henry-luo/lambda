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

    // Parse document
    // NOTE: parse_document is implemented in the old input-markup.cpp for now
    // In Phase 2, it will be moved to block/block_document.cpp
    log_debug("markup_parser: parsing %d lines with format '%s'",
              line_count, adapter_->name());

    // For Phase 1, we just return the basic structure
    // The actual parsing is still done by the old code
    Element* doc = builder.element("doc").final().element;
    if (!doc) {
        log_error("markup_parser: failed to create document element");
        return Item{.item = ITEM_ERROR};
    }

    return Item{.item = (uint64_t)doc};
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

} // namespace markup
} // namespace lambda
