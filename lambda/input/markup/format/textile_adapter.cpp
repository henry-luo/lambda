/**
 * textile_adapter.cpp - Textile format adapter
 *
 * Implements format-specific detection rules for Textile markup.
 * Textile uses:
 * - Headers with h1. h2. etc. (with optional modifiers)
 * - Emphasis with _italic_, *bold*, @code@
 * - Links with "text":url or "text(title)":url
 * - Images with !url! or !url(alt)!
 * - Lists with * (unordered) and # (ordered), nesting via repetition
 * - Definition lists with - term := definition
 * - Block modifiers: (class#id), {style}, [lang], <>=<> alignment
 * - Extended blocks: bc.. bq.. pre.. notextile..
 * - Footnotes: [1] references and fn1. definitions
 * - Tables with | delimiters, |_. for headers, alignment modifiers
 * - Comments: ###. block comments
 */
#include "../format_adapter.hpp"
#include <cstring>
#include <cctype>

namespace lambda {
namespace markup {

/**
 * TextileModifiers - Parsed block modifiers from Textile syntax
 *
 * Textile allows modifiers between block type and period:
 *   p(class#id){style}[lang]<. Text
 */
struct TextileModifiers {
    const char* css_class = nullptr;
    size_t css_class_len = 0;
    const char* css_id = nullptr;
    size_t css_id_len = 0;
    const char* style = nullptr;
    size_t style_len = 0;
    const char* lang = nullptr;
    size_t lang_len = 0;
    char alignment = '\0';  // '<' left, '>' right, '=' center, 'j' justify (<>)
    bool extended = false;  // true if .. (extended block)
};

/**
 * parse_textile_modifiers - Parse modifiers after block type
 *
 * @param start Position after block type (e.g., after "p" in "p(class).text")
 * @param mods Output modifier structure
 * @return Position after modifiers (at '.' or end)
 */
static const char* parse_textile_modifiers(const char* start, TextileModifiers& mods) {
    const char* p = start;

    while (*p && *p != '.' && *p != '\n' && *p != '\r') {
        if (*p == '(') {
            // CSS class and/or ID: (class) or (class#id) or (#id)
            p++;
            const char* class_start = p;
            while (*p && *p != ')' && *p != '#' && *p != '\n') p++;
            if (p > class_start) {
                mods.css_class = class_start;
                mods.css_class_len = p - class_start;
            }
            if (*p == '#') {
                p++;
                const char* id_start = p;
                while (*p && *p != ')' && *p != '\n') p++;
                if (p > id_start) {
                    mods.css_id = id_start;
                    mods.css_id_len = p - id_start;
                }
            }
            if (*p == ')') p++;
        } else if (*p == '{') {
            // CSS style: {color:red;font-weight:bold}
            p++;
            const char* style_start = p;
            while (*p && *p != '}' && *p != '\n') p++;
            if (p > style_start) {
                mods.style = style_start;
                mods.style_len = p - style_start;
            }
            if (*p == '}') p++;
        } else if (*p == '[') {
            // Language attribute: [en] or [fr]
            p++;
            const char* lang_start = p;
            while (*p && *p != ']' && *p != '\n') p++;
            if (p > lang_start) {
                mods.lang = lang_start;
                mods.lang_len = p - lang_start;
            }
            if (*p == ']') p++;
        } else if (*p == '<' && *(p+1) == '>') {
            // Justify alignment
            mods.alignment = 'j';
            p += 2;
        } else if (*p == '<') {
            // Left align
            mods.alignment = '<';
            p++;
        } else if (*p == '>') {
            // Right align
            mods.alignment = '>';
            p++;
        } else if (*p == '=') {
            // Center align
            mods.alignment = '=';
            p++;
        } else {
            break;
        }
    }

    // Check for extended block (..)
    if (*p == '.' && *(p+1) == '.') {
        mods.extended = true;
        p += 2;
    } else if (*p == '.') {
        p++;
    }

    return p;
}

/**
 * is_textile_definition_list - Check if line is a definition list item
 *
 * Textile definition lists: - term := definition
 */
static bool is_textile_definition_list(const char* line) {
    if (!line || *line != '-') return false;

    const char* p = line + 1;
    // skip spaces after -
    while (*p == ' ') p++;

    // look for :=
    while (*p && *p != '\n' && *p != '\r') {
        if (*p == ':' && *(p+1) == '=') {
            return true;
        }
        p++;
    }
    return false;
}

/**
 * is_textile_comment - Check if line starts a comment block
 *
 * Textile comments: ###. or ###..
 */
static bool is_textile_comment(const char* line) {
    return line && strncmp(line, "###", 3) == 0 &&
           (line[3] == '.' || (line[3] == '.' && line[4] == '.'));
}

/**
 * is_textile_footnote_def - Check if line is a footnote definition
 *
 * Textile footnotes: fn1. Footnote text
 */
static bool is_textile_footnote_def(const char* line) {
    if (!line || line[0] != 'f' || line[1] != 'n') return false;

    const char* p = line + 2;
    // Must have at least one digit
    if (!isdigit((unsigned char)*p)) return false;
    while (isdigit((unsigned char)*p)) p++;

    // May have modifiers, then must have .
    while (*p && *p != '.' && *p != '\n') p++;
    return *p == '.';
}

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
        // With optional modifiers: h1(class#id){style}[lang]<. Header text
        if (*line == 'h' && isdigit((unsigned char)line[1])) {
            int level = line[1] - '0';
            if (level >= 1 && level <= 6) {
                TextileModifiers mods;
                const char* after_mods = parse_textile_modifiers(line + 2, mods);

                // After modifiers, should be at content or whitespace
                info.level = level;
                info.text_start = after_mods;
                while (*info.text_start == ' ') info.text_start++;
                info.text_end = info.text_start + strlen(info.text_start);
                while (info.text_end > info.text_start &&
                       (*(info.text_end-1) == '\n' || *(info.text_end-1) == '\r' ||
                        *(info.text_end-1) == ' ')) {
                    info.text_end--;
                }
                info.valid = true;

                // Store modifier info for later use (via custom fields or parser state)
                // Note: HeaderInfo doesn't have fields for these, but the parser
                // can re-parse modifiers when creating the element
            }
        }

