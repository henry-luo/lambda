// html_writer.cpp - Implementation of HtmlWriter interface
// Provides text and node output modes for HTML generation

#include "html_writer.hpp"
#include "../input/input.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <string>  // TODO: remove when NodeHtmlWriter is migrated

namespace lambda {

// =============================================================================
// TextHtmlWriter Implementation
// =============================================================================

TextHtmlWriter::TextHtmlWriter(Pool* pool, bool pretty_print)
    : buf_(strbuf_new()), 
      indent_level_(0), 
      pretty_print_(pretty_print),
      pool_(pool),
      in_tag_(false),
      tag_stack_top_(-1) {
    // initialize tag_stack_ to NULL
    for (int i = 0; i < HTML_TAG_STACK_MAX; i++) {
        tag_stack_[i] = nullptr;
    }
}

TextHtmlWriter::~TextHtmlWriter() {
    if (buf_) {
        strbuf_free(buf_);
    }
}

void TextHtmlWriter::writeText(const char* text, size_t len) {
    if (!text) return;
    
    if (len == 0) {
        len = strlen(text);
    }
    
    // Skip EMPTY_STRING sentinel ("lambda.nil")
    if (len == 10 && strncmp(text, "lambda.nil", 10) == 0) {
        // Output a marker instead to verify this path is taken
        closeTagStart();
        strbuf_append_str(buf_, "[SKIPPED]");
        return;
    }
    
    closeTagStart();  // Close tag if we're in one
    
    escapeHtml(text, len);
}

void TextHtmlWriter::writeRawHtml(const char* html) {
    if (!html) return;
    
    closeTagStart();
    strbuf_append_str(buf_, html);
}

void TextHtmlWriter::trimTrailingWhitespace() {
    // Trim trailing whitespace from the buffer (spaces and tabs, not newlines)
    // This is used before closing paragraph tags to match LaTeX.js behavior
    size_t len = buf_->length;
    while (len > 0) {
        char c = buf_->str[len - 1];
        if (c == ' ' || c == '\t') {
            len--;
        } else {
            break;
        }
    }
    buf_->length = len;
    buf_->str[len] = '\0';
}

bool TextHtmlWriter::hasTrailingWhitespace() const {
    // Check if the buffer ends with whitespace (spaces or tabs)
    if (buf_->length == 0) return false;
    char c = buf_->str[buf_->length - 1];
    return (c == ' ' || c == '\t');
}

bool TextHtmlWriter::removeLastOpenedTagIfEmpty(const char* tag) {
    // Check if the buffer ends with "<tag>" or "<tag" (still open, unclosed)
    // If so, remove it and return true. Otherwise return false.
    // This is used to clean up empty <p> tags created by paragraph breaks at end of list items.
    
    if (!tag || buf_->length == 0) return false;
    
    size_t tag_len = strlen(tag);
    
    // Case 1: Tag is still open (in_tag_ is true), buffer ends with "<tag"
    if (in_tag_) {
        size_t suffix_len = tag_len + 1;  // '<' + tag (no '>')
        if (buf_->length >= suffix_len) {
            const char* suffix_start = buf_->str + buf_->length - suffix_len;
            if (suffix_start[0] == '<' && strncmp(suffix_start + 1, tag, tag_len) == 0) {
                // It matches! Remove it
                buf_->length -= suffix_len;
                buf_->str[buf_->length] = '\0';
                
                // Reset in_tag state and pop from tag_stack_
                in_tag_ = false;
                if (tag_stack_top_ >= 0 && strcmp(tag_stack_[tag_stack_top_], tag) == 0) {
                    tag_stack_top_--;
                }
                return true;
            }
        }
    }
    
    // Case 2: Tag is fully closed, buffer ends with "<tag>"
    size_t suffix_len = tag_len + 2;  // '<' + tag + '>'
    if (buf_->length >= suffix_len) {
        const char* suffix_start = buf_->str + buf_->length - suffix_len;
        if (suffix_start[0] == '<' && 
            suffix_start[suffix_len - 1] == '>' && 
            strncmp(suffix_start + 1, tag, tag_len) == 0) {
            // It matches! Remove it
            buf_->length -= suffix_len;
            buf_->str[buf_->length] = '\0';
            
            // Pop from tag_stack_ if the top matches
            if (tag_stack_top_ >= 0 && strcmp(tag_stack_[tag_stack_top_], tag) == 0) {
                tag_stack_top_--;
            }
            return true;
        }
    }
    
    return false;
}

bool TextHtmlWriter::isTagOpen(const char* tag) const {
    // Check if a specific tag is currently in the open tag stack
    if (!tag) return false;
    
    for (int i = 0; i <= tag_stack_top_; i++) {
        if (strcmp(tag_stack_[i], tag) == 0) {
            return true;
        }
    }
    return false;
}

void TextHtmlWriter::openTag(const char* tag, const char* classes, 
                             const char* id, const char* style) {
    if (!tag) return;
    
    closeTagStart();  // Close any previous tag
    
    if (pretty_print_) {
        appendIndent();
    }
    
    strbuf_append_char(buf_, '<');
    strbuf_append_str(buf_, tag);
    
    // Add attributes
    if (classes && classes[0]) {
        strbuf_append_str(buf_, " class=\"");
        strbuf_append_str(buf_, classes);
        strbuf_append_char(buf_, '"');
    }
    
    if (id && id[0]) {
        strbuf_append_str(buf_, " id=\"");
        strbuf_append_str(buf_, id);
        strbuf_append_char(buf_, '"');
    }
    
    if (style && style[0]) {
        strbuf_append_str(buf_, " style=\"");
        strbuf_append_str(buf_, style);
        strbuf_append_char(buf_, '"');
    }
    
    in_tag_ = true;  // Mark that we're inside a tag
    // Track open tag - push to stack
    if (tag_stack_top_ < HTML_TAG_STACK_MAX - 1) {
        tag_stack_[++tag_stack_top_] = tag;
    }
    
    // Debug: track paragraph opens
    if (strcmp(tag, "p") == 0) {
        log_debug("openTag(p): stack size now %d", tag_stack_top_ + 1);
    }
}

void TextHtmlWriter::openTagRaw(const char* tag, const char* raw_attrs) {
    if (!tag) return;
    
    closeTagStart();  // Close any previous tag
    
    if (pretty_print_) {
        appendIndent();
    }
    
    strbuf_append_char(buf_, '<');
    strbuf_append_str(buf_, tag);
    
    // Add raw attributes
    if (raw_attrs && raw_attrs[0]) {
        strbuf_append_char(buf_, ' ');
        strbuf_append_str(buf_, raw_attrs);
    }
    
    in_tag_ = true;  // Mark that we're inside a tag
    // Track open tag - push to stack
    if (tag_stack_top_ < HTML_TAG_STACK_MAX - 1) {
        tag_stack_[++tag_stack_top_] = tag;
    }
}

void TextHtmlWriter::closeTag(const char* tag) {
    closeTagStart();  // Close tag start if needed
    
    // If tag is nullptr, close the most recent tag from stack
    const char* tag_name = nullptr;
    if (tag == nullptr || tag[0] == '\0') {
        if (tag_stack_top_ >= 0) {
            tag_name = tag_stack_[tag_stack_top_];
            tag_stack_top_--;
        } else {
            return;  // No tags to close
        }
    } else {
        tag_name = tag;
        // Remove from stack if present - search from top
        for (int i = tag_stack_top_; i >= 0; i--) {
            if (strcmp(tag_stack_[i], tag) == 0) {
                // Shift remaining elements down
                for (int j = i; j < tag_stack_top_; j++) {
                    tag_stack_[j] = tag_stack_[j + 1];
                }
                tag_stack_top_--;
                break;
            }
        }
    }
    
    // Debug: track paragraph closes
    if (strcmp(tag_name, "p") == 0) {
        log_debug("closeTag(p): stack size now %d", tag_stack_top_ + 1);
    }
    
    if (pretty_print_) {
        appendIndent();
    }
    
    strbuf_append_str(buf_, "</");
    strbuf_append_str(buf_, tag_name);
    strbuf_append_char(buf_, '>');
    
    if (pretty_print_) {
        strbuf_append_char(buf_, '\n');
    }
}

void TextHtmlWriter::writeSelfClosingTag(const char* tag, const char* classes, 
                                        const char* attrs) {
    if (!tag) return;
    
    closeTagStart();
    
    if (pretty_print_) {
        appendIndent();
    }
    
    strbuf_append_char(buf_, '<');
    strbuf_append_str(buf_, tag);
    
    if (classes && classes[0]) {
        strbuf_append_str(buf_, " class=\"");
        strbuf_append_str(buf_, classes);
        strbuf_append_char(buf_, '"');
    }
    
    if (attrs && attrs[0]) {
        strbuf_append_char(buf_, ' ');
        strbuf_append_str(buf_, attrs);
    }
    
    strbuf_append_str(buf_, ">");

    if (pretty_print_) {
        strbuf_append_char(buf_, '\n');
    }
}

void TextHtmlWriter::writeElement(const char* tag, const char* content, 
                                 const char* classes) {
    openTag(tag, classes);
    if (content) {
        writeText(content, 0);
    }
    closeTag(tag);
}

void TextHtmlWriter::writeAttribute(const char* name, const char* value) {
    if (!name || !in_tag_) return;
    
    strbuf_append_char(buf_, ' ');
    strbuf_append_str(buf_, name);
    
    if (value) {
        strbuf_append_str(buf_, "=\"");
        strbuf_append_str(buf_, value);
        strbuf_append_char(buf_, '"');
    }
}

void TextHtmlWriter::indent() {
    if (pretty_print_) {
        indent_level_++;
    }
}

void TextHtmlWriter::unindent() {
    if (pretty_print_ && indent_level_ > 0) {
        indent_level_--;
    }
}

void TextHtmlWriter::newline() {
    if (pretty_print_) {
        closeTagStart();
        strbuf_append_char(buf_, '\n');
    }
}

Item TextHtmlWriter::getResult() {
    closeTagStart();
    
    // Convert strbuf to Lambda String (allocate from pool)
    size_t len = buf_->length;
    
    // Remove any <p>lambda.nil</p> artifacts from the output
    // These can occur when empty strings end up in the document structure
    const char* search = "<p>lambda.nil</p>";
    size_t search_len = strlen(search);
    char* pos;
    while ((pos = strstr(buf_->str, search)) != nullptr) {
        // Shift the rest of the string left
        size_t remaining = buf_->length - (pos - buf_->str) - search_len;
        memmove(pos, pos + search_len, remaining + 1);  // +1 for null terminator
        buf_->length -= search_len;
    }
    len = buf_->length;
    
    String* str = (String*)pool_calloc(pool_, sizeof(String) + len + 1);
    str->len = len;
    str->ref_cnt = 0;
    memcpy(str->chars, buf_->str, len);
    str->chars[len] = '\0';
    
    Item result;
    result.item = s2it(str);
    return result;
}

const char* TextHtmlWriter::getHtml() {
    closeTagStart();
    return buf_->str;
}

void TextHtmlWriter::appendIndent() {
    for (int i = 0; i < indent_level_; i++) {
        strbuf_append_str(buf_, "  ");
    }
}

void TextHtmlWriter::escapeHtml(const char* text, size_t len) {
    // HTML entity escaping
    // Note: Quotes don't need escaping in text content, only in attributes
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        switch (c) {
            case '<':
                strbuf_append_str(buf_, "&lt;");
                break;
            case '>':
                strbuf_append_str(buf_, "&gt;");
                break;
            case '&':
                strbuf_append_str(buf_, "&amp;");
                break;
            // Note: " is not escaped to match LaTeX.js behavior
            // Quotes only need escaping inside attribute values
            default:
                strbuf_append_char(buf_, c);
                break;
        }
    }
}

