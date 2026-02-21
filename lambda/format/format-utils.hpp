#ifndef FORMAT_UTILS_HPP
#define FORMAT_UTILS_HPP

#include "../../lib/stringbuf.h"
#include "../lambda-data.hpp"

// C++ FormatterContext base class for object-oriented formatter architecture
// Note: Named FormatterContextCpp to avoid conflict with C struct FormatterContext in format-utils.h
class FormatterContextCpp {
public:
    // Constructor
    FormatterContextCpp(Pool* pool, StringBuf* output, int max_depth = 50)
        : output_(output)
        , pool_(pool)
        , recursion_depth_(0)
        , indent_level_(0)
        , max_recursion_depth_(max_depth)
        , compact_mode_(false)
    {}

    virtual ~FormatterContextCpp() = default;

    // Core accessors
    StringBuf* output() const { return output_; }
    Pool* pool() const { return pool_; }
    int recursion_depth() const { return recursion_depth_; }
    int indent_level() const { return indent_level_; }
    bool is_compact() const { return compact_mode_; }
    int max_recursion_depth() const { return max_recursion_depth_; }

    // RAII-based recursion management
    class RecursionGuard {
    public:
        inline RecursionGuard(FormatterContextCpp& ctx)
            : ctx_(ctx)
            , exceeded_(ctx.recursion_depth_ >= ctx.max_recursion_depth_)
        {
            if (!exceeded_) {
                ctx_.enter_recursion();
            }
        }

        inline ~RecursionGuard() {
            if (!exceeded_) {
                ctx_.exit_recursion();
            }
        }

        inline bool exceeded() const { return exceeded_; }

    private:
        FormatterContextCpp& ctx_;
        bool exceeded_;
    };

    // Common formatting operations (utility methods)
    inline void write_text(const char* text) {
        if (output_ && text) {
            stringbuf_append_str(output_, text);
        }
    }

    inline void write_text(String* str) {
        if (output_ && str && str->chars && str->len > 0) {
            stringbuf_append_str_n(output_, str->chars, str->len);
        }
    }

    inline void write_char(char c) {
        if (output_) {
            stringbuf_append_char(output_, c);
        }
    }

    inline void write_indent() {
        if (!compact_mode_ && output_) {
            for (int i = 0; i < indent_level_; i++) {
                stringbuf_append_str(output_, "  ");
            }
        }
    }

    inline void write_newline() {
        if (!compact_mode_ && output_) {
            stringbuf_append_char(output_, '\n');
        }
    }

    // Indentation control
    inline void increase_indent() { indent_level_++; }
    inline void decrease_indent() { if (indent_level_ > 0) indent_level_--; }

    // Compact mode
    inline void set_compact(bool compact) { compact_mode_ = compact; }

    // Printf-style template output for concise formatting.
    // See stringbuf_emit() for format specifiers:
    //   %s (C string), %S (String*), %d (int), %l (int64_t), %f (double),
    //   %c (char), %n (newline), %i (indent N*2 spaces), %r (repeat char N times)
    inline void emit(const char* fmt, ...) {
        if (!output_ || !fmt) return;
        va_list args;
        va_start(args, fmt);
        stringbuf_vemit(output_, fmt, args);
        va_end(args);
    }

protected:
    StringBuf* output_;
    Pool* pool_;
    int recursion_depth_;
    int indent_level_;
    int max_recursion_depth_;
    bool compact_mode_;

private:
    friend class RecursionGuard;

    inline void enter_recursion() { recursion_depth_++; }
    inline void exit_recursion() { recursion_depth_--; }
};

// Text formatter context - simplest formatter, good for pilot
class TextContext : public FormatterContextCpp {
public:
    TextContext(Pool* pool, StringBuf* output)
        : FormatterContextCpp(pool, output, 50)
    {}

    // Text-specific utilities (can be extended as needed)
    inline void write_separator(const char* sep = " ") {
        write_text(sep);
    }
};

// Wiki formatter context - MediaWiki formatting
class WikiContext : public FormatterContextCpp {
public:
    WikiContext(Pool* pool, StringBuf* output)
        : FormatterContextCpp(pool, output, 50)
    {}

    // Wiki-specific utilities
    inline void write_heading_prefix(int level) {
        for (int i = 0; i < level; i++) {
            write_char('=');
        }
        write_char(' ');
    }

