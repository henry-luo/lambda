#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"
#include "layout_flex_multipass.hpp"
#include "layout_grid_multipass.hpp"
#include "layout_positioned.hpp"
#include "intrinsic_sizing.hpp"
#include "grid.hpp"

#include "../lib/log.h"
#include "../lambda/input/css/selector_matcher.hpp"
#include <chrono>
using namespace std::chrono;

// External timing accumulators from layout.cpp
extern double g_table_layout_time;
extern double g_flex_layout_time;
extern double g_grid_layout_time;
extern double g_block_layout_time;
extern int64_t g_block_layout_count;

View* layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);
// void layout_flex_nodes(LayoutContext* lycon, lxb_dom_node_t *first_child);  // Removed: lexbor dependency
void resolve_inline_default(LayoutContext* lycon, ViewSpan* span);
void dom_node_resolve_style(DomNode* node, LayoutContext* lycon);
void layout_table_content(LayoutContext* lycon, DomNode* elmt, DisplayValue display);
void layout_flex_content(LayoutContext* lycon, ViewBlock* block);
void layout_abs_block(LayoutContext* lycon, DomNode *elmt, ViewBlock* block, BlockContext *pa_block, Linebox *pa_line);

// Counter system functions (from layout_counters.cpp)
typedef struct CounterContext CounterContext;
int counter_format(CounterContext* ctx, const char* name, uint32_t style,
                  char* buffer, size_t buffer_size);

// ============================================================================
// Pseudo-element (::before/::after) Layout Support
// ============================================================================

/**
 * Create a pseudo-element DomElement with a DomText child for the content
 *
 * @param lycon Layout context
 * @param parent The parent element
 * @param content The content string for the pseudo-element
 * @param is_before true for ::before, false for ::after
 * @return The created DomElement or NULL on failure
 */
static DomElement* create_pseudo_element(LayoutContext* lycon, DomElement* parent,
                                          const char* content, bool is_before,
                                          FontProp* parent_font) {
    // Allow empty content - pseudo-elements with display:block and clear:both still need to be created
    if (!lycon || !parent) return nullptr;

    Pool* pool = lycon->doc->view_tree->pool;
    if (!pool) return nullptr;

    // Create the pseudo DomElement
    // Per CSS spec: pseudo-element is child of defining element,
    // text node is child of pseudo-element
    DomElement* pseudo_elem = (DomElement*)pool_calloc(pool, sizeof(DomElement));
    if (!pseudo_elem) return nullptr;

    // Initialize as element node
    pseudo_elem->node_type = DOM_NODE_ELEMENT;
    pseudo_elem->tag_name = is_before ? "::before" : "::after";
    pseudo_elem->doc = parent->doc;
    // Pseudo-element is child of defining element
    pseudo_elem->parent = parent;
    pseudo_elem->first_child = nullptr;
    pseudo_elem->next_sibling = nullptr;
    pseudo_elem->prev_sibling = nullptr;

    // Copy inherited styles from parent ViewBlock (has fully resolved font-family)
    // Use parent_font param which comes from ViewBlock, not from DomElement
    pseudo_elem->font = parent_font ? parent_font : parent->font;

    // Log font inheritance for debugging Font Awesome
    if (content && strstr(content, "\\f1")) {
        if (parent->font) {
            if (parent->font->family) {
                log_debug("[PSEUDO FONT] ::before inherits font '%s' (size %.1f) from parent <%s>",
                          parent->font->family, parent->font->font_size,
                          parent->tag_name ? parent->tag_name : "?");
            } else {
                log_debug("[PSEUDO FONT] Parent <%s> has font but NULL family",
                          parent->tag_name ? parent->tag_name : "?");
            }
        } else {
            log_debug("[PSEUDO FONT] Parent <%s> has NULL font pointer - need to resolve styles first!",
                      parent->tag_name ? parent->tag_name : "?");
        }
    }

    // DON'T copy bound - pseudo-element should have its own BoundaryProp
    // pseudo_elem->bound = parent->bound;  // BUG: causes shared BackgroundProp
    pseudo_elem->bound = nullptr;  // Will be allocated when CSS properties are applied
    pseudo_elem->in_line = parent->in_line;

    // Get display value from pseudo-element's styles (before_styles or after_styles)
    // Default to inline for pseudo-elements per CSS spec
    pseudo_elem->display.outer = CSS_VALUE_INLINE;
    pseudo_elem->display.inner = CSS_VALUE_FLOW;

    // Check for explicit display in pseudo-element styles
    StyleTree* pseudo_styles = is_before ? parent->before_styles : parent->after_styles;
    if (pseudo_styles && pseudo_styles->tree) {
        AvlNode* display_node = avl_tree_search(pseudo_styles->tree, CSS_PROPERTY_DISPLAY);
        if (display_node) {
            StyleNode* style_node = (StyleNode*)display_node->declaration;
            if (style_node && style_node->winning_decl && style_node->winning_decl->value) {
                CssValue* val = style_node->winning_decl->value;
                if (val->type == CSS_VALUE_TYPE_KEYWORD) {
                    if (val->data.keyword == CSS_VALUE_BLOCK) {
                        pseudo_elem->display.outer = CSS_VALUE_BLOCK;
                        log_debug("[PSEUDO] Setting display: block for ::%s", is_before ? "before" : "after");
                    } else if (val->data.keyword == CSS_VALUE_INLINE_BLOCK) {
                        pseudo_elem->display.outer = CSS_VALUE_INLINE_BLOCK;
                    }
                }
            }
        }

        // Copy pseudo-element styles to the pseudo element itself
        pseudo_elem->specified_style = pseudo_styles;
    }

    // Create the text child only if there's content
    // Empty content pseudo-elements still participate in layout (e.g., clearfix)
    if (content && *content) {
        log_info("[PSEUDO] Creating text node for pseudo-element, content_len=%zu, first_byte=0x%02x",
            strlen(content), (unsigned char)*content);
        DomText* text_node = (DomText*)pool_calloc(pool, sizeof(DomText));
        if (text_node) {
            // Initialize as text node
            text_node->node_type = DOM_NODE_TEXT;
            // Text node is child of pseudo-element
            text_node->parent = pseudo_elem;
            text_node->next_sibling = nullptr;
            text_node->prev_sibling = nullptr;

            // Copy the content string
            size_t content_len = strlen(content);
            char* text_content = (char*)pool_calloc(pool, content_len + 1);
            if (text_content) {
                memcpy(text_content, content, content_len);
                text_content[content_len] = '\0';
                log_info("[PSEUDO] Text node created with content_len=%zu, bytes=[%02x %02x %02x]",
                    content_len,
                    content_len > 0 ? (unsigned char)text_content[0] : 0,
                    content_len > 1 ? (unsigned char)text_content[1] : 0,
                    content_len > 2 ? (unsigned char)text_content[2] : 0);
            }
            text_node->text = text_content;
            text_node->length = content_len;
            text_node->native_string = nullptr;  // Not backed by Lambda String
            text_node->content_type = DOM_TEXT_STRING;

            // Link text node as child of pseudo element
            pseudo_elem->first_child = text_node;
        }
    } else {
        log_info("[PSEUDO] NOT creating text node: content=%p, first_byte=%s",
            (void*)content, content ? ((*content) ? "nonzero" : "ZERO") : "NULL");
    }

    log_debug("[PSEUDO] Created ::%s element for <%s> with content \"%s\", display.outer=%d",
              is_before ? "before" : "after",
              parent->tag_name ? parent->tag_name : "unknown",
              content ? content : "(empty)",
              pseudo_elem->display.outer);

    return pseudo_elem;
}

/**
 * Allocate PseudoContentProp and create pseudo-elements if needed
 *
 * On first layout: creates pseudo-elements and inserts them into DOM tree
 * On reflow: reuses existing pseudo-elements (already in DOM tree)
 *
 * @param lycon Layout context
 * @param block The block element to check
 * @return PseudoContentProp pointer or NULL if no pseudo content
 */
PseudoContentProp* alloc_pseudo_content_prop(LayoutContext* lycon, ViewBlock* block) {
    if (!block || !block->is_element()) return nullptr;

    DomElement* elem = (DomElement*)block;

    // Check if pseudo-elements already exist (reflow case)
    if (block->pseudo) {
        log_debug("[PSEUDO] Reusing existing pseudo-elements for <%s>",
                  elem->tag_name ? elem->tag_name : "unknown");
        return block->pseudo;
    }

    // Check if element has ::before or ::after content
    bool has_before = dom_element_has_before_content(elem);
    bool has_after = dom_element_has_after_content(elem);

    log_debug("[PSEUDO] Checking <%s>: has_before=%d, has_after=%d, before_styles=%p",
              elem->tag_name ? elem->tag_name : "?", has_before, has_after, (void*)elem->before_styles);

    if (!has_before && !has_after) return nullptr;

    // Allocate PseudoContentProp
    PseudoContentProp* pseudo = (PseudoContentProp*)alloc_prop(lycon, sizeof(PseudoContentProp));
    if (!pseudo) return nullptr;

    // Initialize
    memset(pseudo, 0, sizeof(PseudoContentProp));

    // Create ::before pseudo-element if needed
    // Note: Even empty content "" creates a pseudo-element for layout purposes (e.g., clearfix)
    if (has_before) {
        log_info("[PSEUDO] Getting before content for <%s>", elem->tag_name ? elem->tag_name : "?");
        const char* before_content = nullptr;
        if (lycon->counter_context) {
            log_info("[PSEUDO] Calling get_pseudo_element_content_with_counters");
            before_content = dom_element_get_pseudo_element_content_with_counters(
                elem, PSEUDO_ELEMENT_BEFORE, lycon->counter_context, lycon->doc->arena);
            log_info("[PSEUDO] Returned from with_counters: %p, len=%zu, bytes=[%02x %02x %02x]",
                (void*)before_content,
                before_content ? strlen(before_content) : 0,
                before_content && strlen(before_content) > 0 ? (unsigned char)before_content[0] : 0,
                before_content && strlen(before_content) > 1 ? (unsigned char)before_content[1] : 0,
                before_content && strlen(before_content) > 2 ? (unsigned char)before_content[2] : 0);
        }
        if (!before_content) {
            log_info("[PSEUDO] Calling dom_element_get_pseudo_element_content");
            before_content = dom_element_get_pseudo_element_content(elem, PSEUDO_ELEMENT_BEFORE);
            log_info("[PSEUDO] Returned: %p", (void*)before_content);
        }

        // Debug: log what font we're passing to pseudo-element
        log_debug("[PSEUDO ALLOC] block->font=%p, elem->font=%p", (void*)block->font, (void*)elem->font);
        if (block->font && block->font->family) {
            log_debug("[PSEUDO ALLOC] Passing font '%s' (size %.1f) from ViewBlock",
                     block->font->family, block->font->font_size);
        } else if (block->font) {
            log_debug("[PSEUDO ALLOC] block->font exists but has no family");
        } else {
            log_debug("[PSEUDO ALLOC] block->font is NULL");
        }

        // Create pseudo-element even for empty content if display/clear properties are set
        // Pass block->font (from ViewBlock) for accurate font-family inheritance
        pseudo->before = create_pseudo_element(lycon, elem, before_content ? before_content : "", true, block->font);
        log_debug("[PSEUDO] Created ::before for <%s> with content='%s'",
                  elem->tag_name ? elem->tag_name : "?", before_content ? before_content : "(empty)");
    }

    // Create ::after pseudo-element if needed
    // Note: Even empty content "" creates a pseudo-element for layout purposes
    if (has_after) {
        const char* after_content = nullptr;
        if (lycon->counter_context) {
            after_content = dom_element_get_pseudo_element_content_with_counters(
                elem, PSEUDO_ELEMENT_AFTER, lycon->counter_context, lycon->doc->arena);
        }
        if (!after_content) {
            after_content = dom_element_get_pseudo_element_content(elem, PSEUDO_ELEMENT_AFTER);
        }
        // Pass block->font (from ViewBlock) for accurate font-family inheritance
        pseudo->after = create_pseudo_element(lycon, elem, after_content ? after_content : "", false, block->font);
        log_debug("[PSEUDO] Created ::after for <%s> with content='%s'",
                  elem->tag_name ? elem->tag_name : "?", after_content ? after_content : "(empty)");
    }

    return pseudo;
}