void TextHtmlWriter::closeTagStart() {
    if (in_tag_) {
        strbuf_append_char(buf_, '>');
        if (pretty_print_) {
            strbuf_append_char(buf_, '\n');
        }
        in_tag_ = false;
    }
}

// =============================================================================
// NodeHtmlWriter Implementation
// =============================================================================

NodeHtmlWriter::NodeHtmlWriter(Input* input)
    : builder_(nullptr), pool_(nullptr), input_(input), pending_attributes_(nullptr) {
    if (!input) {
        log_error("NodeHtmlWriter: Input* is null");
        return;
    }
    pool_ = input->pool;
    builder_ = new MarkBuilder(input);
}

NodeHtmlWriter::~NodeHtmlWriter() {
    delete builder_;
}

void NodeHtmlWriter::writeText(const char* text, size_t len) {
    if (!text || !builder_) return;
    
    if (len == 0) {
        len = strlen(text);
    }
    
    // Allocate string from pool
    String* str = (String*)pool_calloc(pool_, sizeof(String) + len + 1);
    str->len = len;
    str->ref_cnt = 0;
    memcpy(str->chars, text, len);
    str->chars[len] = '\0';
    
    if (hasOpenElements()) {
        Item str_item;
        str_item.item = s2it(str);
        current().child(str_item);
    } else {
        // No open element, this shouldn't happen normally
        log_warn("NodeHtmlWriter: writeText called with no open element");
    }
}