        return info;
    }

    ListItemInfo detectListItem(const char* line) override {
        ListItemInfo info;
        const char* p = line;

        // Textile lists: * or # (multiple for nesting)
        // With optional modifiers: *(class). Item
        if (*p == '*' || *p == '#') {
            char marker = *p;
            int depth = 0;
            while (*p == marker) { depth++; p++; }

            // Check for modifiers
            TextileModifiers mods;
            if (*p == '(' || *p == '{' || *p == '[' || *p == '<' || *p == '>' || *p == '=') {
                p = parse_textile_modifiers(p, mods);
            }

            if (*p == ' ' || *p == '\0' || *p == '\n') {
                info.marker = marker;
                info.indent = depth;
                info.is_ordered = (marker == '#');
                info.marker_end = p;
                while (*p == ' ') p++;
                info.text_start = p;
                info.valid = true;
            }
        }

        // Definition list: - term := definition
        if (*line == '-' && *(line+1) == ' ') {
            if (is_textile_definition_list(line)) {
                // Treat as special list item (parser will handle)
                info.marker = '-';
                info.indent = 1;
                info.is_ordered = false;
                info.marker_end = line + 1;
                info.text_start = line + 2;
                info.valid = true;
            }
        }

        return info;
    }

    CodeFenceInfo detectCodeFence(const char* line) override {
        CodeFenceInfo info;

        // Textile uses bc. for block code, pre. for preformatted
        // Extended versions: bc.. and pre.. continue until another block type
        if (strncmp(line, "bc", 2) == 0) {
            TextileModifiers mods;
            const char* after = parse_textile_modifiers(line + 2, mods);
            if (after > line + 2 || (line[2] == '.' || line[2] == '(')) {
                info.fence_char = 'b';
                info.fence_length = mods.extended ? 4 : 3;  // use length to track extended
                info.valid = true;
                // Extract language from modifiers or after bc
                // bc(java). would put "java" in class
            }
        } else if (strncmp(line, "pre", 3) == 0) {
            TextileModifiers mods;
            const char* after = parse_textile_modifiers(line + 3, mods);
            if (after > line + 3 || line[3] == '.') {
                info.fence_char = 'p';
                info.fence_length = mods.extended ? 5 : 4;
                info.valid = true;
            }
        } else if (strncmp(line, "notextile", 9) == 0) {
            TextileModifiers mods;
            const char* after = parse_textile_modifiers(line + 9, mods);
            if (after > line + 9 || line[9] == '.') {
                info.fence_char = 'n';
                info.fence_length = mods.extended ? 11 : 10;
                info.valid = true;
            }
        }

        return info;
    }

