// html_writer.hpp - Abstract interface for HTML generation with dual-mode support
// Translates latex.js html-generator.ls to C++ with text/node output modes

#ifndef HTML_WRITER_HPP
#define HTML_WRITER_HPP

#include "../../lib/strbuf.h"
#include "../mark_builder.hpp"
#include "../lambda-data.hpp"
#include <vector>  // TODO: migrate NodeHtmlWriter to avoid std::vector

// Maximum nesting depth for HTML tags
#define HTML_TAG_STACK_MAX 128

namespace lambda {

// Abstract base class for HTML generation
// Provides unified interface for both text and node output modes
class HtmlWriter {
public:
    virtual ~HtmlWriter() = default;
    
    // Text output
    virtual void writeText(const char* text, size_t len = 0) = 0;
    virtual void writeRawHtml(const char* html) = 0;
    
    // Trim trailing whitespace from the output buffer (for paragraph handling)
    virtual void trimTrailingWhitespace() = 0;
    
    // Check if output buffer has trailing whitespace
    virtual bool hasTrailingWhitespace() const = 0;
    
    // Remove last opened tag if it's empty (e.g., removes "<p>" if nothing written after it)
    // Returns true if tag was removed, false otherwise
    virtual bool removeLastOpenedTagIfEmpty(const char* tag) = 0;
    
    // Check if a specific tag is currently open on the tag stack
    virtual bool isTagOpen(const char* tag) const = 0;
    
    // Element creation
    virtual void openTag(const char* tag, const char* classes = nullptr, 
                         const char* id = nullptr, const char* style = nullptr) = 0;
    virtual void openTagRaw(const char* tag, const char* raw_attrs) = 0;  // Raw attribute string
    virtual void closeTag(const char* tag) = 0;
    virtual void writeSelfClosingTag(const char* tag, const char* classes = nullptr, 
                                     const char* attrs = nullptr) = 0;
    
    // Convenience methods
    virtual void writeElement(const char* tag, const char* content, 
                             const char* classes = nullptr) = 0;
    virtual void writeAttribute(const char* name, const char* value) = 0;
    
    // Indentation control (for text mode pretty-printing)
    virtual void indent() = 0;
    virtual void unindent() = 0;
    virtual void newline() = 0;
    
    // Output retrieval
    virtual Item getResult() = 0;  // For node mode: returns Element tree, text mode: returns String
    virtual const char* getHtml() = 0;  // For text mode: returns HTML string, node mode: nullptr
};

// Text mode implementation: generates HTML strings using strbuf
class TextHtmlWriter : public HtmlWriter {
private:
    StrBuf* buf_;
    int indent_level_;
    bool pretty_print_;
    Pool* pool_;
    bool in_tag_;  // Track if we're inside a tag (for attribute writing)
    const char* tag_stack_[HTML_TAG_STACK_MAX];  // Track open tags for proper closing
    int tag_stack_top_;  // Index of top element (-1 = empty)
    
public:
    explicit TextHtmlWriter(Pool* pool, bool pretty_print = false);
    ~TextHtmlWriter() override;
    
    void writeText(const char* text, size_t len = 0) override;
    void writeRawHtml(const char* html) override;
    void trimTrailingWhitespace() override;
    bool hasTrailingWhitespace() const override;
    bool removeLastOpenedTagIfEmpty(const char* tag) override;
    bool isTagOpen(const char* tag) const override;
    void openTag(const char* tag, const char* classes = nullptr, 
                const char* id = nullptr, const char* style = nullptr) override;
    void openTagRaw(const char* tag, const char* raw_attrs) override;
    void closeTag(const char* tag) override;
    void writeSelfClosingTag(const char* tag, const char* classes = nullptr,
                            const char* attrs = nullptr) override;
    void writeElement(const char* tag, const char* content, 
                     const char* classes = nullptr) override;
    void writeAttribute(const char* name, const char* value) override;
    
