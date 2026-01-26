/**
 * asciidoc_adapter.cpp - AsciiDoc format adapter
 *
 * Implements format-specific detection rules for AsciiDoc.
 * AsciiDoc uses:
 * - Headers with = (level based on count) or underlines
 * - Emphasis with _italic_, *bold*, +mono+
 * - Links with link:url[text]
 * - Blocks with ---- or ====
 */
#include "../format_adapter.hpp"
#include <cstring>
#include <cctype>

namespace lambda {
namespace markup {

class AsciidocAdapter : public FormatAdapter {
public:
    Format format() const override { return Format::ASCIIDOC; }
    const char* name() const override { return "asciidoc"; }

    const char* const* extensions() const override {
        static const char* exts[] = {".adoc", ".asciidoc", ".asc", nullptr};
        return exts;
    }

    HeaderInfo detectHeader(const char* line, const char* next_line) override {
        HeaderInfo info;
        const char* p = line;

        // AsciiDoc ATX-style: = Header (one = per level)
        if (*p == '=') {
            int level = 0;
            while (*p == '=') { level++; p++; }

            if (*p == ' ' || *p == '\t') {
                while (*p == ' ' || *p == '\t') p++;
                info.level = level > 6 ? 6 : level;
                info.text_start = p;
                info.text_end = p + strlen(p);
                while (info.text_end > info.text_start &&
                       (*(info.text_end-1) == '\n' || *(info.text_end-1) == '\r' ||
                        *(info.text_end-1) == ' ' || *(info.text_end-1) == '\t')) {
                    info.text_end--;
                }
                // Strip trailing =
                while (info.text_end > info.text_start && *(info.text_end-1) == '=') {
                    info.text_end--;
                }
                while (info.text_end > info.text_start &&
                       (*(info.text_end-1) == ' ' || *(info.text_end-1) == '\t')) {
                    info.text_end--;
                }
                info.valid = true;
            }
        }

        // Setext-style with underlines (next_line check)
        if (!info.valid && next_line) {
            const char* ul = next_line;
            static const char* ul_chars = "=-~^+";
            if (strchr(ul_chars, *ul)) {
                char ul_char = *ul;
                int ul_len = 0;
                while (*ul == ul_char) { ul_len++; ul++; }
                if (ul_len >= 2) {
                    static const char levels[] = "=-~^+";
                    const char* lp = strchr(levels, ul_char);
                    info.level = lp ? (int)(lp - levels + 1) : 1;
                    if (info.level > 6) info.level = 6;
                    info.text_start = line;
                    info.text_end = line + strlen(line);
                    while (info.text_end > info.text_start &&
                           (*(info.text_end-1) == '\n' || *(info.text_end-1) == '\r')) {
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
        while (*p == ' ') { info.indent++; p++; }

        // Unordered: *, -, or multiple **
        if (*p == '*' || *p == '-') {
            char marker = *p;
            int depth = 0;
            while (*p == marker) { depth++; p++; }
            if (*p == ' ') {
                info.marker = marker;
                info.indent = depth;
                info.marker_end = p;
                info.text_start = p + 1;
                info.valid = true;
            }
        }

        // Ordered: . or multiple ..
        if (!info.valid && *p == '.') {
            int depth = 0;
            while (*p == '.') { depth++; p++; }
            if (*p == ' ') {
                info.marker = '.';
                info.indent = depth;
                info.is_ordered = true;
                info.marker_end = p;
                info.text_start = p + 1;
                info.valid = true;
            }
        }

        return info;
    }

    CodeFenceInfo detectCodeFence(const char* line) override {
        CodeFenceInfo info;
        const char* p = line;

        // ---- or ==== or ....
        if (*p == '-' || *p == '=' || *p == '.') {
            char fence = *p;
            int len = 0;
            while (*p == fence) { len++; p++; }
            if (len >= 4) {
                info.fence_char = fence;
                info.fence_length = len;
                info.valid = true;
            }
        }

        // [source,lang] block
        if (strncmp(line, "[source", 7) == 0) {
            const char* lang = strchr(line, ',');
            if (lang) {
                lang++;
                info.info_string = lang;
                const char* end = strchr(lang, ']');
                if (end) info.info_length = end - lang;
            }
        }

        return info;
    }

    bool isCodeFenceClose(const char* line, const CodeFenceInfo& open_info) override {
        const char* p = line;
        if (*p != open_info.fence_char) return false;
        int len = 0;
        while (*p == open_info.fence_char) { len++; p++; }
        return len >= open_info.fence_length;
    }

    BlockquoteInfo detectBlockquote(const char* line) override {
        BlockquoteInfo info;
        // AsciiDoc uses [quote] blocks or > prefix (some variants)
        if (*line == '>') {
            info.depth = 1;
            info.content_start = line + 1;
            if (*info.content_start == ' ') info.content_start++;
            info.valid = true;
        }
        return info;
    }

    bool detectTable(const char* line, const char* next_line) override {
        (void)next_line;
        // AsciiDoc tables: |===
        return strncmp(line, "|===", 4) == 0;
    }

    bool detectThematicBreak(const char* line) override {
        // ''' or ---
        const char* p = line;
        while (*p == ' ') p++;
        if (*p == '\'' || *p == '-') {
            char c = *p;
            int count = 0;
            while (*p == c) { count++; p++; }
            while (*p == ' ') p++;
            return count >= 3 && (*p == '\0' || *p == '\n');
        }
        return false;
    }

    static constexpr DelimiterSpec adoc_emphasis[] = {
        {"**", "**", InlineType::BOLD, true, false},
        {"*", "*", InlineType::BOLD, true, false},
        {"__", "__", InlineType::ITALIC, true, false},
        {"_", "_", InlineType::ITALIC, true, false},
        {"``", "``", InlineType::CODE, false, false},
        {"`", "`", InlineType::CODE, false, false},
        {"++", "++", InlineType::CODE, false, false},
        {"+", "+", InlineType::CODE, false, false},
        {"~~", "~~", InlineType::STRIKETHROUGH, true, false},
        {"^", "^", InlineType::SUPERSCRIPT, false, false},
        {"~", "~", InlineType::SUBSCRIPT, false, false},
    };

    const DelimiterSpec* emphasisDelimiters() const override { return adoc_emphasis; }
    size_t emphasisDelimiterCount() const override { return sizeof(adoc_emphasis)/sizeof(adoc_emphasis[0]); }

    LinkInfo detectLink(const char* pos) override {
        LinkInfo info;

        // link:url[text] or url[text]
        if (strncmp(pos, "link:", 5) == 0) {
            info.url_start = pos + 5;
            info.url_end = info.url_start;
            while (*info.url_end && *info.url_end != '[') info.url_end++;
            if (*info.url_end == '[') {
                info.text_start = info.url_end + 1;
                info.text_end = info.text_start;
                while (*info.text_end && *info.text_end != ']') info.text_end++;
                if (*info.text_end == ']') {
                    info.end_pos = info.text_end + 1;
                    info.valid = true;
                }
            }
        }
        // http://... or https://...
        else if (strncmp(pos, "http://", 7) == 0 || strncmp(pos, "https://", 8) == 0) {
            info.url_start = pos;
            info.url_end = pos;
            while (*info.url_end && !isspace((unsigned char)*info.url_end) && *info.url_end != '[') {
                info.url_end++;
            }
            if (*info.url_end == '[') {
                info.text_start = info.url_end + 1;
                info.text_end = info.text_start;
                while (*info.text_end && *info.text_end != ']') info.text_end++;
                if (*info.text_end == ']') {
                    info.end_pos = info.text_end + 1;
                    info.valid = true;
                }
            } else {
                info.text_start = info.url_start;
                info.text_end = info.url_end;
                info.end_pos = info.url_end;
                info.valid = true;
            }
        }

        return info;
    }

    LinkInfo detectImage(const char* pos) override {
        LinkInfo info;
        // image:path[alt]
        if (strncmp(pos, "image:", 6) == 0) {
            info.url_start = pos + 6;
            info.url_end = info.url_start;
            while (*info.url_end && *info.url_end != '[') info.url_end++;
            if (*info.url_end == '[') {
                info.text_start = info.url_end + 1;
                info.text_end = info.text_start;
                while (*info.text_end && *info.text_end != ']') info.text_end++;
                if (*info.text_end == ']') {
                    info.end_pos = info.text_end + 1;
                    info.valid = true;
                }
            }
        }
        return info;
    }

    bool supportsFeature(const char* feature) const override {
        if (strcmp(feature, "tables") == 0) return true;
        if (strcmp(feature, "footnotes") == 0) return true;
        if (strcmp(feature, "definition_lists") == 0) return true;
        return false;
    }
};

static AsciidocAdapter s_asciidoc_adapter;

// Accessor function for FormatRegistry
FormatAdapter* getAsciidocAdapter() {
    return &s_asciidoc_adapter;
}

} // namespace markup
} // namespace lambda
