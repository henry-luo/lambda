/**
 * @file input-html-context.cpp
 * @brief HTML parser context implementation
 */

#include "input-html-context.h"
#include "input.h"
#include "input-html-tree.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

extern "C" {
#include "../../lib/log.h"
}

// Head-only elements that belong in <head>
static const char* HEAD_ELEMENTS[] = {
    "title", "base", "link", "meta", "style", "script", "noscript", NULL
};

// Check if element belongs in <head>
static bool is_head_element(const char* tag_name) {
    for (int i = 0; HEAD_ELEMENTS[i]; i++) {
        if (strcasecmp(tag_name, HEAD_ELEMENTS[i]) == 0) {
            return true;
        }
    }
    return false;
}

HtmlParserContext* html_context_create(Input* input) {
    HtmlParserContext* ctx = (HtmlParserContext*)calloc(1, sizeof(HtmlParserContext));
    if (!ctx) {
        log_error("Failed to allocate HTML parser context");
        return NULL;
    }

    ctx->input = input;
    ctx->html_element = NULL;
    ctx->head_element = NULL;
    ctx->body_element = NULL;
    ctx->current_node = NULL;
    ctx->has_explicit_html = false;
    ctx->has_explicit_head = false;
    ctx->has_explicit_body = false;
    ctx->in_head = false;
    ctx->head_closed = false;
    ctx->in_body = false;

    return ctx;
}

void html_context_destroy(HtmlParserContext* ctx) {
    if (ctx) {
        free(ctx);
    }
}

Element* html_context_ensure_html(HtmlParserContext* ctx) {
    if (!ctx) return NULL;

    if (!ctx->html_element) {
        log_debug("Creating implicit <html> element");
        ctx->html_element = input_create_element(ctx->input, "html");
        ctx->has_explicit_html = false;
    }

    return ctx->html_element;
}

Element* html_context_ensure_head(HtmlParserContext* ctx) {
    if (!ctx) return NULL;

    // Ensure html exists first
    Element* html = html_context_ensure_html(ctx);
    if (!html) return NULL;

    if (!ctx->head_element) {
        log_debug("Creating implicit <head> element");
        ctx->head_element = input_create_element(ctx->input, "head");
        ctx->has_explicit_head = false;

        // Add head to html
        html_append_child(html, (Item){.element = ctx->head_element});
        html_set_content_length(html);
    }

    return ctx->head_element;
}

Element* html_context_ensure_body(HtmlParserContext* ctx) {
    if (!ctx) return NULL;

    // Ensure html exists first
    Element* html = html_context_ensure_html(ctx);
    if (!html) return NULL;

    // Close head if it hasn't been closed yet
    // (even if we never explicitly entered head state)
    if (!ctx->head_closed) {
        log_debug("Closing <head> section (implicitly, body starting)");
        ctx->head_closed = true;
        ctx->in_head = false;
    }

    if (!ctx->body_element) {
        log_debug("Creating implicit <body> element");
        ctx->body_element = input_create_element(ctx->input, "body");
        ctx->has_explicit_body = false;

        // Add body to html
        html_append_child(html, (Item){.element = ctx->body_element});
        html_set_content_length(html);

        ctx->in_body = true;
    }

    return ctx->body_element;
}

Element* html_context_get_insertion_point(HtmlParserContext* ctx, const char* tag_name) {
    if (!ctx) return NULL;

    // Special cases for document structure elements
    if (strcasecmp(tag_name, "html") == 0) {
        // <html> should be the root
        return NULL;  // Caller will handle as root
    }

    if (strcasecmp(tag_name, "head") == 0) {
        // <head> goes directly in <html>
        return html_context_ensure_html(ctx);
    }

    if (strcasecmp(tag_name, "body") == 0) {
        // <body> goes directly in <html>
        return html_context_ensure_html(ctx);
    }

    // Elements that belong in <head>
    if (is_head_element(tag_name)) {
        // If body is already open, this is a parse error but we'll handle it
        if (ctx->in_body) {
            log_warn("Head element <%s> found after body started, placing in body", tag_name);
            return html_context_ensure_body(ctx);
        }

        ctx->in_head = true;
        return html_context_ensure_head(ctx);
    }

    // Everything else goes in <body>
    return html_context_ensure_body(ctx);
}

void html_context_set_html(HtmlParserContext* ctx, Element* element) {
    if (!ctx) return;

    log_debug("Explicit <html> element found");
    ctx->html_element = element;
    ctx->has_explicit_html = true;
}

void html_context_set_head(HtmlParserContext* ctx, Element* element) {
    if (!ctx) return;

    log_debug("Explicit <head> element found");
    ctx->head_element = element;
    ctx->has_explicit_head = true;
    ctx->in_head = true;
}

void html_context_set_body(HtmlParserContext* ctx, Element* element) {
    if (!ctx) return;

    log_debug("Explicit <body> element found");
    ctx->body_element = element;
    ctx->has_explicit_body = true;
    ctx->in_body = true;

    // Close head when body starts
    if (ctx->in_head && !ctx->head_closed) {
        html_context_close_head(ctx);
    }
}

void html_context_close_head(HtmlParserContext* ctx) {
    if (!ctx) return;

    if (ctx->in_head && !ctx->head_closed) {
        log_debug("Closing <head> section");
        ctx->head_closed = true;
        ctx->in_head = false;
    }
}
