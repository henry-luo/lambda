/**
 * asciidoc_adapter.cpp - AsciiDoc format adapter
 *
 * Implements format-specific detection rules for AsciiDoc.
 * AsciiDoc uses:
 * - Headers with = (level based on count) or underlines
 * - Emphasis with _italic_, *bold*, +mono+
 * - Links with link:url[text]
 * - Blocks with ---- or ====
 * - Admonitions: NOTE:, TIP:, IMPORTANT:, WARNING:, CAUTION:
 * - Cross-references: <<anchor>> or <<anchor,text>>
 * - Definition lists: term:: definition
 * - Attribute blocks: [source,lang] [quote] etc.
 */
#include "../format_adapter.hpp"
#include <cstring>
#include <cctype>

namespace lambda {
namespace markup {

/**
 * AdmonitionType - AsciiDoc admonition block types
 */
enum class AdmonitionType {
    NONE,
    NOTE,
    TIP,
    IMPORTANT,
    WARNING,
    CAUTION
};

/**
 * AdmonitionInfo - Result of admonition detection
 */
struct AdmonitionInfo {
    AdmonitionType type;
    const char* content_start;  // start of content after label
    bool valid;

    AdmonitionInfo() : type(AdmonitionType::NONE), content_start(nullptr), valid(false) {}
};

/**
 * AttributeBlockInfo - Result of attribute block detection [source,lang] etc.
 */
struct AttributeBlockInfo {
    const char* name;           // primary attribute name (source, quote, etc.)
    size_t name_length;
    const char* options;        // comma-separated options
    size_t options_length;
    bool valid;

    AttributeBlockInfo() : name(nullptr), name_length(0), options(nullptr),
                           options_length(0), valid(false) {}
};

/**
 * CrossRefInfo - Result of cross-reference detection <<anchor>>
 */
struct CrossRefInfo {
    const char* anchor_start;
    const char* anchor_end;
    const char* text_start;     // optional display text
    const char* text_end;
    const char* end_pos;
    bool valid;

    CrossRefInfo() : anchor_start(nullptr), anchor_end(nullptr),
                     text_start(nullptr), text_end(nullptr),
                     end_pos(nullptr), valid(false) {}
};

/**
 * DefinitionListInfo - Result of definition list detection
 */
struct DefinitionListInfo {
    const char* term_start;
    const char* term_end;
    const char* def_start;
    int colons;                 // number of :: (affects nesting)
    bool valid;

    DefinitionListInfo() : term_start(nullptr), term_end(nullptr),
                           def_start(nullptr), colons(0), valid(false) {}
};

class AsciidocAdapter : public FormatAdapter {
public:
    Format format() const override { return Format::ASCIIDOC; }
    const char* name() const override { return "asciidoc"; }

    const char* const* extensions() const override {
        static const char* exts[] = {".adoc", ".asciidoc", ".asc", nullptr};
        return exts;
    }

    // ========================================================================
    // AsciiDoc-Specific Detection Methods
    // ========================================================================

    /**
     * detectAdmonition - Detect admonition blocks (NOTE:, TIP:, etc.)
     */
    AdmonitionInfo detectAdmonition(const char* line) {
        AdmonitionInfo info;
        const char* p = line;

        // skip leading whitespace
        while (*p == ' ' || *p == '\t') p++;

        if (strncmp(p, "NOTE:", 5) == 0) {
            info.type = AdmonitionType::NOTE;
            info.content_start = p + 5;
            info.valid = true;
        } else if (strncmp(p, "TIP:", 4) == 0) {
            info.type = AdmonitionType::TIP;
            info.content_start = p + 4;
            info.valid = true;
        } else if (strncmp(p, "IMPORTANT:", 10) == 0) {
            info.type = AdmonitionType::IMPORTANT;
            info.content_start = p + 10;
            info.valid = true;
        } else if (strncmp(p, "WARNING:", 8) == 0) {
            info.type = AdmonitionType::WARNING;
            info.content_start = p + 8;
            info.valid = true;
        } else if (strncmp(p, "CAUTION:", 8) == 0) {
            info.type = AdmonitionType::CAUTION;
            info.content_start = p + 8;
            info.valid = true;
        }

        // skip whitespace after colon
        if (info.valid) {
            while (*info.content_start == ' ') info.content_start++;
        }

        return info;
    }

    /**
     * getAdmonitionClass - Get CSS class name for admonition type
     */
    static const char* getAdmonitionClass(AdmonitionType type) {
        switch (type) {
            case AdmonitionType::NOTE: return "note";
            case AdmonitionType::TIP: return "tip";
            case AdmonitionType::IMPORTANT: return "important";
            case AdmonitionType::WARNING: return "warning";
            case AdmonitionType::CAUTION: return "caution";
            default: return nullptr;
        }
    }

