/**
 * wiki_adapter.cpp - MediaWiki format adapter
 *
 * Implements format-specific detection rules for MediaWiki markup.
 * Wiki uses unique conventions:
 * - Headers with == Header ==
 * - Emphasis with ''italic'' and '''bold'''
 * - Links with [[internal]] and [external url]
 * - Tables with {| |} syntax
 */
#include "../format_adapter.hpp"
#include <cstring>
#include <cctype>

namespace lambda {
namespace markup {

class WikiAdapter : public FormatAdapter {
public:
    Format format() const override { return Format::WIKI; }
    const char* name() const override { return "wiki"; }

    const char* const* extensions() const override {
        static const char* exts[] = {".wiki", ".mediawiki", nullptr};
        return exts;
    }

    HeaderInfo detectHeader(const char* line, const char* next_line) override {
        (void)next_line;
        HeaderInfo info;

        // Wiki headers: == Header == (level 2), === Header === (level 3), etc.
        const char* p = line;
        while (*p == ' ') p++;

        if (*p != '=') return info;

        int open_equals = 0;
        while (*p == '=') { open_equals++; p++; }

        // Skip whitespace after opening =
        while (*p == ' ') p++;
        info.text_start = p;

        // Find end of text (before closing =)
        const char* end = p + strlen(p);
        while (end > p && (*(end-1) == '\n' || *(end-1) == '\r' || *(end-1) == ' ')) end--;

        // Count closing =
        int close_equals = 0;
        while (end > p && *(end-1) == '=') { close_equals++; end--; }

        // Skip whitespace before closing =
        while (end > p && *(end-1) == ' ') end--;

        info.text_end = end;

        // Level is number of = (Wiki uses 2 for h2, so =1 for h1)
        int level = (open_equals < close_equals) ? open_equals : close_equals;
        if (level >= 1 && level <= 6) {
            info.level = level;
            info.valid = true;
        }

        return info;
    }

    ListItemInfo detectListItem(const char* line) override {
        ListItemInfo info;

        const char* p = line;

        // Wiki lists start at column 0
        // * unordered, # ordered, : definition, ; term
        if (*p == '*' || *p == '#' || *p == ':' || *p == ';') {
            info.marker = *p;
            info.is_ordered = (*p == '#');

            // Count depth
            char marker = *p;
            while (*p == marker) { info.indent++; p++; }

            info.marker_end = p;
            while (*p == ' ') p++;
            info.text_start = p;
            info.valid = true;
        }

        return info;
    }

    CodeFenceInfo detectCodeFence(const char* line) override {
        CodeFenceInfo info;

        // Wiki uses <syntaxhighlight> or <source> or <pre>
        if (strncmp(line, "<syntaxhighlight", 16) == 0 ||
            strncmp(line, "<source", 7) == 0 ||
            strncmp(line, "<pre>", 5) == 0 ||
            strncmp(line, "<code>", 6) == 0) {
            info.fence_char = '<';
            info.fence_length = 1;

            // Try to extract language from lang="..."
            const char* lang = strstr(line, "lang=\"");
            if (lang) {
                info.info_string = lang + 6;
                const char* lang_end = strchr(info.info_string, '"');
                if (lang_end) info.info_length = lang_end - info.info_string;
            }
            info.valid = true;
        }

        return info;
    }

    bool isCodeFenceClose(const char* line, const CodeFenceInfo& open_info) override {
        (void)open_info;
        return strstr(line, "</syntaxhighlight>") != nullptr ||
               strstr(line, "</source>") != nullptr ||
               strstr(line, "</pre>") != nullptr ||
               strstr(line, "</code>") != nullptr;
    }

    BlockquoteInfo detectBlockquote(const char* line) override {
        BlockquoteInfo info;
        // Wiki uses templates or special markup for quotes
        if (strncmp(line, "{{quote|", 8) == 0 || strncmp(line, "{{Quote|", 8) == 0) {
            info.depth = 1;
            info.content_start = line + 8;
            info.valid = true;
        }
        return info;
    }

    bool detectTable(const char* line, const char* next_line) override {
        (void)next_line;
        // Wiki tables start with {|
        const char* p = line;
        while (*p == ' ') p++;
        return strncmp(p, "{|", 2) == 0;
    }

    bool detectThematicBreak(const char* line) override {
        // Wiki uses ---- for horizontal rule
        const char* p = line;
        while (*p == ' ') p++;
        int dashes = 0;
        while (*p == '-') { dashes++; p++; }
        while (*p == ' ') p++;
        return dashes >= 4 && (*p == '\0' || *p == '\n' || *p == '\r');
    }

    // Wiki emphasis: ''italic'', '''bold''', '''''bold italic'''''
    static constexpr DelimiterSpec wiki_emphasis[] = {
        {"'''''", "'''''", InlineType::BOLD_ITALIC, true, false},
        {"'''", "'''", InlineType::BOLD, true, false},
        {"''", "''", InlineType::ITALIC, true, false},
    };

    const DelimiterSpec* emphasisDelimiters() const override { return wiki_emphasis; }
    size_t emphasisDelimiterCount() const override { return sizeof(wiki_emphasis)/sizeof(wiki_emphasis[0]); }

    LinkInfo detectLink(const char* pos) override {
        LinkInfo info;

        // Internal links: [[Article]] or [[Article|display text]]
        if (pos[0] == '[' && pos[1] == '[') {
            const char* p = pos + 2;
            info.url_start = p;

            // Find | or ]]
            while (*p && !(p[0] == ']' && p[1] == ']')) {
                if (*p == '|') {
                    info.url_end = p;
                    p++;
                    info.text_start = p;
                    while (*p && !(p[0] == ']' && p[1] == ']')) p++;
                    info.text_end = p;
                    break;
                }
                p++;
            }

            if (p[0] == ']' && p[1] == ']') {
                if (!info.url_end) {
                    info.url_end = p;
                    info.text_start = info.url_start;
                    info.text_end = info.url_end;
                }
                info.end_pos = p + 2;
                info.valid = true;
            }
        }
        // External links: [url text]
        else if (*pos == '[') {
            const char* p = pos + 1;
            info.url_start = p;

            // Find space or ]
            while (*p && *p != ' ' && *p != ']') p++;
            info.url_end = p;

            if (*p == ' ') {
                p++;
                info.text_start = p;
                while (*p && *p != ']') p++;
                info.text_end = p;
            } else {
                info.text_start = info.url_start;
                info.text_end = info.url_end;
            }

            if (*p == ']') {
                info.end_pos = p + 1;
                info.valid = true;
            }
        }

        return info;
    }

    LinkInfo detectImage(const char* pos) override {
        // Wiki images: [[File:name.jpg|options]]
        LinkInfo info;
        if (strncmp(pos, "[[File:", 7) == 0 || strncmp(pos, "[[Image:", 8) == 0) {
            info = detectLink(pos);
            // URL is the file path
        }
        return info;
    }

    bool supportsFeature(const char* feature) const override {
        if (strcmp(feature, "tables") == 0) return true;
        if (strcmp(feature, "templates") == 0) return true;
        return false;
    }
};

static WikiAdapter s_wiki_adapter;

// Accessor function for FormatRegistry
FormatAdapter* getWikiAdapter() {
    return &s_wiki_adapter;
}

} // namespace markup
} // namespace lambda