    inline void write_heading_suffix(int level) {
        write_char(' ');
        for (int i = 0; i < level; i++) {
            write_char('=');
        }
        write_newline();
    }

    inline void write_list_marker(bool ordered, int level, int index = 0) {
        for (int i = 0; i <= level; i++) {
            write_char(ordered ? '#' : '*');
        }
        write_char(' ');
    }

    inline void write_link(const char* url, const char* text = nullptr) {
        write_char('[');
        write_text(url);
        if (text) {
            write_char(' ');
            write_text(text);
        }
        write_char(']');
    }
};

// RST formatter context - reStructuredText formatting
class RstContext : public FormatterContextCpp {
public:
    RstContext(Pool* pool, StringBuf* output)
        : FormatterContextCpp(pool, output, 50)
    {}

    // RST-specific utilities
    inline void write_heading_underline(int level, int text_length) {
        // RST heading characters in order of preference
        char underline_chars[] = {'=', '-', '~', '^', '"', '\''};
        char underline_char = underline_chars[(level - 1) % 6];

        write_newline();
        for (int i = 0; i < text_length; i++) {
            write_char(underline_char);
        }
        write_text("\n\n");
    }

    inline void write_list_prefix(int depth, bool ordered) {
        // RST uses indentation for nesting
        for (int i = 0; i < depth; i++) {
            write_text("  ");
        }
        if (ordered) {
            write_text("#. ");
        } else {
            write_text("* ");
        }
    }

    inline void write_escaped_rst_char(char c) {
        // RST special characters that need escaping
        switch (c) {
        case '*':
        case '_':
        case '|':
        case '\\':
        case ':':
            write_char('\\');
            write_char(c);
            break;
        default:
            write_char(c);
            break;
        }
    }
};

// Markdown formatter context
class MarkdownContext : public FormatterContextCpp {
public:
    MarkdownContext(Pool* pool, StringBuf* output)
        : FormatterContextCpp(pool, output, 50)
        , list_depth_(0)
        , in_table_(false)
        , in_code_block_(false)
    {}

    // markdown heading: ## Heading
    inline void write_heading_prefix(int level) {
        write_newline();
        for (int i = 0; i < level && i < 6; i++) {
            write_char('#');
        }
        write_char(' ');
    }

    // markdown list marker
    inline void write_list_marker(bool ordered, int index) {
        if (ordered) {
            char num_buf[16];
            snprintf(num_buf, sizeof(num_buf), "%d", index);
            write_text(num_buf);
            write_text(". ");
        } else {
            write_text("- ");
        }
    }

    // markdown code fence: ```lang
    inline void write_code_fence(const char* lang = nullptr) {
        write_text("```");
        if (lang && lang[0] != '\0') {
            write_text(lang);
        }
        write_newline();
    }

    // markdown link: [text](url)
    inline void write_link(const char* url, String* text = nullptr) {
        write_char('[');
        if (text) write_text(text);
        write_text("](");
        write_text(url);
        write_char(')');
    }

    // state tracking
    bool in_list() const { return list_depth_ > 0; }
    void enter_list() { list_depth_++; }
    void exit_list() { if (list_depth_ > 0) list_depth_--; }

    bool in_table() const { return in_table_; }
    void set_in_table(bool in_table) { in_table_ = in_table; }

    bool in_code_block() const { return in_code_block_; }
    void set_in_code_block(bool in_code) { in_code_block_ = in_code; }

private:
    int list_depth_;
    bool in_table_;
    bool in_code_block_;
};

// Org-mode formatter context
class OrgContext : public FormatterContextCpp {
public:
    OrgContext(Pool* pool, StringBuf* output)
        : FormatterContextCpp(pool, output, 50)
    {}

    // Org-mode specific utilities
    inline void write_heading_stars(int level) {
        for (int i = 0; i < level; i++) {
            write_char('*');
        }
        write_char(' ');
    }

