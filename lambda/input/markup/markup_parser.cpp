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
#include "../markup-format.h"
#include "../../utf_string.h"
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

    // Pre-scan for link reference definitions at document level only
    // We need this for forward references (links before their definitions)
    // BUT we must skip code blocks and respect paragraph boundaries
    if (config.format == Format::MARKDOWN) {
        bool in_fenced_code = false;
        char fence_char = 0;
        int fence_length = 0;
        bool in_paragraph = false;

        for (int i = 0; i < line_count; i++) {
            const char* line = lines[i];
            if (!line) continue;

            // Skip leading whitespace for block detection
            const char* pos = line;
            int leading_spaces = 0;
            while (*pos == ' ' && leading_spaces < 4) { leading_spaces++; pos++; }

            // Check for fenced code block start/end
            if (!in_fenced_code && leading_spaces < 4 && (*pos == '`' || *pos == '~')) {
                char c = *pos;
                int count = 0;
                while (*pos == c) { count++; pos++; }
                if (count >= 3) {
                    in_fenced_code = true;
                    fence_char = c;
                    fence_length = count;
                    continue;
                }
            } else if (in_fenced_code) {
                // Check for closing fence
                if (leading_spaces < 4 && *pos == fence_char) {
                    int count = 0;
                    while (*pos == fence_char) { count++; pos++; }
                    // Skip trailing whitespace
                    while (*pos == ' ' || *pos == '\t') pos++;
                    if (count >= fence_length && (*pos == '\0' || *pos == '\n' || *pos == '\r')) {
                        in_fenced_code = false;
                    }
                }
                continue; // Skip everything inside fenced code
            }

            // Check for blank line (resets paragraph state)
            bool is_blank = true;
            for (const char* check = line; *check; check++) {
                if (*check != ' ' && *check != '\t' && *check != '\n' && *check != '\r') {
                    is_blank = false;
                    break;
                }
            }
            if (is_blank) {
                in_paragraph = false;
                continue;
            }

            // If we're in a paragraph continuation, link definitions cannot appear
            if (in_paragraph) {
                // This line continues the paragraph - not a valid link definition position
                continue;
            }

            // Check for indented code block (4+ spaces) - skip these
            if (leading_spaces >= 4) {
                continue;
            }

            // Check for blockquote - strip > marker and check for link def inside
            if (*pos == '>') {
                // Strip blockquote markers
                const char* content = pos;
                while (*content == '>') {
                    content++;
                    if (*content == ' ') content++;
                }
                // Skip leading spaces in content
                while (*content == ' ' && (content - pos) < 4) content++;

                // Check if this is a link definition inside blockquote
                if (is_link_definition_start(content)) {
                    int saved_line = current_line;
                    current_line = i;
                    // Create temporary adjusted line for parsing
                    // Note: parse_link_definition will handle it at blockquote-stripped level
                    // We need to pass the content after > marker
                    if (parse_link_definition(this, content)) {
                        i = current_line;
                    }
                    current_line = saved_line;
                }
                // Blockquotes don't start paragraphs at document level
                continue;
            }

            // Try to parse link definition at document level
            if (is_link_definition_start(line)) {
                int saved_line = current_line;
                current_line = i;
                if (parse_link_definition(this, line)) {
                    // Successfully parsed - skip any additional lines consumed
                    // The parser advances current_line, so we update i
                    i = current_line;
                } else {
                    // Not a valid link definition - treat as start of paragraph
                    in_paragraph = true;
                }
                current_line = saved_line;
            } else {
                // Start of a non-link-definition block - check if it's a paragraph
                // Paragraph: anything that isn't another block type
                pos = line;
                while (*pos == ' ' && leading_spaces < 4) pos++;

                // Check for block types that are NOT paragraphs
                bool is_paragraph = true;
                if (*pos == '#') is_paragraph = false;  // ATX header
                // Blockquote already handled above
                if (*pos == '-' || *pos == '*' || *pos == '+') {
                    if (*(pos+1) == ' ' || *(pos+1) == '\t') is_paragraph = false;  // List
                }
                // Add more block type checks as needed...

                if (is_paragraph) {
                    in_paragraph = true;
                }
            }
        }
        log_debug("markup_parser: pre-scanned %d link definitions", link_def_count_);
    }

    // Pre-scan for RST link definitions: .. _label: URL
    if (config.format == Format::RST) {
        for (int i = 0; i < line_count; i++) {
            const char* line = lines[i];
            if (!line) continue;

            // Skip leading whitespace
            const char* p = line;
            while (*p == ' ' || *p == '\t') p++;

            // Check for RST link definition: .. _label: URL
            if (strncmp(p, ".. _", 4) == 0) {
                p += 4;
                const char* label_start = p;

                // Find label end (terminated by :)
                while (*p && *p != ':' && *p != '\n' && *p != '\r') {
                    p++;
                }

                if (*p == ':') {
                    size_t label_len = p - label_start;
                    p++; // skip :

                    // Skip whitespace
                    while (*p == ' ' || *p == '\t') p++;

                    // Get URL (rest of line)
                    const char* url_start = p;
                    while (*p && *p != '\n' && *p != '\r') p++;
                    size_t url_len = p - url_start;

                    // Trim trailing whitespace
                    while (url_len > 0 && (url_start[url_len-1] == ' ' || url_start[url_len-1] == '\t')) {
                        url_len--;
                    }

                    if (label_len > 0 && url_len > 0) {
                        addLinkDefinition(label_start, label_len, url_start, url_len, nullptr, 0);
                        log_debug("markup_parser: RST link def found at line %d", i);
                    }
                }
            }
        }
        log_debug("markup_parser: pre-scanned %d RST link definitions", link_def_count_);
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
                                   const char* message,
                                   const char* hint) {
    // Get current source location
    SourceLocation loc = tracker.location();

    // Get context line
    const char* context = nullptr;
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
    ParseError err(loc, severity, message, context ? context : "", hint ? hint : "");
    errors().addError(err);

    // Log for debugging
    log_debug("markup_parser: [%s] %s at line %zu: %s",
              category_name(category), severity_name(severity),
              loc.line, message);
}