    bool isCodeFenceClose(const char* line, const CodeFenceInfo& open_info) override {
        // Extended blocks (bc.., pre.., notextile..) end at another block type
        bool is_extended = (open_info.fence_char == 'b' && open_info.fence_length == 4) ||
                          (open_info.fence_char == 'p' && open_info.fence_length == 5) ||
                          (open_info.fence_char == 'n' && open_info.fence_length == 11);

        if (is_extended) {
            // Extended blocks end at p. or another block element
            if (strncmp(line, "p.", 2) == 0 || strncmp(line, "p(", 2) == 0 ||
                strncmp(line, "h1", 2) == 0 || strncmp(line, "h2", 2) == 0 ||
                strncmp(line, "h3", 2) == 0 || strncmp(line, "h4", 2) == 0 ||
                strncmp(line, "h5", 2) == 0 || strncmp(line, "h6", 2) == 0 ||
                strncmp(line, "bc", 2) == 0 || strncmp(line, "bq", 2) == 0 ||
                strncmp(line, "pre", 3) == 0 || strncmp(line, "###", 3) == 0 ||
                (*line == '*' && *(line+1) == ' ') ||
                (*line == '#' && *(line+1) == ' ') ||
                *line == '|') {
                return true;
            }
            return false;
        }

        // Regular blocks end with blank line or new block
        return is_blank_line(line) ||
               (line[0] == 'p' && line[1] == '.') ||
               (line[0] == 'h' && isdigit((unsigned char)line[1]));
    }

    BlockquoteInfo detectBlockquote(const char* line) override {
        BlockquoteInfo info;

        // Textile: bq. for blockquote, bq.. for extended
        // With optional modifiers: bq(class){style}[lang]. Text
        if (strncmp(line, "bq", 2) == 0) {
            TextileModifiers mods;
            const char* after = parse_textile_modifiers(line + 2, mods);
            if (after > line + 2 || line[2] == '.') {
                info.depth = 1;
                info.content_start = after;
                while (*info.content_start == ' ') info.content_start++;
                info.valid = true;
                // Store extended flag for parser
            }
        }

        return info;
    }

    bool detectTable(const char* line, const char* next_line) override {
        (void)next_line;
        // Textile tables: |cell|cell|
        // Header cells: |_. Header |
        // Alignment: |<. left | =. center | >. right |
        // Spanning: |\2. spans 2 columns | /2. spans 2 rows |
        const char* p = line;
        while (*p == ' ') p++;
        return *p == '|';
    }

    bool detectThematicBreak(const char* line) override {
        // Textile doesn't have a standard HR, but some implementations use ---
        const char* p = line;
        while (*p == ' ') p++;
        int dashes = 0;
        while (*p == '-') { dashes++; p++; }
        return dashes >= 3 && (*p == '\0' || *p == '\n' || *p == '\r');
    }