/**
 * Layout a pseudo-element using the existing inline layout infrastructure
 *
 * Per CSS spec: pseudo-element is child of defining element, with display: inline.
 * We use layout_inline to handle the pseudo-element which will recursively
 * lay out its text child.
 *
 * @param lycon Layout context
 * @param pseudo_elem The pseudo-element DomElement (created by create_pseudo_element)
 */
static void layout_pseudo_element(LayoutContext* lycon, DomElement* pseudo_elem) {
    if (!pseudo_elem) return;

    log_debug("[PSEUDO] Laying out %s content", pseudo_elem->tag_name);

    // Resolve CSS styles for the pseudo-element BEFORE layout
    // This ensures font-family and other properties from CSS are applied
    dom_node_resolve_style(pseudo_elem, lycon);

    // Layout the pseudo-element as inline (it will lay out its text child)
    layout_inline(lycon, pseudo_elem, pseudo_elem->display);
}

// ============================================================================
// End of Pseudo-element Layout Support
// ============================================================================

void finalize_block_flow(LayoutContext* lycon, ViewBlock* block, CssEnum display) {
    // finalize the block size
    float flow_width, flow_height;
    if (block->bound) {
        // max_width already includes padding.left and border.left
        block->content_width = lycon->block.max_width + block->bound->padding.right;
        // advance_y already includes padding.top and border.top
        block->content_height = lycon->block.advance_y + block->bound->padding.bottom;
        flow_width = block->content_width +
            (block->bound->border ? block->bound->border->width.right : 0);
        flow_height = block->content_height +
            (block->bound->border ? block->bound->border->width.bottom : 0);
    } else {
        flow_width = block->content_width = lycon->block.max_width;
        flow_height = block->content_height = lycon->block.advance_y;
    }

    log_debug("finalizing block, display=%d, given wd:%f", display, lycon->block.given_width);
    if (display == CSS_VALUE_INLINE_BLOCK && lycon->block.given_width < 0) {
        block->width = min(flow_width, block->width);
        log_debug("inline-block final width set to: %f", block->width);
    }

    // handle horizontal overflow
    if (flow_width > block->width) { // hz overflow
        if (!block->scroller) {
            block->scroller = alloc_scroll_prop(lycon);
        }
        block->scroller->has_hz_overflow = true;
        if (block->scroller->overflow_x == CSS_VALUE_VISIBLE) {
            if (lycon->block.parent) lycon->block.parent->max_width = max(lycon->block.parent->max_width, flow_width);
        }
        else if (block->scroller->overflow_x == CSS_VALUE_SCROLL ||
            block->scroller->overflow_x == CSS_VALUE_AUTO) {
            block->scroller->has_hz_scroll = true;
        }
        if (block->scroller->has_hz_scroll ||
            block->scroller->overflow_x == CSS_VALUE_CLIP ||
            block->scroller->overflow_x == CSS_VALUE_HIDDEN) {
            block->scroller->has_clip = true;
            block->scroller->clip.left = 0;  block->scroller->clip.top = 0;
            block->scroller->clip.right = block->width;  block->scroller->clip.bottom = block->height;
        }
    }

    // handle vertical overflow and determine block->height
    if (lycon->block.given_height >= 0) { // got specified height
        // no change to block->height
        if (flow_height > block->height) { // vt overflow
            if (!block->scroller) {
                block->scroller = alloc_scroll_prop(lycon);
            }
            block->scroller->has_vt_overflow = true;
            if (block->scroller->overflow_y == CSS_VALUE_VISIBLE) {
                if (lycon->block.parent) lycon->block.parent->max_height = max(lycon->block.parent->max_height, block->y + flow_height);
            }
            else if (block->scroller->overflow_y == CSS_VALUE_SCROLL || block->scroller->overflow_y == CSS_VALUE_AUTO) {
                block->scroller->has_vt_scroll = true;
            }
            if (block->scroller->has_hz_scroll ||
                block->scroller->overflow_y == CSS_VALUE_CLIP ||
                block->scroller->overflow_y == CSS_VALUE_HIDDEN) {
                block->scroller->has_clip = true;
                block->scroller->clip.left = 0;  block->scroller->clip.top = 0;
                block->scroller->clip.right = block->width;  block->scroller->clip.bottom = block->height;
            }
        }
        log_debug("block: given_height: %f, height: %f, flow height: %f", lycon->block.given_height, block->height, flow_height);
    }
    else {
        // For non-flex containers, set height to flow height
        // For flex containers, the height is already set by flex algorithm
        bool has_embed = block->embed != nullptr;
        bool has_flex = has_embed && block->embed->flex != nullptr;
        log_debug("finalize block flow: has_embed=%d, has_flex=%d, block=%s",
                  has_embed, has_flex, block->node_name());
        if (!has_flex) {
            log_debug("finalize block flow, set block height to flow height: %f", flow_height);
            block->height = flow_height;
        } else {
            log_debug("finalize block flow: flex container, keeping height: %f (flow=%f)",
                      block->height, flow_height);
        }
    }
    // Update scroller clip if height changed and scroller has clipping enabled
    // This ensures the clip region is correct after auto-height is calculated
    if (block->scroller && block->scroller->has_clip) {
        block->scroller->clip.left = 0;
        block->scroller->clip.top = 0;
        block->scroller->clip.right = block->width;
        block->scroller->clip.bottom = block->height;
    }
    // Also enable clipping when overflow is hidden/clip, even without actual overflow
    // This is needed for border-radius clipping to work correctly
    if (block->scroller && !block->scroller->has_clip) {
        if (block->scroller->overflow_x == CSS_VALUE_HIDDEN ||
            block->scroller->overflow_x == CSS_VALUE_CLIP ||
            block->scroller->overflow_y == CSS_VALUE_HIDDEN ||
            block->scroller->overflow_y == CSS_VALUE_CLIP) {
            block->scroller->has_clip = true;
            block->scroller->clip.left = 0;
            block->scroller->clip.top = 0;
            block->scroller->clip.right = block->width;
            block->scroller->clip.bottom = block->height;
            log_debug("finalize: enabling clip for overflow:hidden, wd:%f, hg:%f", block->width, block->height);
        }
    }
    log_debug("finalized block wd:%f, hg:%f", block->width, block->height);
}

void layout_iframe(LayoutContext* lycon, ViewBlock* block, DisplayValue display) {
    DomDocument* doc = NULL;
    log_debug("layout iframe");
    if (!(block->embed && block->embed->doc)) {
        // load iframe document
        const char *value = block->get_attribute("src");
        if (value) {
            size_t value_len = strlen(value);
            StrBuf* src = strbuf_new_cap(value_len);
            strbuf_append_str_n(src, value, value_len);
            log_debug("load iframe doc src: %s", src->str);
            doc = load_html_doc(lycon->ui_context->document->url, src->str,
                lycon->ui_context->window_width, lycon->ui_context->window_height);
            strbuf_free(src);
            if (!doc) {
                log_debug("failed to load iframe document");
                // todo: use a placeholder
            } else {
                if (!(block->embed)) block->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp));
                block->embed->doc = doc; // assign loaded document to embed property
                if (doc->html_root) {
                    layout_html_doc(lycon->ui_context, doc, false);
                }
            }
        } else {
            log_debug("iframe has no src attribute");
        }
    }
    else {
        doc = block->embed->doc;
    }
    if (doc && doc->view_tree && doc->view_tree->root) {
        ViewBlock* root = (ViewBlock*)doc->view_tree->root;
        lycon->block.max_width = root->content_width;
        lycon->block.advance_y = root->content_height;
    }
    finalize_block_flow(lycon, block, display.outer);
}

/**
 * Insert pseudo-element into DOM tree at appropriate position
 * ::before is inserted as first child, ::after as last child
 */
void insert_pseudo_into_dom(DomElement* parent, DomElement* pseudo, bool is_before) {
    if (!parent || !pseudo) return;

    if (is_before) {
        // Insert as first child
        DomNode* old_first = parent->first_child;
        pseudo->next_sibling = old_first;
        pseudo->prev_sibling = nullptr;
        if (old_first) {
            old_first->prev_sibling = pseudo;
        }
        parent->first_child = pseudo;
    } else {
        // Insert as last child
        if (!parent->first_child) {
            parent->first_child = pseudo;
            pseudo->prev_sibling = nullptr;
            pseudo->next_sibling = nullptr;
        } else {
            // Find last child
            DomNode* last = parent->first_child;
            while (last->next_sibling) {
                last = last->next_sibling;
            }
            last->next_sibling = pseudo;
            pseudo->prev_sibling = last;
            pseudo->next_sibling = nullptr;
        }
    }
}

/**
 * Generate pseudo-element content based on content property
 * CSS 2.1 Section 12.2
 */
