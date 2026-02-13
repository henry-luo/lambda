/**
 * org_adapter.cpp - Org-mode format adapter
 *
 * Implements format-specific detection rules for Emacs Org-mode.
 * Org uses unique conventions:
 * - Headers with * at start of line
 * - Emphasis with /italic/, *bold*, =code=, ~verbatim~
 * - Links with [[url][description]]
 * - Blocks with #+BEGIN_X / #+END_X
 */
#include "../format_adapter.hpp"
#include <cstring>
#include <cctype>
#include "../../../../lib/str.h"

namespace lambda {
namespace markup {

class OrgAdapter : public FormatAdapter {
public:
    Format format() const override { return Format::ORG; }
    const char* name() const override { return "org"; }

    const char* const* extensions() const override {
        static const char* exts[] = {".org", nullptr};
        return exts;
    }

    HeaderInfo detectHeader(const char* line, const char* next_line) override {
        (void)next_line;
        HeaderInfo info;

        const char* p = line;

        // Org headlines: * at column 0, followed by more * for deeper levels
        if (*p != '*') return info;

        int level = 0;
        while (*p == '*') { level++; p++; }

        // Must have space after stars
        if (*p != ' ' && *p != '\t') return info;
        while (*p == ' ' || *p == '\t') p++;

        info.level = (level > 6) ? 6 : level;
        info.text_start = p;

        // Find end of line, skip TODO keywords if present
        static const char* todo_keywords[] = {"TODO", "DONE", "NEXT", "WAIT", "HOLD", "CANCELLED", nullptr};
        for (int i = 0; todo_keywords[i]; i++) {
            size_t kw_len = strlen(todo_keywords[i]);
            if (strncmp(p, todo_keywords[i], kw_len) == 0 &&
                (p[kw_len] == ' ' || p[kw_len] == '\t')) {
                p += kw_len;
                while (*p == ' ' || *p == '\t') p++;
                info.text_start = p;
                break;
            }
        }

        // Find end of text
        info.text_end = info.text_start;
        while (*info.text_end && *info.text_end != '\n' && *info.text_end != '\r') {
            info.text_end++;
        }

        // Trim trailing whitespace and tags (:tag1:tag2:)
        while (info.text_end > info.text_start) {
            if (*(info.text_end-1) == ' ' || *(info.text_end-1) == '\t') {
                info.text_end--;
            } else if (*(info.text_end-1) == ':') {
                // Skip tags
                const char* tag_start = info.text_end - 1;
                while (tag_start > info.text_start && *(tag_start-1) != ' ' && *(tag_start-1) != '\t') {
                    tag_start--;
                }
                if (*tag_start == ':') {
                    info.text_end = tag_start;
                    while (info.text_end > info.text_start &&
                           (*(info.text_end-1) == ' ' || *(info.text_end-1) == '\t')) {
                        info.text_end--;
                    }
                } else {
                    break;
                }
            } else {
                break;
            }
        }

        info.valid = true;
        return info;
    }

    ListItemInfo detectListItem(const char* line) override {
        ListItemInfo info;

        const char* p = line;
        while (*p == ' ') { info.indent++; p++; }

        // Unordered: - or +
        if ((*p == '-' || *p == '+') && *(p+1) == ' ') {
            info.marker = *p;
            info.marker_end = p + 1;
            p += 2;

            // Check for checkbox: [ ] or [X] or [-]
            if (*p == '[' && (*(p+1) == ' ' || *(p+1) == 'X' || *(p+1) == 'x' || *(p+1) == '-') && *(p+2) == ']') {
                info.is_task = true;
                info.task_checked = (*(p+1) == 'X' || *(p+1) == 'x');
                p += 4;
            }

            info.text_start = p;
            info.valid = true;
        }

        // Ordered: 1. or 1)
        if (!info.valid && isdigit((unsigned char)*p)) {
            const char* num_start = p;
            while (isdigit((unsigned char)*p)) p++;
            if ((*p == '.' || *p == ')') && *(p+1) == ' ') {
                info.marker = *p;
                info.number = (int)str_to_int64_or(num_start, strlen(num_start), 0);
                info.is_ordered = true;
                info.marker_end = p + 1;
                info.text_start = p + 2;
                info.valid = true;
            }
        }

        return info;
    }