    inline void write_list_marker(bool ordered, int counter = 1) {
        if (ordered) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d. ", counter);
            write_text(buf);
        } else {
            write_text("- ");
        }
    }

    inline void write_inline_markup(const char* marker, const char* content) {
        write_text(marker);
        write_text(content);
        write_text(marker);
    }

    inline void write_timestamp(const char* timestamp) {
        write_char('<');
        write_text(timestamp);
        write_char('>');
    }

    inline void write_property_drawer_start() {
        write_text(":PROPERTIES:\n");
    }

    inline void write_property_drawer_end() {
        write_text(":END:\n");
    }

    inline void write_property(const char* key, const char* value) {
        write_char(':');
        write_text(key);
        write_text(": ");
        write_text(value);
        write_newline();
    }
};

// JSON formatter context
class JsonContext : public FormatterContextCpp {
public:
    JsonContext(Pool* pool, StringBuf* output)
        : FormatterContextCpp(pool, output, 50)
        , indent_level_(0)
    {}

    // JSON-specific utilities
    inline void write_indent(int indent) {
        for (int i = 0; i < indent; i++) {
            write_text("  ");
        }
    }

    inline void write_string_escaped(const char* str) {
        write_char('"');
        if (str) {
            for (const char* p = str; *p; p++) {
                switch (*p) {
                case '"':  write_text("\\\""); break;
                case '\\': write_text("\\\\"); break;
                case '\n': write_text("\\n"); break;
                case '\r': write_text("\\r"); break;
                case '\t': write_text("\\t"); break;
                case '\b': write_text("\\b"); break;
                case '\f': write_text("\\f"); break;
                default:
                    if ((unsigned char)*p < 32) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)*p);
                        write_text(buf);
                    } else {
                        write_char(*p);
                    }
                    break;
                }
            }
        }
        write_char('"');
    }

    inline void write_key_value_separator() {
        write_char(':');
    }

    inline void write_comma() {
        write_char(',');
    }

    inline void write_object_start() {
        write_char('{');
    }

    inline void write_object_end() {
        write_char('}');
    }

    inline void write_array_start() {
        write_char('[');
    }

    inline void write_array_end() {
        write_char(']');
    }

    inline void write_null() {
        write_text("null");
    }

    inline void write_bool(bool value) {
        write_text(value ? "true" : "false");
    }

    inline void write_number(const char* num) {
        write_text(num);
    }

    // Indentation tracking
    int indent_level() const { return indent_level_; }
    void increase_indent() { indent_level_++; }
    void decrease_indent() { if (indent_level_ > 0) indent_level_--; }

private:
    int indent_level_;
};

// YAML formatter context
class YamlContext : public FormatterContextCpp {
public:
    YamlContext(Pool* pool, StringBuf* output)
        : FormatterContextCpp(pool, output, 50)
    {}

    // YAML-specific utilities
    inline void write_yaml_indent(int indent_level) {
        for (int i = 0; i < indent_level * 2; i++) {
            write_char(' ');
        }
    }

    inline void write_yaml_key(const char* key) {
        write_text(key);
        write_text(": ");
    }

    inline void write_yaml_list_marker() {
        write_text("- ");
    }

    inline void write_yaml_null() {
        write_text("null");
    }

    inline void write_yaml_bool(bool value) {
        write_text(value ? "true" : "false");
    }

    inline void write_document_separator() {
        write_text("---\n");
    }

    inline void write_document_end() {
        write_text("...\n");
    }

    // Check if string needs quoting in YAML
    static bool needs_yaml_quotes(const char* s, size_t len) {
        if (!s || len == 0) return true;

        // Check for special characters
        if (strchr(s, ':') || strchr(s, '\n') || strchr(s, '"') ||
            strchr(s, '\'') || strchr(s, '#') || strchr(s, '-') ||
            strchr(s, '[') || strchr(s, ']') || strchr(s, '{') ||
            strchr(s, '}') || strchr(s, '|') || strchr(s, '>') ||
            strchr(s, '&') || strchr(s, '*') || strchr(s, '!')) {
            return true;
        }

        // Check for leading/trailing whitespace
        if (isspace(s[0]) || isspace(s[len-1])) {
            return true;
        }

        // Check for YAML reserved words
        if (strcmp(s, "true") == 0 || strcmp(s, "false") == 0 ||
            strcmp(s, "null") == 0 || strcmp(s, "yes") == 0 ||
            strcmp(s, "no") == 0 || strcmp(s, "on") == 0 ||
            strcmp(s, "off") == 0 || strcmp(s, "~") == 0) {
            return true;
        }

        return false;
    }