void MarkupParser::warnUnclosed(const char* delimiter, size_t start_line) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Unclosed %s (opened at line %zu)", delimiter, start_line);

    char hint[64];
    snprintf(hint, sizeof(hint), "Add closing %s", delimiter);

    addMarkupError(MarkupErrorCategory::UNCLOSED, msg, hint);
}

void MarkupParser::warnInvalidSyntax(const char* construct, const char* expected) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Invalid %s syntax", construct);

    char hint[128];
    snprintf(hint, sizeof(hint), "Expected: %s", expected);

    addMarkupError(MarkupErrorCategory::SYNTAX, msg, hint);
}

void MarkupParser::noteUnresolvedReference(const char* ref_type, const char* ref_id) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Unresolved %s reference: %s", ref_type, ref_id);

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

    // First, collapse whitespace and trim
    // We need to do this before Unicode case folding
    char* temp = (char*)malloc(len + 1);
    if (!temp) {
        out[0] = '\0';
        return;
    }

    size_t temp_pos = 0;
    bool in_whitespace = true; // start true to skip leading whitespace

    for (size_t i = 0; i < len; i++) {
        char c = label[i];

        // Check for whitespace (space, tab, newline)
        bool is_ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r');

        if (is_ws) {
            if (!in_whitespace) {
                // collapse whitespace to single space
                temp[temp_pos++] = ' ';
            }
            in_whitespace = true;
        } else {
            temp[temp_pos++] = c;
            in_whitespace = false;
        }
    }

    // trim trailing space
    if (temp_pos > 0 && temp[temp_pos - 1] == ' ') {
        temp_pos--;
    }

    temp[temp_pos] = '\0';

    // Now apply Unicode case folding using utf8proc
    int folded_len = 0;
    char* folded = normalize_utf8proc_casefold(temp, (int)temp_pos, &folded_len);
    free(temp);

    if (folded && folded_len > 0) {
        // Copy the folded result to output
        size_t copy_len = (size_t)folded_len < out_size - 1 ? (size_t)folded_len : out_size - 1;
        memcpy(out, folded, copy_len);
        out[copy_len] = '\0';
        free(folded);
    } else {
        // Fallback: if case folding fails, do simple ASCII lowercase
        if (folded) free(folded);
        out[0] = '\0';
    }
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