void NodeHtmlWriter::writeRawHtml(const char* html) {
    // For node mode, we'd need to parse HTML back to elements
    // For now, just treat it as text
    log_warn("NodeHtmlWriter: writeRawHtml not fully implemented, treating as text");
    if (html) {
        writeText(html, 0);
    }
}

void NodeHtmlWriter::openTag(const char* tag, const char* classes,
                            const char* id, const char* style) {
    if (!tag || !builder_) return;
    
    ElementBuilder elem = builder_->element(tag);
    
    // Add attributes directly to element (ElementBuilder supports attr(key, value))
    if (classes && classes[0]) {
        String* class_str = (String*)pool_calloc(pool_, sizeof(String) + strlen(classes) + 1);
        class_str->len = strlen(classes);
        class_str->ref_cnt = 0;
        strcpy(class_str->chars, classes);
        
        Item class_item;
        class_item.item = s2it(class_str);
        elem.attr("class", class_item);
    }
    if (id && id[0]) {
        String* id_str = (String*)pool_calloc(pool_, sizeof(String) + strlen(id) + 1);
        id_str->len = strlen(id);
        id_str->ref_cnt = 0;
        strcpy(id_str->chars, id);
        
        Item id_item;
        id_item.item = s2it(id_str);
        elem.attr("id", id_item);
    }
    if (style && style[0]) {
        String* style_str = (String*)pool_calloc(pool_, sizeof(String) + strlen(style) + 1);
        style_str->len = strlen(style);
        style_str->ref_cnt = 0;
        strcpy(style_str->chars, style);
        
        Item style_item;
        style_item.item = s2it(style_str);
        elem.attr("style", style_item);
    }
    
    stack_.push_back(std::move(elem));
}