void generate_pseudo_element_content(LayoutContext* lycon, ViewBlock* block, bool is_before) {
    if (!block || !block->pseudo) return;

    log_debug("[Pseudo-Generate] Called for %s, block=%p, pseudo=%p",
              is_before ? "::before" : "::after", (void*)block, (void*)block->pseudo);

    PseudoContentProp* pseudo = block->pseudo;

    // Check if already generated
    if ((is_before && pseudo->before_generated) || (!is_before && pseudo->after_generated)) {
        return;
    }

    // Get content string and type
    char* content = is_before ? pseudo->before_content : pseudo->after_content;
    uint8_t content_type = is_before ? pseudo->before_content_type : pseudo->after_content_type;

    // Skip if no content or content is none
    if (content_type == CONTENT_TYPE_NONE || !content) {
        return;
    }

    log_debug("[Pseudo-Element] Generating %s content, type=%d",
              is_before ? "::before" : "::after", content_type);

    // Cast block to DomElement to access DOM fields
    DomElement* parent_elem = (DomElement*)block;

    // Create pseudo-element DomElement
    DomElement* pseudo_elem = dom_element_create(parent_elem->doc,
                                                  is_before ? "::before" : "::after",
                                                  nullptr);
    if (!pseudo_elem) {
        log_error("[Pseudo-Element] Failed to create DomElement");
        return;
    }

    // Set pseudo-element properties - tag_name already set by dom_element_create
    pseudo_elem->parent = parent_elem;

    // Copy inherited properties from parent (CSS inheritance)
    // Font properties are inherited by pseudo-elements
    pseudo_elem->font = parent_elem->font;
    pseudo_elem->in_line = parent_elem->in_line;

    // Log font inheritance for debugging
    if (parent_elem->font && parent_elem->font->family) {
        log_debug("[Pseudo-Element] Inherited font-family '%s' from parent <%s>",
                  parent_elem->font->family, parent_elem->tag_name ? parent_elem->tag_name : "?");
    } else {
        log_debug("[Pseudo-Element] Parent <%s> font is NULL or has no family name",
                  parent_elem->tag_name ? parent_elem->tag_name : "?");
    }

    // Copy pseudo-element-specific styles (::before or ::after styles)
    pseudo_elem->specified_style = is_before ? parent_elem->before_styles : parent_elem->after_styles;

    // Handle different content types
    switch (content_type) {
        case CONTENT_TYPE_STRING: {
            // Create Lambda String for the content
            size_t content_len = strlen(content);
            String* text_string = (String*)arena_alloc(parent_elem->doc->arena,
                                                        sizeof(String) + content_len + 1);
            if (text_string) {
                text_string->ref_cnt = 1;
                text_string->len = content_len;
                memcpy(text_string->chars, content, content_len);
                text_string->chars[content_len] = '\0';

                // Create text node with Lambda String
                DomText* text_node = dom_text_create(text_string, pseudo_elem);
                if (text_node) {
                    pseudo_elem->first_child = text_node;
                    log_debug("[Pseudo-Element] Created text content: \"%s\"", content);
                }
            }
            break;
        }

        case CONTENT_TYPE_COUNTER:
        case CONTENT_TYPE_COUNTERS:
            // TODO: Implement counter resolution (Phase 2)
            log_debug("[Pseudo-Element] Counter content not yet implemented");
            break;

        case CONTENT_TYPE_ATTR:
            // TODO: Implement attribute reading (Phase 5)
            log_debug("[Pseudo-Element] attr() content not yet implemented");
            break;

        case CONTENT_TYPE_URI:
            // TODO: Implement image content (Phase 5)
            log_debug("[Pseudo-Element] url() content not yet implemented");
            break;

        default:
            log_debug("[Pseudo-Element] Unknown content type: %d", content_type);
            break;
    }

    // Insert pseudo-element into DOM
    insert_pseudo_into_dom(parent_elem, pseudo_elem, is_before);

    // Store pseudo-element reference
    if (is_before) {
        pseudo->before = pseudo_elem;
        pseudo->before_generated = true;
    } else {
        pseudo->after = pseudo_elem;
        pseudo->after_generated = true;
    }

    log_debug("[Pseudo-Element] %s pseudo-element inserted", is_before ? "::before" : "::after");
}

// Forward declaration
void layout_block(LayoutContext* lycon, DomNode *elmt, DisplayValue display);

/**
 * Check if an element is a float by examining its specified style
 * This is called before the element has a view, so we check the CSS properties directly
 */
static CssEnum get_element_float_value(DomElement* elem) {
    if (!elem) return CSS_VALUE_NONE;

    // First check if position is already resolved
    if (elem->position) {
        return elem->position->float_prop;
    }

    // Check float property from CSS style tree
    if (elem->specified_style && elem->specified_style->tree) {
        AvlNode* float_node = avl_tree_search(elem->specified_style->tree, CSS_PROPERTY_FLOAT);
        if (float_node) {
            StyleNode* style_node = (StyleNode*)float_node->declaration;
            if (style_node && style_node->winning_decl && style_node->winning_decl->value) {
                CssValue* val = style_node->winning_decl->value;
                if (val->type == CSS_VALUE_TYPE_KEYWORD) {
                    return val->data.keyword;
                }
            }
        }
    }
    return CSS_VALUE_NONE;
}

/**
 * Pre-scan inline siblings for floats and layout them first
 * This ensures floats are positioned before inline content that follows them in DOM order
 *
 * Per CSS 2.2: floats affect the current line, so they must be positioned before
 * we lay out inline content that shares the same line.
 *
 * IMPORTANT: This only applies when there's inline content mixed with floats.
 * If all siblings are block-level, floats appear at their encounter point.
 *
 * @param lycon Layout context
 * @param first_child First child to scan from
 * @param parent_block The parent block establishing the formatting context
 */
/**
 * Pre-scan and layout ALL floats in the content.
 *
 * CSS floats are "out of flow" - they're positioned and then content flows around them.
 * This means floats affect content that comes BEFORE them in DOM order if that content
 * is on the same line.
 *
 * For simplicity, we pre-lay ALL floats at Y=0, then during inline layout, content
 * flows around them via adjust_line_for_floats(). If this causes issues with floats
 * that should appear lower (due to preceding block-level content), we'll need a more
 * sophisticated approach.
 *
 * This handles cases like:
 *   <span>Filler Text</span><float/>  -> float at (0,0), text at (96,0) ✓
 *   <span>Long text...</span><float/> -> float at (0,0) - WRONG, should be (0, line2)
 */
static void prescan_and_layout_floats(LayoutContext* lycon, DomNode* first_child, ViewBlock* parent_block) {
    if (!first_child) return;

    // Check if there are any floats in the content
    // Also check if the content BEFORE the first float is short enough to share a line
    bool has_floats = false;
    bool has_inline_content = false;
    float preceding_content_width = 0.0f;  // Estimated width of content before first float
    // Note: parent_block->content_width might be 0 at this point (set during finalization)
    // Use lycon->block.content_width which is set during block setup
    float container_width = lycon->block.content_width;
    DomNode* first_float_node = nullptr;

    for (DomNode* child = first_child; child; child = child->next_sibling) {
        if (!child->is_element()) {
            // Text nodes - estimate width (rough approximation)
            if (child->is_text()) {
                DomText* text = child->as_text();
                if (text && text->text && !first_float_node) {
                    // Count non-whitespace characters and estimate width
                    const char* p = text->text;
                    int char_count = 0;
                    while (*p) {
                        if (!isspace(*p)) char_count++;
                        p++;
                    }
                    // Rough estimate: 8px per character
                    preceding_content_width += char_count * 8.0f;
                    if (char_count > 0) has_inline_content = true;
                }
            }
            continue;
        }

        DomElement* elem = child->as_element();
        if (elem->float_prelaid) continue;

        // Check if element is a float
        CssEnum float_value = get_element_float_value(elem);
        if (float_value == CSS_VALUE_LEFT || float_value == CSS_VALUE_RIGHT) {
            has_floats = true;
            if (!first_float_node) first_float_node = child;
            continue;
        }

        // Check if element is inline or block content before the first float
        if (!first_float_node) {
            DisplayValue display = resolve_display_value(child);
            if (display.outer == CSS_VALUE_INLINE || display.outer == CSS_VALUE_INLINE_BLOCK) {
                has_inline_content = true;

                // Count text content inside this inline element for width estimation
                for (DomNode* text_node = elem->first_child; text_node; text_node = text_node->next_sibling) {
                    if (text_node->is_text()) {
                        DomText* text = text_node->as_text();
                        if (text && text->text) {
                            const char* p = text->text;
                            int char_count = 0;
                            while (*p) {
                                if (!isspace(*p)) char_count++;
                                p++;
                            }
                            preceding_content_width += char_count * 8.0f;
                        }
                    }
                }
            } else if (display.outer == CSS_VALUE_BLOCK) {
                // Block element before the first float - don't pre-scan
                // The float should appear after this block in normal flow
                log_debug("[FLOAT PRE-SCAN] Block element before float, skipping pre-scan");
                return;
            }
        }
    }

    // No floats to pre-scan
    if (!has_floats) {
        log_debug("[FLOAT PRE-SCAN] No floats found, skipping pre-scan");
        return;
    }

    log_debug("[FLOAT PRE-SCAN] has_inline_content=%d, container_width=%.1f, preceding_content_width=%.1f",
              has_inline_content, container_width, preceding_content_width);

    // Check if preceding content is too wide to share a line with the float
    // If so, don't pre-scan - let the float appear at its encounter point
    if (has_inline_content && container_width > 0) {
        // Rough estimate: assume float is ~100px wide (common case)
        // This heuristic works for:
        // - floats-029: container=1200, content=67, float=96 → 67+96=163 < 1200 → pre-scan
        // - floats-020: container=216, content=~200+, float=96 → 200+96=296 > 216 → no pre-scan
        float float_width = 100.0f;  // Conservative estimate

        // If preceding content + float > container width, don't pre-scan
        if (preceding_content_width + float_width > container_width) {
            log_debug("[FLOAT PRE-SCAN] Content before float (%.1f) + float (%.1f) > container (%.1f), skip pre-scan",
                      preceding_content_width, float_width, container_width);
            return;
        }
    }

    // Float context is now unified in BlockContext - no need to create separate context
    if (!lycon->block.establishing_element && parent_block) {
        // Initialize BlockContext for float tracking if not already done
        lycon->block.establishing_element = parent_block;
        lycon->block.float_right_edge = parent_block->content_width > 0 ? parent_block->content_width : parent_block->width;
        log_debug("[FLOAT PRE-SCAN] Initialized BlockContext for parent block %s",
                  parent_block->node_name());
    }

    if (!lycon->block.establishing_element) {
        log_debug("[FLOAT PRE-SCAN] No establishing element available, cannot pre-scan");
        return;
    }

    log_debug("[FLOAT PRE-SCAN] Pre-laying ALL floats in content");

    // Pre-lay ALL floats in the content
    // This is a simplification - ideally we'd only pre-lay floats that can share a line with preceding content
    for (DomNode* child = first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;

        DomElement* elem = child->as_element();

        // Skip if already pre-laid
        if (elem->float_prelaid) continue;

        // Check display:none first - hidden elements should not participate in float layout
        DisplayValue display = resolve_display_value(child);
        if (display.outer == CSS_VALUE_NONE) continue;

        CssEnum float_value = get_element_float_value(elem);
        if (float_value != CSS_VALUE_LEFT && float_value != CSS_VALUE_RIGHT) continue;

        log_debug("[FLOAT PRE-SCAN] Pre-laying float: %s (float=%d)",
                  child->node_name(), float_value);

        // Layout the float now
        display.outer = CSS_VALUE_BLOCK;  // Floats become block per CSS 9.7

        // Mark as pre-laid to skip during normal flow
        elem->float_prelaid = true;

        // Layout the float block
        layout_block(lycon, child, display);
    }

    // After pre-scanning floats, adjust the current line bounds for the floats we just laid out
    // This is critical: the first line needs to start AFTER the float, not at x=0
    //
    // Use unified BlockContext for float space queries
    // IMPORTANT: Floats are registered to the BFC (parent chain), not lycon->block
    // So we need to check the BFC's float counts, not the current block's
    BlockContext* bfc = block_context_find_bfc(&lycon->block);
    if (bfc && (bfc->left_float_count > 0 || bfc->right_float_count > 0)) {
        log_debug("[FLOAT PRE-SCAN] Adjusting initial line bounds for pre-scanned floats (bfc=%p, left=%d, right=%d)",
                  (void*)bfc, bfc->left_float_count, bfc->right_float_count);

        float line_height = lycon->block.line_height > 0 ? lycon->block.line_height : 16.0f;

        // Calculate current block's Y position in BFC coordinates
        // We need to walk up from the parent_block to the BFC establishing element
        float bfc_y_offset = 0.0f;
        float bfc_x_offset = 0.0f;
        ViewElement* walker = parent_block;
        ViewBlock* bfc_elem = bfc->establishing_element;
        while (walker && walker != bfc_elem) {
            bfc_y_offset += walker->y;
            bfc_x_offset += walker->x;
            walker = walker->parent_view();
        }
        // Add parent_block's border/padding to get to content area
        if (parent_block && parent_block->bound) {
            if (parent_block->bound->border) {
                bfc_y_offset += parent_block->bound->border->width.top;
                bfc_x_offset += parent_block->bound->border->width.left;
            }
            bfc_y_offset += parent_block->bound->padding.top;
            bfc_x_offset += parent_block->bound->padding.left;
        }

        // Query at the BFC-relative Y position of this block's first line
        float query_y = bfc_y_offset + lycon->block.advance_y;
        log_debug("[FLOAT PRE-SCAN] querying space at bfc_y=%.1f, line_height=%.1f, left_count=%d",
               query_y, line_height, bfc->left_float_count);
        FloatAvailableSpace space = block_context_space_at_y(bfc, query_y, line_height);
        log_debug("[FLOAT PRE-SCAN] space=(%.1f, %.1f), has_left=%d, has_right=%d",
               space.left, space.right, space.has_left_float, space.has_right_float);

        if (space.has_left_float) {
            // Left float intrudes - adjust effective_left and advance_x
            // space.left is in BFC coordinates, need to convert to local (block content area) coords
            float local_left = space.left - bfc_x_offset;
            log_debug("[FLOAT PRE-SCAN] space.left=%.1f, bfc_x_offset=%.1f, local_left=%.1f, current effective_left=%.1f",
                   space.left, bfc_x_offset, local_left, lycon->line.effective_left);
            if (local_left > lycon->line.effective_left) {
                log_debug("[FLOAT PRE-SCAN] Adjusting line.effective_left: %.1f -> %.1f, advance_x: %.1f -> %.1f",
                       lycon->line.effective_left, local_left, lycon->line.advance_x, local_left);
                lycon->line.effective_left = local_left;
                lycon->line.has_float_intrusion = true;
                if (lycon->line.advance_x < local_left) {
                    lycon->line.advance_x = local_left;
                }
            }
        }
        if (space.has_right_float) {
            // Right float intrudes - adjust effective_right
            // space.right is in BFC coordinates, convert to local
            float local_right = space.right - bfc_x_offset;
            if (local_right < lycon->line.effective_right) {
                log_debug("[FLOAT PRE-SCAN] Adjusting line.effective_right: %.1f -> %.1f",
                          lycon->line.effective_right, local_right);
                lycon->line.effective_right = local_right;
                lycon->line.has_float_intrusion = true;
            }
        }
    }

    log_debug("[FLOAT PRE-SCAN] Pre-scan complete");
}

