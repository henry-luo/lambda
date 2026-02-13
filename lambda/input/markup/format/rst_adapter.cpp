/**
 * rst_adapter.cpp - reStructuredText format adapter
 *
 * Implements format-specific detection rules for RST.
 * RST uses different conventions:
 * - Underline/overline headers with =, -, ~, ^, etc.
 * - Inline markup with single characters: *italic*, **bold**, ``code``
 * - Directives with .. directive::
 */
#include "../format_adapter.hpp"
#include <cstring>
#include <cctype>
#include "../../../../lib/str.h"

namespace lambda {
namespace markup {

class RstAdapter : public FormatAdapter {
public:
    Format format() const override { return Format::RST; }
    const char* name() const override { return "rst"; }

    const char* const* extensions() const override {
        static const char* exts[] = {".rst", ".rest", ".txt", nullptr};
        return exts;
    }

    HeaderInfo detectHeader(const char* line, const char* next_line) override {
        HeaderInfo info;

        // Safety check for line parameter
        if (!line) return info;

        // RST headers use underlines (and optionally overlines) with characters like = - ` : . ' " ~ ^ _ * + #
        // The character used determines the level based on first occurrence

        if (!next_line) return info;

        // Check if next line could be an underline
        const char* ul = next_line;
        while (*ul == ' ') ul++;

        // RST underline characters - must have at least one character
        if (!*ul) return info;  // Empty line is not a valid underline

        static const char* ul_chars = "=-`:.\'\"~^_*+#";
        if (!strchr(ul_chars, *ul)) return info;

        char ul_char = *ul;
        const char* ul_start = ul;
        while (*ul == ul_char) ul++;

        // Must be only underline characters (and trailing whitespace)
        while (*ul == ' ' || *ul == '\t') ul++;
        if (*ul != '\0' && *ul != '\r' && *ul != '\n') return info;

        // Underline must be at least as long as text
        size_t text_len = 0;
        const char* p = line;
        while (*p == ' ') p++;
        const char* text_start = p;
        while (*p && *p != '\r' && *p != '\n') { text_len++; p++; }

        // Trim trailing whitespace from text
        while (text_len > 0 && (text_start[text_len-1] == ' ' || text_start[text_len-1] == '\t')) {
            text_len--;
        }

        size_t ul_len = ul_start - next_line;
        while (next_line[ul_len] == ul_char) ul_len++;

        if (ul_len < text_len) return info;

        // Valid RST header - level is determined by first-seen order
        // For stub, use simple mapping: = -> 1, - -> 2, ~ -> 3, etc.
        static const char level_order[] = "=-~^`'\".*+#:_";
        const char* level_pos = strchr(level_order, ul_char);
        info.level = level_pos ? (int)(level_pos - level_order + 1) : 1;
        if (info.level > 6) info.level = 6;

        info.text_start = text_start;
        info.text_end = text_start + text_len;
        info.uses_underline = true;
        info.valid = true;

        return info;
    }

    ListItemInfo detectListItem(const char* line) override {
        ListItemInfo info;

        const char* p = line;
        while (*p == ' ') { info.indent++; p++; }

        // Bullet lists: *, +, -, •
        if ((*p == '*' || *p == '+' || *p == '-') && *(p+1) == ' ') {
            info.marker = *p;
            info.marker_end = p + 1;
            info.text_start = p + 2;
            info.valid = true;
        }

        // Enumerated lists: 1. or (1) or 1) or #.
        if (!info.valid) {
            if (*p == '#' && *(p+1) == '.') {
                info.marker = '#';
                info.number = 0; // Auto-number
                info.is_ordered = true;
                info.marker_end = p + 2;
                info.text_start = p + 3;
                info.valid = true;
            } else if (isdigit((unsigned char)*p)) {
                const char* num_start = p;
                while (isdigit((unsigned char)*p)) p++;
                if (*p == '.' || *p == ')') {
                    info.marker = *p;
                    info.number = (int)str_to_int64_default(num_start, strlen(num_start), 0);
                    info.is_ordered = true;
                    info.marker_end = p + 1;
                    info.text_start = p + 2;
                    info.valid = true;
                }
            }
        }

        return info;
    }

    CodeFenceInfo detectCodeFence(const char* line) override {
        // RST uses :: at end of paragraph, then indented block
        // Also supports code-block directive
        CodeFenceInfo info;

        const char* p = line;
        while (*p == ' ') { info.indent++; p++; }

        // Check for .. code-block:: or .. code::
        if (strncmp(p, ".. code-block::", 15) == 0 || strncmp(p, ".. code::", 9) == 0) {
            info.fence_char = '.';
            info.fence_length = 2;
            // Find language after ::
            const char* lang = strchr(p, ':');
            if (lang) {
                lang += 2; // Skip ::
                while (*lang == ' ') lang++;
                info.info_string = lang;
                const char* lang_end = lang;
                while (*lang_end && *lang_end != '\n' && *lang_end != '\r') lang_end++;
                info.info_length = lang_end - lang;
            }
            info.valid = true;
        }

        return info;
    }

