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

#endif // FORMAT_UTILS_HPP