void layout_block_inner_content(LayoutContext* lycon, ViewBlock* block) {
    log_debug("layout block inner content");

    // Allocate pseudo-element content if ::before or ::after is present
    if (block->is_element()) {
        block->pseudo = alloc_pseudo_content_prop(lycon, block);

        // Generate pseudo-element content from CSS content property (CSS 2.1 Section 12.2)
        // Must be done AFTER alloc_pseudo_content_prop populates the content/type fields
        generate_pseudo_element_content(lycon, block, true);   // ::before
        generate_pseudo_element_content(lycon, block, false);  // ::after

        // Insert pseudo-elements into DOM tree for proper view tree linking
        if (block->pseudo) {
            if (block->pseudo->before) {
                insert_pseudo_into_dom((DomElement*)block, block->pseudo->before, true);
            }
            if (block->pseudo->after) {
                insert_pseudo_into_dom((DomElement*)block, block->pseudo->after, false);
            }
        }
    }

    if (block->display.inner == RDT_DISPLAY_REPLACED) {  // image, iframe, hr
        uintptr_t elmt_name = block->tag();
        if (elmt_name == HTM_TAG_IFRAME) {
            layout_iframe(lycon, block, block->display);
        }
        else if (elmt_name == HTM_TAG_HR) {
            // hr element: Use explicit height if specified, otherwise use border height
            if (lycon->block.given_height >= 0) {
                // CSS height property is set - use it as content height
                float content_height = lycon->block.given_height;
                float padding_top = block->bound && block->bound->padding.top > 0 ? block->bound->padding.top : 0;
                float padding_bottom = block->bound && block->bound->padding.bottom > 0 ? block->bound->padding.bottom : 0;
                float border_top = block->bound && block->bound->border ? block->bound->border->width.top : 0;
                float border_bottom = block->bound && block->bound->border ? block->bound->border->width.bottom : 0;
                block->height = content_height + padding_top + padding_bottom + border_top + border_bottom;
                log_debug("hr layout: explicit height=%f, total=%f", content_height, block->height);
            } else {
                // No explicit height - use border thickness (traditional hr behavior)
                float border_top = block->bound && block->bound->border ? block->bound->border->width.top : 0;
                float border_bottom = block->bound && block->bound->border ? block->bound->border->width.bottom : 0;
                block->height = border_top + border_bottom;
                log_debug("hr layout: border-only height=%f", block->height);
            }
        }
        // else HTM_TAG_IMG
    } else {  // layout block child content
        // No longer need separate pseudo-element layout - they're part of child list now
        DomNode *child = nullptr;
        if (block->is_element()) { child = block->first_child; }
        if (child) {
            if (block->display.inner == CSS_VALUE_FLOW) {
                // Pre-scan and layout floats BEFORE laying out inline content
                // This ensures floats are positioned and affect line bounds correctly
                prescan_and_layout_floats(lycon, child, block);

                // inline content flow
                do {
                    layout_flow_node(lycon, child);
                    child = child->next_sibling;
                } while (child);
                // handle last line
                if (!lycon->line.is_line_start) {
                    line_break(lycon);
                }
            }
            else if (block->display.inner == CSS_VALUE_FLEX) {
                auto t_flex_start = high_resolution_clock::now();
                log_debug("Setting up flex container for %s", block->node_name());
                layout_flex_content(lycon, block);
                log_debug("Finished flex container layout for %s", block->node_name());
                g_flex_layout_time += duration<double, std::milli>(high_resolution_clock::now() - t_flex_start).count();
                return;
            }
            else if (block->display.inner == CSS_VALUE_GRID) {
                auto t_grid_start = high_resolution_clock::now();
                log_debug("Setting up grid container for %s (multipass)", block->node_name());
                // Use multipass grid layout (similar to flex layout pattern)
                layout_grid_content(lycon, block);
                log_debug("Finished grid container layout for %s", block->node_name());
                g_grid_layout_time += duration<double, std::milli>(high_resolution_clock::now() - t_grid_start).count();
                return;
            }
            else if (block->display.inner == CSS_VALUE_TABLE) {
                auto t_table_start = high_resolution_clock::now();
                log_debug("TABLE LAYOUT TRIGGERED! outer=%d, inner=%d, element=%s",
                    block->display.outer, block->display.inner, block->node_name());
                layout_table_content(lycon, block, block->display);
                g_table_layout_time += duration<double, std::milli>(high_resolution_clock::now() - t_table_start).count();
                return;
            }
            else {
                log_debug("unknown display type");
            }
        }

        // Final line break after all content
        if (!lycon->line.is_line_start) {
            line_break(lycon);
        }

        finalize_block_flow(lycon, block, block->display.outer);
    }
}

float adjust_min_max_width(ViewBlock* block, float width) {
    if (block->blk) {
        if (block->blk->given_max_width >= 0 && width > block->blk->given_max_width) {
            width = block->blk->given_max_width;
            log_debug("[ADJUST] Clamped to max: %.2f", width);
        }
        // Note: given_min_width overrides given_max_width if both are specified
        if (block->blk->given_min_width >= 0 && width < block->blk->given_min_width) {
            width = block->blk->given_min_width;
            log_debug("[ADJUST] Clamped to min: %.2f", width);
        }
    }
    log_debug("[ADJUST] adjust_min_max_width: output=%.2f", width);
    return width;
}

float adjust_min_max_height(ViewBlock* block, float height) {
    if (block->blk) {
        if (block->blk->given_max_height >= 0 && height > block->blk->given_max_height) {
            height = block->blk->given_max_height;
        }
        // Note: given_min_height overrides given_max_height if both are specified
        if (block->blk->given_min_height >= 0 && height < block->blk->given_min_height) {
            height = block->blk->given_min_height;
        }
    }
    return height;
}

float adjust_border_padding_width(ViewBlock* block, float width) {
    // for border-box, the given width includes padding and borders
    // so we need to subtract them to get the content width
    float padding_and_border = 0;
    if (block->bound) {
        padding_and_border += block->bound->padding.left + block->bound->padding.right;
        if (block->bound->border) {
            padding_and_border += block->bound->border->width.left + block->bound->border->width.right;
        }
    }
    width = max(width - padding_and_border, 0);
    log_debug("box-sizing: border-box - padding+border=%f, content_width=%f, border_width=%f", padding_and_border, width,
        block->bound && block->bound->border ? block->bound->border->width.left + block->bound->border->width.right : 0);
    return width;
}

float adjust_border_padding_height(ViewBlock* block, float height) {
    // for border-box, the given height includes padding and borders
    // so we need to subtract them to get the content height
    float padding_and_border = 0;
    if (block->bound) {
        padding_and_border += block->bound->padding.top + block->bound->padding.bottom;
        if (block->bound->border) {
            padding_and_border += block->bound->border->width.top + block->bound->border->width.bottom;
        }
    }
    height = max(height - padding_and_border, 0);
    log_debug("box-sizing: border-box - padding+border=%f, content_height=%f", padding_and_border, height);
    return height;
}

void setup_inline(LayoutContext* lycon, ViewBlock* block) {
    // setup inline context
    float content_width = lycon->block.content_width;
    lycon->block.advance_y = 0;  lycon->block.max_width = 0;

    // Calculate BFC offset for this block (used for float coordinate conversion)
    BlockContext* bfc = block_context_find_bfc(&lycon->block);
    if (bfc) {
        block_context_calc_bfc_offset((ViewElement*)block, bfc,
                                      &lycon->block.bfc_offset_x, &lycon->block.bfc_offset_y);
    } else {
        lycon->block.bfc_offset_x = 0;
        lycon->block.bfc_offset_y = 0;
    }

    // Calculate the block's inner content bounds based on border and padding
    // Note: content_width is already the inner content width (excluding padding/border)
    // line.left/right define the line box boundaries in local block coordinates
    float inner_left = 0;

    if (block->bound) {
        if (block->bound->border) {
            inner_left += block->bound->border->width.left;
            lycon->block.advance_y += block->bound->border->width.top;
        }
        inner_left += block->bound->padding.left;
        lycon->block.advance_y += block->bound->padding.top;
    }

    // line.right = inner_left + content_width gives the full content area
    float inner_right = inner_left + content_width;

    // Set the block's container bounds (line.left/right)
    // These define the nominal line box boundaries for this block
    lycon->line.left = inner_left;
    lycon->line.right = inner_right;

    // Initialize effective bounds to match container bounds
    // line_reset() will adjust these for floats if needed
    lycon->line.effective_left = inner_left;
    lycon->line.effective_right = inner_right;
    lycon->line.has_float_intrusion = false;
    lycon->line.advance_x = inner_left;
    lycon->line.vertical_align = CSS_VALUE_BASELINE;

    // Now call line_reset to adjust for floats at current Y position
    // This will call adjust_line_for_floats which updates effective_left/right
    line_reset(lycon);

    log_debug("setup_inline: line.left=%.1f, line.right=%.1f, effective_left=%.1f, effective_right=%.1f, advance_x=%.1f",
              lycon->line.left, lycon->line.right, lycon->line.effective_left, lycon->line.effective_right, lycon->line.advance_x);

    if (block->blk) lycon->block.text_align = block->blk->text_align;
    // setup font
    if (block->font) {
        setup_font(lycon->ui_context, &lycon->font, block->font);
    }
    // setup line height
    setup_line_height(lycon, block);

    // setup initial ascender and descender
    // Use OS/2 sTypo metrics only when USE_TYPO_METRICS flag is set (Chrome behavior)
    TypoMetrics typo = get_os2_typo_metrics(lycon->font.ft_face);
    if (typo.valid && typo.use_typo_metrics) {
        lycon->block.init_ascender = typo.ascender;
        lycon->block.init_descender = typo.descender;
    } else {
        // Fallback to FreeType HHEA metrics
        lycon->block.init_ascender = lycon->font.ft_face->size->metrics.ascender / 64.0;
        lycon->block.init_descender = (-lycon->font.ft_face->size->metrics.descender) / 64.0;
    }
    lycon->block.lead_y = max(0.0f, (lycon->block.line_height - (lycon->block.init_ascender + lycon->block.init_descender)) / 2);
    log_debug("block line_height: %f, font height: %f, asc+desc: %f, lead_y: %f", lycon->block.line_height, lycon->font.ft_face->size->metrics.height / 64.0,
        lycon->block.init_ascender + lycon->block.init_descender, lycon->block.lead_y);
}

