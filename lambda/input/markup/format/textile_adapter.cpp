/**
 * textile_adapter.cpp - Textile format adapter
 *
 * Implements format-specific detection rules for Textile markup.
 * Textile uses:
 * - Headers with h1. h2. etc.
 * - Emphasis with _italic_, *bold*, @code@
 * - Links with "text":url
 * - Lists with * and #
 */
#include "../format_adapter.hpp"
#include <cstring>
#include <cctype>

namespace lambda {
namespace markup {

class TextileAdapter : public FormatAdapter {
public:
    Format format() const override { return Format::TEXTILE; }
    const char* name() const override { return "textile"; }

    const char* const* extensions() const override {
        static const char* exts[] = {".textile", nullptr};
        return exts;
    }

    HeaderInfo detectHeader(const char* line, const char* next_line) override {
        (void)next_line;
        HeaderInfo info;

        // Textile headers: h1. Header text
        if (*line == 'h' && isdigit((unsigned char)line[1]) && line[2] == '.') {
            int level = line[1] - '0';
            if (level >= 1 && level <= 6) {
                info.level = level;
                info.text_start = line + 3;
                while (*info.text_start == ' ') info.text_start++;
                info.text_end = info.text_start + strlen(info.text_start);
                while (info.text_end > info.text_start &&
                       (*(info.text_end-1) == '\n' || *(info.text_end-1) == '\r' ||
                        *(info.text_end-1) == ' ')) {
                    info.text_end--;
                }
                info.valid = true;
            }
        }

        return info;
    }

    ListItemInfo detectListItem(const char* line) override {
        ListItemInfo info;
        const char* p = line;

        // Textile lists: * or # (multiple for nesting)
        if (*p == '*' || *p == '#') {
            char marker = *p;
            int depth = 0;
            while (*p == marker) { depth++; p++; }

            if (*p == ' ' || *p == '\0') {
                info.marker = marker;
                info.indent = depth;
                info.is_ordered = (marker == '#');
                info.marker_end = p;
                while (*p == ' ') p++;
                info.text_start = p;
                info.valid = true;
            }
        }

        return info;
    }

    CodeFenceInfo detectCodeFence(const char* line) override {
        CodeFenceInfo info;

        // Textile uses bc. for block code or pre. for preformatted
        if (strncmp(line, "bc.", 3) == 0 || strncmp(line, "pre.", 4) == 0) {
            info.fence_char = 'b';
            info.fence_length = 3;
            info.valid = true;
        }

        return info;
    }

    bool isCodeFenceClose(const char* line, const CodeFenceInfo& open_info) override {
        (void)open_info;
        // Textile code blocks end with blank line or new block
        return is_blank_line(line);
    }

    BlockquoteInfo detectBlockquote(const char* line) override {
        BlockquoteInfo info;

        // Textile: bq. for blockquote
        if (strncmp(line, "bq.", 3) == 0) {
            info.depth = 1;
            info.content_start = line + 3;
            while (*info.content_start == ' ') info.content_start++;
            info.valid = true;
        }

        return info;
    }

    bool detectTable(const char* line, const char* next_line) override {
        (void)next_line;
        // Textile tables: |cell|cell|
        const char* p = line;
        while (*p == ' ') p++;
        return *p == '|';
    }

    bool detectThematicBreak(const char* line) override {
        // Textile doesn't have a standard HR, but some use ---
        const char* p = line;
        while (*p == ' ') p++;
        int dashes = 0;
        while (*p == '-') { dashes++; p++; }
        return dashes >= 3 && (*p == '\0' || *p == '\n');
    }

    // Textile emphasis
    static constexpr DelimiterSpec textile_emphasis[] = {
        {"*", "*", InlineType::BOLD, true, false},
        {"_", "_", InlineType::ITALIC, true, false},
        {"-", "-", InlineType::STRIKETHROUGH, true, false},
        {"+", "+", InlineType::UNDERLINE, true, false},
        {"@", "@", InlineType::CODE, false, false},
        {"^", "^", InlineType::SUPERSCRIPT, false, false},
        {"~", "~", InlineType::SUBSCRIPT, false, false},
    };

    const DelimiterSpec* emphasisDelimiters() const override { return textile_emphasis; }
    size_t emphasisDelimiterCount() const override { return sizeof(textile_emphasis)/sizeof(textile_emphasis[0]); }

    LinkInfo detectLink(const char* pos) override {
        LinkInfo info;

        // Textile links: "text":url or "text(title)":url
        if (*pos == '"') {
            const char* p = pos + 1;
            info.text_start = p;

            // Find closing "
            while (*p && *p != '"') p++;
            if (*p != '"') return info;

            // Check for (title)
            info.text_end = p;
            const char* title_start = nullptr;
            const char* title_end = nullptr;
            if (*(p-1) == ')') {
                // Find matching (
                const char* paren = p - 2;
                while (paren > info.text_start && *paren != '(') paren--;
                if (*paren == '(') {
                    title_start = paren + 1;
                    title_end = p - 1;
                    info.text_end = paren;
                }
            }

            p++; // Skip "

            // Must be followed by :
            if (*p != ':') return info;
            p++;

            info.url_start = p;
            // URL ends at whitespace or end of line
            while (*p && !isspace((unsigned char)*p)) p++;
            info.url_end = p;

            if (title_start) {
                info.title_start = title_start;
                info.title_end = title_end;
            }

            info.end_pos = p;
            info.valid = true;
        }

        return info;
    }

    LinkInfo detectImage(const char* pos) override {
        LinkInfo info;

        // Textile images: !url! or !url(alt)!
        if (*pos == '!') {
            const char* p = pos + 1;
            info.url_start = p;

            while (*p && *p != '!' && *p != '(') p++;

            if (*p == '(') {
                info.url_end = p;
                p++;
                info.text_start = p; // Alt text
                while (*p && *p != ')') p++;
                info.text_end = p;
                if (*p == ')') p++;
                if (*p == '!') p++;
            } else if (*p == '!') {
                info.url_end = p;
                info.text_start = info.url_start;
                info.text_end = info.url_end;
                p++;
            }

            info.end_pos = p;
            info.valid = true;
        }

        return info;
    }

    bool supportsFeature(const char* feature) const override {
        if (strcmp(feature, "tables") == 0) return true;
        if (strcmp(feature, "footnotes") == 0) return true;
        return false;
    }
};

static TextileAdapter s_textile_adapter;

// Accessor function for FormatRegistry
FormatAdapter* getTextileAdapter() {
    return &s_textile_adapter;
}

} // namespace markup
} // namespace lambda