    // Textile emphasis delimiters
    static constexpr DelimiterSpec textile_emphasis[] = {
        {"**", "**", InlineType::BOLD, true, false},       // strong emphasis
        {"__", "__", InlineType::ITALIC, true, false},     // strong italic
        {"*", "*", InlineType::BOLD, true, false},
        {"_", "_", InlineType::ITALIC, true, false},
        {"??", "??", InlineType::CITE, false, false},      // citation
        {"-", "-", InlineType::STRIKETHROUGH, true, false},
        {"+", "+", InlineType::UNDERLINE, true, false},    // inserted text
        {"@", "@", InlineType::CODE, false, false},
        {"^", "^", InlineType::SUPERSCRIPT, false, false},
        {"~", "~", InlineType::SUBSCRIPT, false, false},
        {"%", "%", InlineType::SPAN, false, false},        // generic span with modifiers
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
            while (*p && *p != '"' && *p != '\n') p++;
            if (*p != '"') return info;

            // Check for (title) before closing "
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

        // Footnote reference: [1] or [note]
        if (*pos == '[' && *(pos+1) != '[') {
            const char* p = pos + 1;
            // Check for digits or alphanumeric
            const char* ref_start = p;
            while (*p && (isalnum((unsigned char)*p) || *p == '_') && *p != ']') p++;
            if (*p == ']' && p > ref_start) {
                // This is a footnote reference - mark as special link
                info.url_start = ref_start;
                info.url_end = p;
                info.text_start = ref_start;
                info.text_end = p;
                info.end_pos = p + 1;
                info.valid = true;
                // Note: Parser should detect this is a footnote and handle appropriately
            }
        }

        return info;
    }

    LinkInfo detectImage(const char* pos) override {
        LinkInfo info;

        // Textile images: !url! or !url(alt)! or !(class)url(alt)!
        if (*pos == '!') {
            const char* p = pos + 1;

            // Check for modifiers: !(class#id){style}
            TextileModifiers mods;
            if (*p == '(' || *p == '{' || *p == '[' || *p == '<' || *p == '>' || *p == '=') {
                p = parse_textile_modifiers(p, mods);
            }

            info.url_start = p;

            while (*p && *p != '!' && *p != '(' && *p != '\n') p++;

            if (*p == '(') {
                info.url_end = p;
                p++;
                info.text_start = p; // Alt text
                while (*p && *p != ')' && *p != '\n') p++;
                info.text_end = p;
                if (*p == ')') p++;
                if (*p == '!') p++;
            } else if (*p == '!') {
                info.url_end = p;
                info.text_start = info.url_start;
                info.text_end = info.url_end;
                p++;
            } else {
                return info; // Invalid
            }

            // Optional link: !image!:url
            if (*p == ':') {
                p++;
                // The URL following is a link target
                // For now, just advance past it
                while (*p && !isspace((unsigned char)*p)) p++;
            }

            info.end_pos = p;
            info.valid = true;
        }

        return info;
    }

    bool detectIndentedCode(const char* line, const char** content_start) override {
        // Textile doesn't use indentation for code blocks
        // It uses bc. and pre. instead
        (void)line;
        (void)content_start;
        return false;
    }

    bool supportsFeature(const char* feature) const override {
        if (strcmp(feature, "tables") == 0) return true;
        if (strcmp(feature, "footnotes") == 0) return true;
        if (strcmp(feature, "definition_lists") == 0) return true;
        if (strcmp(feature, "strikethrough") == 0) return true;
        if (strcmp(feature, "superscript") == 0) return true;
        if (strcmp(feature, "subscript") == 0) return true;
        if (strcmp(feature, "underline") == 0) return true;
        if (strcmp(feature, "css_classes") == 0) return true;
        if (strcmp(feature, "css_styles") == 0) return true;
        if (strcmp(feature, "alignment") == 0) return true;
        if (strcmp(feature, "extended_blocks") == 0) return true;
        if (strcmp(feature, "spans") == 0) return true;
        if (strcmp(feature, "citations") == 0) return true;
        return false;
    }

    const char* escapableChars() const override {
        return "\\*_@+-^~\"!|[]{}()#<>=";
    }
};

static TextileAdapter s_textile_adapter;

// Accessor function for FormatRegistry
FormatAdapter* getTextileAdapter() {
    return &s_textile_adapter;
}

} // namespace markup
} // namespace lambda

