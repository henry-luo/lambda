/**
 * man_adapter.cpp - Unix man page (troff) format adapter
 *
 * Implements format-specific detection rules for man pages.
 * Man pages use troff/groff macros:
 * - .TH for title
 * - .SH for section headers
 * - .SS for subsection headers
 * - .B, .I for bold/italic
 * - .TP, .IP for list items
 * - \fB, \fI for inline formatting
 */
#include "../format_adapter.hpp"
#include <cstring>
#include <cctype>

namespace lambda {
namespace markup {

class ManAdapter : public FormatAdapter {
public:
    Format format() const override { return Format::MAN; }
    const char* name() const override { return "man"; }

    const char* const* extensions() const override {
        static const char* exts[] = {".1", ".2", ".3", ".4", ".5", ".6", ".7", ".8", ".9",
                                     ".1m", ".3p", ".man", nullptr};
        return exts;
    }

    HeaderInfo detectHeader(const char* line, const char* next_line) override {
        (void)next_line;
        HeaderInfo info;

        // Man section headers: .SH "Section Name" or .SH Section Name
        // Subsection: .SS "Subsection"
        if (line[0] != '.') return info;

        if (strncmp(line, ".SH", 3) == 0) {
            info.level = 1;
            info.text_start = line + 3;
        } else if (strncmp(line, ".SS", 3) == 0) {
            info.level = 2;
            info.text_start = line + 3;
        } else {
            return info;
        }

        // Skip whitespace
        while (*info.text_start == ' ' || *info.text_start == '\t') info.text_start++;

        // Remove quotes if present
        if (*info.text_start == '"') {
            info.text_start++;
            info.text_end = info.text_start;
            while (*info.text_end && *info.text_end != '"') info.text_end++;
        } else {
            info.text_end = info.text_start;
            while (*info.text_end && *info.text_end != '\n' && *info.text_end != '\r') info.text_end++;
        }

        // Trim trailing whitespace
        while (info.text_end > info.text_start &&
               (*(info.text_end-1) == ' ' || *(info.text_end-1) == '\t')) {
            info.text_end--;
        }

        info.valid = true;
        return info;
    }

    ListItemInfo detectListItem(const char* line) override {
        ListItemInfo info;

        // Man list items: .TP, .IP, .HP
        if (line[0] != '.') return info;

        if (strncmp(line, ".TP", 3) == 0) {
            // Tagged paragraph - next line is the tag
            info.marker = 'T';
            info.marker_end = line + 3;
            info.text_start = line + 3;
            while (*info.text_start == ' ') info.text_start++;
            info.valid = true;
        } else if (strncmp(line, ".IP", 3) == 0) {
            // Indented paragraph
            info.marker = '-';
            info.marker_end = line + 3;
            info.text_start = line + 3;
            while (*info.text_start == ' ') info.text_start++;
            // Skip optional tag in quotes
            if (*info.text_start == '"') {
                info.text_start++;
                while (*info.text_start && *info.text_start != '"') info.text_start++;
                if (*info.text_start == '"') info.text_start++;
                while (*info.text_start == ' ') info.text_start++;
            }
            info.valid = true;
        }

        return info;
    }

    CodeFenceInfo detectCodeFence(const char* line) override {
        CodeFenceInfo info;

        // Man uses .nf (no-fill) and .fi (fill) for preformatted text
        if (strncmp(line, ".nf", 3) == 0) {
            info.fence_char = '.';
            info.fence_length = 3;
            info.valid = true;
        }
        // Also .EX / .EE in some man pages
        else if (strncmp(line, ".EX", 3) == 0) {
            info.fence_char = 'E';
            info.fence_length = 3;
            info.valid = true;
        }

        return info;
    }

    bool isCodeFenceClose(const char* line, const CodeFenceInfo& open_info) override {
        if (open_info.fence_char == '.') {
            return strncmp(line, ".fi", 3) == 0;
        } else if (open_info.fence_char == 'E') {
            return strncmp(line, ".EE", 3) == 0;
        }
        return false;
    }

    BlockquoteInfo detectBlockquote(const char* line) override {
        BlockquoteInfo info;
        // Man uses .RS / .RE for indented blocks
        if (strncmp(line, ".RS", 3) == 0) {
            info.depth = 1;
            info.content_start = line + 3;
            info.valid = true;
        }
        return info;
    }

    bool detectTable(const char* line, const char* next_line) override {
        (void)next_line;
        // Man uses .TS / .TE for tables
        return strncmp(line, ".TS", 3) == 0;
    }

    bool detectThematicBreak(const char* line) override {
        // Man doesn't really have horizontal rules
        // But .sp or blank lines can serve as separators
        return false;
    }

    bool detectIndentedCode(const char* line, const char** content_start) override {
        // Man doesn't use indentation for code
        (void)line;
        (void)content_start;
        return false;
    }

    // Man inline formatting uses escape sequences
    // \fB = bold, \fI = italic, \fR = roman (normal), \fP = previous
    static constexpr DelimiterSpec man_emphasis[] = {
        {"\\fB", "\\fR", InlineType::BOLD, false, false},
        {"\\fB", "\\fP", InlineType::BOLD, false, false},
        {"\\fI", "\\fR", InlineType::ITALIC, false, false},
        {"\\fI", "\\fP", InlineType::ITALIC, false, false},
        {"\\fB\\fI", "\\fR", InlineType::BOLD_ITALIC, false, false},
        {"\\fI\\fB", "\\fR", InlineType::BOLD_ITALIC, false, false},
    };

    const DelimiterSpec* emphasisDelimiters() const override { return man_emphasis; }
    size_t emphasisDelimiterCount() const override { return sizeof(man_emphasis)/sizeof(man_emphasis[0]); }

    LinkInfo detectLink(const char* pos) override {
        // Man pages don't have hyperlinks in traditional sense
        // But we can detect URLs
        LinkInfo info;

        if (strncmp(pos, "http://", 7) == 0 || strncmp(pos, "https://", 8) == 0) {
            info.url_start = pos;
            info.url_end = pos;
            while (*info.url_end && !isspace((unsigned char)*info.url_end) &&
                   *info.url_end != '>' && *info.url_end != ')') {
                info.url_end++;
            }
            info.text_start = info.url_start;
            info.text_end = info.url_end;
            info.end_pos = info.url_end;
            info.valid = true;
        }

        return info;
    }

    LinkInfo detectImage(const char* pos) override {
        // Man pages don't have images
        (void)pos;
        return LinkInfo();
    }

    bool isEscaped(const char* text, const char* pos) override {
        // Man uses backslash escaping
        if (pos <= text) return false;
        return *(pos - 1) == '\\';
    }

    char escapeChar() const override { return '\\'; }

    bool supportsFeature(const char* feature) const override {
        if (strcmp(feature, "bold") == 0) return true;
        if (strcmp(feature, "italic") == 0) return true;
        return false;
    }

    // Man-specific: check if line is a macro
    bool isMacro(const char* line) const {
        return line[0] == '.';
    }

    // Get macro name (without leading .)
    const char* getMacroName(const char* line, size_t* len) const {
        if (line[0] != '.') return nullptr;
        const char* start = line + 1;
        const char* end = start;
        while (*end && !isspace((unsigned char)*end)) end++;
        if (len) *len = end - start;
        return start;
    }
};

static ManAdapter s_man_adapter;

// Accessor function for FormatRegistry
FormatAdapter* getManAdapter() {
    return &s_man_adapter;
}

} // namespace markup
} // namespace lambda
