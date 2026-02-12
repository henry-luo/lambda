/**
 * typst_adapter.cpp - Typst format adapter
 *
 * Implements format-specific detection rules for Typst markup language.
 * Typst is a modern typesetting system with syntax similar to Markdown
 * but with some key differences:
 *
 * - Headings use = instead of # (= H1, == H2, etc.)
 * - Strong uses single * (*bold*) instead of double **
 * - Numbered lists use + instead of 1.
 * - Term/definition lists use / Term: description
 * - Code expressions use # prefix (#let x = 1)
 * - Labels use <name> and references use @name
 * - Comments use // for single-line and block comments
 *
 * See: typst.app/docs/reference/syntax
 */
#include "../format_adapter.hpp"
#include <cstring>
#include <cctype>

namespace lambda {
namespace markup {

class TypstAdapter : public FormatAdapter {
public:
    // ========================================================================
    // Format Identification
    // ========================================================================

    Format format() const override { return Format::TYPST; }
    const char* name() const override { return "typst"; }

    const char* const* extensions() const override {
        static const char* exts[] = {".typ", ".typst", nullptr};
        return exts;
    }

    // ========================================================================
    // Block Detection
    // ========================================================================

    HeaderInfo detectHeader(const char* line, const char* next_line) override {
        (void)next_line;  // typst doesn't use setext-style headers
        HeaderInfo info;

        const char* p = line;

        // skip leading whitespace (typst allows some indentation)
        int spaces = 0;
        while (*p == ' ' && spaces < 4) { spaces++; p++; }
        if (spaces >= 4) return info;  // indented code, not header

        // Typst headings: = H1, == H2, === H3, etc.
        if (*p == '=') {
            int level = 0;
            while (*p == '=' && level < 7) { level++; p++; }

            // must be followed by space or end of line
            if (level >= 1 && level <= 6 &&
                (*p == ' ' || *p == '\t' || *p == '\0' || *p == '\r' || *p == '\n')) {
                info.level = level;
                info.valid = true;

                // skip whitespace after =
                while (*p == ' ' || *p == '\t') p++;
                info.text_start = p;

                // find end of line
                info.text_end = p;
                while (*info.text_end && *info.text_end != '\r' && *info.text_end != '\n') {
                    info.text_end++;
                }

                // trim trailing whitespace
                while (info.text_end > info.text_start &&
                       (*(info.text_end - 1) == ' ' || *(info.text_end - 1) == '\t')) {
                    info.text_end--;
                }
            }
        }

        return info;
    }

    ListItemInfo detectListItem(const char* line) override {
        ListItemInfo info;

        const char* p = line;

        // count leading whitespace
        while (*p == ' ') { info.indent++; p++; }
        if (*p == '\t') {
            info.indent = ((info.indent / 4) + 1) * 4;
            p++;
        }

        // Typst doesn't have the 4-space indented code rule for lists
        // but we still limit nesting depth

        // Bullet list: - item
        if (*p == '-' &&
            (*(p + 1) == ' ' || *(p + 1) == '\t' || *(p + 1) == '\0' || *(p + 1) == '\r' || *(p + 1) == '\n')) {
            info.marker = '-';
            info.is_ordered = false;
            info.marker_end = p + 1;

            // skip whitespace after marker
            p++;
            while (*p == ' ' || *p == '\t') p++;
            info.text_start = p;
            info.valid = true;
        }
        // Numbered list: + item (Typst auto-numbers)
        else if (*p == '+' &&
                 (*(p + 1) == ' ' || *(p + 1) == '\t' || *(p + 1) == '\0' || *(p + 1) == '\r' || *(p + 1) == '\n')) {
            info.marker = '+';
            info.is_ordered = true;
            info.number = 1;  // typst auto-increments
            info.marker_end = p + 1;

            // skip whitespace after marker
            p++;
            while (*p == ' ' || *p == '\t') p++;
            info.text_start = p;
            info.valid = true;
        }
        // Term list: / Term: description
        else if (*p == '/' && *(p + 1) == ' ') {
            info.marker = '/';
            info.is_ordered = false;
            // Note: term lists are like definition lists
            // The term ends at the colon
            info.marker_end = p + 1;

            p++;
            while (*p == ' ' || *p == '\t') p++;
            info.text_start = p;
            info.valid = true;
        }

        return info;
    }

    CodeFenceInfo detectCodeFence(const char* line) override {
        CodeFenceInfo info;

        const char* p = line;

        // skip up to 3 leading spaces
        while (*p == ' ' && info.indent < 4) { info.indent++; p++; }
        if (info.indent >= 4) return info;  // indented code, not fence

        // Typst uses backticks like Markdown: ```lang
        if (*p == '`') {
            int count = 0;
            while (*p == '`') { count++; p++; }

            // need at least 3 fence characters
            if (count >= 3) {
                info.fence_char = '`';
                info.fence_length = count;

                // skip whitespace before info string
                while (*p == ' ' || *p == '\t') p++;

                // info string (language identifier)
                info.info_string = p;

                // find end of info string
                const char* info_end = p;
                while (*info_end && *info_end != '\r' && *info_end != '\n' && *info_end != '`') {
                    info_end++;
                }

                // trim trailing whitespace from info string
                while (info_end > p && (*(info_end - 1) == ' ' || *(info_end - 1) == '\t')) {
                    info_end--;
                }

                info.info_length = info_end - p;
                info.valid = true;
            }
        }

        return info;
    }