    bool isCodeFenceClose(const char* line, const CodeFenceInfo& open_info) override {
        (void)open_info;
        // RST code blocks end when indentation decreases
        // This is handled at block level, not fence level
        return is_blank_line(line);
    }

    bool detectIndentedCode(const char* line, const char** content_start) override {
        // RST doesn't use 4-space indentation for code blocks
        // Instead, RST uses :: at the end of a paragraph followed by indented content
        // This is handled differently - indented blocks in RST are blockquotes
        (void)line;
        if (content_start) *content_start = nullptr;
        return false;
    }

    BlockquoteInfo detectBlockquote(const char* line) override {
        // RST blockquotes are indented blocks
        // They start with indentation (spaces, typically 3-4+)
        BlockquoteInfo info;

        // Count leading spaces
        const char* p = line;
        int indent = 0;
        while (*p == ' ') {
            indent++;
            p++;
        }

        // Need at least 3 spaces of indentation for blockquote
        // and non-empty content after the indent
        if (indent >= 3 && *p && *p != '\n' && *p != '\r') {
            // Not a directive (starting with ..)
            if (*p != '.' || *(p+1) != '.') {
                info.content_start = p;
                info.depth = 1;
                info.valid = true;
            }
        }

        return info;
    }

    bool detectTable(const char* line, const char* next_line) override {
        (void)next_line;
        // Skip leading whitespace
        const char* p = line;
        while (*p == ' ') p++;
        // RST grid tables: +---+---+
        if (*p == '+' && strstr(p, "-+")) return true;
        // RST simple tables: === === or ===  =====
        if (*p == '=') {
            // Count consecutive =
            int count = 0;
            while (*p == '=') { count++; p++; }
            // Must have at least 3 = and be followed by space or more =
            if (count >= 2) {
                while (*p == ' ') p++;
                if (*p == '\0' || *p == '\n' || *p == '\r' || *p == '=') {
                    return true;
                }
            }
        }
        return false;
    }

    bool detectThematicBreak(const char* line) override {
        // RST transitions: 4+ of -, =, _, etc.
        static const char* transition_chars = "-=_*+#";
        const char* p = line;
        while (*p == ' ') p++;
        if (!strchr(transition_chars, *p)) return false;
        char c = *p;
        int count = 0;
        while (*p == c) { count++; p++; }
        while (*p == ' ' || *p == '\t') p++;
        return count >= 4 && (*p == '\0' || *p == '\n' || *p == '\r');
    }

    static constexpr DelimiterSpec rst_emphasis[] = {
        {"**", "**", InlineType::BOLD, true, false},
        {"*", "*", InlineType::ITALIC, true, false},
        {"``", "``", InlineType::CODE, false, false},
    };

    const DelimiterSpec* emphasisDelimiters() const override { return rst_emphasis; }
    size_t emphasisDelimiterCount() const override { return sizeof(rst_emphasis)/sizeof(rst_emphasis[0]); }

    LinkInfo detectLink(const char* pos) override {
        LinkInfo info;
        // RST links: `text <url>`_ or `text`_ with reference
        if (*pos == '`') {
            const char* p = pos + 1;
            info.text_start = p;
            while (*p && *p != '`') p++;
            if (*p == '`' && *(p+1) == '_') {
                // Check for embedded URL: <url>
                const char* angle = strchr(info.text_start, '<');
                if (angle && angle < p) {
                    info.text_end = angle - 1;
                    while (info.text_end > info.text_start && *(info.text_end-1) == ' ') info.text_end--;
                    info.url_start = angle + 1;
                    info.url_end = p - 1;
                    while (info.url_end > info.url_start && *info.url_end != '>') info.url_end--;
                } else {
                    info.text_end = p;
                    info.is_reference = true;
                    info.ref_start = info.text_start;
                    info.ref_end = info.text_end;
                }
                info.end_pos = p + 2;
                info.valid = true;
            }
        }
        return info;
    }

    LinkInfo detectImage(const char* pos) override {
        // RST images: .. image:: path
        LinkInfo info;
        if (strncmp(pos, ".. image::", 10) == 0) {
            info.url_start = pos + 10;
            while (*info.url_start == ' ') info.url_start++;
            info.url_end = info.url_start;
            while (*info.url_end && *info.url_end != '\n' && *info.url_end != '\r') info.url_end++;
            info.valid = true;
        }
        return info;
    }

    bool supportsFeature(const char* feature) const override {
        if (strcmp(feature, "footnotes") == 0) return true;
        if (strcmp(feature, "definition_lists") == 0) return true;
        if (strcmp(feature, "tables") == 0) return true;
        return false;
    }
};

static RstAdapter s_rst_adapter;

// Accessor function for FormatRegistry
FormatAdapter* getRstAdapter() {
    return &s_rst_adapter;
}

} // namespace markup
} // namespace lambda