    void indent() override;
    void unindent() override;
    void newline() override;
    
    Item getResult() override;  // Returns String
    const char* getHtml() override;
    
private:
    void appendIndent();
    void escapeHtml(const char* text, size_t len);
    void closeTagStart();  // Close the opening tag if needed (for attributes)
};

// Node mode implementation: generates Lambda Element tree using MarkBuilder
// TODO: migrate to avoid std::vector - NodeHtmlWriter is not currently used
class NodeHtmlWriter : public HtmlWriter {
private:
    MarkBuilder* builder_;
    std::vector<ElementBuilder> stack_;  // Stack of open elements
    Pool* pool_;
    Input* input_;  // Need Input* for MarkBuilder
    ElementBuilder* pending_attributes_;  // For buffering attributes before children
    
public:
    explicit NodeHtmlWriter(Input* input);
    ~NodeHtmlWriter() override;
    
    void writeText(const char* text, size_t len = 0) override;
    void writeRawHtml(const char* html) override;  // Parse HTML → Elements (not implemented yet)
    void trimTrailingWhitespace() override { /* no-op for node mode */ }
    bool hasTrailingWhitespace() const override { return false; /* TODO: implement for node mode */ }
    bool removeLastOpenedTagIfEmpty(const char* tag) override { return false; /* TODO: implement for node mode */ }
    bool isTagOpen(const char* tag) const override { return false; /* TODO: implement for node mode */ }
    void openTag(const char* tag, const char* classes = nullptr,
                const char* id = nullptr, const char* style = nullptr) override;
    void openTagRaw(const char* tag, const char* raw_attrs) override;
    void closeTag(const char* tag) override;
    void writeSelfClosingTag(const char* tag, const char* classes = nullptr,
                            const char* attrs = nullptr) override;
    void writeElement(const char* tag, const char* content,
                     const char* classes = nullptr) override;
    void writeAttribute(const char* name, const char* value) override;
    
    void indent() override { /* no-op */ }
    void unindent() override { /* no-op */ }
    void newline() override { /* no-op */ }
    
    Item getResult() override;  // Returns root Element
    const char* getHtml() override { return nullptr; }  // Not applicable
    
private:
    ElementBuilder& current();
    bool hasOpenElements() const { return !stack_.empty(); }
};

// Null mode implementation: discards all output (for label collection pass)
class NullHtmlWriter : public HtmlWriter {
public:
    NullHtmlWriter() = default;
    ~NullHtmlWriter() override = default;
    
    void writeText(const char* text, size_t len = 0) override { (void)text; (void)len; }
    void writeRawHtml(const char* html) override { (void)html; }
    void trimTrailingWhitespace() override {}
    bool hasTrailingWhitespace() const override { return false; }
    bool removeLastOpenedTagIfEmpty(const char* tag) override { (void)tag; return false; }
    bool isTagOpen(const char* tag) const override { (void)tag; return false; }
    void openTag(const char* tag, const char* classes = nullptr,
                const char* id = nullptr, const char* style = nullptr) override {
        (void)tag; (void)classes; (void)id; (void)style;
    }
    void openTagRaw(const char* tag, const char* raw_attrs) override {
        (void)tag; (void)raw_attrs;
    }
    void closeTag(const char* tag) override { (void)tag; }
    void writeSelfClosingTag(const char* tag, const char* classes = nullptr,
                            const char* attrs = nullptr) override {
        (void)tag; (void)classes; (void)attrs;
    }
    void writeElement(const char* tag, const char* content,
                     const char* classes = nullptr) override {
        (void)tag; (void)content; (void)classes;
    }
    void writeAttribute(const char* name, const char* value) override {
        (void)name; (void)value;
    }
    
    void indent() override {}
    void unindent() override {}
    void newline() override {}
    
    Item getResult() override { Item result; result.item = ITEM_NULL; return result; }
    const char* getHtml() override { return nullptr; }
};

} // namespace lambda

#endif // HTML_WRITER_HPP
