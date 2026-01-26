/**
 * markdown_adapter.cpp - Markdown format adapter
 *
 * Implements format-specific detection rules for Markdown variants:
 * - CommonMark
 * - GitHub Flavored Markdown (GFM)
 * - GitLab Flavored Markdown
 * - Pandoc Markdown
 *
 * This is the most feature-complete adapter and serves as a reference
 * for implementing other format adapters.
 */
#include "../format_adapter.hpp"
#include <cstring>
#include <cctype>

namespace lambda {
namespace markup {

class MarkdownAdapter : public FormatAdapter {
public:
    // ========================================================================
    // Format Identification
    // ========================================================================

    Format format() const override { return Format::MARKDOWN; }
    const char* name() const override { return "markdown"; }

    const char* const* extensions() const override {
        static const char* exts[] = {".md", ".markdown", ".mdown", ".mkd", ".mkdn", nullptr};
        return exts;
    }

    // ========================================================================
    // Block Detection
    // ========================================================================

    HeaderInfo detectHeader(const char* line, const char* next_line) override {
        HeaderInfo info;

        // Skip leading whitespace (up to 3 spaces allowed)
        const char* p = line;
        int leading_spaces = 0;
        while (*p == ' ' && leading_spaces < 4) { leading_spaces++; p++; }
        if (leading_spaces >= 4) return info; // Indented code, not header

        // ATX-style headers: # Header
        if (*p == '#') {
            int level = 0;
            while (*p == '#' && level < 7) { level++; p++; }

            // Must be level 1-6 and followed by space, tab, or end of line
            if (level >= 1 && level <= 6 && (*p == ' ' || *p == '\t' || *p == '\0' || *p == '\r' || *p == '\n')) {
                info.level = level;
                info.valid = true;

                // Skip whitespace after #
                while (*p == ' ' || *p == '\t') p++;
                info.text_start = p;

                // Find end of line
                const char* end = p;
                while (*end && *end != '\r' && *end != '\n') end++;

                // Strip trailing # and whitespace (closing sequence)
                info.text_end = end;
                while (info.text_end > info.text_start) {
                    char c = *(info.text_end - 1);
                    if (c == ' ' || c == '\t') {
                        info.text_end--;
                    } else if (c == '#') {
                        // Check if this is a closing sequence (all # possibly followed by spaces)
                        const char* hash_start = info.text_end - 1;
                        while (hash_start > info.text_start && *(hash_start - 1) == '#') {
                            hash_start--;
                        }
                        // Must be preceded by whitespace OR be at start of content (entire content is closing #s)
                        if (hash_start == info.text_start ||
                            (hash_start > info.text_start && (*(hash_start - 1) == ' ' || *(hash_start - 1) == '\t'))) {
                            if (hash_start == info.text_start) {
                                // Entire content is just closing #s - result is empty
                                info.text_end = info.text_start;
                            } else {
                                info.text_end = hash_start - 1;
                                // Trim trailing whitespace before the closing #s
                                while (info.text_end > info.text_start &&
                                       (*(info.text_end - 1) == ' ' || *(info.text_end - 1) == '\t')) {
                                    info.text_end--;
                                }
                            }
                        }
                        break;
                    } else {
                        break;
                    }
                }
            }
        }

        // Setext-style headers: underline with === or ---
        if (!info.valid && next_line && *p && *p != '\r' && *p != '\n') {
            // The "header text" line must not be a thematic break itself
            // (e.g., "***" followed by "---" should be hr + hr, not a setext heading)
            if (detectThematicBreak(line)) {
                return info; // This line is a thematic break, not header text
            }

            // Setext heading content cannot start with block structure indicators
            // (blockquotes, list items) - those take precedence
            if (*p == '>') {
                return info; // Blockquote marker, not setext heading content
            }
            // Check for list markers: -, +, *, or digits followed by . or )
            if (*p == '-' || *p == '+' || *p == '*') {
                const char* after = p + 1;
                if (*after == ' ' || *after == '\t' || *after == '\0' || *after == '\r' || *after == '\n') {
                    return info; // List marker, not setext heading content
                }
            }
            if (isdigit((unsigned char)*p)) {
                const char* d = p;
                while (isdigit((unsigned char)*d)) d++;
                if (*d == '.' || *d == ')') {
                    const char* after = d + 1;
                    if (*after == ' ' || *after == '\t' || *after == '\0' || *after == '\r' || *after == '\n') {
                        return info; // Ordered list marker, not setext heading content
                    }
                }
            }

            // Check the underline (next_line)
            const char* ul = next_line;
            int ul_spaces = 0;
            while (*ul == ' ' && ul_spaces < 4) { ul_spaces++; ul++; }
            if (ul_spaces >= 4) return info; // Not a valid underline

            if (*ul == '=' || *ul == '-') {
                char underline_char = *ul;
                const char* ul_start = ul;

                // Count underline characters
                while (*ul == underline_char) ul++;

                // Skip trailing whitespace
                while (*ul == ' ' || *ul == '\t') ul++;

                // Must end at end of line and have at least one character
                if ((*ul == '\0' || *ul == '\r' || *ul == '\n') && ul > ul_start) {
                    info.level = (underline_char == '=') ? 1 : 2;
                    info.text_start = p;
                    // Find end of first line
                    info.text_end = p;
                    while (*info.text_end && *info.text_end != '\r' && *info.text_end != '\n') {
                        info.text_end++;
                    }
                    // Trim trailing whitespace
                    while (info.text_end > info.text_start &&
                           (*(info.text_end - 1) == ' ' || *(info.text_end - 1) == '\t')) {
                        info.text_end--;
                    }
                    info.uses_underline = true;
                    info.valid = true;
                }
            }
        }

        return info;
    }

