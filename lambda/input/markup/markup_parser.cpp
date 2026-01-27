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
#include "block/block_common.hpp"
#include "../html5/html5_parser.h"
#include "../html_entities.h"
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
    , html5_parser_(nullptr)
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
    // html5_parser_ is pool-managed, no explicit cleanup needed
}

void MarkupParser::resetState() {
    state.reset();
    current_line = 0;
    html5_parser_ = nullptr;
}

// ============================================================================
// HTML5 Fragment Parser Interface
// ============================================================================

Html5Parser* MarkupParser::getOrCreateHtml5Parser() {
    if (!html5_parser_) {
        Input* inp = input();
        html5_parser_ = html5_fragment_parser_create(inp->pool, inp->arena, inp);
        if (html5_parser_) {
            log_debug("markup_parser: created HTML5 fragment parser");
        }
    }
    return html5_parser_;
}

bool MarkupParser::parseHtmlFragment(const char* html) {
    Html5Parser* parser = getOrCreateHtml5Parser();
    if (!parser) {
        log_error("markup_parser: failed to get HTML5 parser");
        return false;
    }
    return html5_fragment_parse(parser, html);
}

Element* MarkupParser::getHtmlBody() {
    if (!html5_parser_) return nullptr;
    return html5_fragment_get_body(html5_parser_);
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
        // Update config with detected format so block parsers can check it
        config.format = adapter_->format();
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

    // Pre-scan for link reference definitions (CommonMark: they can appear anywhere)
    // This ensures forward references work correctly
    if (config.format == Format::MARKDOWN) {
        for (int i = 0; i < line_count; i++) {
            const char* line = lines[i];
            if (line && is_link_definition_start(line)) {
                // Try to parse the link definition
                int saved_line = current_line;
                current_line = i;
                parse_link_definition(this, line);
                current_line = saved_line;
            }
        }
        log_debug("markup_parser: pre-scanned %d link definitions", link_def_count_);
    }

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

/**
 * is_escapable_char - Check if character can be backslash-escaped per CommonMark
 */
static bool is_escapable_char(char c) {
    return c == '!' || c == '"' || c == '#' || c == '$' || c == '%' ||
           c == '&' || c == '\'' || c == '(' || c == ')' || c == '*' ||
           c == '+' || c == ',' || c == '-' || c == '.' || c == '/' ||
           c == ':' || c == ';' || c == '<' || c == '=' || c == '>' ||
           c == '?' || c == '@' || c == '[' || c == '\\' || c == ']' ||
           c == '^' || c == '_' || c == '`' || c == '{' || c == '|' ||
           c == '}' || c == '~';
}

/**
 * unescape_to_buffer - Process backslash escapes and entity references in a string into a buffer
 *
 * Copies the input string to the output buffer, processing backslash escapes
 * and HTML entity references (both named and numeric).
 */
static void unescape_to_buffer(const char* src, size_t src_len, char* dst, size_t dst_size) {
    if (!src || !dst || dst_size == 0) return;

    size_t out_pos = 0;
    const char* pos = src;
    const char* end = src + src_len;

    while (pos < end && out_pos < dst_size - 1) {
        if (*pos == '\\' && pos + 1 < end && is_escapable_char(*(pos + 1))) {
            // skip the backslash, copy the escaped character
            pos++;
            dst[out_pos++] = *pos++;
        } else if (*pos == '&') {
            // Try to parse entity reference
            const char* entity_start = pos + 1;
            const char* entity_pos = entity_start;

            if (entity_pos < end && *entity_pos == '#') {
                // Numeric entity
                entity_pos++;
                uint32_t codepoint = 0;
                bool valid = false;

                if (entity_pos < end && (*entity_pos == 'x' || *entity_pos == 'X')) {
                    // Hex
                    entity_pos++;
                    const char* num_start = entity_pos;
                    while (entity_pos < end &&
                           ((*entity_pos >= '0' && *entity_pos <= '9') ||
                            (*entity_pos >= 'a' && *entity_pos <= 'f') ||
                            (*entity_pos >= 'A' && *entity_pos <= 'F'))) {
                        codepoint *= 16;
                        if (*entity_pos >= '0' && *entity_pos <= '9')
                            codepoint += *entity_pos - '0';
                        else if (*entity_pos >= 'a' && *entity_pos <= 'f')
                            codepoint += *entity_pos - 'a' + 10;
                        else
                            codepoint += *entity_pos - 'A' + 10;
                        entity_pos++;
                        if (codepoint > 0x10FFFF) break;
                    }
                    if (entity_pos > num_start && entity_pos < end && *entity_pos == ';' && codepoint <= 0x10FFFF) {
                        valid = true;
                    }
                } else {
                    // Decimal
                    const char* num_start = entity_pos;
                    while (entity_pos < end && *entity_pos >= '0' && *entity_pos <= '9') {
                        codepoint = codepoint * 10 + (*entity_pos - '0');
                        entity_pos++;
                        if (codepoint > 0x10FFFF) break;
                    }
                    if (entity_pos > num_start && entity_pos < end && *entity_pos == ';' && codepoint <= 0x10FFFF) {
                        valid = true;
                    }
                }

                if (valid && out_pos + 4 < dst_size) {
                    if (codepoint == 0) codepoint = 0xFFFD;
                    int utf8_len = unicode_to_utf8(codepoint, dst + out_pos);
                    if (utf8_len > 0) {
                        out_pos += utf8_len;
                        pos = entity_pos + 1;
                        continue;
                    }
                }
            } else {
                // Named entity
                while (entity_pos < end &&
                       ((*entity_pos >= 'a' && *entity_pos <= 'z') ||
                        (*entity_pos >= 'A' && *entity_pos <= 'Z') ||
                        (*entity_pos >= '0' && *entity_pos <= '9'))) {
                    entity_pos++;
                }

                if (entity_pos > entity_start && entity_pos < end && *entity_pos == ';') {
                    size_t name_len = entity_pos - entity_start;
                    EntityResult result = html_entity_resolve(entity_start, name_len);

                    if ((result.type == ENTITY_ASCII_ESCAPE || result.type == ENTITY_UNICODE_MULTI) && out_pos + strlen(result.decoded) < dst_size) {
                        size_t decoded_len = strlen(result.decoded);
                        memcpy(dst + out_pos, result.decoded, decoded_len);
                        out_pos += decoded_len;
                        pos = entity_pos + 1;
                        continue;
                    } else if ((result.type == ENTITY_UNICODE_SPACE || result.type == ENTITY_NAMED) && out_pos + 4 < dst_size) {
                        int utf8_len = unicode_to_utf8(result.named.codepoint, dst + out_pos);
                        if (utf8_len > 0) {
                            out_pos += utf8_len;
                            pos = entity_pos + 1;
                            continue;
                        }
                    }
                }
            }

            // Not a valid entity, copy & literally
            dst[out_pos++] = *pos++;
        } else {
            dst[out_pos++] = *pos++;
        }
    }

    dst[out_pos] = '\0';
}

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
    // Label is required, URL can be empty (e.g., [foo]: <>)
    if (!label || label_len == 0) {
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

    // unescape backslash escapes in URL and title
    // Handle empty URLs (e.g., [foo]: <>)
    if (url && url_len > 0) {
        unescape_to_buffer(url, url_len, def.url, sizeof(def.url));
    } else {
        def.url[0] = '\0';  // empty URL
    }

    if (title && title_len > 0) {
        unescape_to_buffer(title, title_len, def.title, sizeof(def.title));
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