void NodeHtmlWriter::openTagRaw(const char* tag, const char* raw_attrs) {
    if (!tag || !builder_) return;
    
    ElementBuilder elem = builder_->element(tag);
    
    // Parse raw attributes string (e.g., "id=\"foo\" class=\"bar\"")
    // Simple parser for id="...", class="...", style="..." patterns
    if (raw_attrs && raw_attrs[0]) {
        std::string attrs_str(raw_attrs);
        size_t pos = 0;
        while (pos < attrs_str.size()) {
            // Skip whitespace
            while (pos < attrs_str.size() && isspace(attrs_str[pos])) pos++;
            if (pos >= attrs_str.size()) break;
            
            // Find attr name
            size_t name_start = pos;
            while (pos < attrs_str.size() && attrs_str[pos] != '=' && !isspace(attrs_str[pos])) pos++;
            std::string name = attrs_str.substr(name_start, pos - name_start);
            
            // Skip whitespace and =
            while (pos < attrs_str.size() && (isspace(attrs_str[pos]) || attrs_str[pos] == '=')) pos++;
            
            // Get value (handle quoted values)
            std::string value;
            if (pos < attrs_str.size() && attrs_str[pos] == '"') {
                pos++;  // Skip opening quote
                size_t value_start = pos;
                while (pos < attrs_str.size() && attrs_str[pos] != '"') pos++;
                value = attrs_str.substr(value_start, pos - value_start);
                if (pos < attrs_str.size()) pos++;  // Skip closing quote
            } else {
                size_t value_start = pos;
                while (pos < attrs_str.size() && !isspace(attrs_str[pos])) pos++;
                value = attrs_str.substr(value_start, pos - value_start);
            }
            
            // Set attribute
            if (!name.empty() && !value.empty()) {
                String* str = (String*)pool_calloc(pool_, sizeof(String) + value.size() + 1);
                str->len = value.size();
                str->ref_cnt = 0;
                strcpy(str->chars, value.c_str());
                
                Item item;
                item.item = s2it(str);
                elem.attr(name.c_str(), item);
            }
        }
    }
    
    stack_.push_back(std::move(elem));
}