    ListItemInfo detectListItem(const char* line) override {
        ListItemInfo info;

        const char* p = line;

        // Count leading whitespace
        while (*p == ' ') { info.indent++; p++; }
        if (*p == '\t') {
            info.indent = ((info.indent / 4) + 1) * 4;
            p++;
        }

        // Unordered list markers: -, *, +
        if ((*p == '-' || *p == '*' || *p == '+') &&
            (*(p+1) == ' ' || *(p+1) == '\t' || *(p+1) == '\0' || *(p+1) == '\r' || *(p+1) == '\n')) {
            info.marker = *p;
            info.is_ordered = false;
            info.marker_end = p + 1;

            // Skip whitespace after marker
            p++;
            while (*p == ' ' || *p == '\t') p++;
            info.text_start = p;

            // Check for task list: [ ] or [x] or [X]
            if (*p == '[' && (*(p+1) == ' ' || *(p+1) == 'x' || *(p+1) == 'X') && *(p+2) == ']') {
                info.is_task = true;
                info.task_checked = (*(p+1) != ' ');
                p += 3;
                while (*p == ' ' || *p == '\t') p++;
                info.text_start = p;
            }

            info.valid = true;
        }

        // Ordered list markers: 1. or 1) (number can be up to 9 digits)
        if (!info.valid && isdigit((unsigned char)*p)) {
            const char* num_start = p;
            int digits = 0;
            while (isdigit((unsigned char)*p) && digits < 10) { digits++; p++; }

            if ((*p == '.' || *p == ')') &&
                (*(p+1) == ' ' || *(p+1) == '\t' || *(p+1) == '\0' || *(p+1) == '\r' || *(p+1) == '\n')) {
                info.marker = *p;
                info.number = atoi(num_start);
                info.is_ordered = true;
                info.marker_end = p + 1;

                // Skip whitespace after marker
                p++;
                while (*p == ' ' || *p == '\t') p++;
                info.text_start = p;

                info.valid = true;
            }
        }

        return info;
    }

