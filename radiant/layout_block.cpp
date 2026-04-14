#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"
#include "layout_flex_multipass.hpp"
#include "layout_grid_multipass.hpp"
#include "layout_multicol.hpp"
#include "layout_positioned.hpp"
#include "intrinsic_sizing.hpp"
#include "layout_cache.hpp"
#include "layout_counters.hpp"
#include "layout_list.hpp"
#include "layout_table.hpp"
#include "grid.hpp"
#include "form_control.hpp"
#include "render_svg_inline.hpp"

#include "../lib/log.h"
#include "../lib/strbuf.h"
#include "../lib/font/font.h"
#include "../lambda/input/input.hpp"

#include "../lambda/input/css/selector_matcher.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include <utf8proc.h>
#include <chrono>
#include <cfloat>
using namespace std::chrono;

// Check if a view element is a descendant of another view element
static bool view_is_descendant_of(ViewElement* child, ViewElement* ancestor) {
    ViewElement* walker = child->parent_view();
    while (walker) {
        if (walker == ancestor) return true;
        walker = walker->parent_view();
    }
    return false;
}

// CSS 2.1 §8.3.1: Collapse two margins according to spec rules
// - Both positive: max(a, b)
// - Both negative: min(a, b) — most negative
// - Mixed signs: a + b — algebraic sum
static inline float collapse_margins(float a, float b) {
    if (a >= 0 && b >= 0) return max(a, b);
    if (a < 0 && b < 0) return min(a, b);
    return a + b;
}

// CSS 2.1 §8.3.1: Retrieve margin chain components from a block's margin.bottom.
// When margin.bottom resulted from collapsing multiple margins (through self-collapsing
// elements), the chain fields track the max positive and most negative individual margins.
// This allows correct multi-way collapse — without it, intermediate scalar results lose
// information (e.g., collapse(+16,-16)=0 then collapse(0,+16)=16 instead of correct 0).
static inline void get_margin_chain(ViewBlock* block, float* out_pos, float* out_neg) {
    if (!block || !block->bound) { *out_pos = 0; *out_neg = 0; return; }
    if (block->bound->margin_chain_positive != 0 || block->bound->margin_chain_negative != 0) {
        *out_pos = block->bound->margin_chain_positive;
        *out_neg = block->bound->margin_chain_negative;
    } else {
        *out_pos = max(block->bound->margin.bottom, 0.f);
        *out_neg = min(block->bound->margin.bottom, 0.f);
    }
}

// CSS 2.1 §8.3.1: Store a margin value along with its chain components.
// The scalar (positive + negative) goes in margin.bottom; the components are
// preserved for future multi-way collapse.
static inline void set_margin_chain(BoundaryProp* bound, float positive, float negative) {
    bound->margin.bottom = positive + negative;
    bound->margin_chain_positive = positive;
    bound->margin_chain_negative = negative;
}

// CSS 2.1 §8.3.1: Get chain components for a single margin value (not chained yet).
static inline void margin_to_chain(float margin, float* out_pos, float* out_neg) {
    *out_pos = max(margin, 0.f);
    *out_neg = min(margin, 0.f);
}

// CSS 2.1 §8.3.1: Check if a block's margin chain has non-trivial components
// (i.e., it was built from collapsing multiple margins, not a simple scalar).
static inline bool has_margin_chain(BoundaryProp* bound) {
    return bound && (bound->margin_chain_positive != 0 || bound->margin_chain_negative != 0);
}

// Quirks mode: check if an element has "quirky" block-start margin.
// In quirks mode, certain HTML elements have their UA default margins ignored
// when collapsing with a quirky container (body, table cell). This matches
// Chromium's HasMarginBlockStartQuirk / quirky container behavior.
// A margin is quirky when: (1) element is in the quirky list, AND
// (2) margin-top still has UA specificity (not overridden by author CSS).
static inline bool has_quirky_margin_top(ViewBlock* block) {
    if (!block || !block->bound || block->bound->margin.top_specificity >= 0)
        return false;
    uintptr_t tag = block->tag_id;
    return tag == HTM_TAG_P || tag == HTM_TAG_H1 || tag == HTM_TAG_H2 ||
           tag == HTM_TAG_H3 || tag == HTM_TAG_H4 || tag == HTM_TAG_H5 ||
           tag == HTM_TAG_H6 || tag == HTM_TAG_UL || tag == HTM_TAG_OL ||
           tag == HTM_TAG_BLOCKQUOTE || tag == HTM_TAG_PRE ||
           tag == HTM_TAG_DL || tag == HTM_TAG_FIGURE || tag == HTM_TAG_HR ||
           tag == HTM_TAG_FIELDSET || tag == HTM_TAG_MENU || tag == HTM_TAG_DIR;
}

// Same check for block-end (bottom) margin.
static inline bool has_quirky_margin_bottom(ViewBlock* block) {
    if (!block || !block->bound || block->bound->margin.bottom_specificity >= 0)
        return false;
    uintptr_t tag = block->tag_id;
    return tag == HTM_TAG_P || tag == HTM_TAG_H1 || tag == HTM_TAG_H2 ||
           tag == HTM_TAG_H3 || tag == HTM_TAG_H4 || tag == HTM_TAG_H5 ||
           tag == HTM_TAG_H6 || tag == HTM_TAG_UL || tag == HTM_TAG_OL ||
           tag == HTM_TAG_BLOCKQUOTE || tag == HTM_TAG_PRE ||
           tag == HTM_TAG_DL || tag == HTM_TAG_FIGURE || tag == HTM_TAG_HR ||
           tag == HTM_TAG_FIELDSET || tag == HTM_TAG_MENU || tag == HTM_TAG_DIR;
}

// Check if a block is a "quirky container" — in quirks mode, body and table cells
// ignore quirky margins from their children during margin collapse.
static inline bool is_quirky_container(ViewBlock* block, LayoutContext* lycon) {
    if (!block || !lycon->doc || !lycon->doc->view_tree) return false;
    if (!is_quirks_mode(lycon->doc->view_tree->html_version)) return false;
    return block->tag_id == HTM_TAG_BODY;
}

// CSS 2.1 §10.6.4: When an ancestor block's y changes after its absolutely positioned
// descendants have already had their static positions computed (e.g., due to margin
// collapse), the descendants' positions must be updated by the same delta.
// This walks the DOM subtree, adjusting abs/fixed children. Recursion stops at
// abs/fixed elements since their descendants use a different containing block path.
static void adjust_abs_descendants_y(ViewElement* parent, float delta) {
    View* child = parent->first_child;
    while (child) {
        if (child->is_block()) {
            ViewBlock* vb = (ViewBlock*)child;
            bool is_positioned = vb->position &&
                vb->position->position != CSS_VALUE_STATIC;
            if (is_positioned) {
                bool is_abs_fixed = vb->position->position == CSS_VALUE_ABSOLUTE ||
                                    vb->position->position == CSS_VALUE_FIXED;
                if (is_abs_fixed && !vb->position->has_top && !vb->position->has_bottom) {
                    // Static-position abs/fixed element whose parent-to-CB walk
                    // includes the ancestor that was adjusted → needs correction
                    vb->y += delta;
                    log_debug("%s [ABS ADJUST] Adjusted abs-pos %s y by %f to %f", parent->source_loc(),
                              vb->node_name(), delta, vb->y);
                }
                // Don't recurse past ANY positioned element (relative/absolute/fixed):
                // it establishes a containing block for its abs-pos descendants,
                // so their coordinates are relative to it, not the adjusted ancestor
            } else {
                // Non-positioned (static): abs-pos descendants beneath may still
                // reference the adjusted ancestor in their parent-to-CB walk
                adjust_abs_descendants_y((ViewElement*)vb, delta);
            }
        }
        child = (View*)child->next_sibling;
    }
}

// DEBUG: Global for tracking table height between calls
// WORKAROUND: Table height gets corrupted between layout_block_content return and caller
// This is a mysterious issue that needs further investigation
static float g_layout_table_height = 0;

// Thread-local iframe depth counter to prevent infinite recursion
// (e.g., <iframe src="index.html"> loading itself)
// Shared between layout_block.cpp and layout_flex_multipass.cpp
__thread int iframe_depth = 0;

// External timing accumulators from layout.cpp
extern double g_table_layout_time;
extern double g_flex_layout_time;
extern double g_grid_layout_time;
extern double g_block_layout_time;
extern int64_t g_block_layout_count;

View* layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);
extern "C" void process_document_font_faces(UiContext* uicon, DomDocument* doc);
// void layout_flex_nodes(LayoutContext* lycon, lxb_dom_node_t *first_child);  // Removed: lexbor dependency
void resolve_inline_default(LayoutContext* lycon, ViewSpan* span);
void dom_node_resolve_style(DomNode* node, LayoutContext* lycon);
void layout_table_content(LayoutContext* lycon, DomNode* elmt, DisplayValue display);
void layout_flex_content(LayoutContext* lycon, ViewBlock* block);
void layout_form_control(LayoutContext* lycon, ViewBlock* block);
void layout_abs_block(LayoutContext* lycon, DomNode *elmt, ViewBlock* block, BlockContext *pa_block, Linebox *pa_line);

// CSS 2.1 Section 17.2.1: Wrap orphaned table-internal children in anonymous table structures
bool wrap_orphaned_table_children(LayoutContext* lycon, DomElement* parent);
bool is_table_internal_display(CssEnum display);


// Forward declaration for self-collapsing block check (used by compute_collapsible_bottom_margin)
static bool is_block_self_collapsing(ViewBlock* vb);



// ============================================================================
// Math Element Detection and Layout Support
// ============================================================================

/**
 * Check if an element is a display math element (has class "math display").
 * Returns: true if display math, false otherwise.
 */
static bool is_display_math_element(DomElement* elem) {
    if (!elem) return false;

    // check for class="math display"
    return dom_element_has_class(elem, "math") && dom_element_has_class(elem, "display");
}

/**
 * Layout a display math element.
 *
 * NOTE: The legacy MathLive pipeline has been removed. Math elements using
 * the old MathBox-based approach should migrate to RDT_VIEW_TEXNODE.
 * For now, this function is a stub that logs a warning.
 *
 * To enable math rendering, use the unified TeX pipeline:
 *   1. Parse LaTeX with tex::typeset_latex_math()
 *   2. Set elem->view_type = RDT_VIEW_TEXNODE
 *   3. Set elem->tex_root = tex_node
 */
