// Wiki syntax profiles share detection and preserve dialect-specific precedence.
#include "../wiki.hpp"
#include "lib/str.h"
#include <cstring>

namespace lambda {
namespace markup {

#define WIKI_DELIM(open, close, kind, nested) {open, close, InlineType::kind, nested, false}
static const DelimiterSpec mediawiki_emphasis[] = {
    WIKI_DELIM("'''''", "'''''", BOLD_ITALIC, true),
    WIKI_DELIM("'''", "'''", BOLD, true), WIKI_DELIM("''", "''", ITALIC, true)};
static const DelimiterSpec tiki_emphasis[] = {
    WIKI_DELIM("__", "__", BOLD, true), WIKI_DELIM("''", "''", ITALIC, true),
    WIKI_DELIM("-+", "+-", CODE, false)};
static const DelimiterSpec twiki_emphasis[] = {
    WIKI_DELIM("==", "==", CODE, false), WIKI_DELIM("=", "=", CODE, false),
    WIKI_DELIM("__", "__", BOLD_ITALIC, true),
    WIKI_DELIM("*", "*", BOLD, true), WIKI_DELIM("_", "_", ITALIC, true)};
static const DelimiterSpec vim_emphasis[] = {
    WIKI_DELIM("~~", "~~", STRIKETHROUGH, true), WIKI_DELIM("`", "`", CODE, false),
    WIKI_DELIM("*", "*", BOLD, true), WIKI_DELIM("_", "_", ITALIC, true)};
static const DelimiterSpec common_emphasis[] = {
    WIKI_DELIM("**", "**", BOLD, true), WIKI_DELIM("//", "//", ITALIC, true)};
static const DelimiterSpec doku_emphasis[] = {
    WIKI_DELIM("**", "**", BOLD, true), WIKI_DELIM("//", "//", ITALIC, true),
    WIKI_DELIM("__", "__", UNDERLINE, true), WIKI_DELIM("''", "''", CODE, false)};
static const DelimiterSpec zim_emphasis[] = {
    WIKI_DELIM("**", "**", BOLD, true), WIKI_DELIM("//", "//", ITALIC, true),
    WIKI_DELIM("__", "__", UNDERLINE, true), WIKI_DELIM("~~", "~~", STRIKETHROUGH, true),
    WIKI_DELIM("''", "''", CODE, false)};
static const DelimiterSpec xwiki_emphasis[] = {
    WIKI_DELIM("**", "**", BOLD, true), WIKI_DELIM("//", "//", ITALIC, true),
    WIKI_DELIM("__", "__", UNDERLINE, true), WIKI_DELIM("--", "--", STRIKETHROUGH, true),
    WIKI_DELIM("##", "##", CODE, false), WIKI_DELIM("^^", "^^", SUPERSCRIPT, true),
    WIKI_DELIM(",,", ",,", SUBSCRIPT, true)};
static const DelimiterSpec atlassian_emphasis[] = {
    WIKI_DELIM("{{", "}}", CODE, false), WIKI_DELIM("??", "??", CITE, true),
    WIKI_DELIM("*", "*", BOLD, true), WIKI_DELIM("_", "_", ITALIC, true),
    WIKI_DELIM("-", "-", STRIKETHROUGH, true), WIKI_DELIM("+", "+", UNDERLINE, true),
    WIKI_DELIM("^", "^", SUPERSCRIPT, true), WIKI_DELIM("~", "~", SUBSCRIPT, true)};
#undef WIKI_DELIM

#define WIKI_PROFILE(flavor, name, head, links, repeat, escape, delimiters) \
    {Flavor::flavor, name, WikiHeading::head, WikiLinks::links, repeat, escape, delimiters, sizeof(delimiters)/sizeof(*delimiters)}
static const WikiSyntax wiki_profiles[] = {
    WIKI_PROFILE(MEDIAWIKI, "mediawiki", EQUALS, DOUBLE_BRACKET, true, '\\', mediawiki_emphasis),
    WIKI_PROFILE(DOKUWIKI, "dokuwiki", INVERSE_EQUALS, DOUBLE_BRACKET, false, '\\', doku_emphasis),
    WIKI_PROFILE(TIKIWIKI, "tikiwiki", BANG, TIKI, true, '\\', tiki_emphasis),
    WIKI_PROFILE(TWIKI, "twiki", PLUS, TWIKI, false, '!', twiki_emphasis),
    WIKI_PROFILE(VIMWIKI, "vimwiki", EQUALS, DOUBLE_BRACKET, false, '\\', vim_emphasis),
    WIKI_PROFILE(XWIKI, "xwiki", EQUALS, XWIKI, true, '~', xwiki_emphasis),
    WIKI_PROFILE(ZIMWIKI, "zimwiki", INVERSE_EQUALS, DOUBLE_BRACKET, false, '\\', zim_emphasis),
    WIKI_PROFILE(CREOLE, "creole", EQUALS, DOUBLE_BRACKET, true, '~', common_emphasis),
    WIKI_PROFILE(JIRA, "jira", DOT, ATLASSIAN, true, '\\', atlassian_emphasis),
    WIKI_PROFILE(CONFLUENCE, "confluence", DOT, ATLASSIAN, true, '\\', atlassian_emphasis),
};
#undef WIKI_PROFILE

const WikiSyntax* wiki_syntax(Flavor flavor) {
    if (flavor == Flavor::DEFAULT) flavor = Flavor::MEDIAWIKI;
    for (const WikiSyntax& profile : wiki_profiles) if (profile.flavor == flavor) return &profile;
    return nullptr;
}
const WikiSyntax* wiki_syntax(const char* name) {
    if (!name || !strcmp(name, "wiki") || !strcmp(name, "standard")) return wiki_syntax(Flavor::MEDIAWIKI);
    for (const WikiSyntax& profile : wiki_profiles) if (!strcmp(name, profile.name)) return &profile;
    return nullptr;
}
bool wiki_is_mediawiki(const MarkupParser* parser) {
    return parser->config.flavor == Flavor::MEDIAWIKI || parser->config.flavor == Flavor::DEFAULT;
}
bool wiki_is_atlassian(Flavor flavor) { return flavor == Flavor::JIRA || flavor == Flavor::CONFLUENCE; }

static void detect_task(ListItemInfo& info, const WikiSyntax* syntax, const char** source) {
    const char* p = *source;
    if (p[0] != '[' || !p[1] || p[2] != ']' || (p[3] && p[3] != ' ' && p[3] != '\t')) return;
    if (syntax->flavor != Flavor::VIMWIKI && syntax->flavor != Flavor::ZIMWIKI) return;
    info.is_task = true;
    info.task_flavor = syntax->name;
    info.task_marker = p; info.task_marker_length = 3;
    if (p[1] == ' ') { info.task_state = "todo"; info.progress_min = info.progress_max = 0; }
    else if ((syntax->flavor == Flavor::VIMWIKI && p[1] == 'X') ||
             (syntax->flavor == Flavor::ZIMWIKI && p[1] == '*')) {
        info.task_state = "done"; info.task_checked = true; info.progress_min = info.progress_max = 100;
    } else if (syntax->flavor == Flavor::VIMWIKI && strchr(".oO", p[1])) {
        static const char stages[] = ".oO";
        int index = (int)(strchr(stages, p[1]) - stages);
        info.task_state = "in-progress";
        info.progress_min = 1 + index * 33; info.progress_max = 33 + index * 33;
    } else if (syntax->flavor == Flavor::VIMWIKI && p[1] == '-') info.task_state = "rejected";
    else if (syntax->flavor == Flavor::ZIMWIKI && (p[1] == '>' || p[1] == '<')) {
        info.task_state = "migrated"; info.task_label = p[1] == '>' ? "moved" : "moved-out";
    } else {
        info.task_state = "custom";
        info.task_label = syntax->flavor == Flavor::ZIMWIKI && p[1] == 'x' ? "not-completed" : nullptr;
    }
    *source = str_skip_line_space(p + 3);
}

class WikiAdapter : public FormatAdapter {
    const WikiSyntax* syntax_;
public:
    explicit WikiAdapter(const WikiSyntax* syntax = nullptr) : syntax_(syntax) {}
    Format format() const override { return Format::WIKI; }
    const char* name() const override { return syntax_->name; }
    const char* const* extensions() const override {
        static const char* exts[] = {".wiki", ".mediawiki", nullptr};
        return exts;
    }
    HeaderInfo detectHeader(const char* line, const char*) override {
        HeaderInfo info;
        const char* p = str_skip_line_space(line);
        WikiHeading style = syntax_->headings;
        if (style == WikiHeading::DOT) {
            if (*p != 'h' || p[1] < '1' || p[1] > '6' || p[2] != '.') return info;
            info.level = p[1] - '0'; p += 3;
        } else {
            char marker = style == WikiHeading::BANG ? '!' : style == WikiHeading::PLUS ? '+' : '=';
            if (style == WikiHeading::PLUS) { if (strncmp(p, "---", 3)) return info; p += 3; }
            while (*p == marker) { info.level++; p++; }
            if (info.level < 1 || info.level > 6) return HeaderInfo();
            if (style == WikiHeading::INVERSE_EQUALS) info.level = 7 - info.level;
        }
        info.text_start = str_skip_line_space(p);
        info.text_end = info.text_start + strlen(info.text_start);
        while (info.text_end > info.text_start && str_is_space(info.text_end[-1])) info.text_end--;
        if (style == WikiHeading::EQUALS || style == WikiHeading::INVERSE_EQUALS) {
            while (info.text_end > info.text_start && info.text_end[-1] == '=') info.text_end--;
            while (info.text_end > info.text_start && str_is_space(info.text_end[-1])) info.text_end--;
        }
        info.valid = true;
        return info;
    }
    ListItemInfo detectListItem(const char* line) override {
        ListItemInfo info;
        const char* p = line;
        while (*p == ' ' || *p == '\t') { info.indent += *p == '\t' ? 4 : 1; p++; }
        info.markers = p;
        if (syntax_->repeated_lists) {
            while (*p == '*' || *p == '#') p++;
            info.marker_count = p - info.markers;
            if (!info.marker_count) return info;
            info.marker = p[-1]; info.is_ordered = info.marker == '#';
        } else if (*p == '*' || *p == '-' || *p == '+') {
            info.marker = *p++;
            info.is_ordered = syntax_->flavor == Flavor::DOKUWIKI && info.marker == '-';
        } else if (*p >= '0' && *p <= '9') {
            const char* digits = p;
            while (*p >= '0' && *p <= '9') p++;
            if (*p != '.' && *p != ')') return ListItemInfo();
            info.number = (int)str_to_int64_default(digits, p - digits, 1);
            info.marker = *p++; info.is_ordered = true;
        } else if (syntax_->flavor == Flavor::ZIMWIKI && *p == '[') {
            info.marker = '*';
        } else return info;
        info.marker_end = p;
        if (!syntax_->repeated_lists && *p != ' ' && *p != '\t' && !(syntax_->flavor == Flavor::ZIMWIKI && *p == '[')) return ListItemInfo();
        p = str_skip_line_space(p);
        detect_task(info, syntax_, &p);
        info.text_start = p; info.valid = true;
        return info;
    }
    CodeFenceInfo detectCodeFence(const char* line) override {
        CodeFenceInfo info;
        const char* p = str_skip_line_space(line);
        struct Fence { const char* open; const char* close; };
        static const Fence tags[] = {{"<syntaxhighlight", "</syntaxhighlight>"}, {"<source", "</source>"},
            {"<pre", "</pre>"}, {"<code", "</code>"}, {"<verbatim", "</verbatim>"}, {"<nowiki", "</nowiki>"}};
        if (*p == '<' && (syntax_->flavor == Flavor::MEDIAWIKI || syntax_->flavor == Flavor::DOKUWIKI || syntax_->flavor == Flavor::TWIKI)) {
            for (const Fence& fence : tags) if (!strncmp(p, fence.open, strlen(fence.open)) && strchr(p, '>')) {
                const char* body = strchr(p, '>') + 1;
                if (*str_skip_line_space(body)) return info; // inline literals are handled by the inline parser
                info.close = fence.close;
                const char* language = strstr(p, "lang=\"");
                if (language) { info.info_string = language + 6; const char* end = strchr(language + 6, '"'); info.info_length = end ? end - info.info_string : 0; }
                else if (syntax_->flavor == Flavor::DOKUWIKI && !strncmp(p, "<code ", 6)) {
                    info.info_string = p + 6; info.info_length = strchr(p, '>') - info.info_string;
                }
                break;
            }
        }
        if ((syntax_->flavor == Flavor::VIMWIKI || syntax_->flavor == Flavor::CREOLE || syntax_->flavor == Flavor::XWIKI) && !strncmp(p, "{{{", 3)) {
            if (strstr(p + 3, "}}}")) return info;
            info.close = "}}}";
            if (syntax_->flavor == Flavor::VIMWIKI) {
                info.info_string = str_skip_line_space(p + 3); info.info_length = strlen(info.info_string);
                if (!strncmp(info.info_string, "class=", 6)) {
                    info.info_string += 6;
                    if (*info.info_string == '"' || *info.info_string == '\'') info.info_string++;
                    if (!strncmp(info.info_string, "brush:", 6)) info.info_string = str_skip_line_space(info.info_string + 6);
                    info.info_length = strcspn(info.info_string, "\"'");
                }
            }
        }
        if (syntax_->flavor == Flavor::ZIMWIKI && !strcmp(p, "'''")) info.close = "'''";
        if (syntax_->flavor == Flavor::TIKIWIKI && !strcmp(p, "~pp~")) info.close = "~/pp~";
        info.valid = info.close != nullptr;
        return info;
    }
    bool isCodeFenceClose(const char* line, const CodeFenceInfo& info) override {
        if (!info.close) return false;
        const char* begin = str_skip_line_space(line);
        size_t length = strlen(begin);
        while (length && str_is_space(begin[length-1])) length--;
        return strlen(info.close) == length && !strncmp(begin, info.close, length);
    }
    BlockquoteInfo detectBlockquote(const char* line) override {
        BlockquoteInfo info;
        const char* p = str_skip_line_space(line);
        if (!strncmp(p, "bq. ", 4) && wiki_is_atlassian(syntax_->flavor)) {
            info.depth = 1; info.content_start = p + 4; info.valid = true;
        } else if (*p == '>' && p[1] == ' ') {
            info.depth = 1; info.content_start = p + 2; info.valid = true;
        }
        return info;
    }
    bool detectTable(const char* line, const char*) override {
        const char* p = str_skip_line_space(line);
        if (syntax_->flavor == Flavor::MEDIAWIKI) return !strncmp(p, "{|", 2);
        if (syntax_->flavor == Flavor::TIKIWIKI) return !strncmp(p, "||", 2);
        return *p == '|' || (syntax_->flavor == Flavor::DOKUWIKI && *p == '^');
    }
    bool detectThematicBreak(const char* line) override {
        const char* p = str_skip_line_space(line);
        int count = 0; while (*p == '-') { count++; p++; }
        return count >= 4 && !*str_skip_line_space(p);
    }
    bool detectIndentedCode(const char*, const char**) override { return false; }
    const DelimiterSpec* emphasisDelimiters() const override { return syntax_->emphasis; }
    size_t emphasisDelimiterCount() const override { return syntax_->emphasis_count; }
    char escapeChar() const override { return syntax_->escape; }
    LinkInfo detectLink(const char* pos) override {
        LinkInfo info;
        const char* open = syntax_->links == WikiLinks::TIKI ? "((" : "[[";
        const char* close = syntax_->links == WikiLinks::TIKI ? "))" : "]]";
        if (syntax_->links == WikiLinks::ATLASSIAN) { open = "["; close = "]"; }
        bool external = syntax_->flavor == Flavor::MEDIAWIKI && *pos == '[' && pos[1] != '[';
        if (syntax_->flavor == Flavor::TIKIWIKI && *pos == '[' && pos[1] != '[') { open = "["; close = "]"; }
        if (external) { open = "["; close = "]"; }
        size_t width = strlen(open);
        if (strncmp(pos, open, width)) return info;
        const char* begin = pos + width;
        if (external && strncmp(begin, "http://", 7) && strncmp(begin, "https://", 8) &&
            strncmp(begin, "ftp://", 6) && strncmp(begin, "mailto:", 7) && strncmp(begin, "//", 2)) return info;
        const char* end = wiki_find_close(begin, close, syntax_->escape);
        if (!end) return info;
        const char* separator = syntax_->links == WikiLinks::TWIKI ? "][" :
            syntax_->links == WikiLinks::XWIKI ? ">>" : external ? " " : "|";
        const char* split = wiki_find_close(begin, separator, syntax_->escape);
        if (split && split >= end) split = nullptr;
        bool label_first = syntax_->links == WikiLinks::XWIKI || syntax_->links == WikiLinks::ATLASSIAN;
        info.url_start = split && label_first ? split + strlen(separator) : begin;
        info.url_end = split && !label_first ? split : end;
        info.text_start = split ? (label_first ? begin : split + strlen(separator)) : begin;
        info.text_end = split && label_first ? split : end;
        // XWiki link parameters follow the resource reference, not the label.
        if (syntax_->links == WikiLinks::XWIKI) {
            const char* options = strstr(info.url_start, "||");
            if (options && options < info.url_end) info.url_end = options;
        }
        info.end_pos = end + strlen(close); info.valid = true;
        return info;
    }
    LinkInfo detectImage(const char* pos) override {
        LinkInfo info;
        const char* start = nullptr; const char* close = nullptr;
        if (syntax_->flavor == Flavor::MEDIAWIKI && (!strncmp(pos, "[[File:", 7) || !strncmp(pos, "[[Image:", 8))) {
            info = detectLink(pos);
            if (!info.valid) return info;
            info.url_start += pos[2] == 'F' ? 5 : 6;
            return info;
        }
        if (syntax_->links == WikiLinks::ATLASSIAN && *pos == '!') { start = pos + 1; close = "!"; }
        else if ((syntax_->flavor == Flavor::DOKUWIKI || syntax_->flavor == Flavor::VIMWIKI ||
                  syntax_->flavor == Flavor::ZIMWIKI || syntax_->flavor == Flavor::CREOLE) && !strncmp(pos, "{{", 2) && pos[2] != '{') {
            start = pos + 2; close = "}}";
        }
        if (!start) return info;
        const char* end = wiki_find_close(start, close, syntax_->escape);
        if (!end) return info;
        const char* split = (const char*)memchr(start, '|', end - start);
        info.url_start = start; info.url_end = split ? split : end;
        info.text_start = split ? split + 1 : end; info.text_end = end;
        info.end_pos = end + strlen(close); info.valid = true;
        return info;
    }
    bool supportsFeature(const char* feature) const override {
        return !strcmp(feature, "tables") || !strcmp(feature, "templates");
    }
};

FormatAdapter* getWikiAdapter(Flavor flavor) {
    struct Adapters {
        WikiAdapter values[sizeof(wiki_profiles) / sizeof(*wiki_profiles)];
        Adapters() {
            for (size_t i = 0; i < sizeof(wiki_profiles) / sizeof(*wiki_profiles); i++)
                values[i] = WikiAdapter(&wiki_profiles[i]);
        }
    };
    static Adapters adapters;
    const WikiSyntax* syntax = wiki_syntax(flavor);
    return syntax ? &adapters.values[syntax - wiki_profiles] : nullptr;
}

} // namespace markup
} // namespace lambda
