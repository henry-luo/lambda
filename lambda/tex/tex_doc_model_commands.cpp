// tex_doc_model_commands.cpp - Special command builders
//
// This file contains builders for:
// - Image commands (\includegraphics)
// - Link commands (\href, \url)
// - Reference commands (\ref, \label)
// - Footnote/citation commands (\footnote, \cite)
// - Figure environment

#include "tex_document_model.hpp"
#include "tex_doc_model_internal.hpp"

namespace tex {

#ifndef DOC_MODEL_MINIMAL

// ============================================================================
// Image Commands
// ============================================================================

// Build image command (\includegraphics)
DocElement* build_image_command(const ElementReader& elem, Arena* arena,
                                 TexDocumentModel* doc) {
    (void)doc;
    DocElement* img = doc_alloc_element(arena, DocElemType::IMAGE);
    img->image.src = nullptr;
    img->image.width = 0.0f;
    img->image.height = 0.0f;
    img->image.alt = nullptr;
    
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            
            if (child_tag) {
                // Optional arguments contain width/height
                if (tag_eq(child_tag, "optional") || tag_eq(child_tag, "brack_group")) {
                    const char* opts = extract_text_content(child, arena);
                    parse_graphics_options(opts, &img->image.width, &img->image.height, arena);
                }
                // Required argument is the file path
                else if (tag_eq(child_tag, "curly_group") || tag_eq(child_tag, "arg") ||
                         tag_eq(child_tag, "path")) {
                    img->image.src = extract_text_content(child, arena);
                }
            }
        } else if (child.isString()) {
            // Could be the path directly
            if (!img->image.src) {
                img->image.src = child.cstring();
            }
        }
    }
    
    return img;
}

// ============================================================================
// Link Commands
// ============================================================================

// Build href command (\href{url}{text})
DocElement* build_href_command(const ElementReader& elem, Arena* arena,
                                TexDocumentModel* doc) {
    (void)doc;
    DocElement* link = doc_alloc_element(arena, DocElemType::LINK);
    link->link.href = nullptr;
    link->link.link_text = nullptr;
    
    int arg_index = 0;
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            
            if (child_tag && (tag_eq(child_tag, "curly_group") || tag_eq(child_tag, "arg"))) {
                const char* text = extract_text_content(child, arena);
                if (arg_index == 0) {
                    link->link.href = text;  // first arg is URL
                } else {
                    link->link.link_text = text;  // second arg is display text
                }
                arg_index++;
            }
        } else if (child.isString()) {
            // Direct string children (tree-sitter output format)
            const char* text = child.cstring();
            if (text && strlen(text) > 0 && text[0] != '\n') {
                // Copy the string
                size_t len = strlen(text);
                char* str = (char*)arena_alloc(arena, len + 1);
                memcpy(str, text, len + 1);
                
                if (arg_index == 0) {
                    link->link.href = str;  // first string is URL
                } else {
                    link->link.link_text = str;  // second string is display text
                }
                arg_index++;
            }
        }
    }
    
    return link;
}

// Build url command (\url{...})
DocElement* build_url_command(const ElementReader& elem, Arena* arena,
                               TexDocumentModel* doc) {
    (void)doc;
    DocElement* link = doc_alloc_element(arena, DocElemType::LINK);
    link->link.href = nullptr;
    link->link.link_text = nullptr;
    
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            
            if (child_tag && (tag_eq(child_tag, "curly_group") || tag_eq(child_tag, "arg"))) {
                const char* url = extract_text_content(child, arena);
                link->link.href = url;
                link->link.link_text = url;  // Display URL as link text
            }
        } else if (child.isString()) {
            const char* url = child.cstring();
            if (url) {
                link->link.href = url;
                link->link.link_text = url;
            }
        }
    }
    
    return link;
}

// ============================================================================
// Reference Commands
// ============================================================================

// Build label command (\label{...})
void process_label_command(const ElementReader& elem, Arena* arena,
                            TexDocumentModel* doc, DocElement* parent) {
    const char* label = nullptr;
    
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            
            if (child_tag && (tag_eq(child_tag, "curly_group") || tag_eq(child_tag, "arg"))) {
                label = extract_text_content(child, arena);
            }
        } else if (child.isString()) {
            label = child.cstring();
        }
    }
    
    if (label) {
        // Use current referable context if available
        const char* ref_id = doc->current_ref_id;
        const char* ref_text = doc->current_ref_text;
        
        // If parent is a heading, use its label (sec-N) and number
        if (parent && parent->type == DocElemType::HEADING) {
            ref_id = parent->heading.label;  // sec-N
            ref_text = parent->heading.number;  // e.g., "1" or "2.3"
        }
        
        log_debug("process_label_command: label='%s', ref_id='%s', ref_text='%s', parent=%s",
                  label, ref_id ? ref_id : "(null)", ref_text ? ref_text : "(null)",
                  parent ? doc_elem_type_name(parent->type) : "(null)");
        
        doc->add_label_with_id(label, ref_id, ref_text);
    }
}

// Build ref command (\ref{...})
DocElement* build_ref_command(const ElementReader& elem, Arena* arena,
                               TexDocumentModel* doc) {
    DocElement* ref = doc_alloc_element(arena, DocElemType::CROSS_REF);
    ref->ref.ref_label = nullptr;
    ref->ref.ref_text = nullptr;
    
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            
            if (child_tag && (tag_eq(child_tag, "curly_group") || tag_eq(child_tag, "arg"))) {
                ref->ref.ref_label = extract_text_content(child, arena);
            }
        } else if (child.isString()) {
            ref->ref.ref_label = child.cstring();
        }
    }
    
    // Add to pending refs for two-pass resolution
    // The reference will be resolved after the entire document is built
    if (ref->ref.ref_label) {
        doc->add_pending_ref(ref);
    }
    
    return ref;
}