    CodeFenceInfo detectCodeFence(const char* line) override {
        CodeFenceInfo info;

        const char* p = line;
        while (*p == ' ') { info.indent++; p++; }

        // #+BEGIN_SRC language
        if (starts_with_icase(p, "#+BEGIN_SRC")) {
            info.fence_char = '#';
            info.fence_length = 11;
            p += 11;
            while (*p == ' ') p++;
            info.info_string = p;
            const char* lang_end = p;
            while (*lang_end && *lang_end != '\n' && *lang_end != '\r' && *lang_end != ' ') lang_end++;
            info.info_length = lang_end - p;
            info.valid = true;
        }
        // #+BEGIN_EXAMPLE
        else if (starts_with_icase(p, "#+BEGIN_EXAMPLE")) {
            info.fence_char = '#';
            info.fence_length = 15;
            info.info_string = "";
            info.info_length = 0;
            info.valid = true;
        }

        return info;
    }

    bool isCodeFenceClose(const char* line, const CodeFenceInfo& open_info) override {
        (void)open_info;
        const char* p = line;
        while (*p == ' ') p++;
        return starts_with_icase(p, "#+END_SRC") || starts_with_icase(p, "#+END_EXAMPLE");
    }

    BlockquoteInfo detectBlockquote(const char* line) override {
        BlockquoteInfo info;

        const char* p = line;
        while (*p == ' ') p++;

        // #+BEGIN_QUOTE
        if (starts_with_icase(p, "#+BEGIN_QUOTE")) {
            info.depth = 1;
            info.content_start = p + 13;
            info.valid = true;
        }

        return info;
    }

    bool detectTable(const char* line, const char* next_line) override {
        (void)next_line;
        // Org tables start with |
        const char* p = line;
        while (*p == ' ') p++;
        return *p == '|';
    }

    bool detectThematicBreak(const char* line) override {
        // Org uses ----- (5+) for horizontal rule
        const char* p = line;
        while (*p == ' ') p++;
        int dashes = 0;
        while (*p == '-') { dashes++; p++; }
        while (*p == ' ') p++;
        return dashes >= 5 && (*p == '\0' || *p == '\n' || *p == '\r');
    }

    bool detectMetadata(const char* content) override {
        // Org uses #+TITLE: etc. at start of file
        return starts_with_icase(content, "#+");
    }

    // Org emphasis: /italic/, *bold*, =code=, ~verbatim~, +strikethrough+, _underline_
    // Note: These require special boundary rules (can't be in middle of word)
    static constexpr DelimiterSpec org_emphasis[] = {
        {"*", "*", InlineType::BOLD, true, false},
        {"/", "/", InlineType::ITALIC, true, false},
        {"=", "=", InlineType::CODE, false, false},
        {"~", "~", InlineType::CODE, false, false},
        {"+", "+", InlineType::STRIKETHROUGH, true, false},
        {"_", "_", InlineType::UNDERLINE, true, false},
    };

    const DelimiterSpec* emphasisDelimiters() const override { return org_emphasis; }
    size_t emphasisDelimiterCount() const override { return sizeof(org_emphasis)/sizeof(org_emphasis[0]); }

    LinkInfo detectLink(const char* pos) override {
        LinkInfo info;

        // Org links: [[url]] or [[url][description]]
        if (pos[0] == '[' && pos[1] == '[') {
            const char* p = pos + 2;
            info.url_start = p;

            // Find ][ or ]]
            while (*p && !(p[0] == ']' && (p[1] == ']' || p[1] == '['))) p++;

            if (p[0] == ']') {
                info.url_end = p;
                if (p[1] == '[') {
                    // Has description
                    p += 2;
                    info.text_start = p;
                    while (*p && !(p[0] == ']' && p[1] == ']')) p++;
                    info.text_end = p;
                    if (p[0] == ']' && p[1] == ']') {
                        info.end_pos = p + 2;
                        info.valid = true;
                    }
                } else if (p[1] == ']') {
                    // No description
                    info.text_start = info.url_start;
                    info.text_end = info.url_end;
                    info.end_pos = p + 2;
                    info.valid = true;
                }
            }
        }

        return info;
    }

    LinkInfo detectImage(const char* pos) override {
        // Org images are just links to image files
        LinkInfo info = detectLink(pos);
        // Could check if URL ends in image extension
        return info;
    }

    bool supportsFeature(const char* feature) const override {
        if (strcmp(feature, "task_lists") == 0) return true;
        if (strcmp(feature, "tables") == 0) return true;
        if (strcmp(feature, "math") == 0) return true;
        if (strcmp(feature, "footnotes") == 0) return true;
        return false;
    }
};

static OrgAdapter s_org_adapter;

// Accessor function for FormatRegistry
FormatAdapter* getOrgAdapter() {
    return &s_org_adapter;
}

} // namespace markup
} // namespace lambda