    bool isCodeFenceClose(const char* line, const CodeFenceInfo& open_info) override {
        const char* p = line;

        // skip up to 3 leading spaces
        int indent = 0;
        while (*p == ' ' && indent < 4) { indent++; p++; }

        // if line has 4+ leading spaces, it's code content, not a closing fence
        if (indent >= 4) return false;

        // must match fence character
        if (*p != open_info.fence_char) return false;

        // count fence characters
        int fence_len = 0;
        while (*p == open_info.fence_char) { fence_len++; p++; }

        // must have at least as many as opening fence
        if (fence_len < open_info.fence_length) return false;

        // rest of line must be blank (only whitespace)
        while (*p == ' ' || *p == '\t') p++;
        return (*p == '\0' || *p == '\r' || *p == '\n');
    }

    BlockquoteInfo detectBlockquote(const char* line) override {
        // Typst doesn't have native blockquote syntax like Markdown's >
        // Users typically use #quote[] function instead
        // Return invalid to let it fall through to paragraph handling
        BlockquoteInfo info;
        (void)line;
        return info;
    }

    bool detectTable(const char* line, const char* next_line) override {
        // Typst uses #table() function instead of pipe syntax
        // For now, we don't detect tables in markup mode
        (void)line;
        (void)next_line;
        return false;
    }

    bool detectThematicBreak(const char* line) override {
        // Typst doesn't have a native thematic break syntax
        // Users use #line() or similar
        (void)line;
        return false;
    }

    bool detectIndentedCode(const char* line, const char** content_start) override {
        // Typst doesn't use 4-space indented code blocks
        // All code blocks must be fenced with backticks
        (void)line;
        (void)content_start;
        return false;
    }

    bool detectMetadata(const char* content) override {
        // Typst doesn't have YAML frontmatter
        // Metadata is typically set via #set document() or similar
        (void)content;
        return false;
    }

    // ========================================================================
    // Comment Detection (Typst-specific)
    // ========================================================================

    // detectComment - Check if line is a comment
    //
    // Typst supports C-style comments:
    // - Single line: // comment
    // - Multi-line: slash-star ... star-slash
    //
    // Returns: true if line starts with a comment
    bool detectComment(const char* line) {
        const char* p = line;

        // skip leading whitespace
        while (*p == ' ' || *p == '\t') p++;

        // single-line comment: //
        if (*p == '/' && *(p + 1) == '/') {
            return true;
        }

        // block comment start: /*
        if (*p == '/' && *(p + 1) == '*') {
            return true;
        }

        return false;
    }

    // ========================================================================
    // Inline Detection
    // ========================================================================

    // Typst emphasis delimiters:
    // - *bold* (single asterisk for bold, unlike Markdown's **)
    // - _italic_ (single underscore for italic)
    // Note: Typst doesn't support ** or __ for emphasis
    static constexpr DelimiterSpec typst_emphasis[] = {
        // Strong: *text* (single asterisk)
        {"*", "*", InlineType::BOLD, true, true},
        // Emphasis: _text_ (single underscore)
        {"_", "_", InlineType::ITALIC, true, true},
        // Inline code: `code`
        {"`", "`", InlineType::CODE, false, false},
    };

    const DelimiterSpec* emphasisDelimiters() const override { return typst_emphasis; }
    size_t emphasisDelimiterCount() const override { return sizeof(typst_emphasis) / sizeof(typst_emphasis[0]); }

    LinkInfo detectLink(const char* pos) override {
        LinkInfo info;

        // Typst links are typically:
        // 1. Bare URLs: https://example.com (auto-linked)
        // 2. Function syntax: #link("url")[text]
        //
        // For now, we detect bare URLs and let code expressions
        // be handled separately

        // Detect bare URL (http:// or https://)
        if (strncmp(pos, "http://", 7) == 0 || strncmp(pos, "https://", 8) == 0) {
            info.url_start = pos;
            const char* p = pos;

            // find end of URL (whitespace, newline, or certain punctuation)
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                // stop at certain trailing punctuation that's likely not part of URL
                if ((*p == '.' || *p == ',' || *p == ';' || *p == ':' || *p == '!' || *p == '?') &&
                    (*(p + 1) == ' ' || *(p + 1) == '\t' || *(p + 1) == '\n' || *(p + 1) == '\r' || *(p + 1) == '\0')) {
                    break;
                }
                p++;
            }

            if (p > pos) {
                info.url_end = p;
                info.text_start = info.url_start;
                info.text_end = info.url_end;
                info.end_pos = p;
                info.valid = true;
            }
        }

        return info;
    }

    LinkInfo detectImage(const char* pos) override {
        // Typst uses #image("path") function syntax
        // This is handled as a code expression, not inline markup
        (void)pos;
        return LinkInfo();
    }

    // ========================================================================
    // Feature Support
    // ========================================================================

    bool supportsFeature(const char* feature) const override {
        if (strcmp(feature, "math") == 0) return true;
        if (strcmp(feature, "code_expressions") == 0) return true;
        if (strcmp(feature, "labels") == 0) return true;
        if (strcmp(feature, "references") == 0) return true;
        if (strcmp(feature, "smart_quotes") == 0) return true;
        if (strcmp(feature, "definition_lists") == 0) return true;
        if (strcmp(feature, "comments") == 0) return true;
        if (strcmp(feature, "autolink") == 0) return true;
        return false;
    }

    char escapeChar() const override {
        return '\\';
    }

    const char* escapableChars() const override {
        // Typst escapes: \ ` * _ { } [ ] ( ) # + - . ! $ < > @ /
        return "\\`*_{}[]()#+-.!$<>@/";
    }
};

// Static instance for Typst adapter
static TypstAdapter s_typst_adapter;

// Accessor function for FormatRegistry
FormatAdapter* getTypstAdapter() {
    return &s_typst_adapter;
}

} // namespace markup
} // namespace lambda