    /**
     * detectAttributeBlock - Detect [source,lang] or [quote] blocks
     */
    AttributeBlockInfo detectAttributeBlock(const char* line) {
        AttributeBlockInfo info;
        const char* p = line;

        // skip leading whitespace
        while (*p == ' ' || *p == '\t') p++;

        if (*p != '[') return info;
        p++;

        // find attribute name
        info.name = p;
        while (*p && *p != ',' && *p != ']' && *p != ' ') p++;
        info.name_length = p - info.name;

        if (info.name_length == 0) return info;

        // check for options after comma
        if (*p == ',') {
            p++;
            info.options = p;
            while (*p && *p != ']') p++;
            info.options_length = p - info.options;
        }

        if (*p == ']') {
            info.valid = true;
        }

        return info;
    }

    /**
     * detectCrossReference - Detect <<anchor>> or <<anchor,text>>
     */
    CrossRefInfo detectCrossReference(const char* pos) {
        CrossRefInfo info;

        if (pos[0] != '<' || pos[1] != '<') return info;

        const char* p = pos + 2;
        info.anchor_start = p;

        // find end of anchor or comma
        while (*p && *p != '>' && *p != ',') p++;

        if (*p == '\0') return info;

        info.anchor_end = p;

        if (*p == ',') {
            // has display text
            p++;
            info.text_start = p;
            while (*p && *p != '>') p++;
            info.text_end = p;
        }

        // must end with >>
        if (*p == '>' && *(p + 1) == '>') {
            info.end_pos = p + 2;
            info.valid = true;
        }

        return info;
    }

    /**
     * detectDefinitionList - Detect term:: definition syntax
     */
    DefinitionListInfo detectDefinitionList(const char* line) {
        DefinitionListInfo info;
        const char* p = line;

        // skip leading whitespace
        while (*p == ' ' || *p == '\t') p++;

        info.term_start = p;

        // find :: marker
        while (*p && !(*p == ':' && *(p + 1) == ':')) {
            if (*p == '\n' || *p == '\r') return info;
            p++;
        }

        if (*p != ':') return info;

        info.term_end = p;

        // count colons (:: = level 1, ::: = level 2, etc.)
        while (*p == ':') {
            info.colons++;
            p++;
        }

        if (info.colons < 2) return info;

        // skip whitespace after colons
        while (*p == ' ' || *p == '\t') p++;

        info.def_start = p;
        info.valid = true;

        return info;
    }

    /**
     * isDelimitedBlockStart - Check for ==== or **** or ____ delimiters
     */
    bool isDelimitedBlockStart(const char* line, char* out_char = nullptr, int* out_len = nullptr) {
        const char* p = line;
        while (*p == ' ' || *p == '\t') p++;

        // check for 4+ repeated characters
        if (*p == '=' || *p == '*' || *p == '_' || *p == '-' || *p == '+') {
            char c = *p;
            int len = 0;
            while (*p == c) { len++; p++; }

            // skip trailing whitespace
            while (*p == ' ' || *p == '\t') p++;

            if (len >= 4 && (*p == '\0' || *p == '\n' || *p == '\r')) {
                if (out_char) *out_char = c;
                if (out_len) *out_len = len;
                return true;
            }
        }
        return false;
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
        if (strcmp(feature, "admonitions") == 0) return true;
        if (strcmp(feature, "cross_references") == 0) return true;
        if (strcmp(feature, "attribute_blocks") == 0) return true;
        if (strcmp(feature, "callouts") == 0) return true;
        if (strcmp(feature, "include_directive") == 0) return true;
        return false;
    }

    /**
     * detectCallout - Detect code callout markers <1> <2> etc.
     */
    bool detectCallout(const char* pos, int* out_number = nullptr) {
        if (*pos != '<') return false;

        const char* p = pos + 1;
        int num = 0;

        while (*p >= '0' && *p <= '9') {
            num = num * 10 + (*p - '0');
            p++;
        }

        if (num > 0 && *p == '>') {
            if (out_number) *out_number = num;
            return true;
        }
        return false;
    }

    /**
     * detectIncludeDirective - Detect include::path[] directives
     */
    bool detectIncludeDirective(const char* line, const char** path_start = nullptr,
                                 const char** path_end = nullptr) {
        const char* p = line;
        while (*p == ' ' || *p == '\t') p++;

        if (strncmp(p, "include::", 9) != 0) return false;

        p += 9;
        if (path_start) *path_start = p;

        while (*p && *p != '[') p++;

        if (path_end) *path_end = p;

        return *p == '[';
    }
};

static AsciidocAdapter s_asciidoc_adapter;

// Accessor function for FormatRegistry
FormatAdapter* getAsciidocAdapter() {
    return &s_asciidoc_adapter;
}

} // namespace markup
} // namespace lambda