/**
 * label_contains_unescaped_brackets - Check if label has unescaped [ or ]
 *
 * CommonMark: "Link labels cannot contain brackets, unless they are backslash-escaped"
 */
static bool label_contains_unescaped_brackets(const char* label, size_t label_len) {
    const char* pos = label;
    const char* end = label + label_len;

    while (pos < end) {
        if (*pos == '\\' && pos + 1 < end) {
            // Skip escaped character
            pos += 2;
            continue;
        }
        if (*pos == '[' || *pos == ']') {
            return true;  // Unescaped bracket found
        }
        pos++;
    }
    return false;
}

const LinkDefinition* MarkupParser::getLinkDefinition(const char* label, size_t label_len) const {
    if (!label || label_len == 0) {
        return nullptr;
    }

    // CommonMark: Link labels cannot contain unescaped brackets
    if (label_contains_unescaped_brackets(label, label_len)) {
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

/**
 * input_markup_commonmark - Parse strict CommonMark (no GFM extensions)
 *
 * This function parses markdown using strict CommonMark rules without
 * GFM extensions like tables, task lists, and strikethrough.
 */
extern "C" Item input_markup_commonmark(Input* input, const char* content) {
    if (!input || !content) {
        log_error("input_markup_commonmark: null input or content");
        return Item{.item = ITEM_ERROR};
    }

    log_debug("input_markup_commonmark: ENTRY - using COMMONMARK flavor");

    // Create parser config with COMMONMARK flavor
    ParseConfig cfg;
    cfg.format = Format::MARKDOWN;
    cfg.flavor = Flavor::COMMONMARK;
    cfg.strict_mode = false;
    cfg.collect_metadata = true;
    cfg.resolve_refs = true;



    // Create and run parser
    MarkupParser parser(input, cfg);
    
    Item result = parser.parseContent(content);

    if (result.item == ITEM_ERROR) {
        log_error("input_markup_commonmark: parsing failed");
    }

    return result;
}

// Helper to convert MarkupFormat (C enum) to Format (C++ enum class)
static Format markup_format_to_format(MarkupFormat mf) {
    switch (mf) {
        case MARKUP_MARKDOWN:    return Format::MARKDOWN;
        case MARKUP_RST:         return Format::RST;
        case MARKUP_TEXTILE:     return Format::TEXTILE;
        case MARKUP_WIKI:        return Format::WIKI;
        case MARKUP_ORG:         return Format::ORG;
        case MARKUP_ASCIIDOC:    return Format::ASCIIDOC;
        case MARKUP_MAN:         return Format::MAN;
        case MARKUP_AUTO_DETECT:
        default:                 return Format::AUTO_DETECT;
    }
}

/**
 * input_markup - Main entry point for unified markup parsing
 *
 * This function provides a bridge from the input system to the modular
 * parser architecture. Format is auto-detected from content.
 */
extern "C" Item input_markup(Input* input, const char* content) {
    return input_markup_modular(input, content);
}

/**
 * input_markup_with_format - Parse markup with explicit format
 *
 * This function allows specifying the markup format explicitly instead
 * of auto-detecting from content.
 */
extern "C" Item input_markup_with_format(Input* input, const char* content, MarkupFormat format) {
    if (!input || !content) {
        log_error("input_markup_with_format: null input or content");
        return Item{.item = ITEM_ERROR};
    }

    log_debug("input_markup_with_format: called with format=%d", (int)format);

    // Create parser config with specified format
    ParseConfig cfg;
    cfg.format = markup_format_to_format(format);
    log_debug("input_markup_with_format: set cfg.format=%d", (int)cfg.format);
    cfg.strict_mode = false;
    cfg.collect_metadata = true;
    cfg.resolve_refs = true;

    // Create and run parser
    MarkupParser parser(input, cfg);
    Item result = parser.parseContent(content);

    if (result.item == ITEM_ERROR) {
        log_error("input_markup_with_format: parsing failed");
    }

    return result;
}