static void layout_display_math_block(LayoutContext* lycon, DomElement* elem) {
    log_debug("%s layout_display_math_block: MathLive pipeline removed - use RDT_VIEW_TEXNODE instead", elem->source_loc());
    // TODO: Implement using unified TeX pipeline
    // For now, skip math rendering
    (void)lycon;
    (void)elem;
}

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

    // IMPORTANT: Do NOT share parent's FontProp pointer with pseudo-element!
    // If we set pseudo_elem->font = parent->font, then when the pseudo-element's
    // font-size (e.g., 1.2em) is resolved, it would modify the shared FontProp,
    // incorrectly changing the parent's font-size as well.
    // Instead, leave pseudo_elem->font = nullptr so that style resolution will
    // allocate a new FontProp via alloc_font_prop(), which properly copies from
    // lycon->font.style (the parent's computed font values).
    pseudo_elem->font = nullptr;

    // Log that font will be allocated during style resolution
    log_debug("[PSEUDO FONT] %s font=nullptr (will be allocated during style resolution)",
              is_before ? "::before" : "::after");

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
        log_info("%s [PSEUDO] Creating text node for pseudo-element, content_len=%zu, first_byte=0x%02x", parent->source_loc(),
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
                log_info("%s [PSEUDO] Text node created with content_len=%zu, bytes=[%02x %02x %02x]", parent->source_loc(),
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
        log_info("%s [PSEUDO] NOT creating text node: content=%p, first_byte=%s", parent->source_loc(),
            (void*)content, content ? ((*content) ? "nonzero" : "ZERO") : "NULL");
    }

    log_debug("%s [PSEUDO] Created ::%s element for <%s> with content \"%s\", display.outer=%d", parent->source_loc(),
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
    // But if the pseudo was created by marker code, ::before/::after may still need creation
    if (block->pseudo && block->pseudo->before_generated && block->pseudo->after_generated) {
        log_debug("[PSEUDO] Reusing existing pseudo-elements for <%s>",
                  elem->tag_name ? elem->tag_name : "unknown");
        return block->pseudo;
    }

    // Check if element has ::before or ::after content
    bool has_before = dom_element_has_before_content(elem);
    bool has_after = dom_element_has_after_content(elem);

    log_debug("[PSEUDO] Checking <%s>: has_before=%d, has_after=%d, before_styles=%p",
              elem->tag_name ? elem->tag_name : "?", has_before, has_after, (void*)elem->before_styles);

    if (!has_before && !has_after) return block->pseudo;  // Return existing (may have marker) or nullptr

    // Reuse existing PseudoContentProp if already allocated (e.g., by marker creation code)
    PseudoContentProp* pseudo = block->pseudo;
    if (!pseudo) {
        pseudo = (PseudoContentProp*)alloc_prop(lycon, sizeof(PseudoContentProp));
        if (!pseudo) return nullptr;
        memset(pseudo, 0, sizeof(PseudoContentProp));
    }

    // Create ::before pseudo-element if needed and not already created
    // Note: Even empty content "" creates a pseudo-element for layout purposes (e.g., clearfix)
    if (has_before && !pseudo->before_generated) {
        log_info("%s [PSEUDO] Getting before content for <%s>", block->source_loc(), elem->tag_name ? elem->tag_name : "?");

        // CSS 2.1 §12.4: Apply counter operations from ::before styles in a
        // temporary sub-scope, then resolve content. The sub-scope ensures that
        // counter-reset on ::before doesn't leak into the element's scope
        // (it should only be visible to ::before's own content resolution).
        bool pushed_pseudo_scope = false;
        if (lycon->counter_context && elem->before_styles) {
            counter_push_scope(lycon->counter_context);
            pushed_pseudo_scope = true;
            apply_pseudo_counter_ops(lycon, elem->before_styles);
        }

        const char* before_content = nullptr;
        if (lycon->counter_context) {
            log_info("%s [PSEUDO] Calling get_pseudo_element_content_with_counters", block->source_loc());
            before_content = dom_element_get_pseudo_element_content_with_counters(
                elem, PSEUDO_ELEMENT_BEFORE, lycon->counter_context, lycon->doc->arena);
            log_info("%s [PSEUDO] Returned from with_counters: %p, len=%zu, bytes=[%02x %02x %02x]", block->source_loc(),
                (void*)before_content,
                before_content ? strlen(before_content) : 0,
                before_content && strlen(before_content) > 0 ? (unsigned char)before_content[0] : 0,
                before_content && strlen(before_content) > 1 ? (unsigned char)before_content[1] : 0,
                before_content && strlen(before_content) > 2 ? (unsigned char)before_content[2] : 0);
        }
        if (!before_content) {
            log_info("%s [PSEUDO] Calling dom_element_get_pseudo_element_content", block->source_loc());
            before_content = dom_element_get_pseudo_element_content(elem, PSEUDO_ELEMENT_BEFORE);
            log_info("%s [PSEUDO] Returned: %p", block->source_loc(), (void*)before_content);
        }

        // Pop the temporary pseudo scope — counter-reset stays scoped,
        // counter-increment values propagate to parent
        if (pushed_pseudo_scope) {
            counter_pop_scope_propagate(lycon->counter_context, false);
        }

        // Debug: log what font we're passing to pseudo-element
        log_debug("%s [PSEUDO ALLOC] block->font=%p, elem->font=%p", block->source_loc(), (void*)block->font, (void*)elem->font);
        if (block->font && block->font->family) {
            log_debug("%s [PSEUDO ALLOC] Passing font '%s' (size %.1f) from ViewBlock", block->source_loc(),
                     block->font->family, block->font->font_size);
        } else if (block->font) {
            log_debug("%s [PSEUDO ALLOC] block->font exists but has no family", block->source_loc());
        } else {
            log_debug("%s [PSEUDO ALLOC] block->font is NULL", block->source_loc());
        }

        // Create pseudo-element even for empty content if display/clear properties are set
        // Pass block->font (from ViewBlock) for accurate font-family inheritance
        pseudo->before = create_pseudo_element(lycon, elem, before_content ? before_content : "", true, block->font);
        pseudo->before_generated = true;
        log_debug("%s [PSEUDO] Created ::before for <%s> with content='%s'", block->source_loc(),
                  elem->tag_name ? elem->tag_name : "?", before_content ? before_content : "(empty)");
    }

    // Create ::after pseudo-element if needed and not already created
    // Note: Even empty content "" creates a pseudo-element for layout purposes
    if (has_after && !pseudo->after_generated) {
        // NOTE: Unlike ::before, we do NOT apply pseudo counter ops for ::after
        // at this point. Per CSS spec, ::after counter operations (counter-increment,
        // counter-reset) should happen AFTER the element's children are laid out.
        // We only resolve the content string using the current counter values.
        // The ::after counter ops will be handled during the ::after layout phase.
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
        pseudo->after_generated = true;
        log_debug("%s [PSEUDO] Created ::after for <%s> with content='%s'", block->source_loc(),
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

    log_debug("%s [PSEUDO] Laying out %s content", pseudo_elem->source_loc(), pseudo_elem->tag_name);

    // Resolve CSS styles for the pseudo-element BEFORE layout
    // This ensures font-family and other properties from CSS are applied
    dom_node_resolve_style(pseudo_elem, lycon);

    // Layout the pseudo-element as inline (it will lay out its text child)
    layout_inline(lycon, pseudo_elem, pseudo_elem->display);
}

// ============================================================================
// End of Pseudo-element Layout Support
// ============================================================================

// ============================================================================
// ::first-letter Pseudo-element Support (CSS 2.1 §5.12.2)
// ============================================================================

/**
 * Check if a Unicode codepoint is in the punctuation classes that should be
 * included in ::first-letter (CSS 2.1 §5.12.2):
 * Ps (open), Pe (close), Pi (initial quote), Pf (final quote), Po (other)
 */
static bool is_first_letter_punctuation(utf8proc_int32_t codepoint) {
    utf8proc_category_t cat = utf8proc_category(codepoint);
    return cat == UTF8PROC_CATEGORY_PS ||  // open punctuation: ( [ {
           cat == UTF8PROC_CATEGORY_PE ||  // close punctuation: ) ] }
           cat == UTF8PROC_CATEGORY_PI ||  // initial quote: « " '
           cat == UTF8PROC_CATEGORY_PF ||  // final quote: » " '
           cat == UTF8PROC_CATEGORY_PO;    // other punctuation: ! @ # % & * , . / : ; ? \ etc.
}

/**
 * Find the byte length of the ::first-letter content in a UTF-8 string.
 * CSS 2.1 §5.12.2: first letter plus any preceding/following punctuation.
 * Returns 0 if no letter is found.
 */
static int find_first_letter_boundary(const unsigned char* text, int text_len) {
    if (!text || text_len <= 0) return 0;

    const unsigned char* p = text;
    const unsigned char* end = text + text_len;
    bool found_letter = false;
    const unsigned char* after_letter = nullptr;

    // Phase 1: skip leading whitespace (not part of first-letter)
    while (p < end) {
        unsigned char ch = *p;
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            p++;
        } else {
            break;
        }
    }
    const unsigned char* content_start = p;

    // Phase 2: consume leading punctuation + first letter + trailing punctuation
    while (p < end) {
        utf8proc_int32_t codepoint;
        utf8proc_ssize_t bytes = utf8proc_iterate(p, end - p, &codepoint);
        if (bytes <= 0) break;

        if (!found_letter) {
            // Before the letter: accept punctuation, looking for the first letter
            if (is_first_letter_punctuation(codepoint)) {
                p += bytes;  // include leading punctuation
            } else {
                // This should be the first letter
                p += bytes;
                found_letter = true;
                after_letter = p;
            }
        } else {
            // After the letter: include trailing punctuation
            if (is_first_letter_punctuation(codepoint)) {
                p += bytes;
                after_letter = p;
            } else {
                break;  // non-punctuation after letter — done
            }
        }
    }

    if (!found_letter) return 0;
    return (int)(after_letter - content_start); // INT_CAST_OK: pointer diff is character count
}

/**
 * Check if a text node's content is entirely whitespace.
 * Returns true for empty or whitespace-only content.
 */
static bool is_text_all_whitespace(const unsigned char* data) {
    if (!data) return true;
    while (*data) {
        unsigned char c = *data;
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\f') {
            return false;
        }
        data++;
    }
    return true;
}

/**
 * Find the first text node descendant of an element that contains letter content.
 * Walks depth-first, following inline elements.
 * Skips whitespace-only text nodes (CSS 2.1 §5.12.2: first-letter applies to
 * the first letter of the first formatted line, ignoring preceding whitespace).
 * Returns NULL if no text content is found.
 * Sets *suppressed = true if a replaced element or other non-text inline content
 * precedes the first text (CSS 2.1 §5.12.2: ::first-letter must not be created
 * if preceded by non-eligible content such as images on the first formatted line).
 */
static DomText* find_first_text_node(DomNode* node, bool* suppressed) {
    if (!node) return nullptr;

    if (node->is_text()) {
        DomText* text = node->as_text();
        unsigned char* data = node->text_data();
        // Skip empty or whitespace-only text nodes so we find the actual
        // letter content (which may be in a subsequent sibling or nested element)
        if (data && *data && !is_text_all_whitespace(data)) {
            return text;
        }
        return nullptr;
    }

    if (node->is_element()) {
        DomElement* elem = node->as_element();
        // CSS 2.1 §5.12.2: If a replaced element (image, video, etc.) or other
        // non-text inline content appears before the first letter, ::first-letter
        // must not be created. Check for replaced elements before recursing.
        // Note: At the time ::first-letter is created, child styles may not be
        // fully resolved yet. Check both display.inner (if resolved) and the
        // element's tag name for known replaced elements.
        uintptr_t tag = elem->tag();
        bool is_replaced = (elem->display.inner == RDT_DISPLAY_REPLACED) ||
            tag == HTM_TAG_IMG || tag == HTM_TAG_VIDEO || tag == HTM_TAG_CANVAS ||
            tag == HTM_TAG_IFRAME || tag == HTM_TAG_EMBED || tag == HTM_TAG_OBJECT ||
            tag == HTM_TAG_INPUT || tag == HTM_TAG_TEXTAREA || tag == HTM_TAG_SELECT ||
            tag == HTM_TAG_SVG || tag == HTM_TAG_BR;
        if (is_replaced) {
            if (suppressed) *suppressed = true;
            return nullptr;
        }
        // Only descend into inline-level elements, not blocks
        // (::first-letter of a block is the first letter of its first inline content)
        if (elem->display.outer != CSS_VALUE_INLINE &&
            elem->display.outer != CSS_VALUE_NONE &&
            elem != (DomElement*)node) {
            // It's a block-level child — first-letter comes from first formed line
            // For simplicity, skip block children (browser does too for nested blocks)
        }
        DomNode* child = elem->first_child;
        while (child) {
            DomText* result = find_first_text_node(child, suppressed);
            if (result) return result;
            if (suppressed && *suppressed) return nullptr;
            child = child->next_sibling;
        }
    }
    return nullptr;
}

/**
 * Create and insert a ::first-letter pseudo-element for a block element.
 * This extracts the first letter (+ punctuation) from the first text node,
 * wraps it in an inline pseudo-element with the first-letter styles,
 * and adjusts the original text node to skip those characters.
 */
static void create_first_letter_pseudo(LayoutContext* lycon, ViewBlock* block) {
    DomElement* elem = (DomElement*)block;
    if (!elem->first_letter_styles) return;

    // Find the first text node with content
    // CSS 2.1 §5.12.2: If non-eligible content (e.g., an image) precedes the first
    // letter, ::first-letter must not be created.
    bool suppressed = false;
    DomText* text_node = find_first_text_node(elem, &suppressed);
    if (!text_node || suppressed) {
        log_debug("%s [FIRST-LETTER] %s in <%s>", block->source_loc(),
                  suppressed ? "Suppressed by preceding replaced element" : "No text node found",
                  elem->tag_name ? elem->tag_name : "?");
        return;
    }

    unsigned char* text_data = text_node->text_data();
    if (!text_data || !*text_data) return;

    int text_len = (int)strlen((const char*)text_data); // INT_CAST_OK: string length

    // Skip leading whitespace to find content start
    const unsigned char* p = text_data;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (!*p) return;

    int ws_offset = (int)(p - text_data); // INT_CAST_OK: pointer diff is char offset
    int boundary = find_first_letter_boundary(p, text_len - ws_offset);
    if (boundary <= 0) {
        log_debug("%s [FIRST-LETTER] No letter found in text", block->source_loc());
        return;
    }

    log_debug("%s [FIRST-LETTER] Found boundary=%d bytes in '%.*s'", block->source_loc(), boundary, text_len, text_data);

    Pool* pool = lycon->doc->view_tree->pool;
    if (!pool) return;

    // Create the ::first-letter pseudo-element
    DomElement* fl_elem = (DomElement*)pool_calloc(pool, sizeof(DomElement));
    if (!fl_elem) return;

    fl_elem->node_type = DOM_NODE_ELEMENT;
    fl_elem->tag_name = "::first-letter";
    fl_elem->doc = elem->doc;
    fl_elem->parent = text_node->parent;  // same parent as the text node
    fl_elem->first_child = nullptr;
    fl_elem->next_sibling = nullptr;
    fl_elem->prev_sibling = nullptr;
    fl_elem->font = nullptr;  // will be allocated during style resolution
    fl_elem->bound = nullptr;
    fl_elem->in_line = nullptr;

    // Set display — default to inline, but check for float (CSS 2.1 §5.12.2)
    // If ::first-letter has float:left/right specified, it should become a floated
    // block-level box (CSS 2.1 §9.7 blockification)
    CssEnum fl_float_value = CSS_VALUE_NONE;
    if (elem->first_letter_styles && elem->first_letter_styles->tree) {
        AvlNode* fl_float_node = avl_tree_search(elem->first_letter_styles->tree, CSS_PROPERTY_FLOAT);
        if (fl_float_node) {
            StyleNode* fl_sn = (StyleNode*)fl_float_node->declaration;
            if (fl_sn && fl_sn->winning_decl && fl_sn->winning_decl->value &&
                fl_sn->winning_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                fl_float_value = fl_sn->winning_decl->value->data.keyword;
            }
        }
    }

    if (fl_float_value == CSS_VALUE_LEFT || fl_float_value == CSS_VALUE_RIGHT) {
        // CSS 2.1 §9.7: floated elements are blockified
        fl_elem->display.outer = CSS_VALUE_BLOCK;
        fl_elem->display.inner = CSS_VALUE_FLOW;

        // Allocate PositionProp so element_has_float() returns true
        fl_elem->position = alloc_position_prop(lycon);
        fl_elem->position->float_prop = fl_float_value;
        log_debug("%s [FIRST-LETTER] Float %s applied to ::first-letter", block->source_loc(),
                  fl_float_value == CSS_VALUE_LEFT ? "left" : "right");
    } else {
        fl_elem->display.outer = CSS_VALUE_INLINE;
        fl_elem->display.inner = CSS_VALUE_FLOW;
    }

    // Assign the first-letter styles for CSS resolution
    fl_elem->specified_style = elem->first_letter_styles;

    // Create text content for the first-letter pseudo-element
    char* fl_text = (char*)pool_calloc(pool, boundary + 1);
    if (!fl_text) return;
    memcpy(fl_text, p, boundary);
    fl_text[boundary] = '\0';

    DomText* fl_text_node = (DomText*)pool_calloc(pool, sizeof(DomText));
    if (!fl_text_node) return;
    fl_text_node->node_type = DOM_NODE_TEXT;
    fl_text_node->parent = fl_elem;
    fl_text_node->text = fl_text;
    fl_text_node->length = boundary;
    fl_text_node->native_string = nullptr;
    fl_text_node->content_type = DOM_TEXT_STRING;
    fl_text_node->next_sibling = nullptr;
    fl_text_node->prev_sibling = nullptr;

    fl_elem->first_child = fl_text_node;

    // Adjust the original text node to skip the first-letter characters
    int skip = ws_offset + boundary;
    text_node->text = text_node->text + skip;
    text_node->length = text_node->length > (size_t)skip ? text_node->length - skip : 0;

    // Insert the ::first-letter pseudo-element before the original text node
    DomNode* text_parent = text_node->parent;
    fl_elem->parent = text_parent;
    fl_elem->next_sibling = text_node;
    fl_elem->prev_sibling = text_node->prev_sibling;
    if (text_node->prev_sibling) {
        text_node->prev_sibling->next_sibling = fl_elem;
    } else if (text_parent && text_parent->is_element()) {
        // text_node was the first child
        ((DomElement*)text_parent)->first_child = fl_elem;
    }
    text_node->prev_sibling = fl_elem;

    log_debug("%s [FIRST-LETTER] Created ::first-letter pseudo for <%s> with content '%s', remaining='%s'", block->source_loc(),
              elem->tag_name ? elem->tag_name : "?", fl_text,
              text_node->text ? text_node->text : "(null)");
}

// ============================================================================
// End of ::first-letter Pseudo-element Support
// ============================================================================

// CSS 2.1 §10.6.3 + §8.3.1 + erratum q313: Compute the amount of bottom margin
// that must be excluded from auto height BEFORE min/max-height constraints.
//
// Two cases:
// 1. No border/padding: Last child's bottom margin collapses with parent's.
//    Exclude the full margin (it will become the parent's margin).
// 2. Has border/padding: Per §10.6.3 "However" clause, content ends at the
//    last child's margin-edge. But only the child's OWN margin counts — any
//    margin collapsed-through from grandchildren should NOT inflate the parent's
//    auto height. Exclude only the collapsed-through portion.
static float compute_collapsible_bottom_margin(ViewBlock* block) {
    // CSS 2.1 §8.3.1: Root element margins do not collapse.
    if (!block->parent || !block->parent->is_block()) return 0;

    // CSS Box 4 §3.1: When margin-trim:block-end is set, the last child's
    // block-end margin is trimmed, so no margin escapes through the parent's
    // bottom edge.
    if (block->blk && (block->blk->margin_trim & MARGIN_TRIM_BLOCK_END)) return 0;

    // Bottom margin collapse requires: no bottom border, no bottom padding
    bool has_border_bottom = block->bound && block->bound->border && block->bound->border->width.bottom > 0;
    bool has_padding_bottom = block->bound && block->bound->padding.bottom > 0;

    if (has_border_bottom || has_padding_bottom) {
        // CSS 2.1 §10.6.3 "However" clause: content ends at last child's
        // margin-edge, using the child's OWN margin. Exclude only the portion
        // that was collapsed-through from grandchildren.
        if (!block->first_child) return 0;
        View* last_in_flow = nullptr;
        View* child = (View*)block->first_child;
        while (child) {
            if (child->view_type && child->is_block()) {
                ViewBlock* vb = (ViewBlock*)child;
                bool is_inline_block = (vb->view_type == RDT_VIEW_INLINE_BLOCK);
                bool is_out_of_flow = is_inline_block || (vb->position &&
                    (vb->position->position == CSS_VALUE_ABSOLUTE ||
                     vb->position->position == CSS_VALUE_FIXED ||
                     element_has_float(vb)));
                if (!is_out_of_flow) last_in_flow = child;
            } else if (child->view_type) {
                last_in_flow = child;
            }
            child = (View*)child->next_sibling;
        }
        if (!last_in_flow || !last_in_flow->is_block()) return 0;
        ViewBlock* last = (ViewBlock*)last_in_flow;
        float collapsed_through = last->bound ? last->bound->collapsed_through_mb : 0;
        return collapsed_through;
    }

    // BFC roots don't collapse margins with children
    bool creates_bfc = block->scroller &&
        (block->scroller->overflow_x != CSS_VALUE_VISIBLE ||
         block->scroller->overflow_y != CSS_VALUE_VISIBLE);
    if (!creates_bfc && block->position) {
        if (element_has_float(block) ||
            block->position->position == CSS_VALUE_ABSOLUTE ||
            block->position->position == CSS_VALUE_FIXED) {
            creates_bfc = true;
        }
    }
    if (!creates_bfc && block->view_type == RDT_VIEW_INLINE_BLOCK) creates_bfc = true;
    // CSS Multicol §3: Multi-column containers establish a new BFC
    if (!creates_bfc && is_multicol_container(block)) creates_bfc = true;
    if (creates_bfc) return 0;

    // Explicit height prevents bottom margin collapse
    if (block->blk && block->blk->given_height >= 0) return 0;
    if (!block->first_child) return 0;

    // Find last in-flow child (skip abspos, floats, inline-blocks)
    View* last_in_flow = nullptr;
    View* child = (View*)block->first_child;
    while (child) {
        if (child->view_type && child->is_block()) {
            ViewBlock* vb = (ViewBlock*)child;
            bool is_inline_block = (vb->view_type == RDT_VIEW_INLINE_BLOCK);
            bool is_out_of_flow = is_inline_block || (vb->position &&
                (vb->position->position == CSS_VALUE_ABSOLUTE ||
                 vb->position->position == CSS_VALUE_FIXED ||
                 element_has_float(vb)));
            if (!is_out_of_flow) last_in_flow = child;
        } else if (child->view_type) {
            last_in_flow = child;
        }
        child = (View*)child->next_sibling;
    }

    // Skip self-collapsing blocks with zero margin at the end.
    // CSS 2.1 §9.2.1.1: Inline content between/after block children is wrapped
    // in anonymous blocks. If the inline content has zero height (no text, no
    // replaced elements, no inline elements with substance), the implicit
    // anonymous block is self-collapsing and doesn't prevent margin collapse.
    View* effective_last = last_in_flow;
    while (effective_last) {
        if (effective_last->is_block()) {
            ViewBlock* vb = (ViewBlock*)effective_last;
            float mb = vb->bound ? vb->bound->margin.bottom : 0;
            bool has_chain = vb->bound && has_margin_chain(vb->bound);
            if (mb == 0 && !has_chain && is_block_self_collapsing(vb)) {
                effective_last = effective_last->prev_placed_view();
                continue;
            }
            break;
        } else {
            // Inline/text with zero height = empty anonymous block wrapper
            if (effective_last->height <= 0) {
                effective_last = effective_last->prev_placed_view();
                continue;
            }
            break;
        }
    }

    if (!effective_last || !effective_last->is_block()) return 0;
    ViewBlock* last = (ViewBlock*)effective_last;
    // CSS 2.1 §8.3.1: Bottom margins collapse regardless of sign.
    // Negative margins participate in collapsing (result = max_positive + min_negative).
    if (!last->bound || (last->bound->margin.bottom == 0 && !has_margin_chain(last->bound))) return 0;

    // Check for in-flow content after the last block child
    // (content that separates the child's margin from the parent's margin)
    View* sibling = (View*)effective_last->next_sibling;
    while (sibling) {
        if (sibling->view_type) {
            if (sibling->is_block()) {
                ViewBlock* sb = (ViewBlock*)sibling;
                bool is_truly_out_of_flow = sb->position &&
                    (sb->position->position == CSS_VALUE_ABSOLUTE ||
                     sb->position->position == CSS_VALUE_FIXED ||
                     element_has_float(sb));
                bool is_inline_level = (sb->view_type == RDT_VIEW_INLINE_BLOCK);
                if (is_inline_level) return 0;  // content separates margins
                if (!is_truly_out_of_flow && sb->height > 0) return 0;
            } else {
                // CSS 2.1 §9.2.1.1: Zero-height inline/text = empty anonymous
                // block wrapper; doesn't separate margins
                if (sibling->height > 0) return 0;
            }
        }
        sibling = (View*)sibling->next_sibling;
    }

    // CSS 2.1 §8.3.1: If the last child's margin chain includes a self-collapsing
    // element with clearance, that margin must NOT collapse with the parent's
    // bottom margin. Return 0 to prevent the collapse.
    if (last->bound->clearance_in_margin_chain) {
        log_debug("[CLEARANCE] Last child has clearance_in_margin_chain — not collapsing with parent bottom margin");
        return 0;
    }

    return last->bound->margin.bottom;
}

// ============================================================================
// CSS Inline Level 3 §5: text-box-trim
// ============================================================================

// Compute the half-leading (lead_y) for a block element from its font metrics.
// Returns 0 if the block has no font or line-height information.
static float compute_block_lead_y(ViewBlock* block) {
    if (!block->font || !block->font->font_handle) return 0;

    // Get ascender/descender, preferring OS/2 typo metrics when USE_TYPO_METRICS is set
    float ascender, descender;
    TypoMetrics typo = get_os2_typo_metrics(block->font->font_handle);
    if (typo.valid && typo.use_typo_metrics) {
        ascender = typo.ascender;
        descender = typo.descender;
    } else {
        ascender = block->font->ascender;
        descender = block->font->descender;
    }

    // Resolve line-height: walk up parent chain to find inherited value
    float line_height;
    const CssValue* lh = nullptr;
    ViewElement* ancestor = (ViewElement*)block;
    while (ancestor) {
        if (ancestor->blk && ancestor->blk->line_height) {
            lh = ancestor->blk->line_height;
            if (lh->type == CSS_VALUE_TYPE_KEYWORD && lh->data.keyword == CSS_VALUE_INHERIT) {
                ancestor = ancestor->parent_view();
                continue;
            }
            break;
        }
        ancestor = ancestor->parent_view();
    }

    if (lh) {
        if (lh->type == CSS_VALUE_TYPE_KEYWORD && lh->data.keyword == CSS_VALUE_NORMAL) {
            line_height = calc_normal_line_height(block->font->font_handle);
        } else if (lh->type == CSS_VALUE_TYPE_NUMBER) {
            line_height = lh->data.number.value * block->font->font_size;
        } else if (lh->type == CSS_VALUE_TYPE_LENGTH) {
            line_height = (float)lh->data.length.value;
            if (lh->data.length.unit == CSS_UNIT_EM) {
                line_height *= block->font->font_size;
            }
        } else {
            line_height = calc_normal_line_height(block->font->font_handle);
        }
    } else {
        line_height = calc_normal_line_height(block->font->font_handle);
    }

    float lead_y = max(0.0f, (line_height - (ascender + descender)) / 2);
    log_debug("[text-box-trim] compute_block_lead_y: asc=%.1f, desc=%.1f, lh=%.1f, lead_y=%.1f",
              ascender, descender, line_height, lead_y);
    return lead_y;
}

// Check if a block view is out-of-flow (absolutely positioned, fixed, or floated)
static bool is_out_of_flow_block(ViewBlock* vb) {
    if (vb->position && (vb->position->position == CSS_VALUE_ABSOLUTE ||
                          vb->position->position == CSS_VALUE_FIXED)) {
        return true;
    }
    if (element_has_float(vb)) return true;
    return false;
}

// Find the first in-flow block child inside an inline wrapper (block-in-inline).
// In Radiant, a <span> containing a <div> remains as RDT_VIEW_INLINE with a
// block child, rather than being split into anonymous blocks per CSS 2.1 §9.2.1.1.
// Returns null if the inline view has no block children.
static ViewBlock* find_first_block_in_inline(View* inline_view) {
    if (inline_view->view_type != RDT_VIEW_INLINE) return nullptr;
    ViewElement* span = (ViewElement*)inline_view;
    View* child = span->first_placed_child();
    while (child) {
        if (child->is_block()) {
            ViewBlock* vb = (ViewBlock*)child;
            if (!is_out_of_flow_block(vb)) return vb;
        }
        child = child->next();
    }
    return nullptr;
}

// Find the last in-flow block child inside an inline wrapper (block-in-inline).
static ViewBlock* find_last_block_in_inline(View* inline_view) {
    if (inline_view->view_type != RDT_VIEW_INLINE) return nullptr;
    ViewElement* span = (ViewElement*)inline_view;
    View* child = span->first_placed_child();
    ViewBlock* last = nullptr;
    while (child) {
        if (child->is_block()) {
            ViewBlock* vb = (ViewBlock*)child;
            if (!is_out_of_flow_block(vb)) last = vb;
        }
        child = child->next();
    }
    return last;
}

// Check whether any inline/text views exist before the first in-flow block child.
// If so, an implicit anonymous block would wrap them per CSS 2.1 §9.2.1.1.
// Inline wrappers containing blocks (block-in-inline) are treated as block children.
static bool has_any_inline_before_first_block(ViewBlock* container) {
    View* child = container->first_placed_child();
    while (child) {
        if (child->is_block()) {
            ViewBlock* vb = (ViewBlock*)child;
            if (!is_out_of_flow_block(vb)) {
                return false; // hit first in-flow block without finding inline content
            }
            child = child->next();
            continue;
        }
        if (child->view_type == RDT_VIEW_INLINE) {
            // An inline wrapping a block is effectively a block, not inline content
            if (find_first_block_in_inline(child)) {
                return false; // block-in-inline acts as first block
            }
            return true;
        }
        if (child->view_type == RDT_VIEW_TEXT) {
            return true;
        }
        child = child->next();
    }
    return false;
}

// Check whether any inline/text views exist after the last in-flow block child.
// Inline wrappers containing blocks (block-in-inline) are treated as block children.
static bool has_any_inline_after_last_block(ViewBlock* container) {
    View* child = container->first_placed_child();
    View* last_block = nullptr;
    while (child) {
        if (child->is_block()) {
            ViewBlock* vb = (ViewBlock*)child;
            if (!is_out_of_flow_block(vb)) {
                last_block = child;
            }
        } else if (child->view_type == RDT_VIEW_INLINE && find_first_block_in_inline(child)) {
            // Block-in-inline acts as a block child
            last_block = child;
        }
        child = child->next();
    }
    if (!last_block) return false;

    child = last_block->next();
    while (child) {
        if (child->view_type == RDT_VIEW_TEXT) {
            return true;
        }
        if (child->view_type == RDT_VIEW_INLINE && !find_first_block_in_inline(child)) {
            return true; // pure inline after last block
        }
        child = child->next();
    }
    return false;
}

// Check whether inline content before the first block child has real content
// (non-whitespace text or non-empty inline elements that generate line boxes).
// Inline wrappers containing blocks (block-in-inline) are treated as block children.
static bool has_inline_content_before_first_block(ViewBlock* container) {
    View* child = container->first_placed_child();
    while (child) {
        if (child->is_block()) {
            ViewBlock* vb = (ViewBlock*)child;
            if (!is_out_of_flow_block(vb)) {
                return false;
            }
            child = child->next();
            continue;
        }
        if (child->view_type == RDT_VIEW_INLINE && find_first_block_in_inline(child)) {
            return false; // block-in-inline acts as first block
        }
        if (child->view_type == RDT_VIEW_TEXT && child->width > 0) return true;
        if (child->view_type == RDT_VIEW_INLINE && (child->width > 0 || child->height > 0)) return true;
        child = child->next();
    }
    return false;
}

// Check whether inline content after the last block child has real content.
// Inline wrappers containing blocks (block-in-inline) are treated as block children.
static bool has_inline_content_after_last_block(ViewBlock* container) {
    View* child = container->first_placed_child();
    View* last_block = nullptr;
    while (child) {
        if (child->is_block()) {
            ViewBlock* vb = (ViewBlock*)child;
            if (!is_out_of_flow_block(vb)) {
                last_block = child;
            }
        } else if (child->view_type == RDT_VIEW_INLINE && find_first_block_in_inline(child)) {
            last_block = child; // block-in-inline acts as a block
        }
        child = child->next();
    }
    if (!last_block) return false;

    child = last_block->next();
    while (child) {
        if (child->view_type == RDT_VIEW_TEXT && child->width > 0) return true;
        if (child->view_type == RDT_VIEW_INLINE && !find_first_block_in_inline(child) &&
            (child->width > 0 || child->height > 0)) return true;
        child = child->next();
    }
    return false;
}

// Find the block containing the first formatted line, following CSS Inline 3 §5 rules.
// For a block container with block-level content, the first formatted line is
// the first formatted line of its first in-flow block-level child.
// Also handles block-in-inline: an inline wrapper (span) containing a block child.
// Returns nullptr if no first formatted line exists.
static ViewBlock* find_first_formatted_line_block(ViewBlock* container) {
    // Check if container has any in-flow block children (direct or via inline wrapper).
    bool has_block_child = false;
    View* child = container->first_placed_child();
    while (child) {
        if (child->is_block()) {
            ViewBlock* vb = (ViewBlock*)child;
            if (!is_out_of_flow_block(vb)) {
                has_block_child = true;
                break;
            }
        } else if (find_first_block_in_inline(child)) {
            has_block_child = true;
            break;
        }
        child = child->next();
    }

    if (!has_block_child) {
        // Pure inline content container: check if there's any content
        child = container->first_placed_child();
        while (child) {
            if (child->view_type == RDT_VIEW_TEXT || child->view_type == RDT_VIEW_INLINE) {
                return container;
            }
            child = child->next();
        }
        return nullptr; // empty container
    }

    // Container has block children. Per CSS 2.1 §9.2.1.1, inline content
    // alongside block children creates anonymous block boxes.
    //
    // Per CSS Inline 3 §5: "The first formatted line of a block container
    // that contains block-level content is the first formatted line of its
    // first in-flow block-level child."
    //
    // If there's any inline content before the first explicit block child,
    // an anonymous block wraps it — and THAT anonymous block is the first
    // in-flow block-level child. If it has real content → return container.
    // If it has no formatted line (whitespace-only) → no first formatted line.
    if (has_any_inline_before_first_block(container)) {
        if (has_inline_content_before_first_block(container)) {
            return container; // anonymous block has real content
        }
        return nullptr; // anonymous block has no formatted line
    }

    // No inline content before first block child — walk to it and recurse.
    // Also handles block-in-inline: recurse into blocks found inside inline wrappers.
    child = container->first_placed_child();
    while (child) {
        if (child->is_block()) {
            ViewBlock* vb = (ViewBlock*)child;
            if (is_out_of_flow_block(vb)) {
                child = child->next();
                continue;
            }
            return find_first_formatted_line_block(vb);
        }
        ViewBlock* bii = find_first_block_in_inline(child);
        if (bii) {
            return find_first_formatted_line_block(bii);
        }
        child = child->next();
    }
    return nullptr;
}

// Find the block containing the last formatted line.
// Returns nullptr if no last formatted line exists.
static ViewBlock* find_last_formatted_line_block(ViewBlock* container) {
    // Check if container has any in-flow block children (direct or via inline wrapper).
    bool has_block_child = false;
    View* child = container->first_placed_child();
    while (child) {
        if (child->is_block()) {
            ViewBlock* vb = (ViewBlock*)child;
            if (!is_out_of_flow_block(vb)) {
                has_block_child = true;
                break;
            }
        } else if (find_first_block_in_inline(child)) {
            has_block_child = true;
            break;
        }
        child = child->next();
    }

    if (!has_block_child) {
        // Pure inline content container
        child = container->first_placed_child();
        while (child) {
            if (child->view_type == RDT_VIEW_TEXT || child->view_type == RDT_VIEW_INLINE) {
                return container;
            }
            child = child->next();
        }
        return nullptr;
    }

    // Container has block children. Check if inline content after last block
    // forms an implicit anonymous block (which would be the last in-flow
    // block-level child per CSS 2.1 §9.2.1.1).
    if (has_any_inline_after_last_block(container)) {
        if (has_inline_content_after_last_block(container)) {
            return container; // anonymous block after last explicit block has content
        }
        return nullptr; // anonymous block has no formatted line
    }

    // Find last in-flow block child and recurse.
    // Also handles block-in-inline: check inline wrappers for block children.
    View* last_in_flow = nullptr;
    bool last_is_in_inline = false;
    child = container->first_placed_child();
    while (child) {
        if (child->is_block()) {
            ViewBlock* vb = (ViewBlock*)child;
            if (!is_out_of_flow_block(vb)) {
                last_in_flow = child;
                last_is_in_inline = false;
            }
        } else {
            ViewBlock* bii = find_last_block_in_inline(child);
            if (bii) {
                last_in_flow = (View*)bii;
                last_is_in_inline = true;
            }
        }
        child = child->next();
    }
    if (last_in_flow) {
        if (last_is_in_inline) {
            return find_last_formatted_line_block((ViewBlock*)last_in_flow);
        }
        return find_last_formatted_line_block((ViewBlock*)last_in_flow);
    }
    return nullptr;
}

// Get text-box-edge values from a block, walking up to ancestors if not set.
// text-box-edge is an inherited property (CSS Inline 3 §5.1).
static void get_text_box_edge(ViewBlock* block, CssEnum* over_edge, CssEnum* under_edge) {
    // Check the block itself first
    if (block->blk && block->blk->text_box_over_edge != CSS_VALUE__UNDEF) {
        *over_edge = block->blk->text_box_over_edge;
        *under_edge = block->blk->text_box_under_edge;
        return;
    }
    // Walk up parent chain (inheritance)
    ViewElement* parent = block->parent_view();
    while (parent) {
        ViewBlock* pb = (ViewBlock*)parent;
        if (pb->blk && pb->blk->text_box_over_edge != CSS_VALUE__UNDEF) {
            *over_edge = pb->blk->text_box_over_edge;
            *under_edge = pb->blk->text_box_under_edge;
            return;
        }
        parent = parent->parent_view();
    }
    // Default: auto → text
    *over_edge = CSS_VALUE_TEXT;
    *under_edge = CSS_VALUE_TEXT;
}

// Compute the over-edge (block-start) trim amount based on text-box-edge and font.
// Get block container font ascender/descender, matching compute_block_lead_y logic.
static void get_block_font_metrics(ViewBlock* block, float* ascender, float* descender) {
    if (!block->font || !block->font->font_handle) {
        *ascender = *descender = 0;
        return;
    }
    TypoMetrics typo = get_os2_typo_metrics(block->font->font_handle);
    if (typo.valid && typo.use_typo_metrics) {
        *ascender = typo.ascender;
        *descender = typo.descender;
    } else {
        *ascender = block->font->ascender;
        *descender = block->font->descender;
    }
}

// Compute the over-edge (block-start) trim based on text-box-edge and font.
// CSS Inline 3 §5: The trim is the distance from the line box's over edge
// to the block container's text-box-edge metric. When tall inline descendants
// expand the line box beyond the block's own half-leading, the trim must account
// for the full line box extent, not just the block's half-leading.
static float compute_over_trim(ViewBlock* line_block, CssEnum over_edge) {
    float block_ascender, block_descender;
    get_block_font_metrics(line_block, &block_ascender, &block_descender);

    // Use stored first-line max_ascender if available (captures tall inline descendants)
    float line_box_over = 0;
    if (line_block->blk && line_block->blk->first_line_max_ascender > 0) {
        line_box_over = line_block->blk->first_line_max_ascender;
    } else {
        // Fallback: strut-only (block's own ascender + half-leading)
        line_box_over = block_ascender + compute_block_lead_y(line_block);
    }

    if (over_edge == CSS_VALUE_TEXT || over_edge == CSS_VALUE_AUTO) {
        float trim = line_box_over - block_ascender;
        log_debug("[text-box-trim] compute_over_trim(text): line_box_over=%.1f, block_asc=%.1f, trim=%.1f",
                  line_box_over, block_ascender, trim);
        return max(0.0f, trim);
    }
    // cap/ex: trim to metric height instead of ascender
    if (!line_block->font || !line_block->font->font_handle) {
        return max(0.0f, line_box_over - block_ascender);
    }
    const FontMetrics* m = font_get_metrics(line_block->font->font_handle);
    if (!m) return max(0.0f, line_box_over - block_ascender);
    if (over_edge == CSS_VALUE_CAP) {
        float cap_height = m->cap_height;
        if (cap_height > 0) {
            return max(0.0f, line_box_over - cap_height);
        }
    } else if (over_edge == CSS_VALUE_EX) {
        float x_height = m->x_height;
        if (x_height > 0) {
            return max(0.0f, line_box_over - x_height);
        }
    }
    return max(0.0f, line_box_over - block_ascender);
}

// Compute the under-edge (block-end) trim based on text-box-edge and font.
// CSS Inline 3 §5: Same principle as over-edge, using last line's max_descender.
static float compute_under_trim(ViewBlock* line_block, CssEnum under_edge) {
    float block_ascender, block_descender;
    get_block_font_metrics(line_block, &block_ascender, &block_descender);

    // Use stored last-line max_descender if available
    float line_box_under = 0;
    if (line_block->blk && line_block->blk->last_line_max_descender > 0) {
        line_box_under = line_block->blk->last_line_max_descender;
    } else {
        line_box_under = block_descender + compute_block_lead_y(line_block);
    }

    if (under_edge == CSS_VALUE_TEXT || under_edge == CSS_VALUE_AUTO) {
        float trim = line_box_under - block_descender;
        log_debug("[text-box-trim] compute_under_trim(text): line_box_under=%.1f, block_desc=%.1f, trim=%.1f",
                  line_box_under, block_descender, trim);
        return max(0.0f, trim);
    }
    if (under_edge == CSS_VALUE_ALPHABETIC) {
        // Trim to alphabetic baseline: entire under extent
        return max(0.0f, line_box_under);
    }
    return max(0.0f, line_box_under - block_descender);
}

// Apply start trim: at each level from 'container' down to 'target',
// reduce the first in-flow block child's height and shift subsequent siblings up.
// When container == target, shift inline content up instead.
// Shift all TextRect positions within a view subtree by the given delta.
// Text rectangles store absolute positions, so they need separate adjustment
// when the block's content is shifted for text-box-trim.
static void shift_text_rects_y(View* view, float delta) {
    if (view->view_type == RDT_VIEW_TEXT) {
        TextRect* rect = ((ViewText*)view)->rect;
        while (rect) {
            rect->y += delta;
            rect = rect->next;
        }
        return;
    }
    if (view->is_group()) {
        // Recurse into both block and inline containers (ViewBlock, ViewSpan).
        // Inline wrappers (e.g. <span> containing block-in-inline) can have
        // text and block descendants whose TextRects need shifting.
        View* child = ((ViewElement*)view)->first_placed_child();
        while (child) {
            shift_text_rects_y(child, delta);
            child = child->next();
        }
    }
}

// Shift text rects within an inline view, skipping block children.
// Block children inside inline wrappers (block-in-inline) have their text rects
// shifted separately when the block itself is shifted by apply_start_trim_recursive.
static void shift_text_rects_y_inline_only(View* view, float delta) {
    if (view->view_type == RDT_VIEW_TEXT) {
        TextRect* rect = ((ViewText*)view)->rect;
        while (rect) {
            rect->y += delta;
            rect = rect->next;
        }
        return;
    }
    if (view->view_type == RDT_VIEW_INLINE) {
        View* child = ((ViewSpan*)view)->first_placed_child();
        while (child) {
            if (!child->is_block()) {
                shift_text_rects_y_inline_only(child, delta);
            }
            child = child->next();
        }
    }
}

static void apply_start_trim_recursive(ViewBlock* container, ViewBlock* target, float trim) {
    if (container == target) {
        // Inline content in this block. Shift all children up,
        // including their TextRect positions.
        // Floats are also shifted: they're positioned relative to the content
        // area, and trim-start shrinks the top of the content area.
        // Absolute/fixed elements are NOT shifted — they're positioned
        // relative to the containing block edges, not the content area.
        View* child = container->first_placed_child();
        while (child) {
            bool skip = false;
            if (child->is_block()) {
                ViewBlock* vb = (ViewBlock*)child;
                if (vb->position && (vb->position->position == CSS_VALUE_ABSOLUTE ||
                                      vb->position->position == CSS_VALUE_FIXED)) {
                    skip = true;
                }
            }
            if (!skip) {
                child->y -= trim;
                // Block-in-inline: block children inside inline wrappers have y
                // relative to the containing block, not relative to the inline
                // wrapper. Shifting the inline wrapper doesn't move them, so
                // shift their y coordinates and text rects explicitly.
                // Use shift_text_rects_y_inline_only to avoid double-shifting
                // text rects inside block descendants.
                if (child->view_type == RDT_VIEW_INLINE) {
                    shift_text_rects_y_inline_only(child, -trim);
                    // Block-in-inline: block children inside inline wrappers have y
                    // relative to the containing block, not relative to the inline
                    // wrapper. Shift their y coordinates so they move with the wrapper.
                    // Text rects inside blocks are relative to the block, so they
                    // don't need shifting — the absolute position recalculation
                    // already accounts for the block's shifted y.
                    View* ic = ((ViewSpan*)child)->first_placed_child();
                    while (ic) {
                        if (ic->is_block() && !is_out_of_flow_block((ViewBlock*)ic)) {
                            ic->y -= trim;
                        }
                        ic = ic->next();
                    }
                } else {
                    shift_text_rects_y(child, -trim);
                }
            }
            child = child->next();
        }
        return;
    }

    // Find the first in-flow block child (which is on the path to target).
    // Also handles block-in-inline: an inline wrapper containing the target block.
    View* child = container->first_placed_child();
    bool found_first = false;
    while (child) {
        if (child->is_block()) {
            ViewBlock* vb = (ViewBlock*)child;
            if (is_out_of_flow_block(vb)) {
                child = child->next();
                continue;
            }
            if (!found_first) {
                // First in-flow child: reduce its height, recurse into it
                found_first = true;
                vb->height -= trim;
                vb->content_height -= trim;
                apply_start_trim_recursive(vb, target, trim);
            } else {
                // Subsequent in-flow siblings: shift up
                child->y -= trim;
                shift_text_rects_y(child, -trim);
            }
        } else if (!found_first && child->view_type == RDT_VIEW_INLINE) {
            // Check for block-in-inline: inline wrapper containing a block child
            ViewBlock* bii = find_first_block_in_inline(child);
            if (bii) {
                found_first = true;
                // Reduce the inline wrapper's height
                child->height -= trim;
                // Reduce the block inside the inline
                bii->height -= trim;
                bii->content_height -= trim;
                apply_start_trim_recursive(bii, target, trim);
            }
        } else if (!found_first) {
            // Non-block in-flow content before any block child — skip safely
        } else {
            // Non-block content after first block: shift up
            child->y -= trim;
            if (child->view_type == RDT_VIEW_INLINE) {
                shift_text_rects_y_inline_only(child, -trim);
                View* ic = ((ViewSpan*)child)->first_placed_child();
                while (ic) {
                    if (ic->is_block() && !is_out_of_flow_block((ViewBlock*)ic)) {
                        ic->y -= trim;
                    }
                    ic = ic->next();
                }
            } else {
                shift_text_rects_y(child, -trim);
            }
        }
        child = child->next();
    }
}

// Apply end trim: at each level from 'container' down to 'target',
// reduce the last in-flow block child's height.
// Also handles block-in-inline: reduces inline wrapper height when it contains
// the target block.
static void apply_end_trim_recursive(ViewBlock* container, ViewBlock* target, float trim) {
    if (container == target) {
        // CSS Inline 3: invisible line boxes (containing no glyphs with non-zero
        // advance width) at the end of the container are positioned after the last
        // formatted line. After end-trim, reposition them to the new content end.
        // Find the bottom of the last visible line (max y+height of text/br views).
        float last_visible_bottom = 0;
        View* child = container->first_placed_child();
        while (child) {
            if (child->view_type == RDT_VIEW_TEXT || child->view_type == RDT_VIEW_BR) {
                float bottom = child->y + child->height;
                if (bottom > last_visible_bottom) last_visible_bottom = bottom;
            }
            child = child->next();
        }
        // Shift children on invisible lines (positioned at or after last visible bottom)
        // Skip block-level children: in block-in-inline scenarios, block children
        // are structural boundaries between anonymous blocks and must not be moved.
        if (last_visible_bottom > 0) {
            child = container->first_placed_child();
            while (child) {
                if (child->y >= last_visible_bottom && child->view_type != RDT_VIEW_TEXT
                    && child->view_type != RDT_VIEW_BR && !child->is_block()) {
                    child->y -= trim;
                }
                child = child->next();
            }
        }
        return;
    }

    // Find the last in-flow block child, including blocks inside inline wrappers.
    View* child = container->first_placed_child();
    ViewBlock* last_in_flow = nullptr;
    View* last_inline_wrapper = nullptr;
    while (child) {
        if (child->is_block()) {
            ViewBlock* vb = (ViewBlock*)child;
            if (!is_out_of_flow_block(vb)) {
                last_in_flow = vb;
                last_inline_wrapper = nullptr;
            }
        } else {
            ViewBlock* bii = find_last_block_in_inline(child);
            if (bii) {
                last_in_flow = bii;
                last_inline_wrapper = child;
            }
        }
        child = child->next();
    }
    if (last_in_flow) {
        if (last_inline_wrapper) {
            // Reduce the inline wrapper's height too
            last_inline_wrapper->height -= trim;
        }
        last_in_flow->height -= trim;
        last_in_flow->content_height -= trim;
        apply_end_trim_recursive(last_in_flow, target, trim);
    }
}

// Check if there is any non-zero padding or border between 'container' and 'target'
// on the block-start side (top). Returns true if trim should be suppressed.
static bool has_start_padding_or_border_between(ViewBlock* container, ViewBlock* target) {
    if (container == target) return false;
    // Walk the path from container's first in-flow block child down to target
    ViewBlock* current = container;
    while (current != target) {
        View* child = current->first_placed_child();
        ViewBlock* next_block = nullptr;
        while (child) {
            if (child->is_block()) {
                ViewBlock* vb = (ViewBlock*)child;
                if (!is_out_of_flow_block(vb)) {
                    next_block = vb;
                    break;
                }
            } else {
                ViewBlock* bii = find_first_block_in_inline(child);
                if (bii) {
                    next_block = bii;
                    break;
                }
            }
            child = child->next();
        }
        if (!next_block) break;
        // Check this block's padding-top and border-top
        if (next_block->bound) {
            if (next_block->bound->padding.top > 0) return true;
            if (next_block->bound->border && next_block->bound->border->width.top > 0) return true;
        }
        current = next_block;
    }
    return false;
}

// Check if there is any non-zero padding or border between 'container' and 'target'
// on the block-end side (bottom). Returns true if trim should be suppressed.
static bool has_end_padding_or_border_between(ViewBlock* container, ViewBlock* target) {
    if (container == target) return false;
    ViewBlock* current = container;
    while (current != target) {
        View* child = current->first_placed_child();
        ViewBlock* last_block = nullptr;
        while (child) {
            if (child->is_block()) {
                ViewBlock* vb = (ViewBlock*)child;
                if (!is_out_of_flow_block(vb)) {
                    last_block = vb;
                }
            } else {
                ViewBlock* bii = find_last_block_in_inline(child);
                if (bii) {
                    last_block = bii;
                }
            }
            child = child->next();
        }
        if (!last_block) break;
        if (last_block->bound) {
            if (last_block->bound->padding.bottom > 0) return true;
            if (last_block->bound->border && last_block->bound->border->width.bottom > 0) return true;
        }
        current = last_block;
    }
    return false;
}

// Apply text-box-trim adjustments to a block and its children.
// CSS Inline 3 §5: trims half-leading from first/last formatted lines.
// CSS Inline Level 3 §5: Compute text-box-trim amounts and adjust child
// positions.  Returns the total height to subtract from the block's flow
// height.  The caller is responsible for reducing block->height /
// block->content_height so that min/max-height constraints are applied
// AFTER the trim (per spec, trim modifies intrinsic height before
// min-height/max-height kicks in).
static float apply_text_box_trim(ViewBlock* block) {
    if (!block->blk || !block->blk->text_box_trim) return 0;

    uint8_t trim = block->blk->text_box_trim;

    float start_trim = 0, end_trim = 0;
    ViewBlock* first_line_block = nullptr;
    ViewBlock* last_line_block = nullptr;

    if (trim & TEXT_BOX_TRIM_START) {
        first_line_block = find_first_formatted_line_block(block);
        if (first_line_block && !has_start_padding_or_border_between(block, first_line_block)) {
            // CSS Inline 3 §5: text-box-edge is inherited; use the value from
            // the formatted line block, not the trimming block.
            CssEnum over_edge, under_edge;
            get_text_box_edge(first_line_block, &over_edge, &under_edge);
            start_trim = compute_over_trim(first_line_block, over_edge);
        }
    }

    if (trim & TEXT_BOX_TRIM_END) {
        last_line_block = find_last_formatted_line_block(block);
        if (last_line_block && !has_end_padding_or_border_between(block, last_line_block)) {
            CssEnum over_edge, under_edge;
            get_text_box_edge(last_line_block, &over_edge, &under_edge);
            end_trim = compute_under_trim(last_line_block, under_edge);
        }
    }

    if (start_trim <= 0 && end_trim <= 0) return 0;

    log_debug("[text-box-trim] applying trim: start=%.1f, end=%.1f to <%s> (height=%.1f)",
              start_trim, end_trim, block->node_name(), block->height);

    if (start_trim > 0) {
        apply_start_trim_recursive(block, first_line_block, start_trim);
    }

    if (end_trim > 0) {
        apply_end_trim_recursive(block, last_line_block, end_trim);
    }

    float total_trim = start_trim + end_trim;

    log_debug("[text-box-trim] computed total trim: %.1f for <%s>",
              total_trim, block->node_name());

    return total_trim;
}

// CSS 2.1 §10.3.3: After an inline-block shrinks to fit, its block-level children
// in normal flow with auto width must be adjusted to match the new containing block
// width. During initial layout, these children stretched to the full available width
// (before the parent shrank). This function corrects their widths and repositions
// text content for text-align center/right.
static void adjust_block_children_after_shrink(ViewBlock* parent, float new_parent_cw, CssEnum inherited_text_align) {
    for (View* child = ((ViewElement*)parent)->first_placed_child(); child; child = child->next()) {
        // only adjust block-level elements in normal flow
        if (child->view_type != RDT_VIEW_BLOCK && child->view_type != RDT_VIEW_LIST_ITEM)
            continue;

        ViewBlock* cb = (ViewBlock*)child;

        // skip children with explicit width
        if (cb->blk &&
            cb->blk->given_width_type != CSS_VALUE_AUTO &&
            cb->blk->given_width_type != CSS_VALUE__UNDEF)
            continue;

        // skip floats and out-of-flow elements
        if (element_has_float(cb))
            continue;
        if (cb->position &&
            (cb->position->position == CSS_VALUE_ABSOLUTE ||
             cb->position->position == CSS_VALUE_FIXED))
            continue;

        // compute child's new border-box width = containing block - margins
        float ml = 0, mr = 0;
        if (cb->bound) {
            ml = (cb->bound->margin.left_type == CSS_VALUE_AUTO) ? 0 : cb->bound->margin.left;
            mr = (cb->bound->margin.right_type == CSS_VALUE_AUTO) ? 0 : cb->bound->margin.right;
        }
        float new_width = max(new_parent_cw - ml - mr, 0.0f);
        float old_width = cb->width;

        if (fabsf(new_width - old_width) < 0.5f)
            continue;

        log_debug("%s adjust block child after shrink: width %.1f -> %.1f (parent_cw=%.1f)",
            cb->source_loc(), old_width, new_width, new_parent_cw);
        cb->width = new_width;

        // compute padding+border for content area calculations
        float pb = 0;
        if (cb->bound) {
            pb += cb->bound->padding.left + cb->bound->padding.right;
            if (cb->bound->border)
                pb += cb->bound->border->width.left + cb->bound->border->width.right;
        }
        float old_avail_cw = old_width - pb;
        float new_avail_cw = new_width - pb;

        // text-align: use child's own value if it has blk, otherwise inherit from parent
        CssEnum ta = cb->blk ? cb->blk->text_align : inherited_text_align;

        // adjust text positions for text-align center/right within this child
        if (ta == CSS_VALUE_CENTER || ta == CSS_VALUE_RIGHT) {
            float pad_left = cb->bound ? cb->bound->padding.left : 0;
            for (View* gc = ((ViewElement*)cb)->first_placed_child(); gc; gc = gc->next()) {
                if (gc->view_type != RDT_VIEW_TEXT) continue;
                ViewText* tv = (ViewText*)gc;
                for (TextRect* r = tv->rect; r; r = r->next) {
                    float cur_offset = r->x - pad_left;
                    float target;
                    if (ta == CSS_VALUE_CENTER)
                        target = (new_avail_cw - r->width) / 2;
                    else
                        target = new_avail_cw - r->width;
                    float shift = target - cur_offset;
                    if (fabsf(shift) > 0.5f)
                        r->x += shift;
                }
            }
        }

        // recursively adjust nested block children
        adjust_block_children_after_shrink(cb, max(new_avail_cw, 0.0f), ta);
    }
}

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

    // CSS 2.1 §12.5: List-items with visible markers generate at least one line box,
    // even when the list-item has no content. Default list-style-type is 'disc',
    // so all list-items get minimum height unless explicitly set to 'none'.
    // Check actual content height (advance_y minus border/padding top) to detect empty content.
    if (block->view_type == RDT_VIEW_LIST_ITEM) {
        float content_area_height = lycon->block.advance_y;
        if (block->bound) {
            content_area_height -= block->bound->padding.top;
            if (block->bound->border) content_area_height -= block->bound->border->width.top;
        }
        if (content_area_height <= 0) {
            bool has_marker = true;
            if (block->blk && block->blk->list_style_type == CSS_VALUE_NONE) {
                has_marker = false;
            }
            // Also check ::marker { content: none } CSS override
            if (has_marker && block->marker_styles) {
                CssDeclaration* content_decl = style_tree_get_declaration(
                    block->marker_styles, CSS_PROPERTY_CONTENT);
                if (content_decl && content_decl->value) {
                    CssValue* cv = content_decl->value;
                    if (cv->type == CSS_VALUE_TYPE_KEYWORD && cv->data.keyword == CSS_VALUE_NONE) {
                        has_marker = false;
                    }
                }
            }
            if (has_marker) {
                float min_line_height = lycon->block.line_height;
                if (min_line_height <= 0) min_line_height = lycon->font.current_font_size > 0 ? lycon->font.current_font_size * 1.2f : 18.0f;
                flow_height += min_line_height;
                block->content_height += min_line_height;
                log_debug("%s list-item: empty content, setting min height from marker line-height: %.1f", block->source_loc(), min_line_height);
            }
        }
    }

    // CSS Inline Level 3 §5: Apply text-box-trim to the intrinsic flow height
    // BEFORE min-height/max-height constraints (§10.7) are evaluated.
    // The trim modifies content height (removing half-leading from first/last
    // formatted lines) and shifts child positions accordingly.
    // Copy line box metrics from layout context to view tree for use by trim.
    if (block->blk) {
        block->blk->first_line_max_ascender = lycon->block.first_line_max_ascender;
        block->blk->first_line_max_descender = lycon->block.first_line_max_descender;
        block->blk->last_line_max_ascender = lycon->block.last_line_max_ascender;
        block->blk->last_line_max_descender = lycon->block.last_line_max_descender;
        // Persist first line baseline for flex baseline alignment (CSS Flexbox §9.4)
        block->blk->first_line_baseline = lycon->block.first_line_ascender;
    }
    float text_box_trim_amount = apply_text_box_trim(block);
    if (text_box_trim_amount > 0) {
        flow_height -= text_box_trim_amount;
        block->content_height -= text_box_trim_amount;
        // Also reduce advance_y so that absolutely-positioned elements (whose
        // height auto-sizing in layout_abs_block reads advance_y directly)
        // pick up the trimmed value.  For in-flow blocks this is harmless
        // because the parent restores its own block context afterward.
        lycon->block.advance_y -= text_box_trim_amount;
        log_debug("%s finalizing block, display=%d, given wd:%f", block->source_loc(), display, lycon->block.given_width);
    }
    if (display == CSS_VALUE_INLINE_BLOCK && lycon->block.given_width < 0) {
        // CSS 2.1 §10.3.9: Save the pre-layout fit-content width (border-box).
        // When max-content > available, the fit-content equals available width,
        // and text wraps within it. After layout, flow_width (widest wrapped line)
        // can be less than available. Floor at pre-layout width to prevent
        // incorrect shrinking below the shrink-to-fit width.
        float pre_layout_fit_width = block->width;

        // CSS 2.1 §10.3.9: shrink-to-fit width cannot be less than border+padding
        float min_bp_width = 0;
        if (block->bound) {
            min_bp_width = block->bound->padding.left + block->bound->padding.right;
            if (block->bound->border) {
                min_bp_width += block->bound->border->width.left + block->bound->border->width.right;
            }
        }
        // CSS 2.1 §10.3.9: Shrink-to-fit width = min(max(preferred_minimum_width,
        // available_width), preferred_width). After layout, flow_width captures the
        // actual content extent — if content has explicit wider widths, flow_width
        // exceeds available width, and the inline-block MUST expand to accommodate it
        // (the preferred width is at least flow_width). Do NOT cap at available width.
        block->width = max(flow_width, min_bp_width);
        // CSS 2.1 §10.3.9 + §10.4: Apply min-width/max-width constraints
        // to inline-block shrink-to-fit width, same as other auto-width paths
        block->width = adjust_min_max_width(block, block->width);

        // CSS 2.1 §10.3.9: Floor at the pre-layout fit-content width.
        // The fit-content formula min(max-content, max(min-content, available))
        // is the correct shrink-to-fit width. Post-layout flow_width can be less
        // (text wrapped within the available width). Allow expansion (overflow)
        // but not spurious shrinking.
        if (block->width < pre_layout_fit_width) {
            log_debug("%s inline-block floor: flow_width=%.1f -> fit_width=%.1f",
                      block->source_loc(), block->width, pre_layout_fit_width);
            block->width = pre_layout_fit_width;
        }

        log_debug("%s inline-block final width set to: %f, text_align=%d", block->source_loc(), block->width, lycon->block.text_align);

        // For inline-block with auto width and text-align:center/right,
        // we deferred alignment during line_align. Now apply it with final width.
        if (lycon->block.text_align == CSS_VALUE_CENTER || lycon->block.text_align == CSS_VALUE_RIGHT) {
            // Calculate content width (excluding border/padding)
            float final_content_width = block->width;
            if (block->bound) {
                final_content_width -= (block->bound->padding.left + block->bound->padding.right);
                if (block->bound->border) {
                    final_content_width -= (block->bound->border->width.left + block->bound->border->width.right);
                }
            }

            // Align children using the final content width
            View* child = block->first_child;
            while (child) {
                if (child->view_type == RDT_VIEW_TEXT) {
                    ViewText* text = (ViewText*)child;
                    TextRect* rect = text->rect;
                    while (rect) {
                        float line_width = rect->width;
                        // Calculate offset to center/right align within content area
                        // Note: rect->x is relative to block including padding offset
                        float padding_left = block->bound ? block->bound->padding.left : 0;
                        float current_offset_in_content = rect->x - padding_left;
                        float target_offset_in_content;
                        if (lycon->block.text_align == CSS_VALUE_CENTER) {
                            target_offset_in_content = (final_content_width - line_width) / 2;
                        } else { // RIGHT
                            target_offset_in_content = final_content_width - line_width;
                        }
                        float offset = target_offset_in_content - current_offset_in_content;
                        if (abs(offset) > 0.5f) {  // Only adjust if offset is significant
                            rect->x += offset;
                            log_debug("%s deferred text align: rect->x adjusted by %.1f to %.1f (content_width=%.1f)", block->source_loc(),
                                      offset, rect->x, final_content_width);
                        }
                        rect = rect->next;
                    }
                }
                child = child->next();
            }
        }

        // CSS 2.1 §10.3.3: Adjust block-level children that stretched to the
        // pre-shrink available width. Compute the inline-block's new content width
        // and recursively update auto-width block children.
        float shrunk_cw = block->width;
        if (block->bound) {
            shrunk_cw -= block->bound->padding.left + block->bound->padding.right;
            if (block->bound->border)
                shrunk_cw -= block->bound->border->width.left + block->bound->border->width.right;
        }
        adjust_block_children_after_shrink(block, max(shrunk_cw, 0.0f), lycon->block.text_align);
    }

    // HTML rendering spec §15.5.12: First legend child of fieldset shrinks to fit content.
    // Done post-layout because children's styles aren't resolved during pre-layout
    // intrinsic measurement. Uses max_width tracked during child layout.
    if (block->tag_id == HTM_TAG_LEGEND && display != CSS_VALUE_INLINE_BLOCK) {
        ViewElement* pa = block->parent_view();
        if (pa && pa->tag_id == HTM_TAG_FIELDSET) {
            // Only the first legend child gets shrink-to-fit
            bool is_first_legend = true;
            for (View* sib = pa->first_child; sib; sib = sib->next()) {
                if (sib == (View*)block) break;
                if (sib->is_element() && ((ViewElement*)sib)->tag_id == HTM_TAG_LEGEND) {
                    is_first_legend = false;
                    break;
                }
            }
            bool width_is_auto = !block->blk ||
                block->blk->given_width_type == CSS_VALUE_AUTO ||
                block->blk->given_width_type == CSS_VALUE__UNDEF;
            if (is_first_legend && width_is_auto) {
                float min_bp_width = 0;
                if (block->bound) {
                    min_bp_width = block->bound->padding.left + block->bound->padding.right;
                    if (block->bound->border) {
                        min_bp_width += block->bound->border->width.left + block->bound->border->width.right;
                    }
                }
                float shrunk = min(max(flow_width, min_bp_width), block->width);
                log_debug("%s legend shrink-to-fit: flow_width=%.1f, old_width=%.1f, new_width=%.1f", block->source_loc(),
                    flow_width, block->width, shrunk);
                block->width = shrunk;

                // Adjust block children to match the shrunk legend width
                float legend_cw = block->width;
                if (block->bound) {
                    legend_cw -= block->bound->padding.left + block->bound->padding.right;
                    if (block->bound->border)
                        legend_cw -= block->bound->border->width.left + block->bound->border->width.right;
                }
                CssEnum ta = block->blk ? block->blk->text_align : CSS_VALUE__UNDEF;
                adjust_block_children_after_shrink(block, max(legend_cw, 0.0f), ta);
            }
        }
    }

    // handle horizontal overflow
    if (flow_width > block->width) { // hz overflow
        if (!block->scroller) {
            block->scroller = alloc_scroll_prop(lycon);
        }
        block->scroller->has_hz_overflow = true;
        if (block->scroller->overflow_x == CSS_VALUE_VISIBLE) {
            // CSS 2.1 §10.3.9: Inline-blocks are atomic inline-level boxes.
            // The parent already accounts for the inline-block's border-box width
            // via advance_x; internal overflow should not inflate the parent's
            // max_width used for shrink-to-fit width calculations.
            // CSS 2.1 §10.4: When max-width constrains a box, the overflow is
            // the element's own styling choice. The parent's shrink-to-fit
            // should use the constrained width, not the internal overflow.
            // CSS 2.1 §9.3.1: Absolutely positioned elements are out of normal
            // flow and their overflow should not propagate to the parent's
            // shrink-to-fit width calculation.
            bool is_max_width_overflow = block->blk && block->blk->given_max_width >= 0;
            bool is_abs_pos = block->position &&
                (block->position->position == CSS_VALUE_ABSOLUTE || block->position->position == CSS_VALUE_FIXED);
            if (display != CSS_VALUE_INLINE_BLOCK && !is_max_width_overflow && !is_abs_pos && lycon->block.parent) {
                lycon->block.parent->max_width = max(lycon->block.parent->max_width, flow_width);
            }
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
    // Use block->blk->given_height instead of lycon->block.given_height to avoid corruption
    // from child layouts that modify lycon->block during their CSS resolution
    float block_given_height = (block->blk && block->blk->given_height >= 0) ? block->blk->given_height : -1;
    if (block_given_height >= 0) { // got specified height
        // Ensure block->height is set from given_height if it hasn't been set yet
        // This is critical for the html element which doesn't go through normal layout_block path
        if (block->height <= 0) {
            block->height = block_given_height;
            log_debug("%s finalize: set block->height from given_height: %.1f", block->source_loc(), block_given_height);
        }
        // Apply min-height/max-height constraints to explicit height (CSS 2.1 §10.7)
        block->height = adjust_min_max_height(block, block->height);
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
            if (block->scroller->has_vt_scroll ||
                block->scroller->overflow_y == CSS_VALUE_CLIP ||
                block->scroller->overflow_y == CSS_VALUE_HIDDEN) {
                block->scroller->has_clip = true;
                block->scroller->clip.left = 0;  block->scroller->clip.top = 0;
                block->scroller->clip.right = block->width;  block->scroller->clip.bottom = block->height;
            }
        }
    }
    else {
        bool has_embed = block->embed != nullptr;
        bool has_flex = has_embed && block->embed->flex != nullptr;
        bool is_table = (block->view_type == RDT_VIEW_TABLE);
        if (!has_flex && !is_table) {
            // CSS Box 4 §margin-trim: block-end is handled earlier in
            // layout_block_inner_content() — the last child's margin.bottom is
            // zeroed and advance_y adjusted before we reach this point.

            // CSS 2.1 §10.6.3 + erratum q313: When computing auto height, exclude
            // the last child's bottom margin that will collapse with the parent's
            // bottom margin BEFORE applying min/max-height constraints.
            // min/max-height must not affect margin adjacency (q313).
            float collapsible_mb = compute_collapsible_bottom_margin(block);
            // Quirks mode: in a quirky container, quirky margins from children
            // don't collapse with the parent, so they aren't collapsible.
            if (collapsible_mb != 0 && is_quirky_container(block, lycon)) {
                // Find last in-flow child to check if its margin is quirky
                View* last_child = (View*)block->first_child;
                View* last_in_flow = nullptr;
                while (last_child) {
                    if (last_child->view_type && last_child->is_block()) {
                        ViewBlock* vb = (ViewBlock*)last_child;
                        bool is_out_of_flow = (vb->view_type == RDT_VIEW_INLINE_BLOCK) ||
                            (vb->position && (vb->position->position == CSS_VALUE_ABSOLUTE ||
                             vb->position->position == CSS_VALUE_FIXED || element_has_float(vb)));
                        if (!is_out_of_flow) last_in_flow = last_child;
                    }
                    last_child = (View*)last_child->next_sibling;
                }
                if (last_in_flow && last_in_flow->is_block() &&
                    has_quirky_margin_bottom((ViewBlock*)last_in_flow)) {
                    collapsible_mb = 0;
                }
            }
            float auto_height = flow_height - collapsible_mb;
            // CSS 2.1 §10.6.3: Auto height cannot be negative. When all children
            // are collapsed-through (self-collapsing), they don't factor in the
            // auto height calculation — the height is zero.
            if (auto_height < 0) auto_height = 0;

            // CSS 2.1 §10.7: min-height/max-height refer to the content area for
            // content-box elements. auto_height is in border-box space (includes
            // padding+border from flow_height). Convert to content-box space for
            // the min/max comparison, then add padding+border back.
            float final_height;
            bool is_content_box = !block->blk || block->blk->box_sizing != CSS_VALUE_BORDER_BOX;
            if (is_content_box && block->bound) {
                float pb = block->bound->padding.top + block->bound->padding.bottom;
                if (block->bound->border) {
                    pb += block->bound->border->width.top + block->bound->border->width.bottom;
                }
                float content_only = max(auto_height - pb, 0.0f);
                content_only = adjust_min_max_height(block, content_only);
                final_height = content_only + pb;
            } else {
                final_height = adjust_min_max_height(block, auto_height);
            }
            log_debug("%s finalize block flow, set block height to flow height: %f (collapsible_mb=%f, auto=%f, after min/max: %f)", block->source_loc(),
                      flow_height, collapsible_mb, auto_height, final_height);
            block->height = final_height;
        } else {
            log_debug("%s finalize block flow: %s container, keeping height: %f (flow=%f)", block->source_loc(),
                      is_table ? "table" : "flex", block->height, flow_height);
        }
        // DEBUG: Check table height RIGHT BEFORE fprintf (only for body and html)
        if (strcmp(block->node_name(), "html") == 0 || strcmp(block->node_name(), "body") == 0) {
            View* html_or_body = block;
            View* body_view = nullptr;

            // For html, find body first; for body, use itself
            if (strcmp(block->node_name(), "html") == 0) {
                View* child = ((ViewElement*)block)->first_placed_child();
                while (child) {
                    if (child->is_block() && strcmp(child->node_name(), "body") == 0) {
                        body_view = child;
                        break;
                    }
                    child = child->next();
                }
            } else {
                body_view = block;
            }

            if (body_view) {
                View* grandchild = ((ViewElement*)body_view)->first_placed_child();
                while (grandchild) {
                    grandchild = grandchild->next();
                }
            }
        }
    }

    // BFC (Block Formatting Context) height expansion to contain floats
    // CSS 2.1 §10.6.7: For BFC roots with AUTO height, floating descendants
    // are included in height computation. When height is explicitly specified
    // (given_height >= 0), the explicit height is used and floats may overflow.
    if (lycon->block.establishing_element == block && block_given_height < 0) {
        float max_float_bottom = 0;
        // Check all floats in this BFC
        for (FloatBox* fb = lycon->block.left_floats; fb; fb = fb->next) {
            if (fb->margin_box_bottom > max_float_bottom) {
                max_float_bottom = fb->margin_box_bottom;
            }
        }
        for (FloatBox* fb = lycon->block.right_floats; fb; fb = fb->next) {
            if (fb->margin_box_bottom > max_float_bottom) {
                max_float_bottom = fb->margin_box_bottom;
            }
        }
        // max_float_bottom is relative to the BFC root's border-box top
        // (because origin_y for child BFCs is inherited from parent, and float
        // positions include padding-top). To get the full border-box height,
        // we must add padding-bottom and border-bottom.
        float padding_bottom = block->bound ? block->bound->padding.bottom : 0;
        float border_bottom = block->bound && block->bound->border ? block->bound->border->width.bottom : 0;
        float float_border_box_height = max_float_bottom + padding_bottom + border_bottom;
        log_debug("%s finalize BFC %s: max_float_bottom=%.1f, float_bbox_hg=%.1f, block->height=%.1f",
                  block->source_loc(), block->node_name(), max_float_bottom, float_border_box_height, block->height);
        if (float_border_box_height > block->height) {
            float old_height = block->height;
            block->height = float_border_box_height;
            log_debug("%s finalize BFC height expansion: old=%.1f, new=%.1f", block->source_loc(),
                      old_height, block->height);
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
            log_debug("%s finalize: enabling clip for overflow:hidden, wd:%f, hg:%f", block->source_loc(), block->width, block->height);
        }
    }
}

void layout_iframe(LayoutContext* lycon, ViewBlock* block, DisplayValue display) {
    DomDocument* doc = NULL;
    log_debug("layout iframe");

    // Iframe recursion depth limit to prevent infinite loops (e.g., <iframe src="index.html">)
    // This is a thread-local variable shared with layout_flex_multipass.cpp
    // Keep this low since each HTTP download can take seconds
    const int MAX_IFRAME_DEPTH = 3;

    if (iframe_depth >= MAX_IFRAME_DEPTH) {
        log_warn("iframe: maximum nesting depth (%d) exceeded, skipping", MAX_IFRAME_DEPTH);
        return;
    }

    if (!(block->embed && block->embed->doc)) {
        // load iframe document
        const char *value = block->get_attribute("src");
        if (value) {
            size_t value_len = strlen(value);
            StrBuf* src = strbuf_new_cap(value_len);
            strbuf_append_str_n(src, value, value_len);
            // Use iframe's actual dimensions as viewport, not window dimensions
            // This ensures the embedded document layouts to fit within the iframe
            int iframe_width = block->width > 0 ? (int)block->width : lycon->ui_context->window_width; // INT_CAST_OK: iframe viewport expects int
            int iframe_height = block->height > 0 ? (int)block->height : lycon->ui_context->window_height; // INT_CAST_OK: iframe viewport expects int
            log_debug("load iframe doc src: %s (iframe viewport=%dx%d, depth=%d)", src->str, iframe_width, iframe_height, iframe_depth);

            // Increment depth before loading
            iframe_depth++;

            // Load iframe document - pixel_ratio from ui_context is still used internally
            doc = load_html_doc(lycon->ui_context->document->url, src->str,
                iframe_width, iframe_height,
                1.0f);  // Layout in CSS logical pixels
            strbuf_free(src);
            if (!doc) {
                log_debug("failed to load iframe document");
                iframe_depth--;
                // todo: use a placeholder
            } else {
                if (!(block->embed)) block->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp));
                block->embed->doc = doc; // assign loaded document to embed property
                if (doc->html_root) {
                    log_debug("IFRAME TRACE: about to layout iframe document");
                    // Save parent document and window/viewport dimensions
                    DomDocument* parent_doc = lycon->ui_context->document;
                    float saved_window_width = lycon->ui_context->window_width;
                    float saved_window_height = lycon->ui_context->window_height;
                    int saved_viewport_width = lycon->ui_context->viewport_width;
                    int saved_viewport_height = lycon->ui_context->viewport_height;

                    // Temporarily set window/viewport dimensions to iframe size
                    // Both window_ and viewport_ must match so layout_init picks up iframe dims
                    lycon->ui_context->document = doc;
                    lycon->ui_context->window_width = (float)iframe_width;
                    lycon->ui_context->window_height = (float)iframe_height;
                    lycon->ui_context->viewport_width = iframe_width;
                    lycon->ui_context->viewport_height = iframe_height;

                    // Process @font-face rules before layout (critical for custom fonts like Computer Modern)
                    process_document_font_faces(lycon->ui_context, doc);

                    layout_html_doc(lycon->ui_context, doc, false);

                    // Restore parent document and window/viewport dimensions
                    lycon->ui_context->document = parent_doc;
                    lycon->ui_context->window_width = saved_window_width;
                    lycon->ui_context->window_height = saved_window_height;
                    lycon->ui_context->viewport_width = saved_viewport_width;
                    lycon->ui_context->viewport_height = saved_viewport_height;
                    log_debug("IFRAME TRACE: finished layout iframe document");
                }
                iframe_depth--;
                // PDF scaling now happens inside pdf_page_to_view_tree via load_html_doc
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
        log_debug("IFRAME TRACE: iframe embedded doc root->content_width=%.1f, root->content_height=%.1f",
            root->content_width, root->content_height);
        // For PDF and other pre-laid-out documents, use width/height if content_width/height are 0
        float iframe_width = root->content_width > 0 ? root->content_width : root->width;
        float iframe_height = root->content_height > 0 ? root->content_height : root->height;
        lycon->block.max_width = iframe_width;
        lycon->block.advance_y = iframe_height;
        log_debug("IFRAME TRACE: set lycon->block.advance_y = %.1f from iframe_height", lycon->block.advance_y);

        // Disable inner doc's viewport scroller — the iframe container handles scrolling.
        // Otherwise we get double scrolling/clipping (inner root + outer iframe block).
        if (root->scroller) {
            if (root->content_height > root->height) {
                root->height = root->content_height;  // restore full content height
            }
            root->scroller = NULL;
        }
    }
    // Set outer iframe block as scroll container (overflow:auto)
    if (!block->scroller) {
        block->scroller = alloc_scroll_prop(lycon);
    }
    block->scroller->overflow_y = CSS_VALUE_AUTO;
    finalize_block_flow(lycon, block, display.outer);
    // Set v_max_scroll on the iframe block's pane
    if (block->scroller && block->scroller->pane) {
        block->scroller->pane->v_max_scroll = block->content_height > block->height ?
            block->content_height - block->height : 0;
    }
    log_debug("IFRAME TRACE: after finalize_block_flow, iframe block->content_height=%.1f", block->content_height);
}

/**
 * Layout inline SVG element with intrinsic sizing from width/height attributes or viewBox
 */
void layout_inline_svg(LayoutContext* lycon, ViewBlock* block) {
    log_debug("%s layout inline SVG element", block->source_loc());

    // Get intrinsic size from SVG attributes
    Element* native_elem = static_cast<DomElement*>(block)->native_element;
    if (!native_elem) {
        log_debug("%s inline SVG has no native element, using default size", block->source_loc());
        block->width = 300;  // HTML default for SVG
        block->height = 150;
        return;
    }

    SvgIntrinsicSize intrinsic = calculate_svg_intrinsic_size(native_elem);

    log_debug("%s SVG intrinsic: width=%.1f height=%.1f aspect=%.3f has_w=%d has_h=%d", block->source_loc(),
              intrinsic.width, intrinsic.height, intrinsic.aspect_ratio,
              intrinsic.has_intrinsic_width, intrinsic.has_intrinsic_height);

    // Determine final dimensions considering CSS properties
    float width = lycon->block.given_width;
    float height = lycon->block.given_height;

    if (width >= 0 && height >= 0) {
        // Both CSS dimensions specified - use them
        block->width = width;
        block->height = height;
    } else if (width >= 0) {
        // Width specified, calculate height from aspect ratio
        block->width = width;
        if (intrinsic.aspect_ratio > 0) {
            block->height = width / intrinsic.aspect_ratio;
        } else {
            block->height = intrinsic.height;
        }
    } else if (height >= 0) {
        // Height specified, calculate width from aspect ratio
        block->height = height;
        if (intrinsic.aspect_ratio > 0) {
            block->width = height * intrinsic.aspect_ratio;
        } else {
            block->width = intrinsic.width;
        }
    } else {
        // Neither CSS dimension specified - use intrinsic size
        // or parent width if intrinsic width is not available
        if (intrinsic.has_intrinsic_width) {
            block->width = intrinsic.width;
        } else if (lycon->block.parent && lycon->block.parent->content_width > 0) {
            block->width = lycon->block.parent->content_width;
        } else {
            block->width = 300;  // HTML default
        }

        if (intrinsic.has_intrinsic_height) {
            block->height = intrinsic.height;
        } else if (intrinsic.aspect_ratio > 0) {
            block->height = block->width / intrinsic.aspect_ratio;
        } else {
            block->height = 150;  // HTML default
        }
    }

    // Add padding and border
    float padding_top = block->bound && block->bound->padding.top > 0 ? block->bound->padding.top : 0;
    float padding_bottom = block->bound && block->bound->padding.bottom > 0 ? block->bound->padding.bottom : 0;
    float padding_left = block->bound && block->bound->padding.left > 0 ? block->bound->padding.left : 0;
    float padding_right = block->bound && block->bound->padding.right > 0 ? block->bound->padding.right : 0;
    float border_top = block->bound && block->bound->border ? block->bound->border->width.top : 0;
    float border_bottom = block->bound && block->bound->border ? block->bound->border->width.bottom : 0;
    float border_left = block->bound && block->bound->border ? block->bound->border->width.left : 0;
    float border_right = block->bound && block->bound->border ? block->bound->border->width.right : 0;

    block->content_width = block->width;
    block->content_height = block->height;
    block->width += padding_left + padding_right + border_left + border_right;
    block->height += padding_top + padding_bottom + border_top + border_bottom;

    log_debug("%s SVG layout result: content=%.1fx%.1f, total=%.1fx%.1f", block->source_loc(),
              block->content_width, block->content_height, block->width, block->height);
}

/**
 * Insert pseudo-element into DOM tree at appropriate position
 * ::before is inserted as first child, ::after as last child
 */
void insert_pseudo_into_dom(DomElement* parent, DomElement* pseudo, bool is_before) {
    if (!parent || !pseudo) return;

    // Guard against re-insertion during reflow: if pseudo is already a child
    // of parent, skip insertion to prevent creating circular sibling links
    // (e.g., marker->next_sibling = marker) which cause infinite loops.
    for (DomNode* c = parent->first_child; c; c = c->next_sibling) {
        if (c == (DomNode*)pseudo) return;
    }

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

    // IMPORTANT: Do NOT share parent's FontProp pointer with pseudo-element!
    // If we set pseudo_elem->font = parent_elem->font, then when the pseudo-element's
    // font-size (e.g., 1.2em) is resolved, it would modify the shared FontProp,
    // incorrectly changing the parent's font-size as well.
    // Instead, leave pseudo_elem->font = nullptr so that style resolution will
    // allocate a new FontProp via alloc_font_prop(), which properly copies from
    // lycon->font.style (the parent's computed font values).
    pseudo_elem->font = nullptr;
    pseudo_elem->in_line = parent_elem->in_line;

    // Log font inheritance for debugging
    log_debug("[Pseudo-Element] font=nullptr for %s (will be allocated during style resolution)",
              is_before ? "::before" : "::after");

    // Copy pseudo-element-specific styles (::before or ::after styles)
    pseudo_elem->specified_style = is_before ? parent_elem->before_styles : parent_elem->after_styles;

    // Handle different content types
    switch (content_type) {
        case CONTENT_TYPE_COUNTER:
        case CONTENT_TYPE_COUNTERS:
            // Counter content already resolved to string by
            // dom_element_get_pseudo_element_content_with_counters()
            // in alloc_pseudo_content_prop(). Fall through to STRING handling.
        case CONTENT_TYPE_ATTR:
            // attr() content already resolved to string by the same path.
            // Fall through to STRING handling.
        case CONTENT_TYPE_STRING: {
            // Create Lambda String for the content
            size_t content_len = strlen(content);
            String* text_string = (String*)arena_alloc(parent_elem->doc->arena,
                                                        sizeof(String) + content_len + 1);
            if (text_string) {
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
void prescan_and_layout_floats(LayoutContext* lycon, DomNode* first_child, ViewBlock* parent_block) {
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
    // CSS 2.1 §9.5: Floats belong to their nearest BFC ancestor, not to non-BFC
    // parent blocks. Only set establishing_element if the parent ACTUALLY establishes
    // a BFC. Otherwise, use the BFC found via the parent chain. Setting it on non-BFC
    // blocks would cause finalize_block_flow to incorrectly expand height for floats.
    if (!lycon->block.establishing_element && parent_block) {
        // Check if this block establishes a BFC
        if (block_context_establishes_bfc(parent_block)) {
            lycon->block.establishing_element = parent_block;
            lycon->block.float_right_edge = parent_block->content_width > 0 ? parent_block->content_width : parent_block->width;
            log_debug("[FLOAT PRE-SCAN] Initialized BlockContext for BFC parent block %s",
                      parent_block->node_name());
        } else {
            // Non-BFC block: don't set establishing_element. Floats will be
            // registered to the ancestor BFC via block_context_find_bfc().
            log_debug("[FLOAT PRE-SCAN] Parent block %s is not BFC, not setting establishing_element",
                      parent_block->node_name());
        }
    }

    // Float pre-scanning can still proceed without establishing_element on THIS block,
    // because layout_block → layout_block_content will find the BFC via parent chain.
    // Only bail out if there's no BFC at all in the parent chain.
    BlockContext* prescan_bfc = block_context_find_bfc(&lycon->block);
    if (!prescan_bfc || !prescan_bfc->establishing_element) {
        log_debug("[FLOAT PRE-SCAN] No BFC found in parent chain, cannot pre-scan");
        return;
    }

    log_debug("[FLOAT PRE-SCAN] Pre-laying floats before first non-floated block");

    // Save advance_y before prescan — <br> clear points advance it during prescan
    // for correct float positioning, but it must be restored so the main inline
    // flow loop starts at the original y and processes clears itself.
    float saved_advance_y = lycon->block.advance_y;

    // Pre-lay floats ONLY until we encounter a non-floated block element
    // CSS 2.1 §9.5.1 Rule 6: "The outer top of a floating box may not be higher than
    // the outer top of any block or floated box generated by an element earlier in
    // the source document."
    // This means floats that come AFTER a non-floated block in source order must
    // appear at or below that block's top edge - they cannot be pre-scanned to y=0.
    for (DomNode* child = first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;

        DomElement* elem = child->as_element();

        // Skip if already pre-laid
        if (elem->float_prelaid) continue;

        // Check display:none first - hidden elements should not participate in float layout
        DisplayValue display = resolve_display_value(child);
        if (display.outer == CSS_VALUE_NONE) continue;

        CssEnum float_value = get_element_float_value(elem);

        // If this is a non-floated block, stop pre-scanning
        // Subsequent floats must be laid out in normal flow order
        if (float_value != CSS_VALUE_LEFT && float_value != CSS_VALUE_RIGHT) {
            if (display.outer == CSS_VALUE_BLOCK) {
                log_debug("[FLOAT PRE-SCAN] Encountered non-floated block %s, stopping pre-scan",
                          child->node_name());
                break;  // Stop pre-scanning - remaining floats go through normal flow
            }
            // CSS 2.1 §9.5.2: <br> with clear property forces subsequent floats to
            // appear below cleared floats. Advance advance_y past existing floats so
            // subsequent floats in the prescan are positioned on a new row.
            if (elem->tag() == HTM_TAG_BR && elem->specified_style && elem->specified_style->tree) {
                AvlNode* clear_node = avl_tree_search(elem->specified_style->tree, CSS_PROPERTY_CLEAR);
                if (clear_node) {
                    StyleNode* sn = (StyleNode*)clear_node->declaration;
                    if (sn && sn->winning_decl && sn->winning_decl->value &&
                        sn->winning_decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
                        sn->winning_decl->value->data.keyword != CSS_VALUE_NONE) {
                        CssEnum clear_type = sn->winning_decl->value->data.keyword;
                        BlockContext* bfc = block_context_find_bfc(&lycon->block);
                        if (bfc) {
                            float clear_y = block_context_clear_y(bfc, clear_type);
                            float local_clear_y = clear_y - lycon->block.bfc_offset_y;
                            if (local_clear_y > lycon->block.advance_y) {
                                log_debug("[FLOAT PRE-SCAN] <br> clear: advance_y %.1f -> %.1f",
                                          lycon->block.advance_y, local_clear_y);
                                lycon->block.advance_y = local_clear_y;
                            }
                        }
                    }
                }
            }
            continue;  // Skip non-float non-block elements
        }

        log_debug("[FLOAT PRE-SCAN] Pre-laying float: %s (float=%d)",
                  child->node_name(), float_value);

        // Layout the float now
        display.outer = CSS_VALUE_BLOCK;  // Floats become block per CSS 9.7

        // Mark as pre-laid to skip during normal flow
        elem->float_prelaid = true;

        // Layout the float block
        layout_block(lycon, child, display);
    }

    // Restore advance_y to pre-prescan value so the main inline flow starts at
    // the original y. The <br> clear handling in layout_inline() will re-advance
    // advance_y as the inline content is processed.
    lycon->block.advance_y = saved_advance_y;

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

    // Reset abs-child linked list so multiple layout passes (e.g., flex measurement
    // + flex final) don't accumulate duplicates and create a cycle in
    // re_resolve_abs_children_vertical. Absolute children re-register themselves
    // via layout_abs_block during this layout pass.
    if (block->position) {
        // Walk existing list and clear next_abs_sibling on all entries so stale
        // pointers can't form a cycle if this block re-enters layout.
        ViewBlock* abs_walker = block->position->first_abs_child;
        while (abs_walker) {
            ViewBlock* abs_next = abs_walker->position ? abs_walker->position->next_abs_sibling : nullptr;
            if (abs_walker->position) abs_walker->position->next_abs_sibling = nullptr;
            abs_walker = abs_next;
        }
        block->position->first_abs_child = nullptr;
        block->position->last_abs_child = nullptr;
    }

    // Allocate pseudo-element content if ::before or ::after is present
    if (block->is_element()) {
        block->pseudo = alloc_pseudo_content_prop(lycon, block);

        // Generate pseudo-element content from CSS content property (CSS 2.1 Section 12.2)
        // Must be done AFTER alloc_pseudo_content_prop populates the content/type fields
        generate_pseudo_element_content(lycon, block, true);   // ::before
        generate_pseudo_element_content(lycon, block, false);  // ::after

        // Insert pseudo-elements into DOM tree for proper view tree linking
        if (block->pseudo) {
            // Insert ::marker first (before ::before), as it's the first box in list items
            if (block->pseudo->marker) {
                insert_pseudo_into_dom((DomElement*)block, block->pseudo->marker, true);
            }
            if (block->pseudo->before) {
                insert_pseudo_into_dom((DomElement*)block, block->pseudo->before, true);
            }
            if (block->pseudo->after) {
                insert_pseudo_into_dom((DomElement*)block, block->pseudo->after, false);
            }
        }

        // Handle ::first-letter pseudo-element (CSS 2.1 §5.12.2)
        // Must be done AFTER ::before insertion so ::before content is already in the tree
        // (::first-letter applies to the first letter of the element including ::before content)
        if (((DomElement*)block)->first_letter_styles) {
            create_first_letter_pseudo(lycon, block);
        }
    }

    if (block->display.inner == RDT_DISPLAY_REPLACED) {  // image, iframe, hr, form controls, SVG
        uintptr_t elmt_name = block->tag();
        if (elmt_name == HTM_TAG_IFRAME) {
            layout_iframe(lycon, block, block->display);
        }
        else if (elmt_name == HTM_TAG_SVG) {
            // Inline SVG element: use width/height attributes or viewBox for intrinsic size
            layout_inline_svg(lycon, block);
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
                log_debug("%s hr layout: explicit height=%f, total=%f", block->source_loc(), content_height, block->height);
            } else {
                // No explicit height - use border thickness (traditional hr behavior)
                float border_top = block->bound && block->bound->border ? block->bound->border->width.top : 0;
                float border_bottom = block->bound && block->bound->border ? block->bound->border->width.bottom : 0;
                block->height = border_top + border_bottom;
                log_debug("%s hr layout: border-only height=%f", block->source_loc(), block->height);
            }
        }
        else if (block->item_prop_type == DomElement::ITEM_PROP_FORM && block->form &&
                 elmt_name != HTM_TAG_BUTTON) {
            // Form control elements (input, select, textarea) - replaced elements with intrinsic size
            // Note: <button> elements have content children, so they go through normal layout flow
            layout_form_control(lycon, block);
        }
        // else HTM_TAG_IMG - handled by layout_block_content width/height
    } else if (block->item_prop_type == DomElement::ITEM_PROP_FORM && block->form &&
               block->tag() != HTM_TAG_BUTTON) {
        // Form control fallback (for cases where display.inner != RDT_DISPLAY_REPLACED)
        layout_form_control(lycon, block);
    } else {  // layout block child content
        // No longer need separate pseudo-element layout - they're part of child list now
        DomNode *child = nullptr;
        if (block->is_element()) { child = block->first_child; }
        if (child) {
            // CSS 2.1 §17.2.1: Orphaned table-internal elements (table-row, table-cell, etc.)
            // inside non-table contexts should be treated as block+flow for layout purposes.
            // This handles cases like floated table-row-group containing table-row/table-cell.
            bool is_orphaned_table_internal =
                block->display.inner == CSS_VALUE_TABLE_ROW ||
                block->display.inner == CSS_VALUE_TABLE_ROW_GROUP ||
                block->display.inner == CSS_VALUE_TABLE_HEADER_GROUP ||
                block->display.inner == CSS_VALUE_TABLE_FOOTER_GROUP ||
                block->display.inner == CSS_VALUE_TABLE_COLUMN ||
                block->display.inner == CSS_VALUE_TABLE_COLUMN_GROUP ||
                block->display.inner == CSS_VALUE_TABLE_CELL ||
                block->display.inner == CSS_VALUE_TABLE_CAPTION;

            // CSS 2.1 §17.2.1: Before flow layout, check if any children are orphaned
            // table-internal elements (table-cell, table-row, etc.) that need wrapping
            // in anonymous table structures. This must happen before layout.
            if ((block->display.inner == CSS_VALUE_FLOW || block->display.inner == CSS_VALUE_FLOW_ROOT) && !is_orphaned_table_internal) {
                DomElement* block_elem = block->as_element();
                if (block_elem && wrap_orphaned_table_children(lycon, block_elem)) {
                    // Re-get first child after wrapping may have inserted anonymous elements
                    child = block->first_child;
                }
            }

            if (block->display.inner == CSS_VALUE_FLOW || block->display.inner == CSS_VALUE_FLOW_ROOT || is_orphaned_table_internal) {
                // Check for multi-column layout
                bool is_multicol = is_multicol_container(block);

                if (is_multicol) {
                    log_debug("%s [MULTICOL] Container detected: %s", block->source_loc(), block->node_name());
                    // Multi-column layout handles its own content distribution
                    layout_multicol_content(lycon, block);

                    finalize_block_flow(lycon, block, block->display.outer);
                    return;
                } else {
                    // Pre-scan and layout floats BEFORE laying out inline content
                    // This ensures floats are positioned and affect line bounds correctly
                    prescan_and_layout_floats(lycon, child, block);

                    // CSS 2.1 §16.1: text-indent is "with respect to the left (or right)
                    // edge of the line box". The line box edge may have been moved by
                    // float pre-scan AFTER text-indent was applied in line_reset().
                    // Query the actual float-adjusted left edge and re-apply text-indent
                    // on top of it (making them additive, not overlapping).
                    if (!lycon->block.is_first_line && lycon->block.text_indent != 0 &&
                        lycon->block.direction != CSS_VALUE_RTL) {
                        BlockContext* bfc = block_context_find_bfc(&lycon->block);
                        if (bfc && (bfc->left_float_count > 0 || bfc->right_float_count > 0)) {
                            float bfc_y = lycon->block.bfc_offset_y + lycon->block.advance_y;
                            float line_h = lycon->block.line_height > 0 ? lycon->block.line_height : 16.0f;
                            FloatAvailableSpace space = block_context_space_at_y(bfc, bfc_y, line_h);
                            if (space.has_left_float) {
                                float float_left = space.left - lycon->block.bfc_offset_x;
                                float target = float_left + lycon->block.text_indent;
                                if (target > lycon->line.advance_x) {
                                    lycon->line.advance_x = target;
                                    lycon->line.effective_left = target;
                                    lycon->line.has_float_intrusion = true;
                                    log_debug("%s text-indent + float: advance_x=%.1f (float_left=%.1f + indent=%.1f)", block->source_loc(),
                                              target, float_left, lycon->block.text_indent);
                                }
                            }
                        }
                    }

                    // inline content flow
                    do {
                        float pre_advance_y = lycon->block.advance_y;
                        // Phase 16: skip unchanged block elements in incremental layout
                        if (lycon->doc && lycon->doc->incremental_layout
                            && child->is_element() && !child->layout_dirty
                            && child->height > 0 && child->view_type != RDT_VIEW_NONE) {
                            DomElement* skip_elem = (DomElement*)child;
                            // Adjust y position (may have shifted due to upstream changes)
                            skip_elem->y = lycon->block.advance_y;
                            // Advance by stored contribution from previous layout pass
                            lycon->block.advance_y += skip_elem->layout_height_contribution;
                            // Track width for parent shrink-to-fit
                            if (skip_elem->bound) {
                                lycon->block.max_width = max(lycon->block.max_width,
                                    lycon->line.left + skip_elem->width
                                    + skip_elem->bound->margin.left + skip_elem->bound->margin.right);
                            } else {
                                lycon->block.max_width = max(lycon->block.max_width,
                                    lycon->line.left + skip_elem->width);
                            }
                            log_info("[TIMING] Phase 16: skip unchanged subtree %s (h=%.1f, contrib=%.1f)",
                                skip_elem->source_loc(), skip_elem->height, skip_elem->layout_height_contribution);
                        } else {
                            layout_flow_node(lycon, child);
                        }
                        // Phase 16: save height contribution for future incremental passes
                        child->layout_height_contribution = lycon->block.advance_y - pre_advance_y;
                        child = child->next_sibling;
                    } while (child);
                    // handle last line
                    if (!lycon->line.is_line_start) {
                        lycon->line.is_last_line = true;
                        line_break(lycon);
                    }
                }
            }
            else if (block->display.inner == CSS_VALUE_FLEX) {
                auto t_flex_start = high_resolution_clock::now();
                log_debug("Setting up flex container for %s", block->source_loc());
                layout_flex_content(lycon, block);
                g_flex_layout_time += duration<double, std::milli>(high_resolution_clock::now() - t_flex_start).count();

                // After flex layout, update content_height/advance_y from container height
                // so that parent containers (like iframes) get the correct scroll height
                lycon->block.advance_y = block->height;
                if (block->bound && block->bound->border) {
                    lycon->block.advance_y -= block->bound->border->width.bottom;
                }
                if (block->bound) {
                    lycon->block.advance_y -= block->bound->padding.bottom;
                }
                log_debug("%s FLEX FINALIZE: Updated advance_y=%.1f from block->height=%.1f", block->source_loc(),
                    lycon->block.advance_y, block->height);

                // CSS Flexbox §9.9.1: For inline-flex with auto width, compute
                // shrink-to-fit width from the positioned flex items. The flex algorithm
                // uses the full available width as main_axis_size, so items are positioned
                // with flex-start (default). The actual content width is the rightmost
                // edge of any flex item (including its margin).
                if (block->display.outer == CSS_VALUE_INLINE_BLOCK &&
                    (!block->blk || block->blk->given_width < 0)) {
                    float max_right = 0;
                    for (View* child = block->first_child; child; child = child->next_sibling) {
                        if (child->view_type == RDT_VIEW_BLOCK ||
                            child->view_type == RDT_VIEW_INLINE_BLOCK ||
                            child->view_type == RDT_VIEW_LIST_ITEM) {
                            ViewElement* item = (ViewElement*)child->as_element();
                            if (item) {
                                // Skip absolutely positioned children (not flex items per §4.1)
                                ViewBlock* vb = (ViewBlock*)item;
                                if (vb->position && vb->position->position &&
                                    (vb->position->position == CSS_VALUE_ABSOLUTE ||
                                     vb->position->position == CSS_VALUE_FIXED)) {
                                    continue;
                                }
                                float right = item->x + item->width;
                                if (item->bound) {
                                    right += item->bound->margin.right;
                                }
                                if (right > max_right) max_right = right;
                            }
                        }
                    }
                    if (max_right > 0) {
                        lycon->block.max_width = max_right;
                        log_debug("%s INLINE-FLEX: computed max_width %.1f from flex items", block->source_loc(),
                                  lycon->block.max_width);
                    }
                }

                finalize_block_flow(lycon, block, block->display.outer);
                return;
            }
            else if (block->display.inner == CSS_VALUE_GRID) {
                auto t_grid_start = high_resolution_clock::now();
                log_debug("Setting up grid container for %s (multipass)", block->source_loc());
                // Use multipass grid layout (similar to flex layout pattern)
                layout_grid_content(lycon, block);
                log_debug("Finished grid container layout for %s", block->source_loc());
                g_grid_layout_time += duration<double, std::milli>(high_resolution_clock::now() - t_grid_start).count();

                // After grid layout, update content_height/advance_y from container height
                // so that parent containers (like iframes) get the correct scroll height
                lycon->block.advance_y = block->height;
                if (block->bound && block->bound->border) {
                    lycon->block.advance_y -= block->bound->border->width.bottom;
                }
                if (block->bound) {
                    lycon->block.advance_y -= block->bound->padding.bottom;
                }
                log_debug("%s GRID FINALIZE: Updated advance_y=%.1f from block->height=%.1f", block->source_loc(),
                    lycon->block.advance_y, block->height);

                // CSS Grid §12.1: For inline-grid with auto width, compute
                // shrink-to-fit width from the positioned grid items (same as inline-flex).
                if (block->display.outer == CSS_VALUE_INLINE_BLOCK &&
                    (!block->blk || block->blk->given_width < 0)) {
                    float max_right = 0;
                    for (View* child = block->first_child; child; child = child->next_sibling) {
                        if (child->view_type == RDT_VIEW_BLOCK ||
                            child->view_type == RDT_VIEW_INLINE_BLOCK ||
                            child->view_type == RDT_VIEW_LIST_ITEM) {
                            ViewElement* item = (ViewElement*)child->as_element();
                            if (item) {
                                // Skip absolutely positioned children (not grid items)
                                ViewBlock* vb = (ViewBlock*)item;
                                if (vb->position && vb->position->position &&
                                    (vb->position->position == CSS_VALUE_ABSOLUTE ||
                                     vb->position->position == CSS_VALUE_FIXED)) {
                                    continue;
                                }
                                float right = item->x + item->width;
                                if (item->bound) {
                                    right += item->bound->margin.right;
                                }
                                if (right > max_right) max_right = right;
                            }
                        }
                    }
                    if (max_right > 0) {
                        lycon->block.max_width = max_right;
                        log_debug("%s INLINE-GRID: computed max_width %.1f from grid items", block->source_loc(),
                                  lycon->block.max_width);
                    }
                }

                finalize_block_flow(lycon, block, block->display.outer);
                return;
            }
            else if (block->display.inner == CSS_VALUE_TABLE) {
                auto t_table_start = high_resolution_clock::now();
                log_debug("%s TABLE LAYOUT TRIGGERED! outer=%d, inner=%d, element=%s", block->source_loc(),
                    block->display.outer, block->display.inner, block->node_name());
                layout_table_content(lycon, block, block->display);
                g_table_layout_time += duration<double, std::milli>(high_resolution_clock::now() - t_table_start).count();

                // After table layout, update content_height/advance_y from container height
                // so that parent containers (like iframes) get the correct scroll height
                lycon->block.advance_y = block->height;
                if (block->bound && block->bound->border) {
                    lycon->block.advance_y -= block->bound->border->width.bottom;
                }
                if (block->bound) {
                    lycon->block.advance_y -= block->bound->padding.bottom;
                }
                finalize_block_flow(lycon, block, block->display.outer);

                // CSS 2.1 §17.5.2: Tables shrink-to-fit their content (unlike block elements
                // which stretch to container width). For auto-width tables, update block->width
                // from the actual table width computed by table_auto_layout.
                // Pre-layout initialization sets block->width = container_width, which is wrong
                // for auto-width tables narrower than their container (e.g., empty tables).
                // finalize_block_flow already computed flow_width from lycon->block.max_width
                // (which includes pad.left + border.left) + pad.right + border.right.
                // Just use that flow_width (= content_width + border.right) here.
                if (!block->blk || block->blk->given_width < 0) {
                    // flow_width = max_width + padding.right + border.right (per finalize_block_flow)
                    float shrink_width = block->content_width +
                        (block->bound && block->bound->border ? block->bound->border->width.right : 0);
                    // CSS Tables 3: table layout already handles max-width during column
                    // distribution (respecting min-content floor). Only apply min-width here.
                    if (block->blk && block->blk->given_min_width >= 0 && shrink_width < block->blk->given_min_width)
                        shrink_width = block->blk->given_min_width;
                    block->width = shrink_width;
                    log_debug("%s TABLE shrink-to-fit: block->width=%.1f (content_width=%.1f)", block->source_loc(),
                              block->width, block->content_width);
                }

                // WORKAROUND: Save table height to global - it gets corrupted after return
                // This is a mysterious issue where the height field gets zeroed between
                // the return statement and the caller's next instruction
                g_layout_table_height = block->height;
                return;
            }
            else {
                log_debug("%s unknown display type", block->source_loc());
            }
        } else {
            // Empty container (no children) - still need to run flex/grid layout
            // for proper shrink-to-fit sizing (e.g., abs-pos flex with only border/padding)
            if (block->display.inner == CSS_VALUE_FLEX) {
                auto t_flex_start = high_resolution_clock::now();
                layout_flex_content(lycon, block);
                g_flex_layout_time += duration<double, std::milli>(high_resolution_clock::now() - t_flex_start).count();

                lycon->block.advance_y = block->height;
                if (block->bound && block->bound->border) {
                    lycon->block.advance_y -= block->bound->border->width.bottom;
                }
                if (block->bound) {
                    lycon->block.advance_y -= block->bound->padding.bottom;
                }
                finalize_block_flow(lycon, block, block->display.outer);
                return;
            }
            else if (block->display.inner == CSS_VALUE_GRID) {
                auto t_grid_start = high_resolution_clock::now();
                layout_grid_content(lycon, block);
                g_grid_layout_time += duration<double, std::milli>(high_resolution_clock::now() - t_grid_start).count();

                lycon->block.advance_y = block->height;
                if (block->bound && block->bound->border) {
                    lycon->block.advance_y -= block->bound->border->width.bottom;
                }
                if (block->bound) {
                    lycon->block.advance_y -= block->bound->padding.bottom;
                }
                finalize_block_flow(lycon, block, block->display.outer);
                return;
            }
            else if (block->display.inner == CSS_VALUE_TABLE) {
                auto t_table_start = high_resolution_clock::now();
                layout_table_content(lycon, block, block->display);
                g_table_layout_time += duration<double, std::milli>(high_resolution_clock::now() - t_table_start).count();

                lycon->block.advance_y = block->height;
                if (block->bound && block->bound->border) {
                    lycon->block.advance_y -= block->bound->border->width.bottom;
                }
                if (block->bound) {
                    lycon->block.advance_y -= block->bound->padding.bottom;
                }
                finalize_block_flow(lycon, block, block->display.outer);

                // Shrink-to-fit: auto-width tables use their content width, not container width
                // lycon->block.max_width (= table->width from table_auto_layout) already includes
                // border.left + padding.left. finalize_block_flow adds padding.right + border.right
                // to get content_width and flow_width. Use flow_width here.
                if (!block->blk || block->blk->given_width < 0) {
                    float shrink_width = block->content_width +
                        (block->bound && block->bound->border ? block->bound->border->width.right : 0);
                    block->width = adjust_min_max_width(block, shrink_width);
                    log_debug("%s EMPTY TABLE shrink-to-fit: block->width=%.1f (content_width=%.1f)", block->source_loc(),
                              block->width, block->content_width);
                }

                g_layout_table_height = block->height;
                return;
            }
        }

        // Final line break after all content
        if (!lycon->line.is_line_start) {
            lycon->line.is_last_line = true;
            line_break(lycon);
        }

        // CSS Box 4 §3.1 margin-trim: block-end — trim the last in-flow child's
        // block-end margin. Applied after all children are laid out so we know
        // which child is last.
        if (block->blk && (block->blk->margin_trim & MARGIN_TRIM_BLOCK_END)) {
            View* last = block->last_placed_child();
            // skip floats and abspos from the end to find last in-flow child
            while (last && last->is_block()) {
                ViewBlock* lvb = (ViewBlock*)last;
                if (lvb->position && (element_has_float(lvb) ||
                    lvb->position->position == CSS_VALUE_ABSOLUTE ||
                    lvb->position->position == CSS_VALUE_FIXED)) {
                    last = last->prev_placed_view();
                    continue;
                }
                break;
            }
            if (last && last->is_block()) {
                ViewBlock* last_block = (ViewBlock*)last;
                if (last_block->bound) {
                    bool is_sc = is_block_self_collapsing(last_block);
                    if (is_sc) {
                        // CSS Box 4 §3.1: When the last in-flow child is self-collapsing,
                        // its margins (and any previous sibling margins that collapsed
                        // through it) form a trailing margin chain. Trim the entire chain
                        // by walking backward to find the last non-self-collapsing sibling
                        // and setting advance_y to its bottom edge.
                        View* prev = last_block;
                        while (prev) {
                            ViewBlock* vb = (ViewBlock*)prev;
                            if (vb->bound) {
                                vb->bound->margin.bottom = 0;
                                vb->bound->margin_chain_positive = 0;
                                vb->bound->margin_chain_negative = 0;
                            }
                            // find prev in-flow sibling
                            View* p = prev->prev_placed_view();
                            while (p && p->is_block()) {
                                ViewBlock* pb = (ViewBlock*)p;
                                if (pb->position && (element_has_float(pb) ||
                                    pb->position->position == CSS_VALUE_ABSOLUTE ||
                                    pb->position->position == CSS_VALUE_FIXED)) {
                                    p = p->prev_placed_view();
                                    continue;
                                }
                                break;
                            }
                            if (p && p->is_block() && is_block_self_collapsing((ViewBlock*)p)) {
                                prev = p;
                                continue;
                            }
                            // p is either non-self-collapsing block or null
                            if (p && p->is_block()) {
                                ViewBlock* anchor = (ViewBlock*)p;
                                // Trim anchor's margin.bottom too — it collapsed through
                                // the self-collapsing chain
                                if (anchor->bound) {
                                    anchor->bound->margin.bottom = 0;
                                    anchor->bound->margin_chain_positive = 0;
                                    anchor->bound->margin_chain_negative = 0;
                                }
                                lycon->block.advance_y = anchor->y + anchor->height;
                                log_debug("%s [MARGIN-TRIM] block-end: self-collapsing chain, "
                                    "set advance_y=%.1f from anchor %s (y=%.1f h=%.1f)", block->source_loc(),
                                    lycon->block.advance_y, anchor->node_name(),
                                    anchor->y, anchor->height);
                            } else {
                                // All children are self-collapsing — no content height
                                lycon->block.advance_y = 0;
                                log_debug("%s [MARGIN-TRIM] block-end: all children self-collapsing, advance_y=0", block->source_loc());
                            }
                            break;
                        }
                    } else {
                        // Non-self-collapsing last child: simple case
                        float trimmed = last_block->bound->margin.bottom;
                        if (trimmed != 0) {
                            log_debug("%s [MARGIN-TRIM] block-end: trimming margin.bottom=%f on last child %s", block->source_loc(),
                                trimmed, last_block->node_name());
                            lycon->block.advance_y -= trimmed;
                            last_block->bound->margin.bottom = 0;
                        }
                        last_block->bound->margin_chain_positive = 0;
                        last_block->bound->margin_chain_negative = 0;
                    }
                }
            }
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
        // CSS Box Model: In border-box, the box width cannot be smaller than
        // its padding+border. If min/max clamping reduces below padding+border,
        // floor at padding+border (content area becomes 0).
        if (block->blk->box_sizing == CSS_VALUE_BORDER_BOX && block->bound) {
            float pad_border = block->bound->padding.left + block->bound->padding.right;
            if (block->bound->border) {
                pad_border += block->bound->border->width.left + block->bound->border->width.right;
            }
            if (width < pad_border) {
                log_debug("[ADJUST] border-box floor: %.2f → %.2f (padding+border)", width, pad_border);
                width = pad_border;
            }
        }
    }
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
        // CSS Box Model: In border-box, the box height cannot be smaller than
        // its padding+border. If min/max clamping reduces below padding+border,
        // floor at padding+border (content area becomes 0).
        if (block->blk->box_sizing == CSS_VALUE_BORDER_BOX && block->bound) {
            float pad_border = block->bound->padding.top + block->bound->padding.bottom;
            if (block->bound->border) {
                pad_border += block->bound->border->width.top + block->bound->border->width.bottom;
            }
            if (height < pad_border) {
                height = pad_border;
            }
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
    return height;
}

void setup_inline(LayoutContext* lycon, ViewBlock* block) {
    // setup inline context
    float content_width = lycon->block.content_width;
    lycon->block.advance_y = 0;  lycon->block.max_width = 0;

    // CSS 2.1 §16.1: text-indent applies only to the first formatted line of a block container
    // Initialize is_first_line to true when starting a new block
    lycon->block.is_first_line = true;

    // Resolve text-indent: percentage needs containing block width (now available)
    float resolved_text_indent = 0.0f;
    if (block->blk) {
        if (block->blk->text_indent_calc) {
            // calc() expression with potential percentage - resolve now with correct basis
            // CSS Text 3: text-indent percentage resolves against the block's own content width
            // Temporarily set parent content_width so resolve_length_value uses the right basis
            float saved_parent_width = 0;
            bool has_parent = lycon->block.parent != nullptr;
            if (has_parent) {
                saved_parent_width = lycon->block.parent->content_width;
                lycon->block.parent->content_width = content_width;
            }
            resolved_text_indent = resolve_length_value(lycon, CSS_PROPERTY_TEXT_INDENT, block->blk->text_indent_calc);
            if (has_parent) {
                lycon->block.parent->content_width = saved_parent_width;
            }
            log_debug("setup_inline: resolved text-indent calc() -> %.1fpx (content_width=%.1f)",
                     resolved_text_indent, content_width);
        } else if (!isnan(block->blk->text_indent_percent)) {
            // Percentage text-indent: resolve against containing block width
            resolved_text_indent = content_width * block->blk->text_indent_percent / 100.0f;
            log_debug("setup_inline: resolved text-indent %.1f%% -> %.1fpx (content_width=%.1f)",
                     block->blk->text_indent_percent, resolved_text_indent, content_width);
        } else if (block->blk->text_indent != 0.0f) {
            // Fixed length text-indent
            resolved_text_indent = block->blk->text_indent;
        }
    }
    lycon->block.text_indent = resolved_text_indent;
    if (lycon->block.text_indent != 0.0f) {
        log_debug("setup_inline: text-indent=%.1fpx for block", lycon->block.text_indent);
    }

    // Calculate BFC offset for this block (used for float coordinate conversion)
    BlockContext* bfc = block_context_find_bfc(&lycon->block);
    if (bfc) {
        block_context_calc_bfc_offset((ViewElement*)block, bfc,
                                      &lycon->block.bfc_offset_x, &lycon->block.bfc_offset_y);

        // CSS 2.1 §8.3.1: When this block's margin-top will collapse with its parent's
        // margin-top (first in-flow child, parent has no top border/padding, parent is
        // not a BFC root), the parent's y has not yet been shifted to account for the
        // collapse. Floats registered from within the parent (siblings of this block)
        // were placed at the parent's pre-collapse y. The BFC offset computed above
        // includes this block's margin-top, which overshoots the stale float positions.
        // Correct the offset by removing the pending collapse margin so float intrusion
        // queries during inline layout use coordinates consistent with the stale floats.
        //
        // This correction must NOT be applied when this block itself contains float
        // children: in that case the block's own prescan will register those floats at
        // pre-collapse coordinates that are already consistent with the uncorrected offset.
        if (block->bound && block->bound->margin.top > 0 && (bfc->left_float_count > 0 || bfc->right_float_count > 0)) {
            ViewElement* parent = block->parent_view();
            if (parent && parent->parent_view() && parent->is_block()) {
                ViewBlock* pa = (ViewBlock*)parent;
                bool pa_creates_bfc = block_context_establishes_bfc(pa);
                float pa_border_top = pa->bound && pa->bound->border ? pa->bound->border->width.top : 0;
                float pa_padding_top = pa->bound ? pa->bound->padding.top : 0;
                // First in-flow child: block->y == margin.top means parent advance_y was 0
                bool is_first_inflow = (block->y == block->bound->margin.top);
                if (!pa_creates_bfc && pa_border_top == 0 && pa_padding_top == 0 && is_first_inflow) {
                    // Check if this block has float children (quick DOM scan).
                    // Use get_element_float_value() which checks both resolved
                    // position and unresolved CSS style tree properties.
                    bool has_float_children = false;
                    for (DomNode* ch = block->first_child; ch; ch = ch->next_sibling) {
                        if (ch->is_element()) {
                            CssEnum fv = get_element_float_value(ch->as_element());
                            if (fv == CSS_VALUE_LEFT || fv == CSS_VALUE_RIGHT) {
                                has_float_children = true;
                                break;
                            }
                        }
                    }
                    if (!has_float_children) {
                        lycon->block.bfc_offset_y -= block->bound->margin.top;
                        log_debug("setup_inline: pending parent-child collapse correction: "
                                  "bfc_offset_y reduced by %.1f to %.1f",
                                  block->bound->margin.top, lycon->block.bfc_offset_y);
                    }
                }
            }
        }
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
    if (block->blk) lycon->block.text_align = block->blk->text_align;
    if (block->blk) lycon->block.text_align_last = block->blk->text_align_last;
    // CSS 2.1 §9.2.1: Propagate direction to block context
    // Must be set BEFORE line_reset() so text-indent RTL handling works correctly
    if (block->blk) lycon->block.direction = block->blk->direction;

    lycon->line.vertical_align = CSS_VALUE_BASELINE;

    // Now call line_reset to adjust for floats at current Y position
    // This will call adjust_line_for_floats which updates effective_left/right
    line_reset(lycon);

    log_debug("setup_inline: line.left=%.1f, line.right=%.1f, effective_left=%.1f, effective_right=%.1f, advance_x=%.1f",
              lycon->line.left, lycon->line.right, lycon->line.effective_left, lycon->line.effective_right, lycon->line.advance_x);
    // setup font
    if (block->font) {
        setup_font(lycon->ui_context, &lycon->font, block->font);
    }
    // CSS Text 3 §4.2: save the block container's font for tab-size calculation.
    // Tab stops use "the advance width of the space character as rendered by the
    // block's font", not the inline element's font.
    lycon->block.block_container_font = lycon->font.style;
    // CSS 2.1 §10.8.1: Update line_start_font to the block's own font, since
    // line_reset() was called before setup_font() and captured the parent's font.
    // The strut baseline detection needs the block's font, not the parent's.
    lycon->line.line_start_font = lycon->font;
    // setup line height
    setup_line_height(lycon, block);

    // setup initial ascender and descender
    // CSS 2.1 §10.8.1: The strut is a zero-width inline box with the block's font.
    // For line-height:normal, the strut's above-baseline extent = font ascent.
    // The strut contributes NO below-baseline extent (init_descender=0); actual text
    // content adds its own descender via output_text → font_get_normal_lh_split.
    // This matches Chrome/Blink behavior where lines containing only replaced elements
    // (no text) have zero descender below the replaced element's bottom margin edge.
    if (lycon->font.font_handle) {
        if (lycon->block.line_height_is_normal) {
            float split_asc = 0, split_desc = 0;
            font_get_normal_lh_split(lycon->font.font_handle, &split_asc, &split_desc);
            lycon->block.init_ascender = split_asc;
            lycon->block.init_descender = split_desc;
            log_debug("init_metrics (normal, split): asc=%f, desc=%f", split_asc, split_desc);
        } else {
            TypoMetrics typo = get_os2_typo_metrics(lycon->font.font_handle);
            if (typo.valid && typo.use_typo_metrics) {
                lycon->block.init_ascender = typo.ascender;
                lycon->block.init_descender = typo.descender;
                log_debug("init_metrics (typo): asc=%f, desc=%f", typo.ascender, typo.descender);
            } else {
                const FontMetrics* m = font_get_metrics(lycon->font.font_handle);
                if (m) {
                    lycon->block.init_ascender = m->hhea_ascender;
                    lycon->block.init_descender = -(m->hhea_descender);
                    log_debug("init_metrics (hhea): asc=%f, desc=%f", m->hhea_ascender, -(m->hhea_descender));
                }
            }
        }
    }
    lycon->block.lead_y = max(0.0f, (lycon->block.line_height - (lycon->block.init_ascender + lycon->block.init_descender)) / 2);
    const FontMetrics* fm = lycon->font.font_handle ? font_get_metrics(lycon->font.font_handle) : NULL;
    float font_height = fm ? fm->hhea_line_height : 0;
    log_debug("block line_height: %f, font height: %f, asc+desc: %f, lead_y: %f", lycon->block.line_height, font_height,
        lycon->block.init_ascender + lycon->block.init_descender, lycon->block.lead_y);
}

// CSS 2.1 §9.4.2: Check if an inline subtree generates any line boxes.
// Returns true if the inline tree contains text, replaced content, or
// inline elements with non-zero margins/padding/borders.
static bool is_inline_substantial(ViewElement* ve) {
    if (ve->bound) {
        // CSS Inline 3 §2.1: An inline box is NOT invisible if ANY individual
        // inline-axis margin, border, or padding is non-zero.
        if (ve->bound->margin.left != 0 || ve->bound->margin.right != 0 ||
            ve->bound->padding.left != 0 || ve->bound->padding.right != 0) return true;
        if (ve->bound->border) {
            if (ve->bound->border->width.left != 0 || ve->bound->border->width.right != 0) return true;
        }
    }
    View* c = ve->first_placed_child();
    while (c) {
        if (c->view_type == RDT_VIEW_TEXT) return true;
        if (c->view_type == RDT_VIEW_INLINE) {
            if (is_inline_substantial((ViewElement*)c)) return true;
        } else if (c->view_type) {
            // BR, inline-block, image, etc.
            return true;
        }
        View* next = (View*)c->next_sibling;
        while (next && !next->view_type) next = (View*)next->next_sibling;
        c = next;
    }
    return false;
}

// CSS 2.1 §8.3.1: Check if an in-flow block can be considered self-collapsing
// for margin collapse purposes. A block is self-collapsing when it has:
// - zero height, no border, no padding
// - no line boxes (text/inline content) in itself or any descendant blocks
// - all in-flow children are also self-collapsing
// This allows margins to collapse "through" the element via the margin chain.
static bool is_block_self_collapsing(ViewBlock* vb) {
    if (vb->height > 0) return false;
    // Tables and table internals are never self-collapsing (CSS 2.1 §17)
    if (vb->view_type == RDT_VIEW_TABLE || vb->view_type == RDT_VIEW_TABLE_ROW ||
        vb->view_type == RDT_VIEW_TABLE_ROW_GROUP || vb->view_type == RDT_VIEW_TABLE_CELL) return false;
    // CSS 2.1 §8.3.1: Self-collapsing applies only to block-level boxes.
    // Inline-blocks are inline-level and do not self-collapse.
    if (vb->view_type == RDT_VIEW_INLINE_BLOCK) return false;
    float bt = vb->bound && vb->bound->border ? vb->bound->border->width.top : 0;
    float bb = vb->bound && vb->bound->border ? vb->bound->border->width.bottom : 0;
    float pt = vb->bound ? vb->bound->padding.top : 0;
    float pb = vb->bound ? vb->bound->padding.bottom : 0;
    if (bt > 0 || bb > 0 || pt > 0 || pb > 0) return false;
    // BFC roots and floats don't self-collapse (CSS 2.1 §8.3.1)
    bool creates_bfc = vb->scroller &&
        (vb->scroller->overflow_x != CSS_VALUE_VISIBLE ||
         vb->scroller->overflow_y != CSS_VALUE_VISIBLE);
    if (creates_bfc) return false;
    if (vb->position && element_has_float(vb)) return false;
    // CSS Display 3: flow-root, flex, grid containers establish BFC
    if (vb->display.inner == CSS_VALUE_FLOW_ROOT ||
        vb->display.inner == CSS_VALUE_FLEX ||
        vb->display.inner == CSS_VALUE_GRID) return false;
    // Check children recursively
    View* child = ((ViewElement*)vb)->first_placed_child();
    while (child) {
        if (child->is_block()) {
            ViewBlock* cvb = (ViewBlock*)child;
            bool is_out_of_flow = (cvb->position && element_has_float(cvb)) ||
                (cvb->position && (cvb->position->position == CSS_VALUE_ABSOLUTE ||
                                   cvb->position->position == CSS_VALUE_FIXED));
            if (!is_out_of_flow && !is_block_self_collapsing(cvb)) return false;
        } else {
            // CSS 2.1 §9.4.2: Line boxes that contain no text, no preserved
            // white space, no inline elements with non-zero margins, padding,
            // or borders, and no other in-flow content must be treated as not
            // existing. Such empty inline/text children don't prevent self-collapse.
            bool is_substantial = false;
            if (child->view_type == RDT_VIEW_INLINE) {
                // Recursively check if inline element tree generates line boxes
                if (is_inline_substantial((ViewElement*)child)) is_substantial = true;
            } else if (child->view_type == RDT_VIEW_TEXT) {
                // Text view that survived whitespace collapsing — has actual content
                is_substantial = true;
            } else {
                // BR, inline-block, image, etc. — always substantial
                if (child->view_type == RDT_VIEW_MARKER) {
                    // CSS 2.2 §12.5 + §8.3.1: An outside marker with visible content
                    // (i.e., not list-style:none) prevents margin collapse-through.
                    // The marker pseudo-element generates content that, per browser behavior,
                    // makes the list-item non-self-collapsing even though the marker is
                    // positioned outside the principal box.
                    MarkerProp* mp = child->is_element() ? (MarkerProp*)((DomElement*)child)->blk : nullptr;
                    is_substantial = (mp != nullptr);  // marker exists = has content
                } else {
                    is_substantial = true;
                }
            }
            if (is_substantial) return false;
        }
        View* next = (View*)child->next_sibling;
        while (next && !next->view_type) next = (View*)next->next_sibling;
        child = next;
    }
    return true;
}

static int layout_block_content_count = 0;

__attribute__((noinline))
void layout_block_content(LayoutContext* lycon, ViewBlock* block, BlockContext *pa_block, Linebox *pa_line) {
    layout_block_content_count++;
    if (layout_block_content_count % 5000 == 0) {
        log_notice("layout_block_content: count=%d", layout_block_content_count);
    }

    block->x = pa_line->left;  block->y = pa_block->advance_y;

    // CSS 2.2 9.5.1: Float positioning relative to preceding content
    // When a float appears after inline content on the current line, it must be
    // positioned below the current line. The preceding inline content has already
    // been committed to the current line and cannot reflow around the float.
    // Placing the float at the current line's Y would cause overlap.
    bool is_float = block->position && (block->position->float_prop == CSS_VALUE_LEFT || block->position->float_prop == CSS_VALUE_RIGHT);

    if (is_float && !pa_line->is_line_start) {
        // Float appears after inline content - position below current line
        float line_height = pa_block->line_height > 0 ? pa_block->line_height : 18.0f;
        block->y = pa_block->advance_y + line_height;
        log_debug("%s Float positioned below current line: y=%.1f (advance_y=%.1f + line_height=%.1f)", block->source_loc(),
                  block->y, pa_block->advance_y, line_height);
    } else if (is_float) {
        log_debug("%s Float positioned at line start: y=%.1f", block->source_loc(), block->y);
    }

    log_debug("%s block init position (%s): x=%f, y=%f, pa_block.advance_y=%f, display: outer=%d, inner=%d", block->source_loc(),
        block->node_name(), block->x, block->y, pa_block->advance_y, block->display.outer, block->display.inner);

    // Check if this block establishes a new BFC using unified BlockContext
    bool establishes_bfc = block_context_establishes_bfc(block);

    // CSS 2.1 Section 9.5: "The border box of a table, a block-level replaced element,
    // or an element in the normal flow that establishes a new block formatting context...
    // must not overlap the margin box of any floats in the same block formatting context
    // as the element itself."
    // Block-level replaced elements (like <img display:block>) must also avoid floats.
    bool is_block_level_replaced = (block->display.outer == CSS_VALUE_BLOCK &&
                                    block->display.inner == RDT_DISPLAY_REPLACED);

    bool is_normal_flow = !is_float &&
        (!block->position || (block->position->position != CSS_VALUE_ABSOLUTE &&
                              block->position->position != CSS_VALUE_FIXED));

    // Elements that must avoid floats: BFC roots, block-level replaced elements
    bool should_avoid_floats = (establishes_bfc || is_block_level_replaced) && is_normal_flow;

    // Query parent BFC for available space at current y position
    float bfc_float_offset_x = 0;
    float bfc_available_width_reduction = 0;
    bool bfc_width_was_reduced = false;  // true if auto-width was reduced for float avoidance
    float bfc_shift_down = 0;  // Amount to shift down if element doesn't fit beside floats
    BlockContext* parent_bfc = nullptr;

    if (should_avoid_floats) {
        // Find the BFC root from the parent context - that's where floats are registered
        parent_bfc = block_context_find_bfc(pa_block);
        if (parent_bfc && (parent_bfc->left_float_count > 0 || parent_bfc->right_float_count > 0)) {
            // Calculate this block's position in BFC coordinates
            // block->y is relative to parent's content area, need to convert to BFC coordinates
            // CSS 2.1 §9.5: Float avoidance is based on the border edge, so include margin-top
            // (margin is not yet added to block->y at this point — it's added later)
            float block_margin_top = block->bound ? block->bound->margin.top : 0;

            // CSS 2.1 §8.3.1: If this block's margin-top will collapse with its parent
            // (first in-flow child, parent has no top border/padding, parent is not BFC root),
            // then don't add margin_top to y_in_bfc. The parent will shift later to account
            // for the collapsed margin, and the float was registered at pre-collapse coords.
            // Adding margin_top here would make y_in_bfc overshoot the float, hiding the overlap.
            bool margin_will_collapse_with_parent = false;
            if (block_margin_top > 0 && block->y == 0) {
                ViewBlock* pa = block->parent_view()->is_block() ? (ViewBlock*)block->parent_view() : nullptr;
                if (pa && pa->parent_view()) {
                    bool pa_creates_bfc = block_context_establishes_bfc(pa);
                    float pa_border_top = pa->bound && pa->bound->border ? pa->bound->border->width.top : 0;
                    float pa_padding_top = pa->bound ? pa->bound->padding.top : 0;
                    if (!pa_creates_bfc && pa_border_top == 0 && pa_padding_top == 0) {
                        margin_will_collapse_with_parent = true;
                        log_debug("%s BFC float avoidance: margin_top=%.1f will collapse with parent, "
                                  "using y_in_bfc without margin", block->source_loc(), block_margin_top);
                    }
                }
            }

            float effective_margin = margin_will_collapse_with_parent ? 0 : block_margin_top;
            float y_in_bfc = block->y + effective_margin;
            float x_in_bfc = block->x;

            // Walk up from parent to BFC establishing element, accumulating offsets
            ViewElement* walker = block->parent_view();
            while (walker && walker != parent_bfc->establishing_element) {
                y_in_bfc += walker->y;
                x_in_bfc += walker->x;
                walker = walker->parent_view();
            }

            // Get the element's actual width requirement
            // For elements with explicit CSS width, use that; otherwise use parent width
            float element_required_width = pa_block->content_width;
            bool has_explicit_width = false;

            // Check block->blk for CSS width (resolved by dom_node_resolve_style)
            if (block->blk) {
                if (block->blk->given_width > 0) {
                    // Explicit width in px
                    element_required_width = block->blk->given_width;
                    has_explicit_width = true;
                } else if (!isnan(block->blk->given_width_percent)) {
                    // Percentage width - resolve against parent
                    element_required_width = pa_block->content_width * block->blk->given_width_percent / 100.0f;
                    has_explicit_width = true;
                }
            }

            // Add margins if they're explicitly set (not auto)
            if (has_explicit_width && block->bound) {
                if (block->bound->margin.left_type != CSS_VALUE_AUTO)
                    element_required_width += block->bound->margin.left;
                if (block->bound->margin.right_type != CSS_VALUE_AUTO)
                    element_required_width += block->bound->margin.right;
            }

            // CSS 2.1 §9.5: "The border box... must not overlap the margin box of
            // any floats." We must check across the element's FULL vertical extent, not
            // just at the top edge. Compute border-box height for the space query.
            float element_border_box_height = 1.0f;  // fallback for auto-height
            if (block->blk && block->blk->given_height >= 0) {
                // Explicit CSS height → compute border-box height
                element_border_box_height = block->blk->given_height;
                if (block->bound) {
                    element_border_box_height += block->bound->padding.top + block->bound->padding.bottom;
                    if (block->bound->border) {
                        element_border_box_height += block->bound->border->width.top + block->bound->border->width.bottom;
                    }
                }
            } else if (parent_bfc->lowest_float_bottom > y_in_bfc) {
                // Auto-height: conservatively check from top to lowest float bottom.
                // CSS 2.1 §9.5 requires no overlap with ANY float. Since we don't know
                // the final height yet, check the entire float range to be safe.
                element_border_box_height = parent_bfc->lowest_float_bottom - y_in_bfc;
            }

            log_debug("%s [BFC Float Avoid] element %s: required_width=%.1f, has_explicit_width=%d, y_in_bfc=%.1f, border_box_h=%.1f", block->source_loc(),
                      block->node_name(), element_required_width, has_explicit_width, y_in_bfc, element_border_box_height);

            // For elements WITHOUT explicit width, they can shrink to fit - no need to shift down
            // For elements WITH explicit width, shift down if they don't fit
            float current_y = y_in_bfc;

            if (has_explicit_width) {
                // Check if element fits at current Y position
                // If not, shift down like floats do
                int max_iterations = 100;

                while (max_iterations-- > 0) {
                    // CSS 2.1 §9.5: Use element's full border-box height for space query
                    // to ensure no overlap at any vertical position
                    float query_height = element_border_box_height;
                    if (block->blk && block->blk->given_height < 0 && parent_bfc->lowest_float_bottom > current_y) {
                        // Auto-height: recalculate from current_y to lowest float
                        query_height = parent_bfc->lowest_float_bottom - current_y;
                    }
                    FloatAvailableSpace space = block_context_space_at_y(parent_bfc, current_y, query_height);

                    // Calculate how much space is available in the PARENT's content area
                    // (not the BFC's full width, which may be much larger)
                    float local_left = space.left - x_in_bfc;  // Float edge in local coords
                    float local_right = space.right - x_in_bfc;  // Right edge in local coords

                    // Clamp to parent's content area bounds
                    float parent_left_bound = 0;
                    float parent_right_bound = pa_block->content_width;

                    float effective_left = max(local_left, parent_left_bound);
                    float effective_right = min(local_right, parent_right_bound);
                    float available_width = max(0.0f, effective_right - effective_left);

                    log_debug("%s [BFC Float Avoid] Checking y=%.1f (h=%.1f): space=(%.1f,%.1f), local=(%.1f,%.1f), parent_width=%.1f, available=%.1f, needed=%.1f", block->source_loc(),
                              current_y, query_height, space.left, space.right, local_left, local_right,
                              pa_block->content_width, available_width, element_required_width);

                    // Check if element fits
                    if (available_width >= element_required_width ||
                        (!space.has_left_float && !space.has_right_float)) {
                        // Element fits here - calculate offset
                        float float_intrusion_left = max(0.0f, local_left);
                        float float_intrusion_right = max(0.0f, pa_block->content_width - local_right);

                        if (space.has_left_float && float_intrusion_left > 0) {
                            bfc_float_offset_x = float_intrusion_left;
                        }
                        bfc_available_width_reduction = float_intrusion_left + float_intrusion_right;
                        break;
                    }

                    // Element doesn't fit - find next float boundary to try
                    float next_y = FLT_MAX;
                    for (FloatBox* fb = parent_bfc->left_floats; fb; fb = fb->next) {
                        if (fb->margin_box_bottom > current_y && fb->margin_box_bottom < next_y) {
                            next_y = fb->margin_box_bottom;
                        }
                    }
                    for (FloatBox* fb = parent_bfc->right_floats; fb; fb = fb->next) {
                        if (fb->margin_box_bottom > current_y && fb->margin_box_bottom < next_y) {
                            next_y = fb->margin_box_bottom;
                        }
                    }

                    if (next_y == FLT_MAX || next_y <= current_y) {
                        // No more floats below - use current position
                        break;
                    }

                    log_debug("%s [BFC Float Avoid] Element doesn't fit, shifting from y=%.1f to y=%.1f", block->source_loc(),
                              current_y, next_y);
                    current_y = next_y;
                }
            } else {
                // No explicit width - element may shrink to fit beside the float.
                // CSS 2.1 §9.5: "The border box of a table, a block-level replaced element,
                // or an element in the normal flow that establishes a new block formatting
                // context... must not overlap the margin box of any floats in the same BFC.
                // If necessary, implementations should clear the said element by placing it
                // below any preceding floats, but may place it adjacent to such floats if
                // there is sufficient space."
                //
                // For auto-width elements we distinguish two cases:
                //  (a) Tables and block-level replaced elements have rigid intrinsic
                //      min-content widths; if the min-content exceeds the available
                //      space beside the float, the element must step down rather than
                //      overflow.
                //  (b) Other BFC elements (overflow:hidden divs, etc.) simply accept
                //      the reduced available width; their content will wrap or overflow
                //      within the narrower space.
                FloatAvailableSpace space = block_context_space_at_y(parent_bfc, current_y, element_border_box_height);
                float local_left = space.left - x_in_bfc;
                float local_right = space.right - x_in_bfc;
                float float_intrusion_left = max(0.0f, local_left);
                float float_intrusion_right = max(0.0f, pa_block->content_width - local_right);
                float available_beside_float = pa_block->content_width - float_intrusion_left - float_intrusion_right;

                // Determine whether this element has a rigid intrinsic min-content width
                // that prevents it from shrinking into the available space.
                // Tables: column min-widths form a hard lower bound on table width.
                // Block-level replaced elements: intrinsic dimensions are fixed.
                bool is_table = (block->display.inner == CSS_VALUE_TABLE ||
                                 block->view_type == RDT_VIEW_TABLE);
                bool has_rigid_min_width = is_table || is_block_level_replaced;

                // Compute min-content width for rigid elements
                float min_required = 0;
                bool should_step_down = false;
                if (has_rigid_min_width && block->is_element()) {
                    IntrinsicSizes isizes = measure_element_intrinsic_widths(lycon, (DomElement*)block);
                    min_required = isizes.min_content;
                    // Add border/padding (min_content is content-box width)
                    if (block->bound) {
                        min_required += block->bound->padding.left + block->bound->padding.right;
                        if (block->bound->border) {
                            min_required += block->bound->border->width.left + block->bound->border->width.right;
                        }
                    }

                    if (min_required > available_beside_float + 0.5f) {
                        should_step_down = true;
                        log_debug("%s [BFC Float Avoid] Rigid auto-width element (table/replaced) "
                                  "min-content=%.1f > available=%.1f, stepping down", block->source_loc(),
                                  min_required, available_beside_float);
                    }
                }

                if (!should_step_down) {
                    // Element can shrink to fit beside the float
                    if (space.has_left_float && float_intrusion_left > 0) {
                        bfc_float_offset_x = float_intrusion_left;
                    }
                    bfc_available_width_reduction = float_intrusion_left + float_intrusion_right;

                    log_debug("%s [BFC Float Avoid] Auto-width shrink-to-fit: "
                              "avail=%.1f, offset_x=%.1f, width_reduction=%.1f", block->source_loc(),
                              available_beside_float,
                              bfc_float_offset_x, bfc_available_width_reduction);
                } else {
                    // Element's min-content exceeds space beside float - step down
                    // through float boundaries until sufficient space is found.

                    int max_iters = 100;
                    while (max_iters-- > 0) {
                        float query_height = element_border_box_height;
                        if (block->blk && block->blk->given_height < 0 &&
                            parent_bfc->lowest_float_bottom > current_y) {
                            query_height = parent_bfc->lowest_float_bottom - current_y;
                        }
                        FloatAvailableSpace step_space = block_context_space_at_y(
                            parent_bfc, current_y, query_height);

                        float sl = step_space.left - x_in_bfc;
                        float sr = step_space.right - x_in_bfc;
                        float eff_left = max(sl, 0.0f);
                        float eff_right = min(sr, pa_block->content_width);
                        float avail = max(0.0f, eff_right - eff_left);

                        log_debug("%s [BFC Float Avoid] Rigid step check y=%.1f: "
                                  "space=(%.1f,%.1f), avail=%.1f, min_required=%.1f", block->source_loc(),
                                  current_y, step_space.left, step_space.right,
                                  avail, min_required);

                        if (avail >= min_required ||
                            (!step_space.has_left_float && !step_space.has_right_float)) {
                            // Found Y where element fits
                            float fi_left = max(0.0f, sl);
                            float fi_right = max(0.0f, pa_block->content_width - sr);
                            if (step_space.has_left_float && fi_left > 0) {
                                bfc_float_offset_x = fi_left;
                            }
                            bfc_available_width_reduction = fi_left + fi_right;
                            break;
                        }

                        // Find next float boundary to step to
                        float next_y = FLT_MAX;
                        for (FloatBox* fb = parent_bfc->left_floats; fb; fb = fb->next) {
                            if (fb->margin_box_bottom > current_y && fb->margin_box_bottom < next_y)
                                next_y = fb->margin_box_bottom;
                        }
                        for (FloatBox* fb = parent_bfc->right_floats; fb; fb = fb->next) {
                            if (fb->margin_box_bottom > current_y && fb->margin_box_bottom < next_y)
                                next_y = fb->margin_box_bottom;
                        }

                        if (next_y == FLT_MAX || next_y <= current_y) break;
                        log_debug("%s [BFC Float Avoid] Rigid step down: y=%.1f -> y=%.1f", block->source_loc(),
                                  current_y, next_y);
                        current_y = next_y;
                    }
                }

                log_debug("%s [BFC Float Avoid] Auto-width final (h=%.1f): offset_x=%.1f, "
                          "width_reduction=%.1f, current_y=%.1f", block->source_loc(),
                          element_border_box_height, bfc_float_offset_x,
                          bfc_available_width_reduction, current_y);
            }

            // Calculate total shift needed in local coordinates
            bfc_shift_down = current_y - y_in_bfc;
            if (bfc_shift_down > 0) {
                log_debug("%s [BFC Float Avoid] Shifting element down by %.1f to avoid floats", block->source_loc(), bfc_shift_down);
                block->y += bfc_shift_down;
                pa_block->advance_y += bfc_shift_down;
            }
        }
    }

    if (establishes_bfc) {
        lycon->block.is_bfc_root = true;
        lycon->block.establishing_element = block;
        // Reset float lists for new BFC (children won't see parent's floats)
        block_context_reset_floats(&lycon->block);
        log_debug("[BlockContext] Block %s establishes new BFC", block->source_loc());
    } else {
        // Clear is_bfc_root so we don't inherit it from parent
        // This ensures block_context_find_bfc walks up to the actual BFC root
        lycon->block.is_bfc_root = false;
        lycon->block.establishing_element = nullptr;
        // Don't reset floats - they belong to the parent BFC
    }

    uintptr_t elmt_name = block->tag();

    // CSS 2.1 §10.3.2/§10.6.2: For replaced elements with 'width: auto' or
    // 'height: auto', use intrinsic dimensions. Per HTML spec, iframe default
    // intrinsic size is 300x150. Skip if a percentage is stored (resolve later).
    if (elmt_name == HTM_TAG_IFRAME) {
        bool has_width_percent = block->blk && !isnan(block->blk->given_width_percent);
        bool has_height_percent = block->blk && !isnan(block->blk->given_height_percent);
        if (lycon->block.given_width < 0 && !has_width_percent) {
            lycon->block.given_width = 300;
            if (block->blk) block->blk->given_width = 300;
            // clear the AUTO width type so layout uses intrinsic width
            if (block->blk && block->blk->given_width_type == CSS_VALUE_AUTO) {
                block->blk->given_width_type = CSS_VALUE__UNDEF;
            }
        }
        if (lycon->block.given_height < 0 && !has_height_percent) {
            lycon->block.given_height = 150;
            if (block->blk) block->blk->given_height = 150;
            // clear the AUTO height type so layout uses intrinsic height
            if (block->blk && block->blk->given_height_type == CSS_VALUE_AUTO) {
                block->blk->given_height_type = CSS_VALUE__UNDEF;
            }
        }
        // Re-resolve percentage width/height against containing block
        if (has_width_percent && lycon->block.given_width < 0) {
            float container_width = pa_block->content_width;
            if (container_width > 0) {
                lycon->block.given_width = container_width * block->blk->given_width_percent / 100.0f;
                block->blk->given_width = lycon->block.given_width;
                log_debug("%s [IFRAME] re-resolved width: %.0f%% of %.1f = %.1f", block->source_loc(),
                    block->blk->given_width_percent, container_width, lycon->block.given_width);
            }
        }
        if (has_height_percent && lycon->block.given_height < 0) {
            float container_height = pa_block->content_height;
            if (container_height > 0) {
                lycon->block.given_height = container_height * block->blk->given_height_percent / 100.0f;
                block->blk->given_height = lycon->block.given_height;
                log_debug("%s [IFRAME] re-resolved height: %.0f%% of %.1f = %.1f", block->source_loc(),
                    block->blk->given_height_percent, container_height, lycon->block.given_height);
            } else {
                // CSS 2.1 §10.5: percentage height can't resolve (containing block height
                // depends on content) — treat as 'auto', use intrinsic height (150px)
                lycon->block.given_height = 150;
                if (block->blk) block->blk->given_height = 150;
                log_debug("%s [IFRAME] percentage height unresolvable, fallback to intrinsic 150px", block->source_loc());
            }
        }
    }

    // CSS 2.1 §10.4: Track whether image dimensions were auto-derived from intrinsic ratio.
    // When min/max constraints change one dimension, the other must scale proportionally.
    bool image_height_auto_derived = false;
    bool image_width_auto_derived = false;

    if (elmt_name == HTM_TAG_IMG) { // load image intrinsic width and height
        log_debug("[IMG LAYOUT] Processing IMG element: %s", block->source_loc());
        const char *value;
        value = block->get_attribute("src");
        log_debug("%s [IMG LAYOUT] src attribute: %s", block->source_loc(), value ? value : "NULL");
        if (value) {
            size_t value_len = strlen(value);
            StrBuf* src = strbuf_new_cap(value_len);
            strbuf_append_str_n(src, value, value_len);
            log_debug("%s image src: %s", block->source_loc(), src->str);
            if (!block->embed) {
                block->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp));
            }
            block->embed->img = load_image(lycon->ui_context, src->str);
            strbuf_free(src);
            if (!block->embed->img) {
                log_debug("%s Failed to load image", block->source_loc());
                // todo: use a placeholder
            }
        }
        if (block->embed && block->embed->img) {
            ImageSurface* img = block->embed->img;
            // Image intrinsic dimensions are in CSS logical pixels
            float w = img->width;
            float h = img->height;

            // HTML percentage width/height (e.g., width="50%") — re-resolve
            // against the actual containing block width now that layout is known.
            // At HTML parse time, the containing block may not have been laid out yet.
            if (block->blk && !isnan(block->blk->given_width_percent) &&
                block->blk->given_width_percent > 0) {
                float cb_width = pa_block->content_width;
                if (cb_width > 0) {
                    lycon->block.given_width = cb_width * block->blk->given_width_percent / 100.0f;
                    log_debug("%s [IMG] Re-resolved width%%=%.0f against cb_width=%.1f -> %.1fpx", block->source_loc(),
                              block->blk->given_width_percent, cb_width, lycon->block.given_width);
                }
            }

            // Check if width was specified as percentage but resolved to 0
            // This happens when parent has auto/0 width - use intrinsic width instead
            bool width_is_zero_percent = (lycon->block.given_width == 0 &&
                                          block->blk && !isnan(block->blk->given_width_percent));

            log_debug("%s image intrinsic dims: %f x %f, given: %f x %f, zero_percent=%d", block->source_loc(), w, h,
                lycon->block.given_width, lycon->block.given_height, width_is_zero_percent);

            if (lycon->block.given_width < 0 || lycon->block.given_height < 0 || width_is_zero_percent) {
                if (lycon->block.given_width >= 0 && !width_is_zero_percent) {
                    // Width specified, scale unspecified height
                    lycon->block.given_height = lycon->block.given_width * h / w;
                    image_height_auto_derived = true;
                }
                else if (lycon->block.given_height >= 0 && lycon->block.given_width < 0) {
                    // Height specified, scale unspecified width
                    lycon->block.given_width = lycon->block.given_height * w / h;
                    image_width_auto_derived = true;
                }
                else {
                    // Both width and height unspecified, or width was 0% on 0-width parent
                    // CSS 2.1 §10.3.2: For replaced elements with 'width: auto',
                    // use intrinsic width. This applies to both raster and SVG images
                    // loaded via <img>. SVGs with width/height attributes on the root
                    // <svg> element have known intrinsic dimensions; use them directly.
                    lycon->block.given_width = w;
                    lycon->block.given_height = h;
                    image_height_auto_derived = true;
                    image_width_auto_derived = true;
                }
            }
            // else both width and height specified (non-zero)

            // CSS 2.1 §10.3.4: Block-level replaced elements use intrinsic width
            // for 'width: auto'. Clear the AUTO type so the width resolution path
            // uses the resolved intrinsic value instead of filling the container.
            if (block->blk && block->blk->given_width_type == CSS_VALUE_AUTO) {
                block->blk->given_width_type = CSS_VALUE__UNDEF;
            }
            if (block->blk && block->blk->given_height_type == CSS_VALUE_AUTO) {
                block->blk->given_height_type = CSS_VALUE__UNDEF;
            }

            if (img->format == IMAGE_FORMAT_SVG) {
                img->max_render_width = max(lycon->block.given_width, img->max_render_width);
            }
            log_debug("%s image dimensions: %f x %f", block->source_loc(), lycon->block.given_width, lycon->block.given_height);
        }
        else { // failed to load image
            // CSS Images 3 + browser behavior: when an image fails to load,
            // browsers ignore the HTML width/height presentational hints and
            // render a small broken-image icon (Chrome uses 16×16).
            // Only preserve dimensions that were explicitly set by CSS (not HTML attrs).
            // blk->given_width >= 0 means CSS explicitly set the width property;
            // if blk is null or blk->given_width < 0, the width came from HTML attrs only.
            if (!(block->blk && block->blk->given_width >= 0)) {
                lycon->block.given_width = 16;
            }
            if (!(block->blk && block->blk->given_height >= 0)) {
                lycon->block.given_height = 16;
            }
            log_debug("%s broken image: cleared presentational hints, given_width=%.1f, given_height=%.1f", block->source_loc(),
                lycon->block.given_width, lycon->block.given_height);
        }
    }

    // determine block width and height
    float content_width = -1;
    log_debug("%s Block '%s': given_width=%.2f,  given_height=%.2f, blk=%p, width_type=%d", block->source_loc(),
        block->node_name(), lycon->block.given_width, lycon->block.given_height, (void*)block->blk,
        block->blk ? block->blk->given_width_type : -1);

    // Check if parent is measuring intrinsic sizes (propagated via available_space)
    // This allows intrinsic sizing mode to flow down through nested blocks
    bool parent_is_intrinsic_sizing = lycon->available_space.is_intrinsic_sizing();
    if (parent_is_intrinsic_sizing) {
        log_debug("%s Block '%s': parent is in intrinsic sizing mode (width=%s)", block->source_loc(),
            block->node_name(),
            lycon->available_space.width.is_min_content() ? "min-content" : "max-content");
    }

    // Check if this is a floated element with auto width
    // CSS 2.2 Section 10.3.5: Floats with auto width use shrink-to-fit width
    // We'll do a post-layout adjustment after content is laid out
    // Note: width is "auto" if either explicitly set to auto (CSS_VALUE_AUTO=84) or unset (CSS_VALUE__UNDEF=0)
    bool width_is_auto = !block->blk ||
                         block->blk->given_width_type == CSS_VALUE_AUTO ||
                         block->blk->given_width_type == CSS_VALUE__UNDEF;
    bool is_float_auto_width = element_has_float(block) && lycon->block.given_width < 0 && width_is_auto;
    // CSS 2.1 §10.3.9: Inline-blocks with auto width use shrink-to-fit.
    // Check given_width < 0 to exclude percentage widths (resolved to px during
    // CSS resolution, so given_width >= 0 even though given_width_type = _UNDEF).
    bool is_inline_block_auto_width = (block->view_type == RDT_VIEW_INLINE_BLOCK) &&
        lycon->block.given_width < 0 && width_is_auto && !is_float_auto_width;

    // Check for width: max-content or min-content (intrinsic sizing keywords)
    // Either from CSS property OR propagated from parent's intrinsic sizing mode
    bool is_max_content_width = (block->blk && block->blk->given_width_type == CSS_VALUE_MAX_CONTENT) ||
                                (parent_is_intrinsic_sizing && lycon->available_space.is_width_max_content());
    bool is_min_content_width = (block->blk && block->blk->given_width_type == CSS_VALUE_MIN_CONTENT) ||
                                (parent_is_intrinsic_sizing && lycon->available_space.is_width_min_content());

    if (is_max_content_width || is_min_content_width) {
        // For max-content/min-content width, use shrink-to-fit behavior
        // Initially use available width for layout, then shrink to content width post-layout
        float available_width = pa_block->content_width;
        if (block->bound) {
            available_width -= (block->bound->margin.left_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.left)
                + (block->bound->margin.right_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.right);
        }
        content_width = available_width;
        log_debug("%s max/min-content width: initial layout with available_width=%.2f (will shrink post-layout)", block->source_loc(), content_width);
    }
    else if (is_float_auto_width) {
        // For floats with auto width, initially use available width for layout
        // Then shrink to fit content in post-layout step
        float available_width = pa_block->content_width;
        if (block->bound) {
            available_width -= (block->bound->margin.left_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.left)
                + (block->bound->margin.right_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.right);
        }
        content_width = available_width;
        log_debug("%s Float auto-width: initial layout with available_width=%.2f (will shrink post-layout)", block->source_loc(), content_width);
        content_width = adjust_min_max_width(block, content_width);
        if (block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
            if (block->bound) content_width = adjust_border_padding_width(block, content_width);
        }
    }
    else if (lycon->block.given_width >= 0 && (!block->blk || block->blk->given_width_type != CSS_VALUE_AUTO)) {
        content_width = max(lycon->block.given_width, 0);
        log_debug("%s Using given_width: content_width=%.2f", block->source_loc(), content_width);
        bool width_was_clamped = false;
        float pre_clamp_width = content_width;
        content_width = adjust_min_max_width(block, content_width);
        width_was_clamped = (content_width != pre_clamp_width);
        log_debug("%s After adjust_min_max_width: content_width=%.2f, clamped=%d", block->source_loc(), content_width, width_was_clamped);

        // CSS 2.1 §10.4: For replaced elements (images) with intrinsic ratio,
        // when min/max-width constrains the used width, scale height proportionally
        if (image_height_auto_derived && block->embed && block->embed->img && width_was_clamped) {
            float iw = block->embed->img->width;
            float ih = block->embed->img->height;
            if (iw > 0) {
                lycon->block.given_height = content_width * ih / iw;
                log_debug("%s [IMG] Aspect ratio: width %.2f→%.2f, height scaled to %.2f", block->source_loc(),
                          pre_clamp_width, content_width, lycon->block.given_height);
            }
        }

        // CSS 2.1 §10.3.2: For replaced elements with 'width: auto', the intrinsic
        // width is the used width (content-box). box-sizing: border-box only applies
        // to explicitly set CSS widths, not to intrinsic dimensions. However, when
        // min/max-width constrains the width, the constraint IS in border-box terms.
        if (block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
            if (!image_width_auto_derived || width_was_clamped) {
                if (block->bound) content_width = adjust_border_padding_width(block, content_width);
                log_debug("%s After adjust_border_padding (border-box): content_width=%.2f", block->source_loc(), content_width);
            } else {
                log_debug("%s [IMG] Skipping border-box for intrinsic width: content_width=%.2f", block->source_loc(), content_width);
            }
        }
    }
    else { // derive from parent block width
        log_debug("%s Deriving from parent: pa_block->content_width=%.2f", block->source_loc(), pa_block->content_width);
        float available_from_parent = pa_block->content_width;

        // Reduce available width for BFC elements avoiding floats
        if (bfc_available_width_reduction > 0) {
            available_from_parent -= bfc_available_width_reduction;
            bfc_width_was_reduced = true;
            log_debug("%s [BFC Float Avoid] Reduced available width by %.1f to %.1f", block->source_loc(),
                      bfc_available_width_reduction, available_from_parent);
        }

        if (block->bound) {
            content_width = available_from_parent
                - (block->bound->margin.left_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.left)
                - (block->bound->margin.right_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.right);
        }
        else { content_width = available_from_parent; }
        // CSS Tables 3: For auto-width tables, max-width is handled by the table
        // layout algorithm (which respects min-content width). Don't apply max-width
        // here — it would pre-constrain available width below the table's needs.
        // Only apply min-width as a floor.
        bool is_auto_width_table = (block->view_type == RDT_VIEW_TABLE) &&
            (!block->blk || block->blk->given_width < 0 || block->blk->given_width_type == CSS_VALUE_AUTO);
        if (block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
            if (is_auto_width_table) {
                // Only apply min-width, skip max-width
                if (block->blk->given_min_width >= 0 && content_width < block->blk->given_min_width)
                    content_width = block->blk->given_min_width;
            } else {
                content_width = adjust_min_max_width(block, content_width);
            }
            if (block->bound) content_width = adjust_border_padding_width(block, content_width);
        } else {
            if (block->bound) content_width = adjust_border_padding_width(block, content_width);
            if (is_auto_width_table) {
                // Only apply min-width, skip max-width
                if (block->blk && block->blk->given_min_width >= 0 && content_width < block->blk->given_min_width)
                    content_width = block->blk->given_min_width;
            } else {
                content_width = adjust_min_max_width(block, content_width);
            }
        }
    }
    // Clamp to 0 - negative content_width can occur with very narrow containers
    // (e.g., width:1px) after subtracting borders/padding/margins. CSS allows this,
    // with content overflowing the container.
    if (content_width < 0) content_width = 0;

    float content_height = -1;
    if (lycon->block.given_height >= 0) {
        content_height = max(lycon->block.given_height, 0);
        bool height_was_clamped = false;
        float pre_clamp_height = content_height;
        content_height = adjust_min_max_height(block, content_height);
        height_was_clamped = (content_height != pre_clamp_height);

        // CSS 2.1 §10.7: For replaced elements with intrinsic ratio,
        // when min/max-height constrains the used height, scale width proportionally
        if (image_width_auto_derived && block->embed && block->embed->img && height_was_clamped) {
            float iw = block->embed->img->width;
            float ih = block->embed->img->height;
            if (ih > 0) {
                content_width = content_height * iw / ih;
                log_debug("%s [IMG] Aspect ratio: height %.2f→%.2f, width scaled to %.2f", block->source_loc(),
                          pre_clamp_height, content_height, content_width);
            }
        }

        // CSS 2.1 §10.6.2: For replaced elements with 'height: auto', the intrinsic
        // height is the used height (content-box). box-sizing: border-box only applies
        // to explicitly set CSS heights, not to intrinsic dimensions.
        if (block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
            if (!image_height_auto_derived || height_was_clamped) {
                if (block->bound) content_height = adjust_border_padding_height(block, content_height);
            } else {
                log_debug("%s [IMG] Skipping border-box for intrinsic height: content_height=%.2f", block->source_loc(), content_height);
            }
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
            content_height = adjust_min_max_height(block, content_height);
        }
    }
    assert(content_height >= 0);
    lycon->block.content_width = content_width;  lycon->block.content_height = content_height;

    // If this block establishes a BFC, update the float edge boundaries
    // This must be done AFTER content_width is calculated
    if (lycon->block.is_bfc_root && lycon->block.establishing_element == block) {
        lycon->block.float_left_edge = 0;
        lycon->block.float_right_edge = content_width;
        log_debug("%s [BFC] Updated float edges for %s: left=0, right=%.1f", block->source_loc(), block->node_name(), content_width);
    }

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
                log_debug("%s applied margin:auto centering for block inside <center>", block->source_loc());
            }
        }

        // CSS 2.1 §10.3.5: For floats, if margin-left/right is 'auto', its used value is 0
        // CSS 2.1 §10.3.2: For inline-level elements, auto margins resolve to 0
        // CSS 2.1 §10.3.3: For normal flow blocks, auto margins center the element
        bool is_rtl = pa_block->direction == CSS_VALUE_RTL;
        bool is_inline_level = (block->display.outer == CSS_VALUE_INLINE_BLOCK ||
                                block->display.outer == CSS_VALUE_INLINE);
        if (is_float || is_inline_level) {
            // Floats and inline-level elements: auto margins become 0
            if (block->bound->margin.left_type == CSS_VALUE_AUTO) block->bound->margin.left = 0;
            if (block->bound->margin.right_type == CSS_VALUE_AUTO) block->bound->margin.right = 0;
        } else if (block->bound->margin.left_type == CSS_VALUE_AUTO && block->bound->margin.right_type == CSS_VALUE_AUTO)  {
            // CSS 2.1 §10.3.3 + §9.5: For BFC elements avoiding floats, auto margins
            // resolve against the available width after float avoidance, not the full
            // containing block width. This ensures the element is centered/aligned
            // within the space not occupied by floats.
            float margin_available = pa_block->content_width - bfc_available_width_reduction;
            block->bound->margin.left = block->bound->margin.right = max((margin_available - block->width) / 2, 0);
        } else if (block->bound->margin.left_type == CSS_VALUE_AUTO) {
            // CSS 2.1 §10.3.3: Single auto margin absorbs the remaining space
            float margin_available = pa_block->content_width - bfc_available_width_reduction;
            block->bound->margin.left = max(margin_available - block->width - block->bound->margin.right, 0.0f);
        } else if (block->bound->margin.right_type == CSS_VALUE_AUTO) {
            float margin_available = pa_block->content_width - bfc_available_width_reduction;
            block->bound->margin.right = max(margin_available - block->width - block->bound->margin.left, 0.0f);
        } else if (is_rtl) {
            // CSS 2.1 §10.3.3: Over-constrained with direction:rtl — margin-left gets the residual
            block->bound->margin.left = pa_block->content_width - bfc_available_width_reduction - block->width - block->bound->margin.right;
            log_debug("%s [RTL] Over-constrained: computed margin-left=%f (containing=%f, width=%f, margin-right=%f)", block->source_loc(),
                block->bound->margin.left, pa_block->content_width, block->width, block->bound->margin.right);
        }
        // margin-trim — parent trims children's margins (CSS Box 4 §3.1)
        if (block->parent && block->parent->is_block()) {
            ViewBlock* mt_pa = (ViewBlock*)block->parent;
            if (mt_pa->blk && mt_pa->blk->margin_trim) {
                // CSS Box 4 §3.1: margin-trim:inline on a block container only
                // trims the inline-start/end margins of the first/last line boxes,
                // NOT the margins of block-level children. So skip inline trim here
                // (block-level child path). Inline-level trim is handled elsewhere.

                // block-start margin-trim: trim the first in-flow child's block-start margin
                if ((mt_pa->blk->margin_trim & MARGIN_TRIM_BLOCK_START) && block->bound->margin.top != 0) {
                    View* first = mt_pa->first_placed_child();
                    while (first && first->is_block()) {
                        ViewBlock* fvb = (ViewBlock*)first;
                        if (fvb->view_type == RDT_VIEW_MARKER ||
                            (fvb->position && element_has_float(fvb))) {
                            View* next = (View*)first->next_sibling;
                            while (next && !next->view_type) next = (View*)next->next_sibling;
                            first = next;
                            continue;
                        }
                        break;
                    }
                    if (first == (View*)block) {
                        log_debug("%s [MARGIN-TRIM] block-start: trimming margin.top=%f on first child %s", block->source_loc(),
                            block->bound->margin.top, block->node_name());
                        block->bound->margin.top = 0;
                    }
                }
            }
        }

        float y_before_margin = block->y;
        block->x += block->bound->margin.left;
        block->y += block->bound->margin.top;

        // Apply BFC float avoidance offset for normal-flow BFC elements
        if (bfc_float_offset_x > 0) {
            block->x += bfc_float_offset_x;
            log_debug("%s [BFC Float Avoid] Applied x offset: block->x now=%.1f", block->source_loc(), block->x);
        }

        log_debug("%s Y coordinate: before margin=%f, margin.top=%f, after margin=%f (tag=%s)", block->source_loc(),
                  y_before_margin, block->bound->margin.top, block->y, block->node_name());
    }
    else {
        block->width = content_width;  block->height = content_height;
        // no change to block->x, block->y

        // Apply BFC float avoidance offset for normal-flow BFC elements
        if (bfc_float_offset_x > 0) {
            block->x += bfc_float_offset_x;
            log_debug("%s [BFC Float Avoid] Applied x offset (no bounds): block->x now=%.1f", block->source_loc(), block->x);
        }
    }
    log_debug("%s layout-block-sizes: x:%f, y:%f, wd:%f, hg:%f, line-hg:%f, given-w:%f, given-h:%f", block->source_loc(),
        block->x, block->y, block->width, block->height, lycon->block.line_height, lycon->block.given_width, lycon->block.given_height);

    // IMPORTANT: Apply clear BEFORE setting up inline context and laying out children
    // Clear positions this element below earlier floats
    // This must happen after Y position and margins are set, but before children are laid out
    // CSS 2.1 §9.5.2: 'clear' applies only to block-level elements, not inline-level
    bool is_block_level_for_clear = (block->display.outer != CSS_VALUE_INLINE &&
                                     block->display.outer != CSS_VALUE_INLINE_BLOCK);
    // CSS 2.1 §9.5.2: Track whether clearance was applied for margin collapsing
    pa_block->saved_clear_y = -1;  // -1 = no clearance applied
    if (is_block_level_for_clear && block->position &&
        (block->position->clear == CSS_VALUE_LEFT ||
                             block->position->clear == CSS_VALUE_RIGHT ||
                             block->position->clear == CSS_VALUE_BOTH)) {
        bool is_float = block->position && element_has_float(block);
        if (is_float) {
            // Floats: apply clear directly (floats don't participate in margin collapse)
            log_debug("%s Float has clear property, applying clear layout BEFORE children", block->source_loc());
            layout_clear_element(lycon, block);
            // CSS 2.1 §9.5.2: Recalculate x offset after clear moves element below floats
            if (bfc_float_offset_x > 0) {
                BlockContext* clear_bfc = lycon->block.parent
                    ? block_context_find_bfc(lycon->block.parent)
                    : block_context_find_bfc(&lycon->block);
                if (clear_bfc) {
                    float new_y_in_bfc = block->y;
                    ViewElement* pv = block->parent_view();
                    while (pv && clear_bfc->establishing_element && pv != clear_bfc->establishing_element) {
                        new_y_in_bfc += pv->y;
                        ViewElement* ppv = pv->parent_view();
                        if (!ppv) break;
                        pv = ppv;
                    }
                    float query_height = block->height > 0 ? block->height : 16.0f;
                    FloatAvailableSpace space = block_context_space_at_y(clear_bfc, new_y_in_bfc, query_height);
                    if (!space.has_left_float && !space.has_right_float) {
                        // No floats at the new y position — remove the offset
                        block->x -= bfc_float_offset_x;
                        log_debug("%s [CLEARANCE] Removed float x offset after clear: block->x=%.1f", block->source_loc(), block->x);
                        bfc_float_offset_x = 0;
                    }
                }
            }
        } else {
            // CSS 2.1 §9.5.2: In-flow non-float block with margins.
            // Compute hypothetical position (with margin collapsing) and compare
            // with the float bottom. If hypothetical >= clear_y: no clearance needed,
            // allow margin collapse later. If hypothetical < clear_y: clearance needed.
            // Use PARENT's BlockContext to find the BFC — if this element establishes
            // its own BFC (e.g. overflow:hidden), its own context has zero floats.
            // This mirrors layout_clear_element() which also uses lycon->block.parent.
            BlockContext* bfc = lycon->block.parent
                ? block_context_find_bfc(lycon->block.parent)
                : block_context_find_bfc(&lycon->block);
            float clear_y = 0;
            if (bfc) {
                float clear_y_bfc = block_context_clear_y(bfc, block->position->clear);
                float parent_y_in_bfc = 0;
                ViewElement* parent_view_elem = block->parent_view();
                if (parent_view_elem) {
                    ViewElement* v = parent_view_elem;
                    while (v && bfc->establishing_element && v != bfc->establishing_element) {
                        parent_y_in_bfc += v->y;
                        ViewElement* pv = v->parent_view();
                        if (!pv) break;
                        v = pv;
                    }
                }
                clear_y = clear_y_bfc - parent_y_in_bfc;
            }

            // Compute hypothetical position (what if margins collapsed normally)
            float own_margin_top = block->bound ? block->bound->margin.top : 0;
            float uncollapsed_y = block->y;
            float hypothetical_y = uncollapsed_y;

            // CSS 2.1 §9.5.2 + §8.3.1: The hypothetical position must account for
            // all margin collapses that would occur if clear were 'none', including
            // parent-child collapse with the first in-flow child. When the block has
            // no border-top and no padding-top, its margin is adjoining with the first
            // in-flow child's margin-top. This extended margin affects the hypothetical
            // position and may push the border edge past the float, eliminating the
            // need for clearance entirely.
            float child_margin_contribution = 0;
            {
                float block_border_top = block->bound && block->bound->border ?
                    block->bound->border->width.top : 0;
                float block_padding_top = block->bound ? block->bound->padding.top : 0;
                if (block_border_top == 0 && block_padding_top == 0) {
                    // Parent-child collapse is possible — peek at first in-flow child's margin.
                    // Use DOM tree (first_child/next_sibling) because View tree children
                    // haven't been placed yet (view_type is 0 at this point).
                    DomNode* dom_child = block->first_child;
                    while (dom_child) {
                        if (dom_child->is_element()) {
                            DomElement* child_elem = dom_child->as_element();
                            ViewBlock* child_vb = (ViewBlock*)child_elem;
                            // Skip floats — they don't participate in normal flow margins
                            bool child_is_float = child_vb->position &&
                                (child_vb->position->float_prop == CSS_VALUE_LEFT ||
                                 child_vb->position->float_prop == CSS_VALUE_RIGHT);
                            if (!child_is_float) {
                                // Found first in-flow element child
                                float child_mt = child_vb->bound ? child_vb->bound->margin.top : 0;
                                // If child margin isn't resolved yet (bound NULL or margin
                                // still at default 0), try the specified style tree.
                                // Children haven't been through dom_node_resolve_style yet
                                // at this point, so bound margins may be unresolved.
                                if (child_mt == 0 && child_elem->specified_style) {
                                    // Try individual margin-top property first
                                    CssDeclaration* mt_decl = style_tree_get_declaration(
                                        child_elem->specified_style, CSS_PROPERTY_MARGIN_TOP);
                                    if (mt_decl && mt_decl->value) {
                                        float resolved = resolve_length_value(lycon, CSS_PROPERTY_MARGIN_TOP, mt_decl->value);
                                        if (resolved > child_mt) child_mt = resolved;
                                    }
                                    // Also try shorthand 'margin' property (e.g. margin: 1em)
                                    if (child_mt == 0) {
                                        CssDeclaration* m_decl = style_tree_get_declaration(
                                            child_elem->specified_style, CSS_PROPERTY_MARGIN);
                                        if (m_decl && m_decl->value) {
                                            const CssValue* top_val = m_decl->value;
                                            // For multi-value shorthand, first value is top
                                            if (top_val->type == CSS_VALUE_TYPE_LIST && top_val->data.list.count > 0) {
                                                top_val = top_val->data.list.values[0];
                                            }
                                            float resolved = resolve_length_value(lycon, CSS_PROPERTY_MARGIN, top_val);
                                            if (resolved > child_mt) child_mt = resolved;
                                        }
                                    }
                                }
                                if (child_mt > own_margin_top) {
                                    child_margin_contribution = child_mt - own_margin_top;
                                    log_debug("%s [CLEARANCE] Peek at first child margin: child_mt=%.1f, own_mt=%.1f, contribution=%.1f", block->source_loc(),
                                              child_mt, own_margin_top, child_margin_contribution);
                                }
                                break;
                            }
                        } else if (dom_child->is_text()) {
                            // Text node — check if whitespace-only (skip) or real content (stops search)
                            const unsigned char* text = ((DomText*)dom_child)->text_data();
                            if (text) {
                                bool all_ws = true;
                                for (const unsigned char* p = text; *p; p++) {
                                    unsigned char c = *p;
                                    if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\f') {
                                        all_ws = false; break;
                                    }
                                }
                                if (!all_ws) break;  // real text content stops parent-child collapse
                            }
                        }
                        dom_child = dom_child->next_sibling;
                    }
                }
            }

            // Check if this is the first in-flow child (parent-child collapse case)
            View* first_in_flow = block->parent_view()->first_placed_child();
            while (first_in_flow && first_in_flow->is_block()) {
                ViewBlock* vb = (ViewBlock*)first_in_flow;
                if (vb->position && element_has_float(vb)) {
                    first_in_flow = (View*)first_in_flow->next_sibling;
                    while (first_in_flow && !first_in_flow->view_type)
                        first_in_flow = (View*)first_in_flow->next_sibling;
                    continue;
                }
                break;
            }

            if (first_in_flow == (View*)block) {
                // First child: hypothetical with parent-child collapse
                ViewBlock* parent = block->parent->is_block() ? (ViewBlock*)block->parent : nullptr;
                bool parent_creates_bfc = parent && block_context_establishes_bfc(parent);
                float parent_padding_top = parent && parent->bound ? parent->bound->padding.top : 0;
                float parent_border_top = parent && parent->bound && parent->bound->border ?
                    parent->bound->border->width.top : 0;
                // Quirks mode: effective margin is 0 for quirky margins in a quirky container
                float effective_mt = own_margin_top;
                if (is_quirky_container(parent, lycon) && has_quirky_margin_top(block))
                    effective_mt = 0;
                if (parent && parent->parent && !parent_creates_bfc &&
                    parent_padding_top == 0 && parent_border_top == 0 &&
                    effective_mt != 0) {
                    float advance_y_before = block->y - own_margin_top;
                    hypothetical_y = advance_y_before;
                }
            } else {
                // Sibling: hypothetical with sibling margin collapse
                View* prev_view = block->prev_placed_view();
                while (prev_view && prev_view->is_block()) {
                    ViewBlock* vb = (ViewBlock*)prev_view;
                    if (vb->position && element_has_float(vb)) {
                        prev_view = prev_view->prev_placed_view(); continue;
                    }
                    if (vb->position && (vb->position->position == CSS_VALUE_ABSOLUTE ||
                                          vb->position->position == CSS_VALUE_FIXED)) {
                        prev_view = prev_view->prev_placed_view(); continue;
                    }
                    break;
                }
                if (prev_view && prev_view->is_block() && prev_view->view_type != RDT_VIEW_INLINE_BLOCK
                    && ((ViewBlock*)prev_view)->bound) {
                    ViewBlock* prev_block = (ViewBlock*)prev_view;
                    float prev_mb = prev_block->bound->margin.bottom;
                    float cur_mt = own_margin_top;
                    if (prev_mb != 0 || cur_mt != 0) {
                        float collapsed = collapse_margins(prev_mb, cur_mt);
                        float collapse_amount = (prev_mb + cur_mt) - collapsed;
                        hypothetical_y = uncollapsed_y - collapse_amount;
                    }
                }
            }

            // CSS 2.1 §9.5.2 + §8.3.1: Include the child margin contribution in
            // the hypothetical position. The child's margin-top extends the effective
            // margin through parent-child collapse, pushing the border edge further down.
            hypothetical_y += child_margin_contribution;

            log_debug("%s [CLEARANCE §9.5.2] hypothetical=%.1f, clear_y=%.1f, uncollapsed=%.1f, child_margin_contrib=%.1f", block->source_loc(),
                      hypothetical_y, clear_y, uncollapsed_y, child_margin_contribution);

            if (hypothetical_y >= clear_y) {
                // No clearance needed — margin collapse proceeds normally
                log_debug("%s [CLEARANCE] No clearance needed, margins will collapse normally", block->source_loc());
            } else {
                // Clearance IS needed. CSS 2.1 §9.5.2: clearance places border edge
                // at max(float_bottom, hypothetical) = clear_y (since hypothetical < clear_y).
                // This may result in NEGATIVE clearance if clear_y < uncollapsed position.
                if (clear_y >= block->y) {
                    // Normal (positive) clearance: use layout_clear_element
                    layout_clear_element(lycon, block);
                } else {
                    // Negative clearance: block moves UP from uncollapsed to clear_y
                    float delta = clear_y - block->y;
                    block->y = clear_y;
                    pa_block->advance_y += delta;
                    log_debug("%s [CLEARANCE] Negative clearance: moved from %.1f to %.1f (delta=%.1f)", block->source_loc(),
                              uncollapsed_y, clear_y, delta);
                }
                // Signal margin collapsing section to skip collapse for this block
                pa_block->saved_clear_y = clear_y;
                // Mark the block itself so child layout knows not to double-adjust y
                if (block->bound) block->bound->has_clearance = true;
                log_debug("%s [CLEARANCE] Applied: y=%.1f, saved_clear_y=%.1f", block->source_loc(), block->y, clear_y);

                // CSS 2.1 §9.5.2: After clearance moves the element below the floats,
                // the BFC float-avoidance x offset computed at the original y position
                // may no longer apply. Recalculate the x offset at the new y position.
                if (bfc_float_offset_x > 0) {
                    BlockContext* clear_bfc = lycon->block.parent
                        ? block_context_find_bfc(lycon->block.parent)
                        : block_context_find_bfc(&lycon->block);
                    if (clear_bfc) {
                        // Compute BFC y coordinate at the new position
                        float new_y_in_bfc = block->y;
                        ViewElement* pv = block->parent_view();
                        while (pv && clear_bfc->establishing_element && pv != clear_bfc->establishing_element) {
                            new_y_in_bfc += pv->y;
                            ViewElement* ppv = pv->parent_view();
                            if (!ppv) break;
                            pv = ppv;
                        }
                        float query_height = block->height > 0 ? block->height : 16.0f;
                        FloatAvailableSpace space = block_context_space_at_y(clear_bfc, new_y_in_bfc, query_height);
                        float new_offset_x = 0;
                        if (space.has_left_float) {
                            float x_in_bfc = block->x - bfc_float_offset_x;  // original x in BFC coords
                            float local_left = space.left - x_in_bfc + (block->bound ? block->bound->margin.left : 0);
                            new_offset_x = max(0.0f, local_left);
                        }
                        // Remove old offset and apply new one
                        float offset_delta = new_offset_x - bfc_float_offset_x;
                        if (offset_delta != 0) {
                            block->x += offset_delta;
                            bfc_float_offset_x = new_offset_x;
                            log_debug("%s [CLEARANCE] Recalculated x offset after clear: old_offset=%.1f, new_offset=%.1f, block->x=%.1f", block->source_loc(),
                                      bfc_float_offset_x - offset_delta, new_offset_x, block->x);
                        }
                    }
                }
            }
        }
    }

    // CSS 2.1 §9.5 + §9.5.2: After clearance moves a BFC block below floats,
    // recalculate width reduction — floats may no longer intrude at the new Y.
    // Only when the width reduction was actually applied to the block width.
    if (bfc_width_was_reduced && pa_block->saved_clear_y >= 0 && parent_bfc) {
        float new_y_in_bfc = block->y;
        ViewElement* pv = block->parent_view();
        while (pv && parent_bfc->establishing_element && pv != parent_bfc->establishing_element) {
            new_y_in_bfc += pv->y;
            ViewElement* ppv = pv->parent_view();
            if (!ppv) break;
            pv = ppv;
        }
        float query_height = block->height > 0 ? block->height : 16.0f;
        FloatAvailableSpace space = block_context_space_at_y(parent_bfc, new_y_in_bfc, query_height);
        float new_reduction = 0;
        if (space.has_left_float || space.has_right_float) {
            float fi_left = space.has_left_float ? space.left : 0;
            float fi_right = space.has_right_float ? (parent_bfc->establishing_element->width - space.right) : 0;
            new_reduction = fi_left + fi_right;
        }
        if (new_reduction < bfc_available_width_reduction) {
            float width_increase = bfc_available_width_reduction - new_reduction;
            bfc_available_width_reduction = new_reduction;
            content_width += width_increase;
            if (block->bound) {
                block->width += width_increase;
            }
            lycon->block.content_width = content_width;
            log_debug("%s [CLEARANCE] Recalculated BFC width after clear: reduction %.1f->%.1f, width+=%.1f, block->width=%.1f", block->source_loc(),
                      new_reduction + width_increase, new_reduction, width_increase, block->width);
        }
    }

    // setup inline context
    setup_inline(lycon, block);

    // For floats and inline-blocks with auto width, calculate intrinsic width BEFORE laying out children
    // This ensures children are laid out with the correct shrink-to-fit width
    // CSS 2.1 §10.3.5 (floats) and §10.3.9 (inline-blocks): shrink-to-fit width =
    //   min(max-content, max(min-content, available))
    if ((is_float_auto_width || is_inline_block_auto_width || is_max_content_width || is_min_content_width) && block->is_element()) {
        // Font is loaded after setup_inline, so now we can calculate intrinsic width
        DomElement* dom_element = (DomElement*)block;
        float available = pa_block->content_width;
        if (block->bound) {
            available -= (block->bound->margin.left_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.left)
                      + (block->bound->margin.right_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.right);
        }

        // Calculate fit-content width (shrink-to-fit)
        // Use float to avoid truncation that could cause text wrapping issues
        float fit_content = calculate_fit_content_width(lycon, dom_element, available);

        // For min-content, use min-content width instead of fit-content
        if (is_min_content_width) {
            fit_content = calculate_min_content_width(lycon, (DomNode*)dom_element);
            log_debug("%s min-content width: using min_content=%.1f", block->source_loc(), fit_content);
        }

        // CSS 2.1 §10.3.5: Float/shrink-to-fit width replaces the initial placeholder.
        // The fit_content formula (min(max-content, max(min-content, available))) is
        // always correct, whether it expands (narrow container) or shrinks (wide container).
        if (fit_content >= 0 && fabsf(fit_content - block->width) > 0.01f) {
            log_debug("%s Shrink-to-fit (%s): fit_content=%.1f, old_width=%.1f, available=%.1f", block->source_loc(),
                is_max_content_width ? "max-content" : (is_min_content_width ? "min-content" :
                (is_inline_block_auto_width ? "inline-block" : "float")),
                fit_content, block->width, available);

            // Update block width to shrink-to-fit size
            // Round up to next 0.5px to prevent text wrapping due to floating-point precision issues
            // while avoiding larger additions that prevent adjacent content from fitting
            float rounded_width = ceilf(fit_content * 2.0f) / 2.0f;
            block->width = rounded_width;

            // CSS 2.1 §10.4: Apply min-width/max-width constraints to the
            // shrink-to-fit width. Elements with max-width (e.g., table with
            // display:block and max-width:100%) must not expand beyond their
            // max-width even when intrinsic content is wider.
            block->width = adjust_min_max_width(block, block->width);

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

            // Update BFC float edges to match new content width after shrink-to-fit.
            // The BFC edges were set earlier (line ~4608) with the initial content_width
            // before shrink-to-fit. If not updated, inline-block children will see
            // stale float boundaries and be mispositioned.
            if (lycon->block.is_bfc_root && lycon->block.establishing_element == block) {
                lycon->block.float_left_edge = 0;
                lycon->block.float_right_edge = block->content_width;
                log_debug("%s [BFC] Updated float edges after shrink-to-fit: right=%.1f",
                          block->source_loc(), block->content_width);
            }

            // CSS 2.1 §16.1: Reset is_first_line BEFORE line_init so that
            // line_reset() (called inside line_init) applies text-indent on the
            // actual first content line. The initial setup_inline → line_reset
            // consumed is_first_line during the intrinsic measurement pass.
            lycon->block.is_first_line = true;

            // Re-setup line context with new width, mirroring setup_inline logic:
            // compute inner_left from border+padding, then pass to line_init so
            // line_reset correctly applies text-indent on top of it.
            float inner_left = 0;
            if (block->bound) {
                if (block->bound->border) {
                    inner_left += block->bound->border->width.left;
                }
                inner_left += block->bound->padding.left;
            }
            line_init(lycon, inner_left, inner_left + block->content_width);
        }

        // CSS 2.1 §10.3.3: Re-resolve auto margins after shrink-to-fit changed the
        // block width. The initial margin resolution (above) saw the placeholder
        // available width; now that the actual width is known, recalculate.
        // Also update block->x since margin.left was already applied earlier.
        if (block->bound && !is_float &&
            block->display.outer != CSS_VALUE_INLINE_BLOCK &&
            block->display.outer != CSS_VALUE_INLINE &&
            (block->bound->margin.left_type == CSS_VALUE_AUTO || block->bound->margin.right_type == CSS_VALUE_AUTO)) {
            float old_margin_left = block->bound->margin.left;
            float margin_available = pa_block->content_width - bfc_available_width_reduction;
            if (block->bound->margin.left_type == CSS_VALUE_AUTO && block->bound->margin.right_type == CSS_VALUE_AUTO) {
                block->bound->margin.left = block->bound->margin.right = max((margin_available - block->width) / 2, 0.0f);
            } else if (block->bound->margin.left_type == CSS_VALUE_AUTO) {
                block->bound->margin.left = max(margin_available - block->width - block->bound->margin.right, 0.0f);
            } else {
                block->bound->margin.right = max(margin_available - block->width - block->bound->margin.left, 0.0f);
            }
            block->x += block->bound->margin.left - old_margin_left;
            log_debug("%s Re-resolved auto margins after shrink-to-fit: margin_left=%.1f, margin_right=%.1f, width=%.1f, x=%.1f",
                block->source_loc(), block->bound->margin.left, block->bound->margin.right, block->width, block->x);
        }
    }

    // layout block content, and determine flow width and height
    layout_block_inner_content(lycon, block);

    // HTML rendering spec §15.5.12: Fieldset legend positioning.
    // The first legend child of a fieldset is positioned at the block-start border edge
    // (overlapping the top border), not inside the content area. All subsequent siblings
    // shift up by the border-top width since the legend replaces the border gap.
    if (block->is_element() && block->tag() == HTM_TAG_FIELDSET && block->first_child) {
        float border_top = (block->bound && block->bound->border) ? block->bound->border->width.top : 0;
        float padding_top = block->bound ? block->bound->padding.top : 0;
        float legend_shift = border_top + padding_top;

        if (legend_shift > 0) {
            // Find the first legend child
            ViewBlock* first_legend = nullptr;
            for (DomNode* child = block->first_child; child; child = child->next_sibling) {
                if (child->is_element() && child->as_element()->tag() == HTM_TAG_LEGEND) {
                    first_legend = (ViewBlock*)child->as_element();
                    break;
                }
            }
            if (first_legend && first_legend->view_type) {
                // Shift legend up to the border-box top edge
                first_legend->y -= legend_shift;
                // Shift all subsequent siblings up by border_top (content bypasses the border gap)
                for (DomNode* sib = first_legend->next_sibling; sib; sib = sib->next_sibling) {
                    if (sib->is_element() && sib->as_element()->view_type) {
                        sib->as_element()->y -= border_top;
                    }
                }
                // Reduce fieldset height by border_top
                block->height -= border_top;
                if (block->content_height > border_top)
                    block->content_height -= border_top;
                log_debug("%s [FIELDSET] Legend repositioned: legend_shift=%.1f, border_top=%.1f, new_height=%.1f", block->source_loc(),
                    legend_shift, border_top, block->height);
            }
        }
    }

    // CSS 2.1 §10.3.4: For block-level replaced elements (SVG, IMG) with auto margins,
    // the intrinsic width is determined inside layout_block_inner_content (e.g., layout_inline_svg).
    // Re-compute auto margins now that the actual width is known.
    // Skip floats and inline-level elements — their auto margins resolve to 0 (CSS 2.1 §10.3.5, §10.3.2).
    if (block->bound && block->display.inner == RDT_DISPLAY_REPLACED && !is_float &&
        block->display.outer != CSS_VALUE_INLINE_BLOCK && block->display.outer != CSS_VALUE_INLINE &&
        (block->bound->margin.left_type == CSS_VALUE_AUTO || block->bound->margin.right_type == CSS_VALUE_AUTO)) {
        float margin_available = pa_block->content_width;
        float old_margin_left = block->bound->margin.left;
        if (block->bound->margin.left_type == CSS_VALUE_AUTO && block->bound->margin.right_type == CSS_VALUE_AUTO) {
            float m = max((margin_available - block->width) / 2, 0.0f);
            block->bound->margin.left = block->bound->margin.right = m;
            log_debug("%s re-finalized replaced element auto margins: left=right=%f (avail=%f, width=%f)", block->source_loc(),
                      m, margin_available, block->width);
        } else if (block->bound->margin.left_type == CSS_VALUE_AUTO) {
            block->bound->margin.left = max(margin_available - block->width - block->bound->margin.right, 0.0f);
        } else {
            block->bound->margin.right = max(margin_available - block->width - block->bound->margin.left, 0.0f);
        }
        block->x += block->bound->margin.left - old_margin_left;
    }

    // check for margin collapsing with children
    // CSS 2.2 Section 8.3.1: Margins collapse when parent has no border/padding
    // This applies when block->bound is NULL (no border/padding/margin) OR
    // when block->bound exists but has no bottom border/padding
    // IMPORTANT: Elements that establish a BFC do NOT collapse margins with their children
    // CSS 2.1 §8.3.1: Bottom margin of parent collapses with last child's bottom margin
    // ONLY when parent has 'auto' computed height (not explicit height)
    // Also requires min-height to be zero for the collapse to happen
    bool has_border_bottom = block->bound && block->bound->border && block->bound->border->width.bottom > 0;
    bool has_padding_bottom = block->bound && block->bound->padding.bottom > 0;
    // CSS 2.1 §9.4.1: Elements that establish a BFC prevent margin collapsing
    // with their in-flow children. This includes: overflow != visible,
    // float, position absolute/fixed, inline-block, table cells, etc.
    bool creates_bfc_for_collapse = block_context_establishes_bfc(block);

    // CSS 2.1 §8.3.1: Bottom margins only collapse when parent has auto computed height.
    // Per CSS 2.1 erratum q313, min-height has no influence on bottom margin adjacency.
    bool has_explicit_height = (block->blk && block->blk->given_height >= 0);

    // Quirks mode: quirky container flag for bottom margin collapse
    bool quirky_container_bottom = is_quirky_container(block, lycon);

    if (!has_border_bottom && !has_padding_bottom && !creates_bfc_for_collapse &&
        !has_explicit_height && block->first_child) {
        // collapse bottom margin with last in-flow child block
        // Skip absolutely positioned and floated children - they're out of normal flow
        // Find last in-flow child (skip abs-positioned, floated elements, and empty zero-height blocks)
        // CSS 2.2 Section 8.3.1: An empty block allows margins to collapse "through" it when:
        // - It has zero height
        // - It has no borders, padding, or line boxes
        View* last_in_flow = nullptr;
        View* child = (View*)block->first_child;
        while (child) {
            if (child->view_type && child->is_block()) {
                ViewBlock* vb = (ViewBlock*)child;
                // CSS 2.1 Section 8.3.1: Only block-level boxes participate in margin collapsing
                // Inline-blocks are inline-level boxes in inline formatting context - they don't collapse
                bool is_inline_block = (vb->view_type == RDT_VIEW_INLINE_BLOCK);
                bool is_out_of_flow = is_inline_block || (vb->position &&
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

        // Skip empty zero-height blocks at the end - margins collapse "through" them
        // Find the effective last child whose margin-bottom should collapse with parent
        // CSS 2.2 Section 8.3.1: Margins collapse through self-collapsing blocks
        // (no height, no border/padding, no line boxes in self or descendants)
        // CSS 2.1 §9.2.1.1: Zero-height inline content is wrapped in implicit
        // self-collapsing anonymous blocks that also don't prevent collapse.
        View* effective_last = last_in_flow;
        while (effective_last) {
            if (effective_last->is_block()) {
                ViewBlock* vb = (ViewBlock*)effective_last;
                float margin_bottom = vb->bound ? vb->bound->margin.bottom : 0;
                bool has_chain_mb = vb->bound && has_margin_chain(vb->bound);
                if (margin_bottom == 0 && !has_chain_mb && is_block_self_collapsing(vb)) {
                    log_debug("%s skipping self-collapsing block for bottom margin collapsing", block->source_loc());
                    View* prev = effective_last->prev_placed_view();
                    effective_last = prev;
                    continue;
                }
                break;
            } else {
                // Inline/text with zero height = empty anonymous block wrapper
                if (effective_last->height <= 0) {
                    effective_last = effective_last->prev_placed_view();
                    continue;
                }
                break;
            }
        }

        if (effective_last && effective_last->is_block() && ((ViewBlock*)effective_last)->bound) {
            ViewBlock* last_child_block = (ViewBlock*)effective_last;
            // CSS 2.1 §8.3.1: Check if last child has margin to collapse with parent.
            // Also check chain components — a scalar 0 may represent mixed-sign margins
            // (e.g., {+16, -16}) that need to participate in further collapse.
            // Quirks mode: quirky margins are treated as 0 in a quirky container.
            bool child_mb_is_quirky = quirky_container_bottom &&
                has_quirky_margin_bottom(last_child_block);
            float effective_child_mb = child_mb_is_quirky ? 0 : last_child_block->bound->margin.bottom;
            if (effective_child_mb != 0 || (!child_mb_is_quirky && has_margin_chain(last_child_block->bound))) {
                // CSS 2.1 §8.3.1: Bottom margins collapse regardless of sign (positive or negative).
                // CSS 2.2 Section 8.3.1: Margins collapse only if there's NO content separating them.
                // Check if there's any inline-level content (inline-blocks, text) AFTER the last
                // block-level child. If so, this content separates the child's margin from the
                // parent's margin, and they should NOT collapse.
                // Note: Empty zero-height blocks (like containers for only floats) don't count as
                // "separating content" - margins can collapse through them.
                bool has_content_after = false;
                View* sibling = (View*)effective_last->next_sibling;
                while (sibling) {
                    if (sibling->view_type) {
                        // Any placed view after effective_last means content separates the margins
                        // Except for absolutely/fixed positioned elements and floats which are out of flow
                        if (sibling->is_block()) {
                            ViewBlock* sb = (ViewBlock*)sibling;
                            bool is_truly_out_of_flow = sb->position &&
                                (sb->position->position == CSS_VALUE_ABSOLUTE ||
                                 sb->position->position == CSS_VALUE_FIXED ||
                                 element_has_float(sb));
                            // Inline-blocks ARE inline-level content that separates margins
                            bool is_inline_level = (sb->view_type == RDT_VIEW_INLINE_BLOCK);
                            if (is_inline_level) {
                                has_content_after = true;
                                break;
                            }
                            // Regular blocks with zero height don't separate margins (CSS 8.3.1)
                            // Margins can collapse "through" empty blocks
                            if (!is_truly_out_of_flow && sb->height > 0) {
                                has_content_after = true;
                                break;
                            }
                        } else {
                            // Non-block content (text, inline elements)
                            // CSS 2.1 §9.2.1.1: Zero-height inline content is
                            // wrapped in implicit self-collapsing anonymous blocks
                            if (sibling->height > 0) {
                                has_content_after = true;
                                break;
                            }
                        }
                    }
                    sibling = (View*)sibling->next_sibling;
                }

                if (has_content_after) {
                    log_debug("%s NOT collapsing bottom margin - content exists after last block child", block->source_loc());
                } else if (last_child_block->bound->clearance_in_margin_chain) {
                    // CSS 2.1 §8.3.1: If the last child's margin chain includes a
                    // self-collapsing element with clearance, the resulting margin
                    // does NOT collapse with the parent block's bottom margin.
                    log_debug("%s [CLEARANCE] NOT collapsing bottom margin - last child has clearance_in_margin_chain", block->source_loc());
                } else {
                    float parent_margin = block->bound ? block->bound->margin.bottom : 0;
                    // CSS 2.1 §8.3.1: Use chain-aware collapse when the child has chain components.
                    // This preserves mixed-sign information through parent-child bottom collapse.
                    float margin_bottom;
                    float chain_pos = 0, chain_neg = 0;
                    if (!child_mb_is_quirky && has_margin_chain(last_child_block->bound)) {
                        float parent_pos, parent_neg;
                        margin_to_chain(parent_margin, &parent_pos, &parent_neg);
                        chain_pos = max(parent_pos, last_child_block->bound->margin_chain_positive);
                        chain_neg = min(parent_neg, last_child_block->bound->margin_chain_negative);
                        margin_bottom = chain_pos + chain_neg;
                    } else {
                        margin_bottom = collapse_margins(parent_margin, effective_child_mb);
                        margin_to_chain(margin_bottom, &chain_pos, &chain_neg);
                    }
                    // CSS 2.1 §10.6.3 + erratum q313: The collapsible margin was already
                    // excluded from auto height in finalize_block_flow (before min/max
                    // constraints). Don't subtract from height again — just transfer
                    // the collapsed margin to the parent.

                    // If parent has no bound yet, allocate one to store the collapsed margin
                    if (!block->bound) {
                        block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
                        memset(block->bound, 0, sizeof(BoundaryProp));
                    }
                    // Track the amount of margin that was added by collapse (from child),
                    // separate from the block's own specified margin. This is needed by
                    // the grandparent's auto height calculation (§10.6.3 "However" clause).
                    float own_mb = block->bound->margin.bottom;
                    block->bound->margin.bottom = margin_bottom;
                    block->bound->margin_chain_positive = chain_pos;
                    block->bound->margin_chain_negative = chain_neg;
                    block->bound->collapsed_through_mb = max(0.f, margin_bottom - own_mb);
                    last_child_block->bound->margin.bottom = 0;
                    last_child_block->bound->margin_chain_positive = 0;
                    last_child_block->bound->margin_chain_negative = 0;
                    log_debug("%s collapsed bottom margin %f (chain pos=%f neg=%f) between block and last child (height unchanged at %f)", block->source_loc(),
                        margin_bottom, chain_pos, chain_neg, block->height);
                }
            }
        }
    }

    // CSS 2.1 §8.3.1: Transitive margin adjacency through self-collapsing children.
    // When ALL in-flow children are self-collapsing and the parent has no top/bottom
    // border/padding (auto height), the margin chain connects:
    //   parent.mt ↔ first_child.mt → ... sibling chain → last_child.mb ↔ parent.mb
    // All margins form one adjoining set. The bottom margin chain already accumulated
    // all margins through sibling collapse, so unify the top margin with it.
    if (block->bound && block->bound->margin.bottom != block->bound->margin.top &&
        !has_border_bottom && !has_padding_bottom && !creates_bfc_for_collapse && !has_explicit_height) {
        bool has_border_t = block->bound->border && block->bound->border->width.top > 0;
        bool has_padding_t = block->bound->padding.top > 0;
        if (!has_border_t && !has_padding_t) {
            // check if ALL in-flow children are self-collapsing
            bool all_self_collapsing = true;
            bool has_any_in_flow = false;
            View* sc_child = ((ViewElement*)block)->first_placed_child();
            while (sc_child) {
                if (sc_child->is_block()) {
                    ViewBlock* vb = (ViewBlock*)sc_child;
                    bool oof = (vb->position && element_has_float(vb)) ||
                        (vb->position && (vb->position->position == CSS_VALUE_ABSOLUTE ||
                                          vb->position->position == CSS_VALUE_FIXED));
                    if (!oof) {
                        has_any_in_flow = true;
                        if (!is_block_self_collapsing(vb)) {
                            all_self_collapsing = false;
                            break;
                        }
                        // CSS 2.1 §9.5.2: clearance breaks margin adjacency.
                        // A self-collapsing child with clearance creates real space
                        // (its y position is non-zero due to clearance), so the
                        // parent's margins do NOT collapse through.
                        if (vb->bound && vb->bound->clearance_in_margin_chain) {
                            all_self_collapsing = false;
                            break;
                        }
                    }
                } else if (sc_child->view_type) {
                    // non-block in-flow content (text, inline) breaks the chain
                    if (sc_child->height > 0) {
                        all_self_collapsing = false;
                        break;
                    }
                    // CSS Inline 3 §2.1: zero-height inline with non-zero inline-axis
                    // decorations generates a non-phantom line box
                    if (sc_child->view_type == RDT_VIEW_INLINE &&
                        is_inline_substantial((ViewElement*)sc_child)) {
                        all_self_collapsing = false;
                        break;
                    }
                }
                View* next = (View*)sc_child->next_sibling;
                while (next && !next->view_type) next = (View*)next->next_sibling;
                sc_child = next;
            }

            if (has_any_in_flow && all_self_collapsing) {
                float old_mt = block->bound->margin.top;
                float new_mt = block->bound->margin.bottom;
                float delta = new_mt - old_mt;
                block->bound->margin.top = new_mt;
                block->bound->margin.bottom = 0;  // consumed by unified collapse
                block->bound->margin_chain_positive = 0;
                block->bound->margin_chain_negative = 0;
                block->y += delta;
                // CSS 2.1 §10.6.4: The parent block's y changed; adjust any abs-pos
                // descendants whose static position was computed using the old y.
                if (delta != 0 && (!block->position || block->position->position == CSS_VALUE_STATIC)) {
                    adjust_abs_descendants_y((ViewElement*)block, delta);
                }
                // CSS 2.1 §8.3.1: All self-collapsing children's margins were absorbed
                // by the unified parent margin. Reset their positions to y=0 within
                // the parent (they are all zero-height, so this is safe).
                View* fix_child = ((ViewElement*)block)->first_placed_child();
                while (fix_child) {
                    if (fix_child->is_block()) {
                        ViewBlock* vb = (ViewBlock*)fix_child;
                        bool oof = (vb->position && element_has_float(vb)) ||
                            (vb->position && (vb->position->position == CSS_VALUE_ABSOLUTE ||
                                              vb->position->position == CSS_VALUE_FIXED));
                        if (!oof) {
                            float child_delta = -vb->y;  // resetting to 0
                            vb->y = 0;
                            if (vb->bound) vb->bound->margin.top = 0;
                            // Also adjust abs-pos descendants inside this child
                            if (child_delta != 0 &&
                                (!vb->position || vb->position->position == CSS_VALUE_STATIC)) {
                                adjust_abs_descendants_y((ViewElement*)vb, child_delta);
                            }
                        }
                    }
                    View* next = (View*)fix_child->next_sibling;
                    while (next && !next->view_type) next = (View*)next->next_sibling;
                    fix_child = next;
                }
                // CSS 2.1 §8.3.1: Floats inside this block were positioned in
                // the parent BFC using the old block.y. Update their BFC coordinates
                // to reflect the new position after margin unification.
                if (delta != 0) {
                    BlockContext* bfc = block_context_find_bfc(&lycon->block);
                    if (bfc) {
                        for (int pass = 0; pass < 2; pass++) {
                            FloatBox* fb = (pass == 0) ? bfc->left_floats : bfc->right_floats;
                            for (; fb; fb = fb->next) {
                                if (!fb->element) continue;
                                // check if this float is a descendant of the current block
                                DomNode* ancestor = (DomNode*)fb->element->parent;
                                while (ancestor) {
                                    if (ancestor == (DomNode*)block) {
                                        fb->margin_box_top += delta;
                                        fb->margin_box_bottom += delta;
                                        fb->y += delta;
                                        break;
                                    }
                                    ancestor = ancestor->parent;
                                }
                            }
                        }
                    }
                }
                log_debug("%s unified margin chain: all children self-collapsing, mt %f -> %f, y adjusted by %f", block->source_loc(),
                    old_mt, new_mt, delta);
            }
        }
    }

    // BFC (Block Formatting Context) height expansion to contain floats
    // CSS 2.1 §10.6.7: In certain cases (BFC roots with AUTO height), the heights
    // of floating descendants are also taken into account when computing the height.
    // When height is explicitly specified, the explicit height is used and floats may overflow.
    // BFC is established by: overflow != visible, display: flow-root, float != none, etc.
    bool creates_bfc = block->scroller &&
                       (block->scroller->overflow_x != CSS_VALUE_VISIBLE ||
                        block->scroller->overflow_y != CSS_VALUE_VISIBLE);

    if ((creates_bfc || lycon->block.is_bfc_root) && !has_explicit_height) {
        // Check unified BlockContext for float containment
        if (lycon->block.establishing_element == block) {
            float max_float_bottom = lycon->block.lowest_float_bottom;
            float content_bottom = block->y + block->height;
            log_debug("%s [BlockContext] Height expansion check: max_float_bottom=%.1f, content_bottom=%.1f", block->source_loc(),
                      max_float_bottom, content_bottom);
            if (max_float_bottom > content_bottom - block->y) {
                float old_height = block->height;
                block->height = max_float_bottom;
                log_debug("%s [BlockContext] Height expanded: old=%.1f, new=%.1f", block->source_loc(), old_height, block->height);
            }
        }

        // Also check for floats in block context
        log_debug("%s BFC %s: left_float_count=%d, right_float_count=%d", block->source_loc(),
            block->node_name(), lycon->block.left_float_count, lycon->block.right_float_count);
        if (lycon->block.establishing_element == block) {
            // Find the maximum bottom of all floated children
            float max_float_bottom = 0;
            log_debug("%s BFC %s: checking left floats", block->source_loc(), block->node_name());
            for (FloatBox* fb = lycon->block.left_floats; fb; fb = fb->next) {
                log_debug("%s BFC left float: margin_box_bottom=%.1f", block->source_loc(), fb->margin_box_bottom);
                if (fb->margin_box_bottom > max_float_bottom) {
                    max_float_bottom = fb->margin_box_bottom;
                }
            }
            log_debug("%s BFC %s: checking right floats", block->source_loc(), block->node_name());
            for (FloatBox* fb = lycon->block.right_floats; fb; fb = fb->next) {
                log_debug("%s BFC right float: margin_box_bottom=%.1f", block->source_loc(), fb->margin_box_bottom);
                if (fb->margin_box_bottom > max_float_bottom) {
                    max_float_bottom = fb->margin_box_bottom;
                }
            }

            // Float margin_box coordinates are relative to container's content area
            // Compare to block->height which is also relative/local
            log_debug("%s BFC %s: max_float_bottom=%.1f, block->height=%.1f", block->source_loc(),
                block->node_name(), max_float_bottom, block->height);
            if (max_float_bottom > block->height) {
                float old_height = block->height;
                block->height = max_float_bottom;
                log_debug("%s BFC height expansion: old=%.1f, new=%.1f (float_bottom=%.1f)", block->source_loc(),
                          old_height, block->height, max_float_bottom);

                // Update scroller clip to match new height (for overflow:hidden rendering)
                if (block->scroller && block->scroller->has_clip) {
                    block->scroller->clip.bottom = block->height;
                    log_debug("%s BFC updated clip.bottom to %.1f", block->source_loc(), block->height);
                }
            }
        }
    }

    // CSS 2.1 §10.5: Re-resolve percentage heights for absolutely positioned children.
    // When this block is a containing block (has position:relative/absolute/fixed) with
    // auto height, abs children' percentage heights were initially resolved against 0
    // because the auto height wasn't known yet. Now that it's finalized, re-resolve them.
    if (block->position && block->position->first_abs_child) {
        bool had_auto_height = !(block->blk && block->blk->given_height >= 0);
        if (had_auto_height) {
            re_resolve_abs_children_vertical(block);
        }
    }

    // Apply CSS float layout using BlockContext
    // IMPORTANT: Floats must be added to the BFC root, not just the immediate parent
    if (block->position && element_has_float(block)) {
        log_debug("%s Element has float property, applying float layout", block->source_loc());

        // Position the float using the parent's BlockContext
        // layout_float_element uses block_context_find_bfc which walks up parent chain
        layout_float_element(lycon, block);

        // Add float to the BFC root (not just immediate parent)
        // This ensures sibling elements can see the float via block_context_find_bfc
        BlockContext* bfc = block_context_find_bfc(pa_block);
        if (bfc) {
            block_context_add_float(bfc, block);
            log_debug("%s [BlockContext] Float added to BFC root (bfc=%p, pa_block=%p)", block->source_loc(), (void*)bfc, (void*)pa_block);
        } else {
            // Fallback to parent if no BFC found
            block_context_add_float(pa_block, block);
            log_debug("%s [BlockContext] Float added to parent context (no BFC found)", block->source_loc());
        }
    }

    // Restore parent BFC if we created a new one - handled by block.parent in calling code
}

static int layout_block_count = 0;

void layout_block(LayoutContext* lycon, DomNode *elmt, DisplayValue display) {
    layout_block_count++;
    auto t_block_start = high_resolution_clock::now();
    uintptr_t tag = elmt->tag();
    if (tag == HTM_TAG_IMG) {
        log_debug("[LAYOUT_BLOCK IMG] layout_block ENTRY for IMG element: %s", elmt->source_loc());
    }

    log_enter();
    // display: CSS_VALUE_BLOCK, CSS_VALUE_INLINE_BLOCK, CSS_VALUE_LIST_ITEM
    log_debug("layout block %s (display: outer=%d, inner=%d)", elmt->source_loc(), display.outer, display.inner);

    // check for display math elements (class="math display")
    if (elmt->is_element()) {
        DomElement* elem = static_cast<DomElement*>(elmt);
        if (is_display_math_element(elem)) {
            // ensure line break before display math
            if (!lycon->line.is_line_start) { line_break(lycon); }
            layout_display_math_block(lycon, elem);
            log_leave();
            return;
        }
    }

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

    // CSS 2.1 §10.3.7: For absolutely positioned elements whose specified display
    // was inline (blockified by §9.7), the static position is the inline cursor.
    // Skip line_break to preserve the inline cursor in pa_line->advance_x.
    // For originally block-level abs-pos elements, line_break is needed so the
    // static position is at the start of a new line.
    bool is_blockified_inline_abspos = false;
    if (elmt->is_element()) {
        DomElement* elem = elmt->as_element();
        if (elem->display.outer == CSS_VALUE_INLINE &&
            elem->position &&
            (elem->position->position == CSS_VALUE_ABSOLUTE ||
             elem->position->position == CSS_VALUE_FIXED)) {
            is_blockified_inline_abspos = true;
        }
    }

    // Only cause line break for non-inline-block, non-float, non-blockified-inline-abspos blocks
    if (display.outer != CSS_VALUE_INLINE_BLOCK && !is_float && !is_blockified_inline_abspos) {
        if (!lycon->line.is_line_start) {
            line_break(lycon);
        } else if (lycon->line.start_view) {
            // CSS 2.1 §9.4.2: A block starts but the current line had no actual
            // content (is_line_start still true). Any views allocated for empty
            // inline elements on this "line" should not carry to the next line.
            // Clear start_view so the collapsed-inline fixup in line_break()
            // won't incorrectly assign height to those empty spans.
            lycon->line.start_view = NULL;
        }
    }
    // save parent context
    BlockContext pa_block = lycon->block;  Linebox pa_line = lycon->line;
    FontBox pa_font = lycon->font;  lycon->font.current_font_size = -1;  // -1 as unresolved
    lycon->block.parent = &pa_block;  lycon->elmt = elmt;
    log_debug("saved pa_block.advance_y: %.2f for element %s", pa_block.advance_y, elmt->source_loc());
    lycon->block.content_width = lycon->block.content_height = 0;
    lycon->block.given_width = -1;  lycon->block.given_height = -1;
    // reset line ascender/descender state so stale values from the parent context
    // do not contaminate this child block's baseline computation
    lycon->block.first_line_ascender = 0;
    lycon->block.last_line_ascender = 0;
    lycon->block.last_line_max_ascender = 0;
    lycon->block.last_line_max_descender = 0;
    lycon->block.first_line_max_ascender = 0;
    lycon->block.first_line_max_descender = 0;

    uintptr_t elmt_name = elmt->tag();
    ViewBlock* block = (ViewBlock*)set_view(lycon,
        // Check table first to handle inline-table correctly
        display.inner == CSS_VALUE_TABLE ? RDT_VIEW_TABLE :
        display.outer == CSS_VALUE_INLINE_BLOCK ? RDT_VIEW_INLINE_BLOCK :
        display.outer == CSS_VALUE_LIST_ITEM ? RDT_VIEW_LIST_ITEM :
        RDT_VIEW_BLOCK,
        elmt);
    block->display = display;

    // resolve CSS styles
    dom_node_resolve_style(elmt, lycon);

    // =========================================================================
    // LAYOUT CACHE INTEGRATION (Phase 3: Run Mode Integration)
    // Try cache lookup for early bailout when dimensions already computed
    // =========================================================================
    DomElement* dom_elem = elmt->is_element() ? elmt->as_element() : nullptr;
    radiant::LayoutCache* cache = dom_elem ? dom_elem->layout_cache : nullptr;

    // Build known dimensions from current constraints
    radiant::KnownDimensions known_dims = radiant::known_dimensions_none();
    if (block->blk && block->blk->given_width > 0) {
        known_dims.width = block->blk->given_width;
        known_dims.has_width = true;
    }
    if (block->blk && block->blk->given_height > 0) {
        known_dims.height = block->blk->given_height;
        known_dims.has_height = true;
    }

    // Try cache lookup
    if (cache) {
        radiant::SizeF cached_size;
        if (radiant::layout_cache_get(cache, known_dims, lycon->available_space,
                                       lycon->run_mode, &cached_size)) {
            // Cache hit! Use cached dimensions
            block->width = cached_size.width;
            block->height = cached_size.height;
            g_layout_cache_hits++;
            log_info("%s BLOCK CACHE HIT: element=%s, size=(%.1f x %.1f), mode=%d", elmt->source_loc(),
                     elmt->node_name(), cached_size.width, cached_size.height, (int)lycon->run_mode); // INT_CAST_OK: enum for log
            // Restore parent context and return early
            lycon->block = pa_block;  lycon->font = pa_font;  lycon->line = pa_line;
            log_leave();
            auto t_block_end = high_resolution_clock::now();
            g_block_layout_time += duration<double, std::milli>(t_block_end - t_block_start).count();
            g_block_layout_count++;
            return;
        }
        g_layout_cache_misses++;
        log_debug("BLOCK CACHE MISS: element=%s, mode=%d", elmt->source_loc(), (int)lycon->run_mode); // INT_CAST_OK: enum for log
    }

    // Early bailout for ComputeSize mode when both dimensions are known
    if (lycon->run_mode == radiant::RunMode::ComputeSize) {
        bool has_definite_width = (block->blk && block->blk->given_width > 0);
        bool has_definite_height = (block->blk && block->blk->given_height > 0);

        if (has_definite_width && has_definite_height) {
            // Both dimensions known - can skip full layout
            block->width = block->blk->given_width;
            block->height = block->blk->given_height;
            log_info("%s BLOCK EARLY BAILOUT: Both dimensions known (%.1fx%.1f), skipping full layout", elmt->source_loc(),
                     block->width, block->height);
            // Restore parent context and return early
            lycon->block = pa_block;  lycon->font = pa_font;  lycon->line = pa_line;
            log_leave();
            auto t_block_end = high_resolution_clock::now();
            g_block_layout_time += duration<double, std::milli>(t_block_end - t_block_start).count();
            g_block_layout_count++;
            return;
        }
        log_debug("%s BLOCK: ComputeSize mode but dimensions not fully known (w=%d, h=%d)", elmt->source_loc(),
                  has_definite_width, has_definite_height);
    }

    // CSS Counter handling (CSS 2.1 Section 12.4)
    // Push a new counter scope for this element
    if (lycon->counter_context) {
        counter_push_scope(lycon->counter_context);

        // OL/UL/MENU/DIR implicit counter-reset: list-item (CSS 2.1 §12.5)
        setup_list_container_counters(lycon, block, dom_elem);

        // Apply counter-reset if specified
        if (block->blk && block->blk->counter_reset) {
            log_debug("%s     [Block] Applying counter-reset: %s", elmt->source_loc(), block->blk->counter_reset);
            counter_reset(lycon->counter_context, block->blk->counter_reset);
            // CSS Lists 3: compute initial values for reversed() counters
            compute_reversed_counter_initial(lycon, dom_elem);
        }

        // Apply counter-increment if specified
        if (block->blk && block->blk->counter_increment) {
            log_debug("%s     [Block] Applying counter-increment: %s", elmt->source_loc(), block->blk->counter_increment);
            counter_increment(lycon->counter_context, block->blk->counter_increment);
        }

        // Apply counter-set if specified (CSS Lists 3)
        // Processed after counter-reset and counter-increment per spec
        if (block->blk && block->blk->counter_set) {
            log_debug("%s     [Block] Applying counter-set: %s", elmt->source_loc(), block->blk->counter_set);
            counter_set(lycon->counter_context, block->blk->counter_set);
        }

        // CSS 2.1 Section 12.5: List markers use implicit "list-item" counter
        if (display.outer == CSS_VALUE_LIST_ITEM || display.list_item) {
            process_list_item(lycon, block, elmt, dom_elem, display);

            // CSS 2.1 §8.3.1: Ensure list items have BoundaryProp allocated so
            // they participate correctly in margin collapsing. Without bound,
            // the retroactive sibling collapse path (designed for anonymous
            // wrappers) fires incorrectly, and parent-child collapse cannot
            // store the propagated margin-top from child elements.
            if (!block->bound) {
                block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
                memset(block->bound, 0, sizeof(BoundaryProp));
            }
        }
    }

    if (block->position && (block->position->position == CSS_VALUE_ABSOLUTE || block->position->position == CSS_VALUE_FIXED)) {
        layout_abs_block(lycon, elmt, block, &pa_block, &pa_line);
        lycon->block = pa_block;  lycon->font = pa_font;  lycon->line = pa_line;
    } else {
        // layout block content to determine content width and height
        // DEBUG: Check if this is a table before layout_block_content
        // Always print block type for debugging
        bool is_table = (block->view_type == RDT_VIEW_TABLE);
        layout_block_content(lycon, block, &pa_block, &pa_line);

        // WORKAROUND: Restore table height from global - it gets corrupted after return
        // This is a mysterious issue where the height field gets zeroed between
        // the return statement in layout_block_content and this point
        if (is_table && g_layout_table_height > 0) {
            block->height = g_layout_table_height;
            g_layout_table_height = 0;  // Reset for next table
        }

        // CSS 2.1 Section 10.8.1: For non-replaced inline-blocks with in-flow line boxes
        // and overflow:visible, the baseline is the baseline of the last line box.
        // last_line_ascender stores the distance from the border-box top to the baseline
        // (set in line_break() as advance_y - used_line_height + max_ascender).
        // The current (unbroken) line is always the true last line: if it has content,
        // use its baseline (advance_y + max_ascender) regardless of what line_break()
        // saved for prior lines. Only fall back to the line_break() value when the
        // current line is empty (e.g., trailing whitespace was collapsed).
        float content_last_line_ascender;
        if (lycon->line.max_ascender > 0) {
            // Current line has content — it IS the last line
            content_last_line_ascender = lycon->block.advance_y + lycon->line.max_ascender;
        } else {
            // Current line is empty — use the last broken line's baseline
            content_last_line_ascender = lycon->block.last_line_ascender;
        }

        // <button> elements go through normal child layout (not layout_form_control),
        // so line_break() previously overwrote last_line_ascender with just the raw
        // text ascender. Now that line_break() stores the full baseline offset
        // (advance_y - used_line_height + max_ascender, which includes border_top
        // and pad_top), and the single-line fallback also includes advance_y, the
        // override is no longer needed. Buttons correctly get their baseline from
        // the last line box position, matching other inline-blocks.

        bool content_has_line_boxes = content_last_line_ascender > 0;

        // CSS 2.1 §10.8.1: Replaced elements (img, iframe, video, embed, object)
        // always have their baseline at the bottom margin edge, regardless of
        // whether their internal layout produced line boxes.
        // Textarea is a multi-line scrollable control; per CSS 2.1 §10.8.1,
        // inline-blocks with overflow != visible use bottom margin edge baseline.
        // Even though textarea may not have an explicit scroller, treat it as
        // replaced for baseline purposes.
        // Select has no in-flow CSS line boxes (options are UA-rendered in a
        // shadow/internal context), so it also uses bottom-margin-edge baseline
        // per CSS 2.1 §10.8.1. This ensures correct baseline for mixed-height
        // selects on the same line (e.g., dropdown h=19 + listbox h=70).
        bool is_replaced = (block->tag() == HTM_TAG_IMG || block->tag() == HTM_TAG_IFRAME ||
            block->tag() == HTM_TAG_VIDEO || block->tag() == HTM_TAG_EMBED ||
            (block->tag() == HTM_TAG_OBJECT && block->get_attribute("data")) ||
            block->tag() == HTM_TAG_TEXTAREA ||
            block->tag() == HTM_TAG_SELECT);
        if (is_replaced) {
            content_has_line_boxes = false;
        }

        log_debug("%s inline-block content baseline: last_line_ascender=%.1f, has_line_boxes=%d, is_replaced=%d, block_height=%.1f", elmt->source_loc(),
            content_last_line_ascender, content_has_line_boxes, is_replaced, block->height);

        // Save content baseline to BlockProp for use in view_vertical_align second pass.
        // finalize_block_flow does this for normal blocks, but form controls skip it.
        if (block->blk && content_has_line_boxes && content_last_line_ascender > 0) {
            block->blk->last_line_max_ascender = content_last_line_ascender;
        }

        log_debug("%s flow block in parent context, block->y before restoration: %.2f, display.outer=%d, display.inner=%d, block->display.outer=%d", elmt->source_loc(),
            block->y, display.outer, display.inner, block->display.outer);
        lycon->block = pa_block;  lycon->font = pa_font;  lycon->line = pa_line;

        // flow the block in parent context
        // CSS 2.1 §9.7: Floats are blockified — a floated element that was
        // originally inline-block should NOT be positioned as inline-block.
        // The float positioning in finalize_block already set the correct x/y.
        bool is_float_element = block->position && element_has_float(block);
        if (display.outer == CSS_VALUE_INLINE_BLOCK && !is_float_element) {
            if (!lycon->line.start_view) lycon->line.start_view = (View*)block;

            // CSS 2.1 §9.5.1: inline-blocks must account for floats across their
            // full height, not just the line-height.  A float whose top is below
            // the current line but within the inline-block's height must still
            // reduce the available width.
            float inline_block_height = block->height;
            update_line_for_bfc_floats(lycon, inline_block_height);

            // Check available width considering floats
            float effective_left = lycon->line.has_float_intrusion ?
                lycon->line.effective_left : lycon->line.left;
            float effective_right = lycon->line.has_float_intrusion ?
                lycon->line.effective_right : lycon->line.right;

            log_debug("%s inline-block float check: has_float_intrusion=%d, effective_left=%.1f, effective_right=%.1f, line.left=%.1f, line.right=%.1f, advance_x=%.1f", elmt->source_loc(),
                lycon->line.has_float_intrusion, lycon->line.effective_left, lycon->line.effective_right,
                lycon->line.left, lycon->line.right, lycon->line.advance_x);

            // Ensure advance_x is at least at effective_left.
            // CSS 2.1 §16.1: negative text-indent can legitimately position first-line
            // items before the content area edge (advance_x < line.left).  Only clamp
            // when advance_x is within the content area but behind a float edge.
            if (lycon->line.advance_x < effective_left &&
                lycon->line.advance_x >= lycon->line.left) {
                lycon->line.advance_x = effective_left;
            }

            // Check if any ancestor has white-space: nowrap/pre (no wrapping allowed)
            // Walk up parent chain since inline spans don't have blk
            bool parent_nowrap = false;
            for (DomNode* cur = block->parent; cur; cur = cur->parent) {
                if (!cur->is_element()) break;
                DomElement* elem = static_cast<DomElement*>(cur);
                if (elem->blk && elem->blk->white_space != 0) {
                    CssEnum ws = elem->blk->white_space;
                    parent_nowrap = (ws == CSS_VALUE_NOWRAP || ws == CSS_VALUE_PRE);
                    break;
                }
            }

            if (lycon->line.advance_x + block->width > effective_right && !parent_nowrap) {
                if (!lycon->line.is_line_start) {
                    // CSS 2.1 §9.4.2: Break to next line if there's prior content
                    line_break(lycon);
                    // After line break, update effective bounds for new Y
                    update_line_for_bfc_floats(lycon, inline_block_height);
                    effective_left = lycon->line.has_float_intrusion ?
                        lycon->line.effective_left : lycon->line.left;
                    block->x = effective_left;
                } else if (lycon->line.has_float_intrusion) {
                    // CSS 2.1 §9.5: First item on line doesn't fit due to float —
                    // push below the float to find room
                    BlockContext* bfc = block_context_find_bfc(&lycon->block);
                    if (bfc) {
                        float bfc_y = lycon->block.bfc_offset_y + lycon->block.advance_y;
                        float new_y = block_context_find_y_for_width(bfc, block->width, bfc_y, inline_block_height);
                        float local_new_y = new_y - lycon->block.bfc_offset_y;
                        if (local_new_y > lycon->block.advance_y) {
                            lycon->block.advance_y = local_new_y;
                            line_reset(lycon);
                            update_line_for_bfc_floats(lycon, inline_block_height);
                        }
                        effective_left = lycon->line.has_float_intrusion ?
                            lycon->line.effective_left : lycon->line.left;
                    }
                    block->x = effective_left;
                } else if (lycon->line.advance_x > effective_left &&
                           effective_left + block->width <= effective_right) {
                    // advance_x was pushed by a float that no longer intrudes at this y.
                    // Reset to effective_left where the content fits.
                    lycon->line.advance_x = effective_left;
                    block->x = effective_left;
                } else {
                    // Genuine overflow, no float — stays on this line
                    block->x = lycon->line.advance_x;
                }
            } else {
                block->x = lycon->line.advance_x;
            }
            // Determine vertical-align: use explicit value or default to baseline
            bool has_explicit_valign = (block->in_line && block->in_line->vertical_align);
            bool is_inline_table = (display.inner == CSS_VALUE_TABLE);

            // CSS 2.1 §17.5.1: inline-table baseline = baseline of the first row
            float table_baseline = -1;
            if (is_inline_table) {
                table_baseline = find_first_baseline_recursive(lycon, (View*)block, 0, true);
                log_debug("%s inline-table baseline lookup for positioning: table_baseline=%.1f, block_h=%.1f", elmt->source_loc(),
                    table_baseline, block->height);
            }

            {
                CssEnum valign = has_explicit_valign ?
                    block->in_line->vertical_align : CSS_VALUE_BASELINE;
                float valign_offset = (block->in_line) ?
                    block->in_line->vertical_align_offset : 0;

                float item_height = block->height + (block->bound ?
                    block->bound->margin.top + block->bound->margin.bottom : 0);
                // For non-replaced inline-blocks with content: baseline is at content baseline
                // For replaced elements (like img): baseline is at bottom margin edge
                // CSS 2.1 §17.5.1: inline-table baseline = first-row baseline
                bool overflow_visible = !block->scroller ||
                    (block->scroller->overflow_x == CSS_VALUE_VISIBLE &&
                     block->scroller->overflow_y == CSS_VALUE_VISIBLE);
                float item_baseline;
                if (is_inline_table && table_baseline >= 0) {
                    // Inline-table with first-row baseline found
                    item_baseline = (block->bound ? block->bound->margin.top : 0) + table_baseline;
                } else if (content_has_line_boxes && overflow_visible) {
                    // Baseline from top of margin-box = margin.top + content_baseline
                    item_baseline = (block->bound ? block->bound->margin.top : 0) + content_last_line_ascender;
                } else {
                    // Replaced or no content: baseline at bottom margin edge
                    item_baseline = item_height;
                }
                // CSS 2.1 §10.8.1: Use the strut's baseline as a reference for alignment
                // when no text content has established a baseline on this line yet.
                // The strut is a zero-width inline box with the block container's font/line-height.
                // Only apply strut when no text has contributed to max_ascender yet,
                // to avoid font metrics inconsistencies with text-derived ascender.
                float baseline_ref = lycon->line.max_ascender > 0 ?
                    lycon->line.max_ascender :
                    lycon->block.init_ascender + lycon->block.lead_y;
                float line_height = max(lycon->block.line_height, baseline_ref + lycon->line.max_descender);
                float offset = calculate_vertical_align_offset(
                    lycon, valign, item_height, line_height,
                    baseline_ref, item_baseline, valign_offset);
                // For baseline-relative alignment (including length/percentage offsets),
                // update BOTH max_ascender and max_descender so the line box expands
                // to accommodate raised/lowered inline-blocks.  This ensures that the
                // offset recomputed below (and in view_vertical_align's second pass)
                // is non-negative, making the max(offset,0) clamp a no-op.
                if (has_explicit_valign && valign != CSS_VALUE_TOP && valign != CSS_VALUE_BOTTOM) {
                    float asc_contribution, desc_contribution;
                    if (valign == CSS_VALUE_MIDDLE) {
                        // CSS 2.1 §10.8.1: "Align the vertical midpoint of the box with
                        // the baseline of the parent box plus half the x-height of the parent."
                        // Image midpoint at: baseline - x_height/2  (above baseline)
                        // Ascender = item_height/2 + x_height/2 (amount above baseline)
                        // Descender = item_height/2 - x_height/2 (amount below baseline)
                        float x_height_half;
                        if (lycon->font.font_handle) {
                            float x_ratio = font_get_x_height_ratio(lycon->font.font_handle);
                            x_height_half = lycon->font.current_font_size * x_ratio / 2.0f;
                        } else {
                            x_height_half = lycon->font.current_font_size * 0.25f;
                        }
                        asc_contribution = item_height / 2.0f + x_height_half;
                        desc_contribution = item_height / 2.0f - x_height_half;
                    } else {
                        // For baseline, sub, super, length/percentage offsets
                        asc_contribution = item_baseline + valign_offset;
                        desc_contribution = item_height - item_baseline - valign_offset;
                    }
                    lycon->line.max_ascender = max(lycon->line.max_ascender, asc_contribution);
                    lycon->line.max_descender = max(lycon->line.max_descender, desc_contribution);
                    // Recompute offset with updated baseline so clamp is harmless
                    float updated_baseline_ref = lycon->line.max_ascender;
                    float updated_line_height = max(lycon->block.line_height,
                        updated_baseline_ref + lycon->line.max_descender);
                    offset = calculate_vertical_align_offset(
                        lycon, valign, item_height, updated_line_height,
                        updated_baseline_ref, item_baseline, valign_offset);
                }
                // Clamp offset to >= 0 to match view_vertical_align's behavior.
                // After the max_ascender update above, offset should already be >= 0
                // for baseline-relative alignment.
                block->y = lycon->block.advance_y + max(offset, 0.0f);  // block->bound->margin.top will be added below
                log_debug("%s valigned-inline-block: offset %f, line %f, block %f, adv: %f, y: %f, va:%d", elmt->source_loc(),
                    offset, line_height, block->height, lycon->block.advance_y, block->y, valign);
                // max_ascender/max_descender already updated above for
                // baseline-relative alignment; only update here for other cases.
                if (has_explicit_valign && valign != CSS_VALUE_TOP && valign != CSS_VALUE_BOTTOM) {
                    // already handled above
                }
                log_debug("%s new max_descender=%f", elmt->source_loc(), lycon->line.max_descender);
            }
            lycon->line.advance_x += block->width;
            if (block->bound) {
                block->x += block->bound->margin.left;
                block->y += block->bound->margin.top;
                lycon->line.advance_x += block->bound->margin.left + block->bound->margin.right;
            }
            log_debug("%s inline-block in line: x: %d, y: %d, adv-x: %d, mg-left: %d, mg-top: %d", elmt->source_loc(),
                block->x, block->y, lycon->line.advance_x, block->bound ? block->bound->margin.left : 0, block->bound ? block->bound->margin.top : 0);
            lycon->line.has_replaced_content = true;  // inline-block contributes to line box
            // update baseline
            // CSS 2.1 §10.8.1: vertical-align defaults to 'baseline' (CSS_VALUE__UNDEF=0 also means baseline).
            // Only treat as non-baseline if an explicit non-baseline value is set.
            bool has_non_baseline_valign = block->in_line &&
                block->in_line->vertical_align != 0 &&
                block->in_line->vertical_align != CSS_VALUE_BASELINE;
            if (has_non_baseline_valign) {
                float block_flow_height = block->height + (block->bound ? block->bound->margin.top + block->bound->margin.bottom : 0);
                if (block->in_line->vertical_align == CSS_VALUE_TEXT_TOP) {
                    lycon->line.max_descender = max(lycon->line.max_descender, block_flow_height - lycon->block.init_ascender);
                }
                else if (block->in_line->vertical_align == CSS_VALUE_TEXT_BOTTOM) {
                    lycon->line.max_ascender = max(lycon->line.max_ascender, block_flow_height - lycon->block.init_descender);
                }
                else if (block->in_line->vertical_align == CSS_VALUE_TOP) {
                    // CSS 2.1 §10.8.1: vertical-align:top/bottom elements don't participate
                    // in the first-pass baseline-relative line box height calculation.
                    // Their height is tracked separately and used in a second pass to
                    // expand the line box if needed.
                    lycon->line.max_top_bottom_height = max(lycon->line.max_top_bottom_height, block_flow_height);
                }
                else if (block->in_line->vertical_align == CSS_VALUE_BOTTOM) {
                    // CSS 2.1 §10.8.1: Same second-pass treatment as vertical-align:top.
                    lycon->line.max_top_bottom_height = max(lycon->line.max_top_bottom_height, block_flow_height);
                }
                else {
                    // For other vertical-align values (sub, super, middle, etc.)
                    lycon->line.max_descender = max(lycon->line.max_descender, block_flow_height - lycon->line.max_ascender);
                }
            } else {
                // default baseline alignment for inline block
                // CSS 2.1 Section 10.8.1:
                // - Non-replaced inline-block with in-flow line boxes and overflow:visible:
                //   baseline = baseline of last line box (captured as content_last_line_ascender)
                // - Non-replaced inline-block with no in-flow line boxes OR overflow != visible:
                //   baseline = bottom margin edge
                // - Replaced inline-block (like img): baseline = bottom margin edge
                // CSS 2.1 §17.5.1: inline-table baseline = baseline of first row

                // Check if this inline-block has overflow:visible and in-flow line boxes
                bool overflow_visible = !block->scroller ||
                    (block->scroller->overflow_x == CSS_VALUE_VISIBLE &&
                     block->scroller->overflow_y == CSS_VALUE_VISIBLE);

                // Form controls with text always report an internal text baseline,
                // regardless of overflow setting (they set last_line_ascender in layout_form_control).
                // SELECT is excluded: it uses bottom-margin-edge baseline (handled via is_replaced above).
                bool is_form_text_control = (block->item_prop_type == DomElement::ITEM_PROP_FORM &&
                    block->form && block->form->control_type != FORM_CONTROL_HIDDEN &&
                    block->form->control_type != FORM_CONTROL_IMAGE &&
                    block->form->control_type != FORM_CONTROL_SELECT);

                bool uses_content_baseline = (content_has_line_boxes && overflow_visible) ||
                    (content_has_line_boxes && is_form_text_control) ||
                    (is_inline_table && table_baseline >= 0);

                float effective_baseline = content_last_line_ascender;
                if (is_inline_table && table_baseline >= 0) {
                    effective_baseline = table_baseline;
                }

                if (uses_content_baseline) {
                    // Non-replaced inline-block with text content and overflow:visible,
                    // or inline-table with first-row baseline
                    // Distance above parent baseline = effective_baseline
                    // Distance below parent baseline = block->height - effective_baseline
                    lycon->line.max_ascender = max(lycon->line.max_ascender, effective_baseline +
                        (block->bound ? block->bound->margin.top : 0));
                    float descender_part = block->height - effective_baseline +
                        (block->bound ? block->bound->margin.bottom : 0);
                    lycon->line.max_descender = max(lycon->line.max_descender, descender_part);
                    log_debug("%s inline-block with content baseline: ascender=%.1f, descender=%.1f, block_h=%.1f", elmt->source_loc(),
                        effective_baseline, descender_part, block->height);
                } else {
                    // Replaced element or no in-flow content: baseline at bottom margin edge
                    // CSS 2.1 §10.8.1: The entire margin-box sits above the baseline.
                    if (block->bound) {
                        // margin-box above baseline = height + margin-top + margin-bottom
                        lycon->line.max_ascender = max(lycon->line.max_ascender,
                            block->height + block->bound->margin.top + block->bound->margin.bottom);
                    }
                    else {
                        lycon->line.max_ascender = max(lycon->line.max_ascender, block->height);
                    }
                    // CSS 2.1 §10.8.1: The strut defines minimum height above and
                    // below the baseline. Even when only replaced content is present,
                    // the strut's below-baseline extent still contributes to the
                    // line box height. Compute actual half-leading (may be negative
                    // when line-height < font-size, e.g. line-height: 0).
                    float half_leading = (lycon->block.line_height -
                        (lycon->block.init_ascender + lycon->block.init_descender)) / 2;
                    float strut_below = lycon->block.init_descender + half_leading;
                    if (strut_below > 0) {
                        lycon->line.max_descender = max(lycon->line.max_descender, strut_below);
                    }
                }
                log_debug("%s inline-block set max_ascender to: %d", elmt->source_loc(), lycon->line.max_ascender);
            }
            // Inline-block's descent contribution must survive trailing whitespace rollback.
            // Update max_desc_before_last_text so rollback only undoes text contributions,
            // not inline-block contributions that came after the last text output.
            lycon->line.max_desc_before_last_text = max(lycon->line.max_desc_before_last_text,
                lycon->line.max_descender);
            // line got content
            lycon->line.reset_space();
        }
        else { // normal block
            // CSS 2.1 §8.3.1: Propagate first-child margin through pass-through blocks.
            // When a block has no border, padding, or margin (no bound), its first
            // child's top margin collapses "through" it. layout_block_content()
            // handles this by shifting parent->y, but the margin isn't stored — so
            // it can't participate in further parent-child collapse with the
            // grandparent.  Convert the y-shift into a proper margin.top ONLY when
            // this block is the first in-flow child of its parent (the only case
            // where further parent-child collapse is needed). For non-first-child
            // blocks, the y-shift alone is correct because it implicitly provides
            // sibling spacing that matches the collapsed margin.
            {
                float stored_mt = block->bound ? block->bound->margin.top : 0;
                float y_shift = block->y - pa_block.advance_y;
                float unaccounted = y_shift - stored_mt;
                if (fabsf(unaccounted) > 0.01f) {
                    ViewBlock* gp = block->parent->is_block() ? (ViewBlock*)block->parent : NULL;
                    if (gp) {
                        View* first = gp->first_placed_child();
                        // Find the first block child that participates in margin
                        // collapse. Skip markers (CSS Lists 3 §4), floats, and
                        // non-substantial inline/text content (whitespace between
                        // block elements) that doesn't create separating line boxes.
                        while (first) {
                            if (first->view_type == RDT_VIEW_MARKER) {
                                first = (View*)first->next_sibling;
                                while (first && !first->view_type) first = (View*)first->next_sibling;
                                continue;
                            }
                            if (!first->is_block()) {
                                // Substantial inline/text content creates line boxes
                                // that separate margins — stop searching.
                                if (first->height > 0) break;
                                if (first->view_type == RDT_VIEW_INLINE &&
                                    is_inline_substantial((ViewElement*)first)) break;
                                // Non-substantial (zero-height whitespace, empty inline)
                                first = (View*)first->next_sibling;
                                while (first && !first->view_type) first = (View*)first->next_sibling;
                                continue;
                            }
                            ViewBlock* fvb = (ViewBlock*)first;
                            if (fvb->position && element_has_float(fvb)) {
                                first = (View*)first->next_sibling;
                                while (first && !first->view_type) first = (View*)first->next_sibling;
                                continue;
                            }
                            break;
                        }
                        if (first == (View*)block) {
                            if (!block->bound) {
                                block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
                                memset(block->bound, 0, sizeof(BoundaryProp));
                            }
                            block->bound->margin.top = y_shift;
                            // Don't reset block->y here. If parent-child collapse occurs,
                            // the collapse code sets block->y = 0 itself. If the grandparent
                            // has border/padding preventing collapse, the y position must
                            // remain at its current value (advance_y + child_margin).
                            log_debug("%s propagated first-child margin through pass-through block: margin.top=%f (unaccounted=%f)", elmt->source_loc(), y_shift, unaccounted);
                        }
                    }
                }
            }

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
                log_debug("%s float block end (no advance_y update), pa max_width: %f, block hg: %f", elmt->source_loc(),
                    lycon->block.max_width, block->height);
                // Note: floats don't require is_line_start - they're out of flow
            } else if (block->bound) {
                // collapse top margin with parent block
                log_debug("%s check margin collapsing", elmt->source_loc());

                // Find first in-flow child that can participate in margin collapsing
                // Skip floats AND empty zero-height blocks (CSS 2.2 Section 8.3.1)
                // An empty block allows margins to collapse "through" it when:
                // - It has zero height
                // - It has no borders, padding, or line boxes
                View* first_in_flow_child = block->parent_view()->first_placed_child();
                while (first_in_flow_child) {
                    // Skip outside markers: they sit outside the principal box and
                    // don't participate in margin collapsing (CSS Lists 3 §4)
                    if (first_in_flow_child->view_type == RDT_VIEW_MARKER) {
                        View* next = (View*)first_in_flow_child->next_sibling;
                        while (next && !next->view_type) {
                            next = (View*)next->next_sibling;
                        }
                        first_in_flow_child = next;
                        continue;
                    }
                    if (!first_in_flow_child->is_block()) {
                        // CSS 2.1 §8.3.1 + CSS Inline 3 §2.1: Only real (non-phantom)
                        // line boxes separate the parent's margin from the first block
                        // child's margin. Skip invisible inline elements that generate
                        // phantom line boxes.
                        if (first_in_flow_child->view_type == RDT_VIEW_INLINE &&
                            !is_inline_substantial((ViewElement*)first_in_flow_child)) {
                            View* next = (View*)first_in_flow_child->next_sibling;
                            while (next && !next->view_type) next = (View*)next->next_sibling;
                            first_in_flow_child = next;
                            continue;
                        }
                        break;
                    }
                    ViewBlock* vb = (ViewBlock*)first_in_flow_child;
                    // Skip out-of-flow elements (floats, absolute, fixed)
                    // CSS 2.1 §9.4.1: Out-of-flow elements don't participate in
                    // parent-child margin collapsing
                    if (is_out_of_flow_block(vb)) {
                        View* next = (View*)first_in_flow_child->next_sibling;
                        while (next && !next->view_type) {
                            next = (View*)next->next_sibling;
                        }
                        first_in_flow_child = next;
                        continue;
                    }
                    // Skip empty zero-height blocks that have no borders/padding AND no margins.
                    // CSS 2.1 §8.3.1: Self-collapsing blocks (height=0, no border/padding) with margins
                    // ARE valid first in-flow children — their margins participate in parent-child collapse.
                    // Only skip blocks that have no margins either (truly invisible/empty).
                    if (vb->height == 0) {
                        // CSS 2.1 §8.3.1 + §9.5.2: Never skip blocks with a clear property.
                        // Clearance introduces spacing that breaks margin adjacency between
                        // the parent's top margin and subsequent children. Even if the block
                        // has no margins/border/padding, its clearance prevents later siblings'
                        // margins from collapsing through to the parent.
                        bool has_clear_prop = vb->position &&
                            (vb->position->clear == CSS_VALUE_LEFT ||
                             vb->position->clear == CSS_VALUE_RIGHT ||
                             vb->position->clear == CSS_VALUE_BOTH);
                        if (has_clear_prop) {
                            log_debug("%s not skipping zero-height block with clear property for margin collapsing", elmt->source_loc());
                            break;
                        }
                        // CSS 2.1 §17 + §8.3.1: Tables and other BFC-establishing elements
                        // are never self-collapsing, even with zero height. They act as
                        // margin-collapse barriers between parent and subsequent children.
                        // Only truly self-collapsing blocks can be skipped.
                        if (!is_block_self_collapsing(vb)) {
                            log_debug("%s not skipping zero-height non-self-collapsing block (table/BFC) for margin collapsing", elmt->source_loc());
                            break;
                        }
                        float border_top = vb->bound && vb->bound->border ? vb->bound->border->width.top : 0;
                        float border_bottom = vb->bound && vb->bound->border ? vb->bound->border->width.bottom : 0;
                        float padding_top = vb->bound ? vb->bound->padding.top : 0;
                        float padding_bottom = vb->bound ? vb->bound->padding.bottom : 0;
                        float margin_top_val = vb->bound ? vb->bound->margin.top : 0;
                        float margin_bottom_val = vb->bound ? vb->bound->margin.bottom : 0;
                        bool has_chain_margins = vb->bound && has_margin_chain(vb->bound);
                        if (border_top == 0 && border_bottom == 0 && padding_top == 0 && padding_bottom == 0
                            && margin_top_val == 0 && margin_bottom_val == 0 && !has_chain_margins) {
                            log_debug("%s skipping empty zero-height block (no margins) for margin collapsing", elmt->source_loc());
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

                // save original margin_top before any collapse modifies it
                // (needed for self-collapsing block calculation below)
                float original_margin_top = block->bound->margin.top;

                // Track whether parent-child collapse consumed this block's margins
                bool parent_child_collapsed = false;

                if (first_in_flow_child == block) {  // first in-flow child
                    // CSS Box 4 §margin-trim: block-start trims the first child's block-start
                    // margin. This is done earlier in the layout (before position calculation)
                    // by zeroing margin.top when the parent has MARGIN_TRIM_BLOCK_START.
                    // By the time we reach here, the margin is already 0 if trimmed.

                    // CSS 2.1 §9.5.2: If clearance was applied (saved_clear_y >= 0),
                    // skip parent-child margin collapse.
                    bool has_clearance = (lycon->block.saved_clear_y >= 0);

                    ViewBlock* parent = block->parent->is_block() ? (ViewBlock*)block->parent : NULL;
                    bool parent_creates_bfc = parent && block_context_establishes_bfc(parent);
                    float parent_padding_top = parent && parent->bound ? parent->bound->padding.top : 0;
                    float parent_border_top = parent && parent->bound && parent->bound->border ? parent->bound->border->width.top : 0;
                    float parent_margin_top = parent && parent->bound ? parent->bound->margin.top : 0;

                    // CSS 2.1 §8.3.1: Parent-child top margin collapse.
                    // Enter when child has non-zero mt, OR when the child is self-collapsing
                    // with non-zero mb or margin chain (the mb is adjoining to mt through
                    // self-collapsing, so it participates in the parent-child collapse).
                    bool first_child_self_collapsing = is_block_self_collapsing(block);
                    bool has_self_collapsing_margins = first_child_self_collapsing &&
                        (block->bound->margin.bottom != 0 || has_margin_chain(block->bound));
                    if (!has_clearance && (block->bound->margin.top != 0 || has_self_collapsing_margins)) {
                        // CSS 2.1 §9.5.2: If the PARENT had clearance applied, the parent's
                        // position is fixed by clearance. The child's margin collapses with the
                        // parent's margin conceptually, but the clearance hypothetical position
                        // already accounted for the child's margin contribution. Skip the parent
                        // y-adjustment to avoid double-counting.
                        bool parent_has_clearance = parent && parent->bound && parent->bound->has_clearance;
                        // Quirks mode: in a quirky container (body), quirky margins
                        // (UA default margins from h1-h6, p, ul, ol, etc.) are ignored
                        // during parent-child margin collapse. Non-quirky margins
                        // (author-specified) still collapse normally.
                        bool quirky_container = is_quirky_container(parent, lycon);
                        if (parent && parent->parent && !parent_creates_bfc &&
                            parent_padding_top == 0 && parent_border_top == 0) {
                            // Effective child margin-top for collapse: 0 if quirky in quirky container
                            float child_mt = (quirky_container && has_quirky_margin_top(block))
                                ? 0 : block->bound->margin.top;
                            // CSS 2.1 §8.3.1: collapse child and parent margins
                            float margin_top = collapse_margins(child_mt, parent_margin_top);

                            // CSS 2.1 §8.3.1: For self-collapsing first children, both mt and mb
                            // are adjoining to parent's mt through transitivity:
                            //   parent.mt <-> child.mt (first-child rule)
                            //   child.mt <-> child.mb (self-collapsing rule)
                            // Therefore all three collapse together as one set.
                            if (first_child_self_collapsing) {
                                // CSS 2.1 §8.3.1: Use chain-aware 3-way collapse to avoid
                                // information loss from pairwise collapse_margins with mixed signs.
                                // collapse(collapse(a,b),c) != collapse(a,b,c) for mixed signs.
                                float child_mb = (quirky_container && has_quirky_margin_bottom(block))
                                    ? 0 : block->bound->margin.bottom;
                                float chain_pos = max(max(child_mt, parent_margin_top), 0.f);
                                chain_pos = max(chain_pos, max(child_mb, 0.f));
                                float chain_neg = min(min(child_mt, parent_margin_top), 0.f);
                                chain_neg = min(chain_neg, min(child_mb, 0.f));
                                // CSS 2.1 §8.3.1: If child has a margin chain from its own
                                // self-collapsing descendants (propagated via bottom margin
                                // collapse), incorporate those values into the collapse set.
                                if (has_margin_chain(block->bound)) {
                                    chain_pos = max(chain_pos, block->bound->margin_chain_positive);
                                    chain_neg = min(chain_neg, block->bound->margin_chain_negative);
                                }
                                margin_top = chain_pos + chain_neg;
                                log_debug("%s self-collapsing first child: including mb=%f in parent collapse, margin_top=%f (chain pos=%f neg=%f)", elmt->source_loc(),
                                    block->bound->margin.bottom, margin_top, chain_pos, chain_neg);
                            }

                            // CSS 2.1 §8.3.1: Parent-child collapse propagates the child's
                            // margin to the parent. The parent's sibling collapse (step 7)
                            // will handle collapsing with the parent's previous sibling.
                            // Do NOT do retroactive sibling collapse here — the bound
                            // allocated below makes the parent visible to step 7, which
                            // would double-count the sibling margin deduction.

                            if (!parent_has_clearance) {
                                float y_delta = margin_top - parent_margin_top;
                                parent->y += y_delta;

                                // CSS 2.1 §8.3.1: When parent-child margin collapse shifts
                                // the parent's y, floats registered from within the parent
                                // during prescan have stale BFC coordinates. Update them so
                                // float intrusion queries for subsequent children use correct
                                // positions. Only shift floats that are descendants of the
                                // parent but NOT descendants of the child (block): the child's
                                // y is zeroed, which compensates for the parent's shift for
                                // anything inside the child.
                                if (y_delta != 0) {
                                    BlockContext* float_bfc = block_context_find_bfc(&lycon->block);
                                    if (float_bfc) {
                                        ViewElement* child_view = (ViewElement*)block;
                                        for (FloatBox* fb = float_bfc->left_floats; fb; fb = fb->next) {
                                            if (fb->element && view_is_descendant_of((ViewElement*)fb->element, parent)
                                                && !view_is_descendant_of((ViewElement*)fb->element, child_view)) {
                                                fb->margin_box_top += y_delta;
                                                fb->margin_box_bottom += y_delta;
                                                fb->y += y_delta;
                                                log_debug("%s margin collapse float update: float %s shifted by %.1f",
                                                          block->source_loc(), fb->element->node_name(), y_delta);
                                            }
                                        }
                                        for (FloatBox* fb = float_bfc->right_floats; fb; fb = fb->next) {
                                            if (fb->element && view_is_descendant_of((ViewElement*)fb->element, parent)
                                                && !view_is_descendant_of((ViewElement*)fb->element, child_view)) {
                                                fb->margin_box_top += y_delta;
                                                fb->margin_box_bottom += y_delta;
                                                fb->y += y_delta;
                                                log_debug("%s margin collapse float update: float %s shifted by %.1f",
                                                          block->source_loc(), fb->element->node_name(), y_delta);
                                            }
                                        }
                                    }
                                }

                                // CSS 2.1 §8.3.1: Ensure parent->bound exists so margin.top
                                // can be stored. Without this, the "unified margin chain"
                                // code in finalize sees margin.top=0 and double-adjusts y.
                                if (!parent->bound) {
                                    parent->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
                                    memset(parent->bound, 0, sizeof(BoundaryProp));
                                }
                                parent->bound->margin.top = margin_top;
                            } else {
                                log_debug("%s [CLEARANCE] Parent has clearance — skipping y adjustment (margin would be %f)", elmt->source_loc(), margin_top);
                            }
                            block->y = 0;  block->bound->margin.top = 0;
                            parent_child_collapsed = true;

                            // For self-collapsing first child, set mb to carry the full
                            // collapsed margin for the next sibling's collapse.
                            // CSS 2.1 §8.3.1: Track chain components (max positive, most negative)
                            // so that subsequent siblings can correctly perform multi-way collapse.
                            // Without chain tracking, intermediate mixed-sign collapse loses the
                            // negative component (e.g., collapse(+16,-16)=0 stored as scalar 0,
                            // then collapse(0,+16)=16 instead of the correct 3-way result 0).
                            if (first_child_self_collapsing) {
                                // Use original_margin_top (saved before block->bound->margin.top was zeroed)
                                float sc_mt = (quirky_container && has_quirky_margin_top(block))
                                    ? 0 : original_margin_top;
                                float sc_mb = (quirky_container && has_quirky_margin_bottom(block))
                                    ? 0 : block->bound->margin.bottom;
                                float chain_pos = max(max(sc_mt, parent_margin_top), 0.f);
                                chain_pos = max(chain_pos, max(sc_mb, 0.f));
                                float chain_neg = min(min(sc_mt, parent_margin_top), 0.f);
                                chain_neg = min(chain_neg, min(sc_mb, 0.f));
                                // CSS 2.1 §8.3.1: Incorporate child's existing margin chain
                                // from descendants (propagated via bottom margin collapse).
                                if (has_margin_chain(block->bound)) {
                                    chain_pos = max(chain_pos, block->bound->margin_chain_positive);
                                    chain_neg = min(chain_neg, block->bound->margin_chain_negative);
                                }
                                set_margin_chain(block->bound, chain_pos, chain_neg);
                                log_debug("%s self-collapsing first child chain: pos=%f, neg=%f, mb=%f", elmt->source_loc(),
                                    chain_pos, chain_neg, block->bound->margin.bottom);
                            }

                            log_debug("%s collapsed margin between block and first child: %f, parent y: %f, block y: %f", elmt->source_loc(),
                                margin_top, parent->y, block->y);
                        }
                        else {
                            log_debug("%s no parent margin collapsing: parent->bound=%p, border-top=%f, padding-top=%f, parent_creates_bfc=%d", elmt->source_loc(),
                                parent ? parent->bound : NULL, parent_border_top, parent_padding_top, parent_creates_bfc);
                        }
                    }
                }
                else {
                    // CSS 2.1 §9.5.2: If clearance was applied (saved_clear_y >= 0),
                    // skip sibling margin collapse.
                    bool has_clearance = (lycon->block.saved_clear_y >= 0);

                    if (!has_clearance) {
                        // Normal sibling margin collapsing
                        float collapse = 0;
                        View* prev_view = block->prev_placed_view();
                        while (prev_view && prev_view->is_block()) {
                            ViewBlock* vb = (ViewBlock*)prev_view;
                            if (vb->position && element_has_float(vb)) {
                                prev_view = prev_view->prev_placed_view();
                                continue;
                            }
                            if (vb->position && (vb->position->position == CSS_VALUE_ABSOLUTE ||
                                                  vb->position->position == CSS_VALUE_FIXED)) {
                                prev_view = prev_view->prev_placed_view();
                                continue;
                            }
                            // CSS 2.1 §8.3.1: Skip zero-height blocks with no bound
                            // (self-collapsing with zero margins). Margins on either
                            // side are still adjacent through such elements.
                            if (vb->height == 0 && !vb->bound) {
                                prev_view = prev_view->prev_placed_view();
                                continue;
                            }
                            break;
                        }

                        if (prev_view && prev_view->is_block() && prev_view->view_type != RDT_VIEW_INLINE_BLOCK
                            && ((ViewBlock*)prev_view)->bound) {
                            ViewBlock* prev_block = (ViewBlock*)prev_view;
                            float prev_mb = prev_block->bound->margin.bottom;
                            float cur_mt = block->bound->margin.top;
                            if (prev_mb != 0 || cur_mt != 0 || has_margin_chain(prev_block->bound)) {
                                // CSS 2.1 §8.3.1: Use chain-aware collapse when previous block
                                // has chain components (from self-collapsing elements with mixed signs).
                                float collapsed;
                                if (has_margin_chain(prev_block->bound)) {
                                    float prev_pos = prev_block->bound->margin_chain_positive;
                                    float prev_neg = prev_block->bound->margin_chain_negative;
                                    float combined_pos = max(prev_pos, max(cur_mt, 0.f));
                                    float combined_neg = min(prev_neg, min(cur_mt, 0.f));
                                    collapsed = combined_pos + combined_neg;
                                } else {
                                    collapsed = collapse_margins(prev_mb, cur_mt);
                                }
                                collapse = (prev_mb + cur_mt) - collapsed;
                            }
                        }

                        if (collapse != 0) {
                            block->y -= collapse;
                            block->bound->margin.top -= collapse;
                            // CSS 2.1 §10.6.4: Adjust abs-pos descendants whose static
                            // position was computed before this margin collapse adjustment.
                            // Only needed when the block itself is NOT positioned, because
                            // a positioned block IS the containing block for its abs-pos
                            // descendants (their coordinates are relative to it, not affected
                            // by changes to the block's own position).
                            if (!block->position || block->position->position == CSS_VALUE_STATIC) {
                                adjust_abs_descendants_y((ViewElement*)block, -collapse);
                            }
                            // CSS 2.1 §10.6.7: Float boxes in the parent BFC were
                            // positioned using the old block->y. Update their BFC
                            // coordinates to reflect the margin collapse shift.
                            BlockContext* bfc = block_context_find_bfc(&lycon->block);
                            if (bfc) {
                                for (int pass = 0; pass < 2; pass++) {
                                    FloatBox* fb = (pass == 0) ? bfc->left_floats : bfc->right_floats;
                                    for (; fb; fb = fb->next) {
                                        if (!fb->element) continue;
                                        DomNode* ancestor = (DomNode*)fb->element->parent;
                                        while (ancestor) {
                                            if (ancestor == (DomNode*)block) {
                                                fb->margin_box_top -= collapse;
                                                fb->margin_box_bottom -= collapse;
                                                fb->y -= collapse;
                                                break;
                                            }
                                            ancestor = ancestor->parent;
                                        }
                                    }
                                }
                            }
                            log_debug("%s collapsed margin between sibling blocks: %f, block->y now: %f", elmt->source_loc(), collapse, block->y);
                        }
                    } else {
                        log_debug("%s [CLEARANCE] Clearance was applied, skipping sibling margin collapse", elmt->source_loc());
                    }
                }

                // CSS 2.2 Section 8.3.1: Self-collapsing blocks
                // A block is "self-collapsing" when its top and bottom margins are adjoining.
                // CSS 2.1 §8.3.1: Elements with clearance CAN be self-collapsing.
                // Clearance only prevents the element's margins from being adjoining with
                // the preceding sibling's margins, not the element's own top/bottom margins.
                bool is_self_collapsing = is_block_self_collapsing(block);

                // CSS 2.1 §8.3.1: Track if this block (or its margin chain) includes
                // a self-collapsing element with clearance. Such margins must NOT
                // collapse with the parent's bottom margin.
                bool block_has_clearance = (lycon->block.saved_clear_y >= 0);

                if (is_self_collapsing) {
                    if (parent_child_collapsed) {
                        // CSS 2.1 §8.3.1: Parent-child collapse already absorbed both
                        // mt and mb of this self-collapsing first child. The contribution
                        // to advance_y is zero — all margin space was pushed to the parent.
                        // margin.bottom was already set to carry the full collapsed margin
                        // for next sibling's use.
                        log_debug("%s self-collapsing first child: parent-child collapse absorbed margins, mb=%f (no contribution)", elmt->source_loc(),
                            block->bound->margin.bottom);
                    } else {
                        // self-collapsing: margins collapse through this element
                        // CSS 2.1 §8.3.1: The element's own margins merge via collapse_margins
                        // This merged margin then participates in sibling collapsing
                        float prev_mb = 0;
                        bool prev_has_clearance_chain = false;
                        ViewBlock* prev_block_for_chain = nullptr;
                        {
                            View* pv = block->prev_placed_view();
                            while (pv && pv->is_block()) {
                                ViewBlock* vb = (ViewBlock*)pv;
                                if (vb->position && element_has_float(vb)) { pv = pv->prev_placed_view(); continue; }
                                if (vb->position && (vb->position->position == CSS_VALUE_ABSOLUTE ||
                                                      vb->position->position == CSS_VALUE_FIXED)) { pv = pv->prev_placed_view(); continue; }
                                // CSS 2.1 §8.3.1: Skip zero-height no-bound blocks
                                if (vb->height == 0 && !vb->bound) { pv = pv->prev_placed_view(); continue; }
                                break;
                            }
                            if (pv && pv->is_block() && pv->view_type != RDT_VIEW_INLINE_BLOCK && ((ViewBlock*)pv)->bound) {
                                prev_block_for_chain = (ViewBlock*)pv;
                                prev_mb = prev_block_for_chain->bound->margin.bottom;
                                prev_has_clearance_chain = prev_block_for_chain->bound->clearance_in_margin_chain;
                            }
                        }

                        float self_collapsed = collapse_margins(original_margin_top, block->bound->margin.bottom);
                        float new_pending, contribution;

                        if (block_has_clearance) {
                            // CSS 2.1 §8.3.1 + §9.5.2: Clearance separates this element's
                            // margins from the preceding sibling's margins. The self-collapsed
                            // margin stands alone — it does NOT collapse with prev_mb.
                            new_pending = self_collapsed;
                            contribution = self_collapsed;
                        } else {
                            // CSS 2.1 §8.3.1: Multi-way collapse using chain components.
                            // When the previous sibling or this block has chained margins
                            // (from prior collapsing through self-collapsing elements),
                            // use the {max_positive, most_negative} components to avoid
                            // information loss from intermediate scalar collapse.
                            float prev_pos, prev_neg;
                            if (prev_block_for_chain) {
                                get_margin_chain(prev_block_for_chain, &prev_pos, &prev_neg);
                            } else {
                                prev_pos = 0; prev_neg = 0;
                            }
                            // Current block's chain: own margins + any chain from children
                            float cur_pos, cur_neg;
                            if (has_margin_chain(block->bound)) {
                                // Block already has chain (from children's parent-child collapse)
                                cur_pos = max(block->bound->margin_chain_positive, max(original_margin_top, 0.f));
                                cur_neg = min(block->bound->margin_chain_negative, min(original_margin_top, 0.f));
                            } else {
                                cur_pos = max(max(original_margin_top, 0.f), max(block->bound->margin.bottom, 0.f));
                                cur_neg = min(min(original_margin_top, 0.f), min(block->bound->margin.bottom, 0.f));
                            }

                            bool use_chain = (prev_neg < 0 || cur_neg < 0 ||
                                              has_margin_chain(block->bound) ||
                                              (prev_block_for_chain && has_margin_chain(prev_block_for_chain->bound)));
                            if (use_chain) {
                                float combined_pos = max(prev_pos, cur_pos);
                                float combined_neg = min(prev_neg, cur_neg);
                                new_pending = combined_pos + combined_neg;
                                // CSS 2.1 §8.3.1: contribution can undo up to prev_mb
                                // (which is already baked into advance_y), but cannot push
                                // advance_y backward past the pre-prev position. When
                                // new_pending < 0 and prev_mb = 0, there is nothing to
                                // undo — the negative margin carries as pending for the
                                // next sibling.
                                contribution = max(0.f, new_pending) - prev_mb;
                                log_debug("%s chain collapse: prev{pos=%f,neg=%f} + cur{pos=%f,neg=%f} = pending=%f, contribution=%f", elmt->source_loc(),
                                    prev_pos, prev_neg, cur_pos, cur_neg, new_pending, contribution);
                            } else {
                                new_pending = collapse_margins(prev_mb, self_collapsed);
                                contribution = max(0.f, new_pending - prev_mb);
                            }
                        }
                        lycon->block.advance_y += contribution;
                        // CSS 2.1 §8.3.1: When chain collapse produces a negative contribution,
                        // retroactively adjust this self-collapsing block's position.
                        // The block was placed at advance_y + margin.top, but the chain's
                        // negative component means the effective gap is smaller.
                        if (contribution < 0) {
                            block->y += contribution;
                            // CSS 2.1 §10.6.4: Adjust abs-pos descendants whose static
                            // position was computed before this y adjustment
                            if (!block->position || block->position->position == CSS_VALUE_STATIC) {
                                adjust_abs_descendants_y((ViewElement*)block, contribution);
                            }
                        }
                        // expose the merged margin to next sibling via margin.bottom
                        if (block_has_clearance) {
                            block->bound->margin.bottom = new_pending;
                        } else {
                            float prev_pos, prev_neg;
                            if (prev_block_for_chain) {
                                get_margin_chain(prev_block_for_chain, &prev_pos, &prev_neg);
                            } else {
                                prev_pos = 0; prev_neg = 0;
                            }
                            float cur_pos, cur_neg;
                            if (has_margin_chain(block->bound)) {
                                cur_pos = max(block->bound->margin_chain_positive, max(original_margin_top, 0.f));
                                cur_neg = min(block->bound->margin_chain_negative, min(original_margin_top, 0.f));
                            } else {
                                cur_pos = max(max(original_margin_top, 0.f), max(block->bound->margin.bottom, 0.f));
                                cur_neg = min(min(original_margin_top, 0.f), min(block->bound->margin.bottom, 0.f));
                            }
                            float combined_pos = max(prev_pos, cur_pos);
                            float combined_neg = min(prev_neg, cur_neg);
                            set_margin_chain(block->bound, combined_pos, combined_neg);
                        }

                        // CSS 2.1 §8.3.1: Propagate clearance flag through the margin chain.
                        // If this block has clearance, or the previous sibling's margin chain
                        // included a cleared element, the resulting margin must not collapse
                        // with the parent's bottom margin.
                        if (block_has_clearance || prev_has_clearance_chain) {
                            block->bound->clearance_in_margin_chain = true;
                            log_debug("%s [CLEARANCE] Self-collapsing block: marking clearance_in_margin_chain=true "
                                      "(has_clearance=%d, prev_chain=%d)", elmt->source_loc(), block_has_clearance, prev_has_clearance_chain);
                        }

                        log_debug("%s self-collapsing block: original_mt=%f, mb=%f, self_collapsed=%f, prev_mb=%f, contribution=%f, new_pending=%f, has_clearance=%d", elmt->source_loc(),
                            original_margin_top, block->bound->margin.bottom, self_collapsed, prev_mb, contribution, new_pending, block_has_clearance);
                    }
                } else {
                    lycon->block.advance_y += block->height + block->bound->margin.top + block->bound->margin.bottom;
                }
                // Include lycon->line.left to account for parent's left border+padding
                // For auto-width parents (shrink-to-fit), auto-width block children
                // should contribute their intrinsic content width, not their container-expanded width
                float child_w = block->width;
                if (lycon->block.given_width < 0
                    && (!block->blk || (block->blk->given_width < 0 && isnan(block->blk->given_width_percent)))) {
                    child_w = block->content_width;
                    if (block->bound->border) {
                        child_w += block->bound->border->width.left + block->bound->border->width.right;
                    }
                }
                lycon->block.max_width = max(lycon->block.max_width, lycon->line.left + child_w
                    + block->bound->margin.left + block->bound->margin.right);
            } else {
                // For no-bound blocks, use actual y position to compute advance_y.
                // Child margin collapsing (parent-child) may have shifted block->y beyond
                // the original advance_y, so we must use the actual position.
                lycon->block.advance_y = block->y + block->height;
                // Include lycon->line.left to account for parent's left border+padding
                // For auto-width parents (shrink-to-fit), auto-width block children
                // should contribute their intrinsic content width, not container-expanded width
                float child_w_nb = block->width;
                if (lycon->block.given_width < 0
                    && (!block->blk || (block->blk->given_width < 0 && isnan(block->blk->given_width_percent)))
                    && block->content_width < block->width) {
                    child_w_nb = block->content_width;
                }
                lycon->block.max_width = max(lycon->block.max_width, lycon->line.left + child_w_nb);
            }
            // For non-float blocks, we should be at line start after the block
            // (floats are handled above and don't require this assertion)
            if (!is_float) {
                assert(lycon->line.is_line_start);
            }
            log_debug("%s block end, pa max_width: %f, pa advance_y: %f, block hg: %f", elmt->source_loc(),
                lycon->block.max_width, lycon->block.advance_y, block->height);
        }

        // apply CSS relative/sticky positioning after normal layout
        if (block->position && block->position->position == CSS_VALUE_RELATIVE) {
            log_debug("%s Applying relative positioning", elmt->source_loc());
            layout_relative_positioned(lycon, block);
        } else if (block->position && block->position->position == CSS_VALUE_STICKY) {
            layout_sticky_positioned(lycon, block);
        }
    }

    // Pop counter scope when leaving this block — propagate so siblings see counters
    // propagate_resets=true for regular elements (sibling visibility per CSS 2.1 §12.4.1)
    if (lycon->counter_context) {
        counter_pop_scope_propagate(lycon->counter_context, true);
    }

    // =========================================================================
    // CACHE STORE: Save computed dimensions for future lookups
    // =========================================================================
    if (cache) {
        radiant::SizeF result = radiant::size_f(block->width, block->height);
        radiant::layout_cache_store(cache, known_dims, lycon->available_space,
                                    lycon->run_mode, result);
        g_layout_cache_stores++;
        log_debug("%s BLOCK CACHE STORE: element=%s, size=(%.1f x %.1f), mode=%d", elmt->source_loc(),
                  elmt->node_name(), block->width, block->height, (int)lycon->run_mode); // INT_CAST_OK: enum for log
    }

    log_leave();

    auto t_block_end = high_resolution_clock::now();
    double block_ms = duration<double, std::milli>(t_block_end - t_block_start).count();
    g_block_layout_time += block_ms;
    g_block_layout_count++;
    if (block_ms > 50.0) {
        log_warn("SLOW BLOCK: %s took %.0fms (count=%d)", elmt->source_loc(), block_ms, layout_block_count);
    }
}