    inline void write_yaml_string(const char* s, size_t len, bool force_quotes = false) {
        if (!s) {
            write_yaml_null();
            return;
        }

        if (force_quotes || needs_yaml_quotes(s, len)) {
            write_char('"');
            for (size_t i = 0; i < len; i++) {
                switch (s[i]) {
                case '"':  write_text("\\\""); break;
                case '\\': write_text("\\\\"); break;
                case '\n': write_text("\\n"); break;
                case '\r': write_text("\\r"); break;
                case '\t': write_text("\\t"); break;
                default:   write_char(s[i]); break;
                }
            }
            write_char('"');
        } else {
            for (size_t i = 0; i < len; i++) {
                write_char(s[i]);
            }
        }
    }
};

// HTML formatter context
class HtmlContext : public FormatterContextCpp {
public:
    HtmlContext(Pool* pool, StringBuf* output)
        : FormatterContextCpp(pool, output, 50)
        , depth_(0)
    {}

    // HTML-specific utilities
    inline void write_tag_open(const char* tag_name) {
        write_char('<');
        write_text(tag_name);
    }

    inline void write_tag_close() {
        write_char('>');
    }

    inline void write_tag_self_close() {
        write_text(" />");
    }

    inline void write_closing_tag(const char* tag_name) {
        write_text("</");
        write_text(tag_name);
        write_char('>');
    }

    inline void write_attribute(const char* name, const char* value) {
        write_char(' ');
        write_text(name);
        write_text("=\"");
        if (value) {
            write_html_escaped_attribute(value);
        }
        write_char('"');
    }

    inline void write_html_escaped_text(const char* text) {
        if (!text) return;
        for (const char* p = text; *p; p++) {
            switch (*p) {
            case '<':  write_text("&lt;"); break;
            case '>':  write_text("&gt;"); break;
            case '&':  write_text("&amp;"); break;
            default:   write_char(*p); break;
            }
        }
    }

    inline void write_html_escaped_attribute(const char* text) {
        if (!text) return;
        for (const char* p = text; *p; p++) {
            switch (*p) {
            case '<':  write_text("&lt;"); break;
            case '>':  write_text("&gt;"); break;
            case '&':  write_text("&amp;"); break;
            case '"':  write_text("&quot;"); break;
            case '\'': write_text("&#39;"); break;
            default:   write_char(*p); break;
            }
        }
    }

    inline void write_doctype() {
        write_text("<!DOCTYPE html>\n");
    }

    inline void write_comment(const char* text) {
        write_text("<!--");
        if (text) write_text(text);
        write_text("-->");
    }

    // Depth tracking for indentation
    int depth() const { return depth_; }
    void increase_depth() { depth_++; }
    void decrease_depth() { if (depth_ > 0) depth_--; }

    inline void write_indent() {
        for (int i = 0; i < depth_; i++) {
            write_text("  ");
        }
    }

private:
    int depth_;
};

// LaTeX formatter context
class LaTeXContext : public FormatterContextCpp {
public:
    LaTeXContext(Pool* pool, StringBuf* output)
        : FormatterContextCpp(pool, output, 50)
    {}

    // LaTeX-specific utilities
    inline void write_command(const char* cmd_name) {
        write_char('\\');
        write_text(cmd_name);
    }

    inline void write_command_with_arg(const char* cmd_name, const char* arg) {
        write_char('\\');
        write_text(cmd_name);
        write_char('{');
        if (arg) write_text(arg);
        write_char('}');
    }

    inline void write_begin_environment(const char* env_name) {
        write_text("\\begin{");
        write_text(env_name);
        write_char('}');
    }

    inline void write_end_environment(const char* env_name) {
        write_text("\\end{");
        write_text(env_name);
        write_char('}');
    }

    inline void write_latex_escaped_text(const char* text) {
        if (!text) return;
        for (const char* p = text; *p; p++) {
            switch (*p) {
            case '\\': write_text("\\textbackslash{}"); break;
            case '{':  write_text("\\{"); break;
            case '}':  write_text("\\}"); break;
            case '$':  write_text("\\$"); break;
            case '&':  write_text("\\&"); break;
            case '%':  write_text("\\%"); break;
            case '#':  write_text("\\#"); break;
            case '_':  write_text("\\_"); break;
            case '^':  write_text("\\^{}"); break;
            case '~':  write_text("\\~{}"); break;
            default:   write_char(*p); break;
            }
        }
    }

