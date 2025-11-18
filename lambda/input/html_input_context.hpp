#pragma once

#include "input_context.hpp"
#include "input-html-context.h"

namespace lambda {

/**
 * Specialized InputContext for HTML parsing
 *
 * Extends InputContext with HTML5-specific parsing state management.
 * Wraps the existing HtmlParserContext C structure for backwards compatibility
 * while providing a modern C++ interface integrated with MarkBuilder.
 *
 * Usage:
 *   HtmlInputContext ctx(input, source, len);
 *   ctx.ensureHtml();
 *   Element* body = ctx.ensureBody();
 *   ctx.builder().putToElement(body, "class", ctx.builder().createString("main"));
 */
class HtmlInputContext : public InputContext {
public:
    /**
     * Create HTML input context with source tracking
     *
     * @param input Input stream for HTML content
     * @param source Source code buffer
     * @param len Source length in bytes
     */
    HtmlInputContext(Input* input, const char* source, size_t len)
        : InputContext(input, source, len)
        , html_ctx_(html_context_create(input))
    {
        if (!html_ctx_) {
            addError("Failed to create HTML parser context", 0, 0);
        }
    }

    /**
     * Create HTML input context without source tracking
     *
     * @param input Input stream for HTML content
     */
    explicit HtmlInputContext(Input* input)
        : InputContext(input)
        , html_ctx_(html_context_create(input))
    {
        if (!html_ctx_) {
            addError("Failed to create HTML parser context", 0, 0);
        }
    }

    /**
     * Destructor - cleans up HTML parser context
     */
    ~HtmlInputContext() {
        if (html_ctx_) {
            html_context_destroy(html_ctx_);
        }
    }

    // Non-copyable
    HtmlInputContext(const HtmlInputContext&) = delete;
    HtmlInputContext& operator=(const HtmlInputContext&) = delete;

    // HTML5-specific methods

    /**
     * Ensure <html> element exists and return it
     * Creates the element if not already present
     */
    Element* ensureHtml() {
        if (!html_ctx_) return nullptr;
        html_context_ensure_html(html_ctx_);
        return html_ctx_->html_element;
    }

    /**
     * Ensure <head> element exists and return it
     * Creates the element if not already present
     */
    Element* ensureHead() {
        if (!html_ctx_) return nullptr;
        html_context_ensure_head(html_ctx_);
        return html_ctx_->head_element;
    }

    /**
     * Ensure <body> element exists and return it
     * Creates the element if not already present
     */
    Element* ensureBody() {
        if (!html_ctx_) return nullptr;
        html_context_ensure_body(html_ctx_);
        return html_ctx_->body_element;
    }

    /**
     * Get the current insertion point for content
     *
     * @return Element where new content should be inserted
     */
    Element* getInsertionPoint() const {
        if (!html_ctx_) return nullptr;
        return html_context_get_insertion_point(html_ctx_, nullptr);
    }

    /**
     * Transition to a new HTML5 insertion mode
     *
     * @param mode New insertion mode
     */
    void transitionMode(HtmlInsertionMode mode) {
        if (html_ctx_) {
            html_context_set_mode(html_ctx_, mode);
        }
    }    /**
     * Get current insertion mode
     */
    HtmlInsertionMode getInsertionMode() const {
        return html_ctx_ ? html_ctx_->insertion_mode : HTML_MODE_INITIAL;
    }

    /**
     * Get the current node in the parse tree
     */
    Element* getCurrentNode() const {
        return html_ctx_ ? html_ctx_->current_node : nullptr;
    }

    /**
     * Set the current node
     */
    void setCurrentNode(Element* node) {
        if (html_ctx_) {
            html_ctx_->current_node = node;
        }
    }

    /**
     * Check if <html> element was explicitly provided in source
     */
    bool hasExplicitHtml() const {
        return html_ctx_ && html_ctx_->has_explicit_html;
    }

    /**
     * Check if <head> element was explicitly provided in source
     */
    bool hasExplicitHead() const {
        return html_ctx_ && html_ctx_->has_explicit_head;
    }

    /**
     * Check if <body> element was explicitly provided in source
     */
    bool hasExplicitBody() const {
        return html_ctx_ && html_ctx_->has_explicit_body;
    }

    /**
     * Check if parser is currently in <head> element
     */
    bool isInHead() const {
        return html_ctx_ && html_ctx_->in_head;
    }

    /**
     * Check if parser is currently in <body> element
     */
    bool isInBody() const {
        return html_ctx_ && html_ctx_->in_body;
    }

    /**
     * Check if <head> has been closed
     */
    bool isHeadClosed() const {
        return html_ctx_ && html_ctx_->head_closed;
    }

    /**
     * Get the open elements stack
     */
    HtmlElementStack* getOpenElements() const {
        return html_ctx_ ? html_ctx_->open_elements : nullptr;
    }

    /**
     * Get the active formatting elements list
     */
    HtmlFormattingList* getActiveFormatting() const {
        return html_ctx_ ? html_ctx_->active_formatting : nullptr;
    }

    /**
     * Access underlying HTML parser context (for advanced use)
     *
     * @return Pointer to C HtmlParserContext structure
     */
    HtmlParserContext* htmlContext() const {
        return html_ctx_;
    }

private:
    HtmlParserContext* html_ctx_;
};

} // namespace lambda