// ============================================================================
// Footnote and Citation Commands
// ============================================================================

// Build footnote command (\footnote{...})
DocElement* build_footnote_command(const ElementReader& elem, Arena* arena,
                                    TexDocumentModel* doc) {
    DocElement* fn = doc_alloc_element(arena, DocElemType::FOOTNOTE);
    
    static int footnote_num = 0;
    footnote_num++;
    fn->footnote.footnote_number = footnote_num;
    
    // Process footnote content
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            
            if (child_tag && (tag_eq(child_tag, "curly_group") || tag_eq(child_tag, "arg"))) {
                DocElement* content = build_doc_element(child, arena, doc);
                if (content) {
                    doc_append_child(fn, content);
                }
            }
        }
    }
    
    return fn;
}

// Build cite command (\cite{...})
DocElement* build_cite_command(const ElementReader& elem, Arena* arena,
                                TexDocumentModel* doc) {
    DocElement* cite = doc_alloc_element(arena, DocElemType::CITATION);
    cite->citation.key = nullptr;
    cite->citation.cite_text = nullptr;
    
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            
            if (child_tag && (tag_eq(child_tag, "curly_group") || tag_eq(child_tag, "arg"))) {
                cite->citation.key = extract_text_content(child, arena);
            }
        } else if (child.isString()) {
            cite->citation.key = child.cstring();
        }
    }
    
    // Try to resolve citation
    if (cite->citation.key) {
        const char* resolved = doc->resolve_cite(cite->citation.key);
        if (resolved) {
            cite->citation.cite_text = resolved;
        } else {
            // Unresolved citation - format as [key]
            size_t len = strlen(cite->citation.key) + 3;
            char* text = (char*)arena_alloc(arena, len);
            snprintf(text, len, "[%s]", cite->citation.key);
            cite->citation.cite_text = text;
        }
    }
    
    return cite;
}

// ============================================================================
// Figure Environment
// ============================================================================

// Build figure environment
DocElement* build_figure_environment(const ElementReader& elem, Arena* arena,
                                      TexDocumentModel* doc) {
    DocElement* fig = doc_alloc_element(arena, DocElemType::FIGURE);
    fig->flags |= DocElement::FLAG_NUMBERED;
    
    // Track caption and label for this figure
    const char* caption_text = nullptr;
    const char* label = nullptr;
    
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (!child.isElement()) continue;
        
        ElementReader child_elem = child.asElement();
        const char* child_tag = child_elem.tagName();
        
        if (!child_tag) continue;
        
        // Handle caption
        if (tag_eq(child_tag, "caption")) {
            auto caption_iter = child_elem.children();
            ItemReader caption_child;
            while (caption_iter.next(&caption_child)) {
                if (caption_child.isElement()) {
                    ElementReader cc_elem = caption_child.asElement();
                    const char* cc_tag = cc_elem.tagName();
                    if (cc_tag && (tag_eq(cc_tag, "curly_group") || tag_eq(cc_tag, "arg"))) {
                        caption_text = extract_text_content(caption_child, arena);
                    }
                }
            }
        }
        // Handle label
        else if (tag_eq(child_tag, "label")) {
            auto label_iter = child_elem.children();
            ItemReader label_child;
            while (label_iter.next(&label_child)) {
                if (label_child.isElement()) {
                    ElementReader lc_elem = label_child.asElement();
                    const char* lc_tag = lc_elem.tagName();
                    if (lc_tag && (tag_eq(lc_tag, "curly_group") || tag_eq(lc_tag, "arg"))) {
                        label = extract_text_content(label_child, arena);
                    }
                } else if (label_child.isString()) {
                    label = label_child.cstring();
                }
            }
        }
        // Handle centering (skip)
        else if (tag_eq(child_tag, "centering")) {
            fig->flags |= DocElement::FLAG_CENTERED;
        }
        // Handle includegraphics
        else if (tag_eq(child_tag, "includegraphics")) {
            DocElement* img = build_image_command(child_elem, arena, doc);
            if (img) {
                doc_append_child(fig, img);
            }
        }
        // Other content
        else {
            DocElement* content = build_doc_element(child, arena, doc);
            if (content) {
                doc_append_child(fig, content);
            }
        }
    }
    
    // Add caption element if present
    if (caption_text) {
        // Create figcaption structure
        DocElement* caption_elem = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
        caption_elem->text.style = DocTextStyle::plain();
        
        // Format caption with figure number
        // Note: In a full implementation, we'd track figure numbering per chapter
        static int figure_num = 0;
        figure_num++;
        
        size_t formatted_len = strlen(caption_text) + 32;
        char* formatted = (char*)arena_alloc(arena, formatted_len);
        snprintf(formatted, formatted_len, "Figure %d: %s", figure_num, caption_text);
        caption_elem->text.text = formatted;
        caption_elem->text.text_len = strlen(formatted);
        
        doc_append_child(fig, caption_elem);
        
        // Register label if present
        if (label) {
            char num_str[16];
            snprintf(num_str, sizeof(num_str), "%d", figure_num);
            doc->add_label(label, num_str, -1);
        }
    }
    
    return fig;
}

#endif // DOC_MODEL_MINIMAL

} // namespace tex