    inline void write_optional_arg(const char* arg) {
        write_char('[');
        if (arg) write_text(arg);
        write_char(']');
    }

    inline void write_latex_comment(const char* text) {
        write_char('%');
        if (text) write_text(text);
        write_newline();
    }

    inline void write_math_inline(const char* math) {
        write_char('$');
        if (math) write_text(math);
        write_char('$');
    }

    inline void write_math_display(const char* math) {
        write_text("\\[");
        if (math) write_text(math);
        write_text("\\]");
    }

    inline void write_latex_indent(int level) {
        for (int i = 0; i < level; i++) {
            write_text("  ");
        }
    }
};

// XML formatter context
class XmlContext : public FormatterContextCpp {
public:
    XmlContext(Pool* pool, StringBuf* output)
        : FormatterContextCpp(pool, output, 50)
    {}

    // XML-specific utilities
    inline void write_xml_declaration() {
        write_text("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    }

    inline void write_tag_open(const char* tag_name) {
        write_char('<');
        write_text(tag_name);
    }

    inline void write_tag_close() {
        write_char('>');
    }

    inline void write_tag_self_close() {
        write_text(" />");
    }

    inline void write_closing_tag(const char* tag_name) {
        write_text("</");
        write_text(tag_name);
        write_char('>');
    }

    inline void write_attribute(const char* name, const char* value) {
        write_char(' ');
        write_text(name);
        write_text("=\"");
        if (value) {
            write_xml_escaped_attribute(value);
        }
        write_char('"');
    }

    inline void write_xml_escaped_text(const char* text) {
        if (!text) return;
        for (const char* p = text; *p; p++) {
            switch (*p) {
            case '<':  write_text("&lt;"); break;
            case '>':  write_text("&gt;"); break;
            case '&':  write_text("&amp;"); break;
            default:   write_char(*p); break;
            }
        }
    }

    inline void write_xml_escaped_attribute(const char* text) {
        if (!text) return;
        for (const char* p = text; *p; p++) {
            switch (*p) {
            case '<':  write_text("&lt;"); break;
            case '>':  write_text("&gt;"); break;
            case '&':  write_text("&amp;"); break;
            case '"':  write_text("&quot;"); break;
            case '\'': write_text("&apos;"); break;
            default:   write_char(*p); break;
            }
        }
    }

    inline void write_cdata_start() {
        write_text("<![CDATA[");
    }

    inline void write_cdata_end() {
        write_text("]]>");
    }

    inline void write_comment(const char* text) {
        write_text("<!--");
        if (text) write_text(text);
        write_text("-->");
    }

    inline void write_xml_indent(int level) {
        for (int i = 0; i < level; i++) {
            write_text("  ");
        }
    }
};

// CSS formatter context
class CssContext : public FormatterContextCpp {
public:
    CssContext(Pool* pool, StringBuf* output)
        : FormatterContextCpp(pool, output, 50)
    {}

    // CSS-specific utilities
    inline void write_css_indent(int level) {
        for (int i = 0; i < level; i++) {
            write_text("  ");
        }
    }

    inline void write_selector(const char* selector) {
        if (selector) write_text(selector);
    }

    inline void write_property(const char* property, const char* value) {
        if (property) write_text(property);
        write_text(": ");
        if (value) write_text(value);
        write_char(';');
    }

    inline void write_rule_start() {
        write_text(" {");
        write_newline();
    }

    inline void write_rule_end(int indent) {
        write_css_indent(indent);
        write_char('}');
        write_newline();
    }

    inline void write_at_rule(const char* name) {
        write_char('@');
        if (name) write_text(name);
    }

    inline void write_media_query(const char* query) {
        write_text("@media ");
        if (query) write_text(query);
        write_rule_start();
    }

    inline void write_keyframe_selector(const char* selector) {
        if (selector) write_text(selector);
        write_rule_start();
    }

    inline void write_comment(const char* text) {
        write_text("/* ");
        if (text) write_text(text);
        write_text(" */");
    }
};

#endif // FORMAT_UTILS_HPP