    CodeFenceInfo detectCodeFence(const char* line) override {
        CodeFenceInfo info;

        const char* p = line;

        // Skip up to 3 leading spaces
        while (*p == ' ' && info.indent < 4) { info.indent++; p++; }
        if (info.indent >= 4) return info; // Indented code, not fence

        // Check for ``` or ~~~
        if (*p == '`' || *p == '~') {
            char fence_char = *p;
            int fence_len = 0;

            while (*p == fence_char) { fence_len++; p++; }

            // Need at least 3 fence characters
            if (fence_len >= 3) {
                info.fence_char = fence_char;
                info.fence_length = fence_len;

                // Skip whitespace before info string
                while (*p == ' ' || *p == '\t') p++;

                // Info string (language identifier)
                info.info_string = p;

                // Find end of info string (backticks can't have ` in info string)
                const char* info_end = p;
                while (*info_end && *info_end != '\r' && *info_end != '\n') {
                    if (fence_char == '`' && *info_end == '`') {
                        // Backtick in info string - invalid
                        return CodeFenceInfo();
                    }
                    info_end++;
                }

                // Trim trailing whitespace from info string
                while (info_end > p && (*(info_end-1) == ' ' || *(info_end-1) == '\t')) {
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

        // Skip up to 3 leading spaces
        int indent = 0;
        while (*p == ' ' && indent < 4) { indent++; p++; }

        // Must match fence character
        if (*p != open_info.fence_char) return false;

        // Count fence characters
        int fence_len = 0;
        while (*p == open_info.fence_char) { fence_len++; p++; }

        // Must have at least as many as opening fence
        if (fence_len < open_info.fence_length) return false;

        // Rest of line must be blank (only whitespace)
        while (*p == ' ' || *p == '\t') p++;
        return (*p == '\0' || *p == '\r' || *p == '\n');
    }

    BlockquoteInfo detectBlockquote(const char* line) override {
        BlockquoteInfo info;

        const char* p = line;

        // Skip up to 3 leading spaces
        int spaces = 0;
        while (*p == ' ' && spaces < 4) { spaces++; p++; }
        if (spaces >= 4) return info;

        // Count > markers
        while (*p == '>') {
            info.depth++;
            p++;
            // Optional space after each >
            if (*p == ' ') p++;
        }

        if (info.depth > 0) {
            info.content_start = p;
            info.valid = true;
        }

        return info;
    }

    bool detectTable(const char* line, const char* next_line) override {
        // GFM pipe tables: must have at least one | and next line must be separator
        const char* pipe = strchr(line, '|');
        if (!pipe) return false;

        if (!next_line) return false;

        // Check if next line is a separator row: | --- | --- |
        const char* p = next_line;
        while (*p == ' ' || *p == '\t') p++;

        bool has_separator = false;
        while (*p && *p != '\r' && *p != '\n') {
            if (*p == '|') {
                p++;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == ':') p++;
                if (*p == '-') {
                    while (*p == '-') p++;
                    if (*p == ':') p++;
                    while (*p == ' ' || *p == '\t') p++;
                    has_separator = true;
                }
            } else if (*p == '-' || *p == ':') {
                p++;
            } else if (*p == ' ' || *p == '\t') {
                p++;
            } else {
                return false;
            }
        }

        return has_separator;
    }

    bool detectThematicBreak(const char* line) override {
        const char* p = line;

        // Skip up to 3 leading spaces
        int spaces = 0;
        while (*p == ' ' && spaces < 4) { spaces++; p++; }
        if (spaces >= 4) return false;

        // Must be *, -, or _
        if (*p != '*' && *p != '-' && *p != '_') return false;
        char marker = *p;

        // Count marker characters
        int count = 0;
        while (*p) {
            if (*p == marker) {
                count++;
            } else if (*p != ' ' && *p != '\t') {
                return false; // Invalid character
            }
            p++;
        }

        return count >= 3;
    }

    bool detectMetadata(const char* content) override {
        // YAML frontmatter: document starts with ---
        if (strncmp(content, "---", 3) == 0) {
            char next = content[3];
            return (next == '\n' || next == '\r' || next == '\0');
        }
        return false;
    }

    // ========================================================================
    // Inline Detection
    // ========================================================================

    static constexpr DelimiterSpec md_emphasis[] = {
        {"***", "***", InlineType::BOLD_ITALIC, true, true},
        {"___", "___", InlineType::BOLD_ITALIC, true, true},
        {"**", "**", InlineType::BOLD, true, true},
        {"__", "__", InlineType::BOLD, true, true},
        {"*", "*", InlineType::ITALIC, true, true},
        {"_", "_", InlineType::ITALIC, true, true},
        {"~~", "~~", InlineType::STRIKETHROUGH, true, false},
        {"``", "``", InlineType::CODE, false, false},
        {"`", "`", InlineType::CODE, false, false},
    };

    const DelimiterSpec* emphasisDelimiters() const override { return md_emphasis; }
    size_t emphasisDelimiterCount() const override { return sizeof(md_emphasis)/sizeof(md_emphasis[0]); }

    LinkInfo detectLink(const char* pos) override {
        LinkInfo info;

        // Standard links: [text](url "title")
        if (*pos != '[') return info;

        const char* text_start = pos + 1;
        int bracket_depth = 1;
        const char* p = text_start;

        // Find matching ]
        while (*p && bracket_depth > 0) {
            if (*p == '\\' && *(p+1)) {
                p += 2; // Skip escaped char
                continue;
            }
            if (*p == '[') bracket_depth++;
            else if (*p == ']') bracket_depth--;
            if (bracket_depth > 0) p++;
        }

        if (bracket_depth != 0) return info;
        const char* text_end = p;
        p++; // Skip ]

        // Check what follows ]
        if (*p == '(') {
            // Inline link: [text](url "title")
            p++; // Skip (

            // Skip whitespace
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;

            // URL can be in < > or bare
            if (*p == '<') {
                p++;
                info.url_start = p;
                while (*p && *p != '>' && *p != '\n' && *p != '\r') p++;
                if (*p != '>') return info;
                info.url_end = p;
                p++;
            } else {
                info.url_start = p;
                int paren_depth = 1;
                while (*p && paren_depth > 0) {
                    if (*p == '\\' && *(p+1)) {
                        p += 2;
                        continue;
                    }
                    if (*p == '(') paren_depth++;
                    else if (*p == ')') paren_depth--;
                    else if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') break;
                    if (paren_depth > 0) p++;
                }
                info.url_end = p;
            }

            // Skip whitespace
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;

            // Optional title
            if (*p == '"' || *p == '\'' || *p == '(') {
                char quote = (*p == '(') ? ')' : *p;
                p++;
                info.title_start = p;
                while (*p && *p != quote) {
                    if (*p == '\\' && *(p+1)) p++;
                    p++;
                }
                info.title_end = p;
                if (*p == quote) p++;
            }

            // Skip whitespace and find closing )
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
            if (*p != ')') return info;
            p++;

            info.text_start = text_start;
            info.text_end = text_end;
            info.end_pos = p;
            info.valid = true;
        }
        else if (*p == '[') {
            // Reference link: [text][ref]
            p++;
            info.ref_start = p;
            while (*p && *p != ']') p++;
            if (*p != ']') return info;
            info.ref_end = p;
            p++;

            info.text_start = text_start;
            info.text_end = text_end;
            info.end_pos = p;
            info.is_reference = true;
            info.valid = true;
        }

        return info;
    }

    LinkInfo detectImage(const char* pos) override {
        // Images: ![alt](src "title")
        if (*pos != '!' || *(pos+1) != '[') return LinkInfo();

        LinkInfo info = detectLink(pos + 1);
        if (info.valid) {
            info.end_pos = info.end_pos; // Already correct
        }
        return info;
    }

    // ========================================================================
    // Feature Support
    // ========================================================================

    bool supportsFeature(const char* feature) const override {
        // GFM features
        if (strcmp(feature, "task_lists") == 0) return true;
        if (strcmp(feature, "tables") == 0) return true;
        if (strcmp(feature, "strikethrough") == 0) return true;
        if (strcmp(feature, "autolink") == 0) return true;
        if (strcmp(feature, "math") == 0) return true;
        if (strcmp(feature, "emoji") == 0) return true;
        if (strcmp(feature, "footnotes") == 0) return true;
        return false;
    }

    const char* escapableChars() const override {
        return "\\`*_{}[]()#+-.!|<>~^";
    }
};

// Static instance for Markdown adapter
static MarkdownAdapter s_markdown_adapter;

// Accessor function for FormatRegistry
FormatAdapter* getMarkdownAdapter() {
    return &s_markdown_adapter;
}

} // namespace markup
} // namespace lambda