void NodeHtmlWriter::closeTag(const char* tag) {
    if (!hasOpenElements()) {
        log_error("NodeHtmlWriter: closeTag called with no open elements");
        return;
    }
    
    ElementBuilder elem = std::move(stack_.back());
    stack_.pop_back();
    
    Item result = elem.final();
    
    if (hasOpenElements()) {
        // Add to parent
        current().child(result);
    } else {
        // This is the root, store it
        // We'll return it from getResult()
    }
}

void NodeHtmlWriter::writeSelfClosingTag(const char* tag, const char* classes,
                                        const char* attrs) {
    if (!tag) return;
    
    // For self-closing tags, open and immediately close
    openTag(tag, classes);
    closeTag(tag);
}

void NodeHtmlWriter::writeElement(const char* tag, const char* content,
                                 const char* classes) {
    openTag(tag, classes);
    if (content) {
        writeText(content, 0);
    }
    closeTag(tag);
}

void NodeHtmlWriter::writeAttribute(const char* name, const char* value) {
    if (!name || !hasOpenElements()) return;
    
    // Attributes should be added before children
    // This is tricky with MarkBuilder API - for now, log a warning
    log_warn("NodeHtmlWriter: writeAttribute should be called before children are added");
    
    // TODO: Implement attribute buffering system if needed
}

Item NodeHtmlWriter::getResult() {
    // If we have elements on the stack, finalize them
    while (hasOpenElements()) {
        ElementBuilder elem = std::move(stack_.back());
        stack_.pop_back();
        
        Item result = elem.final();
        
        if (hasOpenElements()) {
            current().child(result);
        } else {
            // This is the root
            return result;
        }
    }
    
    // No elements - return null
    Item null_item;
    null_item.item = ITEM_NULL;
    return null_item;
}

ElementBuilder& NodeHtmlWriter::current() {
    if (!hasOpenElements()) {
        log_error("NodeHtmlWriter: current() called with no open elements");
        static ElementBuilder dummy = builder_->element("error");
        return dummy;
    }
    return stack_.back();
}

} // namespace lambda
