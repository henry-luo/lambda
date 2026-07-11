#ifndef FORMAT_UTILS_HPP
#define FORMAT_UTILS_HPP

#include "../../lib/stringbuf.h"
#include "../../lib/str.h"
#include "../../lib/mem_factory.h"
#include "../lambda-data.hpp"
#include "../mark_reader.hpp"
#include "format-utils.h"

class FormatItemHandlersDefault {
public:
    virtual ~FormatItemHandlersDefault() = default;

    virtual void max_depth(const ItemReader& item) { (void)item; }
    virtual void null_value(const ItemReader& item) { (void)item; }
    virtual void bool_value(const ItemReader& item) { (void)item; }
    virtual void number_value(const ItemReader& item) { (void)item; }
    virtual void string_value(const ItemReader& item, String* str) { (void)item; (void)str; }
    virtual void symbol_value(const ItemReader& item, Symbol* sym) { (void)sym; unknown_value(item); }
    virtual void array_value(const ItemReader& item, ArrayReader arr) { (void)arr; unknown_value(item); }
    virtual void map_value(const ItemReader& item, MapReader map) { (void)map; unknown_value(item); }
    virtual void object_value(const ItemReader& item, Object* obj) { (void)obj; unknown_value(item); }
    virtual void element_value(const ItemReader& item, ElementReader elem) { (void)elem; unknown_value(item); }
    virtual void datetime_value(const ItemReader& item, DateTime* dt) { (void)dt; unknown_value(item); }
    virtual void unknown_value(const ItemReader& item) { (void)item; }
};

class ScopedFormatPool {
public:
    explicit ScopedFormatPool(const char* name)
        : pool_(mem_pool_create(NULL, MEM_ROLE_TEMP, name)) {}

    ~ScopedFormatPool() {
        if (pool_) mem_pool_destroy(pool_);
    }

    Pool* get() const { return pool_; }
    operator Pool*() const { return pool_; }

private:
    Pool* pool_;
};

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
    int indent_level() const { return indent_level_; }

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

    inline void dispatch_item(const ItemReader& item, FormatItemHandlersDefault& handlers) {
        RecursionGuard guard(*this);
        if (guard.exceeded()) {
            handlers.max_depth(item);
            return;
        }

        if (item.isNull()) {
            handlers.null_value(item);
        } else if (item.isBool()) {
            handlers.bool_value(item);
        } else if (item.isInt() || item.isFloat() || item.getType() == LMD_TYPE_DECIMAL) {
            handlers.number_value(item);
        } else if (item.isString()) {
            handlers.string_value(item, item.asString());
        } else if (item.isSymbol()) {
            handlers.symbol_value(item, item.asSymbol());
        } else if (item.isArray() || item.isList()) {
            handlers.array_value(item, item.asArray());
        } else if (item.isMap()) {
            handlers.map_value(item, item.asMap());
        } else if (auto object = item.asItem<LMD_TYPE_OBJECT>()) {
            handlers.object_value(item, object.ptr());
        } else if (item.isElement()) {
            handlers.element_value(item, item.asElement());
        } else if (item.isDatetime()) {
            DateTime* dt = (DateTime*)item.item().datetime_ptr;
            handlers.datetime_value(item, dt);
        } else {
            handlers.unknown_value(item);
        }
    }

    // Common formatting operations (utility methods)
    inline void write_text(const char* text) {
        if (output_ && text) {
            stringbuf_append_str(output_, text);
        }
    }

    inline void write_text(String* str) {
        if (output_ && str && str->len > 0) {
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

    // Write explicit indent level (for formatters that pass indent as parameter)
    inline void write_indent(int level) {
        if (output_) {
            for (int i = 0; i < level; i++) {
                stringbuf_append_str(output_, "  ");
            }
        }
    }

    inline void write_newline() {
        if (!compact_mode_ && output_) {
            stringbuf_append_char(output_, '\n');
        }
    }

    // Printf-style template output for concise formatting.
    // Specifiers: %s (C str), %S (String*), %d (int), %l (int64_t), %f (double),
    //   %c (char), %n (newline), %i (indent N×2 spaces), %r (repeat char N times),
    //   %q (quoted C str with \" \\ escaping), %Q (quoted String*),
    //   %b (bool → true/false), %N (name/key alias for %s), %% (literal %)
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
};

// JSON formatter context
class JsonContext : public FormatterContextCpp {
public:
    JsonContext(Pool* pool, StringBuf* output)
        : FormatterContextCpp(pool, output, 50)
    {}
};

// YAML formatter context
class YamlContext : public FormatterContextCpp {
public:
    YamlContext(Pool* pool, StringBuf* output)
        : FormatterContextCpp(pool, output, 50)
    {}
};

// HTML formatter context
class HtmlContext : public FormatterContextCpp {
public:
    HtmlContext(Pool* pool, StringBuf* output)
        : FormatterContextCpp(pool, output, 50)
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

    inline void write_html_escaped_attribute(const char* text) {
        if (!text) return;
        format_escaped_string(output_, text, strlen(text),
            HTML_ATTR_ESCAPE_RULES, HTML_ATTR_ESCAPE_RULES_COUNT);
    }

    inline void write_comment(const char* text) {
        write_text("<!--");
        if (text) write_text(text);
        write_text("-->");
    }
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
};

// XML formatter context
class XmlContext : public FormatterContextCpp {
public:
    XmlContext(Pool* pool, StringBuf* output)
        : FormatterContextCpp(pool, output, 50)
    {}

    // XML-specific utilities
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

    inline void write_xml_escaped_attribute(const char* text) {
        if (!text) return;
        format_escaped_string(output_, text, strlen(text),
            XML_ATTR_ESCAPE_RULES, XML_ATTR_ESCAPE_RULES_COUNT);
    }

    inline void write_comment(const char* text) {
        write_text("<!--");
        if (text) write_text(text);
        write_text("-->");
    }

};

class TomlContext : public FormatterContextCpp {
public:
    TomlContext(Pool* pool, StringBuf* output)
        : FormatterContextCpp(pool, output, 20)
    {}
};

static inline void format_write_datetime_iso8601(StringBuf* output, DateTime* dt) {
    if (!output || !dt) return;
    StrBuf* temp = strbuf_new();
    if (!temp) return;
    datetime_format_iso8601(temp, dt);
    stringbuf_append_str_n(output, temp->str, temp->length);
    strbuf_free(temp);
}

#endif // FORMAT_UTILS_HPP