void layout_block_content(LayoutContext* lycon, ViewBlock* block, BlockContext *pa_block, Linebox *pa_line) {
    block->x = pa_line->left;  block->y = pa_block->advance_y;

    // CSS 2.2 9.5: For floats appearing after inline content, position at bottom of current line
    // "A floating box's outer top may not be higher than the outer top of any block or floated
    // box generated by an element earlier in the source document."
    // When a float appears after inline content on the current line, it should start
    // below that content, not at advance_y which may be 0.
    bool is_float =  block->position && (block->position->float_prop == CSS_VALUE_LEFT || block->position->float_prop == CSS_VALUE_RIGHT);

    if (is_float && !pa_line->is_line_start) {
        // Float appears after inline content - position at bottom of current line
        float line_height = pa_block->line_height > 0 ? pa_block->line_height : 18.0f;
        block->y = pa_block->advance_y + line_height;
        log_debug("Float positioned below current line: y=%.1f (advance_y=%.1f + line_height=%.1f)",
                  block->y, pa_block->advance_y, line_height);
    }

    log_debug("block init position (%s): x=%f, y=%f, pa_block.advance_y=%f, display: outer=%d, inner=%d",
        block->node_name(), block->x, block->y, pa_block->advance_y, block->display.outer, block->display.inner);

    // Check if this block establishes a new BFC using unified BlockContext
    bool establishes_bfc = block_context_establishes_bfc(block);
    if (establishes_bfc) {
        lycon->block.is_bfc_root = true;
        lycon->block.establishing_element = block;
        // Reset float lists for new BFC
        block_context_reset_floats(&lycon->block);
        log_debug("[BlockContext] Block %s establishes new BFC", block->node_name());
    } else {
        // Clear is_bfc_root so we don't inherit it from parent
        // This ensures block_context_find_bfc walks up to the actual BFC root
        lycon->block.is_bfc_root = false;
        lycon->block.establishing_element = nullptr;
        // Don't reset floats - they belong to the parent BFC
    }

    uintptr_t elmt_name = block->tag();
    if (elmt_name == HTM_TAG_IMG) { // load image intrinsic width and height
        log_debug("[IMG LAYOUT] Processing IMG element: %s", block->node_name());
        const char *value;
        value = block->get_attribute("src");
        log_debug("[IMG LAYOUT] src attribute: %s", value ? value : "NULL");
        if (value) {
            size_t value_len = strlen(value);
            StrBuf* src = strbuf_new_cap(value_len);
            strbuf_append_str_n(src, value, value_len);
            log_debug("image src: %s", src->str);
            if (!block->embed) {
                block->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp));
            }
            block->embed->img = load_image(lycon->ui_context, src->str);
            strbuf_free(src);
            if (!block->embed->img) {
                log_debug("Failed to load image");
                // todo: use a placeholder
            }
        }
        if (block->embed && block->embed->img) {
            ImageSurface* img = block->embed->img;
            // scale image by pixel ratio
            float w = img->width * lycon->ui_context->pixel_ratio;
            float h = img->height * lycon->ui_context->pixel_ratio;

            // Check if width was specified as percentage but resolved to 0
            // This happens when parent has auto/0 width - use intrinsic width instead
            bool width_is_zero_percent = (lycon->block.given_width == 0 &&
                                          block->blk && !isnan(block->blk->given_width_percent));

            log_debug("image intrinsic dims: %f x %f, given: %f x %f, zero_percent=%d", w, h,
                lycon->block.given_width, lycon->block.given_height, width_is_zero_percent);

            if (lycon->block.given_width < 0 || lycon->block.given_height < 0 || width_is_zero_percent) {
                if (lycon->block.given_width >= 0 && !width_is_zero_percent) {
                    // Width specified, scale unspecified height
                    lycon->block.given_height = lycon->block.given_width * h / w;
                }
                else if (lycon->block.given_height >= 0 && lycon->block.given_width < 0) {
                    // Height specified, scale unspecified width
                    lycon->block.given_width = lycon->block.given_height * w / h;
                }
                else {
                    // Both width and height unspecified, or width was 0% on 0-width parent
                    if (img->format == IMAGE_FORMAT_SVG) {
                        // For SVG, try to use parent width, but fall back to intrinsic if parent is 0
                        float parent_width = lycon->block.parent ? lycon->block.parent->content_width : 0;
                        if (parent_width > 0) {
                            lycon->block.given_width = parent_width;
                        } else {
                            // Parent has no width, use intrinsic SVG dimensions
                            lycon->block.given_width = w;
                        }
                        lycon->block.given_height = lycon->block.given_width * h / w;
                    }
                    else { // use image intrinsic dimensions
                        lycon->block.given_width = w;
                        lycon->block.given_height = h;
                    }
                }
            }
            // else both width and height specified (non-zero)
            if (img->format == IMAGE_FORMAT_SVG) {
                img->max_render_width = max(lycon->block.given_width, img->max_render_width);
            }
            log_debug("image dimensions: %f x %f", lycon->block.given_width, lycon->block.given_height);
        }
        else { // failed to load image
            // use html width/height attributes if specified, otherwise use placeholder size
            if (lycon->block.given_width <= 0) lycon->block.given_width = 40;
            if (lycon->block.given_height <= 0) lycon->block.given_height = 30;
            // todo: use a placeholder
        }
    }

    // determine block width and height
    float content_width = -1;
    log_debug("Block '%s': given_width=%.2f,  given_height=%.2f, blk=%p, width_type=%d",
        block->node_name(), lycon->block.given_width, lycon->block.given_height, (void*)block->blk,
        block->blk ? block->blk->given_width_type : -1);

    // Check if this is a floated element with auto width
    // CSS 2.2 Section 10.3.5: Floats with auto width use shrink-to-fit width
    // We'll do a post-layout adjustment after content is laid out
    // Note: width is "auto" if either explicitly set to auto (CSS_VALUE_AUTO=84) or unset (CSS_VALUE__UNDEF=0)
    bool width_is_auto = !block->blk ||
                         block->blk->given_width_type == CSS_VALUE_AUTO ||
                         block->blk->given_width_type == CSS_VALUE__UNDEF;
    bool is_float_auto_width = element_has_float(block) && lycon->block.given_width < 0 && width_is_auto;

    if (is_float_auto_width) {
        // For floats with auto width, initially use available width for layout
        // Then shrink to fit content in post-layout step
        float available_width = pa_block->content_width;
        if (block->bound) {
            available_width -= (block->bound->margin.left_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.left)
                + (block->bound->margin.right_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.right);
        }
        content_width = available_width;
        log_debug("Float auto-width: initial layout with available_width=%.2f (will shrink post-layout)", content_width);
        content_width = adjust_min_max_width(block, content_width);
        if (block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
            if (block->bound) content_width = adjust_border_padding_width(block, content_width);
        }
    }
    else if (lycon->block.given_width >= 0 && (!block->blk || block->blk->given_width_type != CSS_VALUE_AUTO)) {
        content_width = max(lycon->block.given_width, 0);
        log_debug("Using given_width: content_width=%.2f", content_width);
        content_width = adjust_min_max_width(block, content_width);
        log_debug("After adjust_min_max_width: content_width=%.2f", content_width);
        if (block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
            if (block->bound) content_width = adjust_border_padding_width(block, content_width);
            log_debug("After adjust_border_padding (border-box): content_width=%.2f", content_width);
        }
    }
    else { // derive from parent block width
        log_debug("Deriving from parent: pa_block->content_width=%.2f", pa_block->content_width);
        if (block->bound) {
            content_width = pa_block->content_width
                - (block->bound->margin.left_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.left)
                - (block->bound->margin.right_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.right);
        }
        else { content_width = pa_block->content_width; }
        if (block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
            content_width = adjust_min_max_width(block, content_width);
            if (block->bound) content_width = adjust_border_padding_width(block, content_width);
        } else {
            content_width = adjust_border_padding_width(block, content_width);
            if (block->bound) content_width = adjust_min_max_width(block, content_width);
        }
    }
    assert(content_width >= 0);
    log_debug("content_width=%f, given_width=%f, max_width=%f", content_width, lycon->block.given_width,
        block->blk && block->blk->given_max_width >= 0 ? block->blk->given_max_width : -1);

    float content_height = -1;
    if (lycon->block.given_height >= 0) {
        content_height = max(lycon->block.given_height, 0);
        content_height = adjust_min_max_height(block, content_height);
        if (block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
            if (block->bound) content_height = adjust_border_padding_height(block, content_height);
        }
    }
    else { // auto height - will be determined by content
        // Don't inherit parent's content_height for auto height blocks
        // The height will be finalized after content is laid out in finalize_block_flow
        content_height = 0;  // Initial value, will be updated during layout
        if (block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
            content_height = adjust_min_max_height(block, content_height);
            if (block->bound) content_height = adjust_border_padding_height(block, content_height);
        } else {
            content_height = adjust_border_padding_height(block, content_height);
            if (block->bound) content_height = adjust_min_max_height(block, content_height);
        }
    }
    assert(content_height >= 0);
    log_debug("content_height=%f, given_height=%f, max_height=%f", content_height, lycon->block.given_height,
        block->blk && block->blk->given_max_height >= 0 ? block->blk->given_max_height : -1);
    lycon->block.content_width = content_width;  lycon->block.content_height = content_height;

    // Update available space to match content dimensions
    // Preserve intrinsic sizing mode if already set (for nested measurement)
    if (!lycon->available_space.is_intrinsic_sizing()) {
        lycon->available_space.width = AvailableSize::make_definite(content_width);
        if (content_height > 0) {
            lycon->available_space.height = AvailableSize::make_definite(content_height);
        }
    }

    if (block->bound) {
        block->width = content_width + block->bound->padding.left + block->bound->padding.right +
            (block->bound->border ? block->bound->border->width.left + block->bound->border->width.right : 0);
        block->height = content_height + block->bound->padding.top + block->bound->padding.bottom +
            (block->bound->border ? block->bound->border->width.top + block->bound->border->width.bottom : 0);
        // todo: we should keep LENGTH_AUTO (may be in flags) for reflow

        // CSS behavior for <center> element: block children should be horizontally centered
        // This is achieved by applying margin:auto to block children
        // Note: <center> is deprecated HTML but still widely used
        if (block->parent && block->parent->is_element() && block->parent->tag() == HTM_TAG_CENTER) {
            // Only apply centering to blocks that don't already have explicit margin values
            // and that are not full-width (i.e., have a defined width less than parent)
            if (block->width < pa_block->content_width &&
                block->bound->margin.left_type != CSS_VALUE_AUTO &&
                block->bound->margin.right_type != CSS_VALUE_AUTO) {
                block->bound->margin.left_type = CSS_VALUE_AUTO;
                block->bound->margin.right_type = CSS_VALUE_AUTO;
                log_debug("applied margin:auto centering for block inside <center>");
            }
        }

        log_debug("block margins: left=%f, right=%f, left_type=%d, right_type=%d",
            block->bound->margin.left, block->bound->margin.right, block->bound->margin.left_type, block->bound->margin.right_type);
        if (block->bound->margin.left_type == CSS_VALUE_AUTO && block->bound->margin.right_type == CSS_VALUE_AUTO)  {
            block->bound->margin.left = block->bound->margin.right = max((pa_block->content_width - block->width) / 2, 0);
        } else {
            if (block->bound->margin.left_type == CSS_VALUE_AUTO) block->bound->margin.left = 0;
            if (block->bound->margin.right_type == CSS_VALUE_AUTO) block->bound->margin.right = 0;
        }
        log_debug("finalize block margins: left=%f, right=%f", block->bound->margin.left, block->bound->margin.right);
        float y_before_margin = block->y;
        block->x += block->bound->margin.left;
        block->y += block->bound->margin.top;
        log_debug("Y coordinate: before margin=%f, margin.top=%f, after margin=%f (tag=%s)",
                  y_before_margin, block->bound->margin.top, block->y, block->node_name());
    }
    else {
        block->width = content_width;  block->height = content_height;
        // no change to block->x, block->y
    }
    log_debug("layout-block-sizes: x:%f, y:%f, wd:%f, hg:%f, line-hg:%f, given-w:%f, given-h:%f",
        block->x, block->y, block->width, block->height, lycon->block.line_height, lycon->block.given_width, lycon->block.given_height);

    // IMPORTANT: Apply clear BEFORE setting up inline context and laying out children
    // Clear positions this element below earlier floats
    // This must happen after Y position and margins are set, but before children are laid out
    // Note: Check for actual clear values (LEFT=50, RIGHT=51, BOTH), not just "not NONE"
    // because uninitialized clear is 0 (CSS_VALUE__UNDEF) which would incorrectly pass "!= NONE"
    if (block->position && (block->position->clear == CSS_VALUE_LEFT ||
                             block->position->clear == CSS_VALUE_RIGHT ||
                             block->position->clear == CSS_VALUE_BOTH)) {
        log_debug("Element has clear property, applying clear layout BEFORE children");
        layout_clear_element(lycon, block);
    }

    // setup inline context
    setup_inline(lycon, block);

    // For floats with auto width, calculate intrinsic width BEFORE laying out children
    // This ensures children are laid out with the correct shrink-to-fit width
    if (is_float_auto_width && block->is_element()) {
        // Font is loaded after setup_inline, so now we can calculate intrinsic width
        DomElement* dom_element = (DomElement*)block;
        float available = pa_block->content_width;
        if (block->bound) {
            available -= (block->bound->margin.left_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.left)
                      + (block->bound->margin.right_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.right);
        }

        // Calculate fit-content width (shrink-to-fit)
        int fit_content = calculate_fit_content_width(lycon, dom_element, (int)available);

        if (fit_content > 0 && fit_content < (int)block->width) {
            log_debug("Float shrink-to-fit: fit_content=%d, old_width=%.1f, available=%.1f",
                fit_content, block->width, available);

            // Update block width to shrink-to-fit size
            block->width = (float)fit_content;

            // Also update content_width for child layout
            float new_content_width = block->width;
            if (block->bound) {
                new_content_width -= block->bound->padding.left + block->bound->padding.right;
                if (block->bound->border) {
                    new_content_width -= block->bound->border->width.left + block->bound->border->width.right;
                }
            }
            block->content_width = max(new_content_width, 0.0f);
            lycon->block.content_width = block->content_width;

            // Re-setup line context with new width
            line_init(lycon, 0, block->content_width);
            if (block->bound) {
                if (block->bound->border) {
                    lycon->line.advance_x += block->bound->border->width.left;
                    lycon->line.right -= block->bound->border->width.right;
                }
                lycon->line.advance_x += block->bound->padding.left;
                lycon->line.left = lycon->line.advance_x;
                lycon->line.right = lycon->line.left + block->content_width;
            }
        }
    }

    // layout block content, and determine flow width and height
    layout_block_inner_content(lycon, block);

    // check for margin collapsing with children
    if (block->bound) {
        // collapse bottom margin with last in-flow child block
        // Skip absolutely positioned and floated children - they're out of normal flow
        if ((!block->bound->border || block->bound->border->width.bottom == 0) &&
            block->bound->padding.bottom == 0 && block->first_child) {
            // Find last in-flow child (skip abs-positioned and floated elements)
            View* last_in_flow = nullptr;
            View* child = (View*)block->first_child;
            while (child) {
                if (child->view_type && child->is_block()) {
                    ViewBlock* vb = (ViewBlock*)child;
                    bool is_out_of_flow = (vb->position &&
                        (vb->position->position == CSS_VALUE_ABSOLUTE ||
                         vb->position->position == CSS_VALUE_FIXED ||
                         element_has_float(vb)));
                    if (!is_out_of_flow) {
                        last_in_flow = child;
                    }
                } else if (child->view_type) {
                    // Non-block placed children (like inline elements) count as in-flow
                    last_in_flow = child;
                }
                child = (View*)child->next_sibling;
            }

            if (last_in_flow && last_in_flow->is_block() && ((ViewBlock*)last_in_flow)->bound) {
                ViewBlock* last_child_block = (ViewBlock*)last_in_flow;
                if (last_child_block->bound->margin.bottom > 0) {
                    float margin_bottom = max(block->bound->margin.bottom, last_child_block->bound->margin.bottom);
                    block->height -= last_child_block->bound->margin.bottom;
                    block->bound->margin.bottom = margin_bottom;
                    last_child_block->bound->margin.bottom = 0;
                    log_debug("collapsed bottom margin %f between block and last child", margin_bottom);
                }
            }
        }
    }

    // BFC (Block Formatting Context) height expansion to contain floats
    // CSS 2.2 Section 10.6.7: In certain cases (BFC roots), the heights of floating
    // descendants are also taken into account when computing the height
    // BFC is established by: overflow != visible, display: flow-root, float != none, etc.
    bool creates_bfc = block->scroller &&
                       (block->scroller->overflow_x != CSS_VALUE_VISIBLE ||
                        block->scroller->overflow_y != CSS_VALUE_VISIBLE);

    log_debug("BFC check for %s: creates_bfc=%d, scroller=%p, overflow_x=%d",
        block->node_name(), creates_bfc, block->scroller, block->scroller ? block->scroller->overflow_x : -1);

    if (creates_bfc || lycon->block.is_bfc_root) {
        // Check unified BlockContext for float containment
        if (lycon->block.establishing_element == block) {
            float max_float_bottom = lycon->block.lowest_float_bottom;
            float content_bottom = block->y + block->height;
            log_debug("[BlockContext] Height expansion check: max_float_bottom=%.1f, content_bottom=%.1f",
                      max_float_bottom, content_bottom);
            if (max_float_bottom > content_bottom - block->y) {
                float old_height = block->height;
                block->height = max_float_bottom;
                log_debug("[BlockContext] Height expanded: old=%.1f, new=%.1f", old_height, block->height);
            }
        }

        // Also check for floats in block context
        log_debug("BFC %s: left_float_count=%d, right_float_count=%d",
            block->node_name(), lycon->block.left_float_count, lycon->block.right_float_count);
        if (lycon->block.establishing_element == block) {
            // Find the maximum bottom of all floated children
            float max_float_bottom = 0;
            log_debug("BFC %s: checking left floats", block->node_name());
            for (FloatBox* fb = lycon->block.left_floats; fb; fb = fb->next) {
                log_debug("BFC left float: margin_box_bottom=%.1f", fb->margin_box_bottom);
                if (fb->margin_box_bottom > max_float_bottom) {
                    max_float_bottom = fb->margin_box_bottom;
                }
            }
            log_debug("BFC %s: checking right floats", block->node_name());
            for (FloatBox* fb = lycon->block.right_floats; fb; fb = fb->next) {
                log_debug("BFC right float: margin_box_bottom=%.1f", fb->margin_box_bottom);
                if (fb->margin_box_bottom > max_float_bottom) {
                    max_float_bottom = fb->margin_box_bottom;
                }
            }

            // Float margin_box coordinates are relative to container's content area
            // Compare to block->height which is also relative/local
            log_debug("BFC %s: max_float_bottom=%.1f, block->height=%.1f",
                block->node_name(), max_float_bottom, block->height);
            if (max_float_bottom > block->height) {
                float old_height = block->height;
                block->height = max_float_bottom;
                log_debug("BFC height expansion: old=%.1f, new=%.1f (float_bottom=%.1f)",
                          old_height, block->height, max_float_bottom);

                // Update scroller clip to match new height (for overflow:hidden rendering)
                if (block->scroller && block->scroller->has_clip) {
                    block->scroller->clip.bottom = block->height;
                    log_debug("BFC updated clip.bottom to %.1f", block->height);
                }
            }
        }
    }

    // Apply CSS float layout using BlockContext
    // IMPORTANT: Floats must be added to the BFC root, not just the immediate parent
    if (block->position && element_has_float(block)) {
        log_debug("Element has float property, applying float layout");

        // Position the float using the parent's BlockContext
        // layout_float_element uses block_context_find_bfc which walks up parent chain
        layout_float_element(lycon, block);

        // Add float to the BFC root (not just immediate parent)
        // This ensures sibling elements can see the float via block_context_find_bfc
        BlockContext* bfc = block_context_find_bfc(pa_block);
        if (bfc) {
            block_context_add_float(bfc, block);
            log_debug("[BlockContext] Float added to BFC root (bfc=%p, pa_block=%p)", (void*)bfc, (void*)pa_block);
        } else {
            // Fallback to parent if no BFC found
            block_context_add_float(pa_block, block);
            log_debug("[BlockContext] Float added to parent context (no BFC found)");
        }
    }

    // Restore parent BFC if we created a new one - handled by block.parent in calling code
}

void layout_block(LayoutContext* lycon, DomNode *elmt, DisplayValue display) {
    uintptr_t tag = elmt->tag();
    if (tag == HTM_TAG_IMG) {
        log_debug("[LAYOUT_BLOCK IMG] layout_block ENTRY for IMG element: %s", elmt->node_name());
    }
    auto t_block_start = high_resolution_clock::now();

    log_enter();
    // display: CSS_VALUE_BLOCK, CSS_VALUE_INLINE_BLOCK, CSS_VALUE_LIST_ITEM
    log_debug("layout block %s (display: outer=%d, inner=%d)", elmt->node_name(), display.outer, display.inner);

    // Check if this block is a flex item
    ViewElement* parent_block = (ViewElement*)elmt->parent;
    bool is_flex_item = (parent_block && parent_block->display.inner == CSS_VALUE_FLEX);

    // CSS 2.2: Floats are removed from normal flow and don't cause line breaks
    // Check for float property before deciding on line break
    bool is_float = false;
    if (elmt->is_element()) {
        DomElement* elem = elmt->as_element();
        if (elem->position && elem->position->float_prop != CSS_VALUE_NONE) {
            is_float = true;
        } else if (elem->specified_style && elem->specified_style->tree) {
            // Check float property from CSS style tree
            AvlNode* float_node = avl_tree_search(elem->specified_style->tree, CSS_PROPERTY_FLOAT);
            if (float_node) {
                StyleNode* style_node = (StyleNode*)float_node->declaration;
                if (style_node && style_node->winning_decl && style_node->winning_decl->value) {
                    CssValue* val = style_node->winning_decl->value;
                    if (val->type == CSS_VALUE_TYPE_KEYWORD &&
                        (val->data.keyword == CSS_VALUE_LEFT || val->data.keyword == CSS_VALUE_RIGHT)) {
                        is_float = true;
                    }
                }
            }
        }
    }

    // Only cause line break for non-inline-block, non-float blocks
    if (display.outer != CSS_VALUE_INLINE_BLOCK && !is_float) {
        if (!lycon->line.is_line_start) { line_break(lycon); }
    }
    // save parent context
    BlockContext pa_block = lycon->block;  Linebox pa_line = lycon->line;
    FontBox pa_font = lycon->font;  lycon->font.current_font_size = -1;  // -1 as unresolved
    lycon->block.parent = &pa_block;  lycon->elmt = elmt;
    log_debug("saved pa_block.advance_y: %.2f for element %s", pa_block.advance_y, elmt->node_name());
    lycon->block.content_width = lycon->block.content_height = 0;
    lycon->block.given_width = -1;  lycon->block.given_height = -1;

    uintptr_t elmt_name = elmt->tag();
    ViewBlock* block = (ViewBlock*)set_view(lycon,
        display.outer == CSS_VALUE_INLINE_BLOCK ? RDT_VIEW_INLINE_BLOCK :
        display.outer == CSS_VALUE_LIST_ITEM ? RDT_VIEW_LIST_ITEM :
        display.inner == CSS_VALUE_TABLE ? RDT_VIEW_TABLE : RDT_VIEW_BLOCK,
        elmt);
    block->display = display;

    // resolve CSS styles
    dom_node_resolve_style(elmt, lycon);

    // CSS Counter handling (CSS 2.1 Section 12.4)
    // Push a new counter scope for this element
    if (lycon->counter_context) {
        counter_push_scope(lycon->counter_context);

        // Apply counter-reset if specified
        if (block->blk && block->blk->counter_reset) {
            log_debug("    [Block] Applying counter-reset: %s", block->blk->counter_reset);
            counter_reset(lycon->counter_context, block->blk->counter_reset);
        }

        // Apply counter-increment if specified
        if (block->blk && block->blk->counter_increment) {
            log_debug("    [Block] Applying counter-increment: %s", block->blk->counter_increment);
            counter_increment(lycon->counter_context, block->blk->counter_increment);
        }

        // CSS 2.1 Section 12.5: List markers use implicit "list-item" counter
        // For display:list-item, auto-increment list-item counter
        if (display.outer == CSS_VALUE_LIST_ITEM) {
            log_debug("    [List] Auto-incrementing list-item counter");
            counter_increment(lycon->counter_context, "list-item 1");

            // Set default list-style-position to outside if not specified
            // CSS 2.1 Section 12.5.1: Initial value is 'outside'
            bool is_outside_position = true;  // Default is outside
            if (block->blk && block->blk->list_style_position != 0) {
                // Check if position is explicitly set to "inside"
                // Position values: 1=inside, 2=outside (from shorthand expansion)
                if (block->blk->list_style_position == 1) {
                    is_outside_position = false;
                    log_debug("    [List] list-style-position=inside (is_outside=0)");
                } else {
                    is_outside_position = true;
                    log_debug("    [List] list-style-position=outside (is_outside=1)");
                }
            } else {
                log_debug("    [List] Using default list-style-position=outside");
            }

            // Generate list marker if list-style-type is not 'none'
            // Only create ::marker pseudo-element for 'inside' positioned markers
            // Outside markers are rendered directly in the margin area (not in DOM tree)
            if (block->blk && block->blk->list_style_type && block->blk->list_style_type != CSS_VALUE_NONE) {
                CssEnum marker_style = block->blk->list_style_type;
                const CssEnumInfo* info = css_enum_info(marker_style);
                log_debug("    [List] Generating marker with style: %s (0x%04X)", info ? info->name : "unknown", marker_style);

                // Format the counter value using list-style-type
                char marker_text[64];
                int marker_len = counter_format(lycon->counter_context, "list-item", marker_style, marker_text, sizeof(marker_text));

                if (marker_len > 0 && !is_outside_position) {
                    // Only create ::marker pseudo-element for 'inside' positioned markers
                    // Add suffix for non-bullet markers (decimal, alpha, roman get periods)
                    bool needs_period = (marker_style == CSS_VALUE_DECIMAL ||
                                       marker_style == CSS_VALUE_LOWER_ALPHA || marker_style == CSS_VALUE_UPPER_ALPHA ||
                                       marker_style == CSS_VALUE_LOWER_ROMAN || marker_style == CSS_VALUE_UPPER_ROMAN);

                    if (needs_period && marker_len + 2 < (int)sizeof(marker_text)) {
                        marker_text[marker_len] = '.';
                        marker_text[marker_len + 1] = ' ';
                        marker_text[marker_len + 2] = '\0';
                        marker_len += 2;
                    } else if (marker_len + 7 < (int)sizeof(marker_text)) {
                        // For disc/circle/square, add non-breaking spaces for proper spacing
                        // Browsers typically render markers with ~0.5-1em spacing after the bullet
                        // Using non-breaking spaces (0xC2 0xA0 in UTF-8) to prevent collapse
                        // Square bullets are wider, so use no extra spaces - just regular space
                        if (marker_style == CSS_VALUE_SQUARE) {
                            marker_text[marker_len] = ' ';  // Just regular space for square
                            marker_text[marker_len + 1] = '\0';
                            marker_len += 1;
                        } else {
                            // Disc and circle need extra spacing (3 non-breaking spaces)
                            marker_text[marker_len] = ' ';      // Regular space
                            marker_len += 1;

                            for (int sp = 0; sp < 3 && marker_len + 2 < (int)sizeof(marker_text); sp++) {
                                marker_text[marker_len] = 0xC2;     // Non-breaking space (UTF-8)
                                marker_text[marker_len + 1] = 0xA0;
                                marker_len += 2;
                            }
                            marker_text[marker_len] = '\0';
                        }
                        marker_len += 7;
                    }

                    log_debug("    [List] Created 'inside' marker: '%s' (length=%d)", marker_text, marker_len);

                    // Create ::marker pseudo-element for 'inside' positioned markers
                    // (using ::before infrastructure to add inline content)
                    // Cast block to DomElement to access DOM fields
                    DomElement* parent_elem = (DomElement*)elmt;

                    if (!block->pseudo) {
                        block->pseudo = (PseudoContentProp*)alloc_prop(lycon, sizeof(PseudoContentProp));
                        memset(block->pseudo, 0, sizeof(PseudoContentProp));
                    }

                    if (!block->pseudo->before_generated) {
                        // Create DomElement for ::marker (as ::before)
                        DomElement* marker_elem = dom_element_create(parent_elem->doc, "::marker", nullptr);
                        if (marker_elem) {
                            marker_elem->parent = parent_elem;

                            // Create Lambda String for marker text
                            String* text_string = (String*)arena_alloc(parent_elem->doc->arena,
                                                                        sizeof(String) + marker_len + 1);
                            if (text_string) {
                                text_string->ref_cnt = 1;
                                text_string->len = marker_len;
                                memcpy(text_string->chars, marker_text, marker_len);
                                text_string->chars[marker_len] = '\0';

                                // Create text node with Lambda String
                                DomText* text_node = dom_text_create(text_string, marker_elem);
                                if (text_node) {
                                    marker_elem->first_child = text_node;
                                    log_debug("    [List] Created ::marker text content: \"%s\"", marker_text);
                                }
                            }

                            block->pseudo->before = marker_elem;
                            block->pseudo->before_generated = true;
                            log_debug("    [List] Created ::marker pseudo-element");
                        }
                    }
                } else if (marker_len > 0 && is_outside_position) {
                    // Outside markers are not added to DOM tree
                    // They should be rendered directly in the margin area during paint
                    log_debug("    [List] Skipping 'outside' marker creation (should render in margin area)");
                }
            }
        }
    }

    if (block->position && (block->position->position == CSS_VALUE_ABSOLUTE || block->position->position == CSS_VALUE_FIXED)) {
        layout_abs_block(lycon, elmt, block, &pa_block, &pa_line);
        lycon->block = pa_block;  lycon->font = pa_font;  lycon->line = pa_line;
    } else {
        // layout block content to determine content width and height
        layout_block_content(lycon, block, &pa_block, &pa_line);

        log_debug("flow block in parent context, block->y before restoration: %.2f", block->y);
        lycon->block = pa_block;  lycon->font = pa_font;  lycon->line = pa_line;

        // flow the block in parent context
        if (display.outer == CSS_VALUE_INLINE_BLOCK) {
            if (!lycon->line.start_view) lycon->line.start_view = (View*)block;

            // Update effective line bounds for floats at current Y position
            // This is critical for inline-blocks to wrap around floats correctly
            update_line_for_bfc_floats(lycon);

            // Check available width considering floats
            float effective_left = lycon->line.has_float_intrusion ?
                lycon->line.effective_left : lycon->line.left;
            float effective_right = lycon->line.has_float_intrusion ?
                lycon->line.effective_right : lycon->line.right;

            log_debug("inline-block float check: has_float_intrusion=%d, effective_left=%.1f, effective_right=%.1f, line.left=%.1f, line.right=%.1f, advance_x=%.1f",
                lycon->line.has_float_intrusion, lycon->line.effective_left, lycon->line.effective_right,
                lycon->line.left, lycon->line.right, lycon->line.advance_x);

            // Ensure advance_x is at least at effective_left
            if (lycon->line.advance_x < effective_left) {
                lycon->line.advance_x = effective_left;
            }

            if (lycon->line.advance_x + block->width > effective_right) {
                line_break(lycon);
                // After line break, update effective bounds for new Y
                update_line_for_bfc_floats(lycon);
                effective_left = lycon->line.has_float_intrusion ?
                    lycon->line.effective_left : lycon->line.left;
                block->x = effective_left;
            } else {
                block->x = lycon->line.advance_x;
            }
            if (block->in_line && block->in_line->vertical_align) {
                float item_height = block->height + (block->bound ?
                    block->bound->margin.top + block->bound->margin.bottom : 0);
                float item_baseline = block->height + (block->bound ? block->bound->margin.top: 0);
                float line_height = max(lycon->block.line_height, lycon->line.max_ascender + lycon->line.max_descender);
                float offset = calculate_vertical_align_offset(
                    lycon, block->in_line->vertical_align, item_height, line_height,
                    lycon->line.max_ascender, item_baseline);
                block->y = lycon->block.advance_y + offset;  // block->bound->margin.top will be added below
                log_debug("valigned-inline-block: offset %f, line %f, block %f, adv: %f, y: %f, va:%d",
                    offset, line_height, block->height, lycon->block.advance_y, block->y, block->in_line->vertical_align);
                lycon->line.max_descender = max(lycon->line.max_descender, offset + item_height - lycon->line.max_ascender);
                log_debug("new max_descender=%f", lycon->line.max_descender);
            } else {
                log_debug("valigned-inline-block: default baseline align");
                block->y = lycon->block.advance_y;
            }
            lycon->line.advance_x += block->width;
            if (block->bound) {
                block->x += block->bound->margin.left;
                block->y += block->bound->margin.top;
                lycon->line.advance_x += block->bound->margin.left + block->bound->margin.right;
            }
            log_debug("inline-block in line: x: %d, y: %d, adv-x: %d, mg-left: %d, mg-top: %d",
                block->x, block->y, lycon->line.advance_x, block->bound ? block->bound->margin.left : 0, block->bound ? block->bound->margin.top : 0);
            // update baseline
            if (block->in_line && block->in_line->vertical_align != CSS_VALUE_BASELINE) {
                float block_flow_height = block->height + (block->bound ? block->bound->margin.top + block->bound->margin.bottom : 0);
                if (block->in_line->vertical_align == CSS_VALUE_TEXT_TOP) {
                    lycon->line.max_descender = max(lycon->line.max_descender, block_flow_height - lycon->block.init_ascender);
                }
                else if (block->in_line->vertical_align == CSS_VALUE_TEXT_BOTTOM) {
                    lycon->line.max_ascender = max(lycon->line.max_ascender, block_flow_height - lycon->block.init_descender);
                }
                else {
                    lycon->line.max_descender = max(lycon->line.max_descender, block_flow_height - lycon->line.max_ascender);
                }
            } else {
                // default baseline alignment for inline block
                if (block->bound) {
                    lycon->line.max_ascender = max(lycon->line.max_ascender, block->height + block->bound->margin.top);
                    // bottom margin is placed below the baseline as descender
                    lycon->line.max_descender = max(lycon->line.max_descender, block->bound->margin.bottom);
                }
                else {
                    lycon->line.max_ascender = max(lycon->line.max_ascender, block->height);
                }
                log_debug("inline-block set max_ascender to: %d", lycon->line.max_ascender);
            }
            // line got content
            lycon->line.reset_space();
        }
        else { // normal block
            // Check if this is a floated element - floats are out of normal flow
            // and should NOT advance the parent's advance_y
            bool is_float = block->position && element_has_float(block);

            if (is_float) {
                // Floated elements don't participate in normal flow
                // They don't advance the parent's advance_y
                // Only update max_width for containing block sizing
                // Include lycon->line.left to account for parent's left border+padding
                if (block->bound) {
                    lycon->block.max_width = max(lycon->block.max_width, lycon->line.left + block->width
                        + block->bound->margin.left + block->bound->margin.right);
                } else {
                    lycon->block.max_width = max(lycon->block.max_width, lycon->line.left + block->width);
                }
                log_debug("float block end (no advance_y update), pa max_width: %f, block hg: %f",
                    lycon->block.max_width, block->height);
                // Note: floats don't require is_line_start - they're out of flow
            } else if (block->bound) {
                // collapse top margin with parent block
                log_debug("check margin collapsing");

                // Find first in-flow child that can participate in margin collapsing
                // Skip floats AND empty zero-height blocks (CSS 2.2 Section 8.3.1)
                // An empty block allows margins to collapse "through" it when:
                // - It has zero height
                // - It has no borders, padding, or line boxes
                View* first_in_flow_child = block->parent_view()->first_placed_child();
                while (first_in_flow_child) {
                    if (!first_in_flow_child->is_block()) break;
                    ViewBlock* vb = (ViewBlock*)first_in_flow_child;
                    // Skip floats
                    if (vb->position && element_has_float(vb)) {
                        View* next = (View*)first_in_flow_child->next_sibling;
                        while (next && !next->view_type) {
                            next = (View*)next->next_sibling;
                        }
                        first_in_flow_child = next;
                        continue;
                    }
                    // Skip empty zero-height blocks that have no borders/padding
                    // These blocks allow margins to collapse through them (CSS 2.2 8.3.1)
                    if (vb->height == 0) {
                        float border_top = vb->bound && vb->bound->border ? vb->bound->border->width.top : 0;
                        float border_bottom = vb->bound && vb->bound->border ? vb->bound->border->width.bottom : 0;
                        float padding_top = vb->bound ? vb->bound->padding.top : 0;
                        float padding_bottom = vb->bound ? vb->bound->padding.bottom : 0;
                        if (border_top == 0 && border_bottom == 0 && padding_top == 0 && padding_bottom == 0) {
                            log_debug("skipping empty zero-height block for margin collapsing");
                            View* next = (View*)first_in_flow_child->next_sibling;
                            while (next && !next->view_type) {
                                next = (View*)next->next_sibling;
                            }
                            first_in_flow_child = next;
                            continue;
                        }
                    }
                    break;
                }

                if (first_in_flow_child == block) {  // first in-flow child
                    if (block->bound->margin.top > 0) {
                        ViewBlock* parent = block->parent->is_block() ? (ViewBlock*)block->parent : NULL;
                        // Check if parent creates a BFC - BFC prevents margin collapsing
                        // BFC is created by: overflow != visible, float, position absolute/fixed, etc.
                        bool parent_creates_bfc = parent && parent->scroller &&
                            (parent->scroller->overflow_x != CSS_VALUE_VISIBLE ||
                             parent->scroller->overflow_y != CSS_VALUE_VISIBLE);
                        // parent has top margin, but no border, no padding;  parent->parent to exclude html
                        // Also: no margin collapsing if parent creates BFC
                        // If parent->bound is NULL, parent has no margin/border/padding - margins collapse through
                        float parent_padding_top = parent && parent->bound ? parent->bound->padding.top : 0;
                        float parent_border_top = parent && parent->bound && parent->bound->border ? parent->bound->border->width.top : 0;
                        float parent_margin_top = parent && parent->bound ? parent->bound->margin.top : 0;
                        if (parent && parent->parent && !parent_creates_bfc &&
                            parent_padding_top == 0 && parent_border_top == 0) {
                            float margin_top = max(block->bound->margin.top, parent_margin_top);

                            // CSS 8.3.1: When parent has no border/padding, child margin collapses through parent
                            // If parent had no margin (parent_margin_top == 0), we need to retroactively collapse
                            // with parent's previous sibling
                            float sibling_collapse = 0;
                            if (parent_margin_top == 0) {
                                // Find parent's previous in-flow sibling for retroactive sibling collapsing
                                View* prev_view = parent->prev_placed_view();
                                while (prev_view && prev_view->is_block()) {
                                    ViewBlock* vb = (ViewBlock*)prev_view;
                                    if (vb->position && element_has_float(vb)) {
                                        prev_view = prev_view->prev_placed_view();
                                        continue;
                                    }
                                    break;
                                }
                                if (prev_view && prev_view->is_block() && ((ViewBlock*)prev_view)->bound) {
                                    ViewBlock* prev_block = (ViewBlock*)prev_view;
                                    if (prev_block->bound->margin.bottom > 0 && margin_top > 0) {
                                        sibling_collapse = min(prev_block->bound->margin.bottom, margin_top);
                                        log_debug("retroactive sibling collapse for parent-child: sibling_collapse=%f", sibling_collapse);
                                    }
                                }
                            }

                            parent->y += margin_top - parent_margin_top - sibling_collapse;
                            if (parent->bound) {
                                parent->bound->margin.top = margin_top - sibling_collapse;
                            }
                            block->y = 0;  block->bound->margin.top = 0;
                            log_debug("collapsed margin between block and first child: %f, parent y: %f, block y: %f, sibling_collapse: %f",
                                margin_top, parent->y, block->y, sibling_collapse);
                        }
                        else {
                            log_debug("no parent margin collapsing: parent->bound=%p, border-top=%f, padding-top=%f, parent_creates_bfc=%d",
                                parent ? parent->bound : NULL, parent_border_top, parent_padding_top, parent_creates_bfc);
                        }
                    }
                }
                else {
                    // check sibling margin collapsing
                    // CSS 2.2 Section 8.3.1: Margins do NOT collapse when there's clearance
                    // If this block has clear property, skip sibling margin collapsing
                    bool has_clearance = block->position &&
                        (block->position->clear == CSS_VALUE_LEFT ||
                         block->position->clear == CSS_VALUE_RIGHT ||
                         block->position->clear == CSS_VALUE_BOTH);

                    if (!has_clearance) {
                        float collapse = 0;
                        // Find previous in-flow sibling (skip floats)
                        View* prev_view = block->prev_placed_view();
                        while (prev_view && prev_view->is_block()) {
                            ViewBlock* vb = (ViewBlock*)prev_view;
                            // Skip floats - they're out of normal flow and don't participate in margin collapsing
                            if (vb->position && element_has_float(vb)) {
                                prev_view = prev_view->prev_placed_view();
                                continue;
                            }
                            break;
                        }
                        if (prev_view && prev_view->is_block() && ((ViewBlock*)prev_view)->bound) {
                            ViewBlock* prev_block = (ViewBlock*)prev_view;
                            if (prev_block->bound->margin.bottom > 0 && block->bound->margin.top > 0) {
                                collapse = min(prev_block->bound->margin.bottom, block->bound->margin.top);
                                block->y -= collapse;
                                block->bound->margin.top -= collapse;
                                log_debug("collapsed margin between sibling blocks: %f, block->y now: %f", collapse, block->y);
                            }
                        }
                    } else {
                        log_debug("skipping sibling margin collapsing for element with clear property");
                    }
                }
                lycon->block.advance_y += block->height + block->bound->margin.top + block->bound->margin.bottom;
                // Include lycon->line.left to account for parent's left border+padding
                lycon->block.max_width = max(lycon->block.max_width, lycon->line.left + block->width
                    + block->bound->margin.left + block->bound->margin.right);
            } else {
                lycon->block.advance_y += block->height;
                // Include lycon->line.left to account for parent's left border+padding
                lycon->block.max_width = max(lycon->block.max_width, lycon->line.left + block->width);
            }
            // For non-float blocks, we should be at line start after the block
            // (floats are handled above and don't require this assertion)
            if (!is_float) {
                assert(lycon->line.is_line_start);
            }
            log_debug("block end, pa max_width: %f, pa advance_y: %f, block hg: %f",
                lycon->block.max_width, lycon->block.advance_y, block->height);
        }

        // apply CSS relative positioning after normal layout
        if (block->position && block->position->position == CSS_VALUE_RELATIVE) {
            log_debug("Applying relative positioning");
            layout_relative_positioned(lycon, block);
        }
    }

    // Pop counter scope when leaving this block
    if (lycon->counter_context) {
        counter_pop_scope(lycon->counter_context);
    }

    log_leave();

    auto t_block_end = high_resolution_clock::now();
    g_block_layout_time += duration<double, std::milli>(t_block_end - t_block_start).count();
    g_block_layout_count++;
}
