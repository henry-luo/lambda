/**
 * Event Simulation Implementation
 */

#include "event_sim.hpp"
#include "event.hpp"
#include "state_store.hpp"
#include "dom_range.hpp"
#include "form_control.hpp"
#include "view.hpp"
#include "webview.h"
#include "../lib/log.h"
#include "../lib/str.h"
#include "../lib/strbuf.h"
#include "../lib/file.h"
#include "../lib/memtrack.h"
#include "../lambda/lambda-data.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/input/css/css_parser.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/css_value.hpp"
#include "../lib/image.h"
#include "animation.h"
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <cstring>
#include <ctime>

// Portable monotonic time (works without GLFW initialization)
static double get_monotonic_time() {
#if defined(__APPLE__)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#elif defined(_WIN32)
    // Windows: use QueryPerformanceCounter
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

// Forward declarations for callbacks (defined in window.cpp)
extern void handle_event(UiContext* uicon, DomDocument* doc, RdtEvent* event);

// Forward declaration for parse_json
void parse_json(Input* input, const char* json_string);

// Map key name string to GLFW key code
static int key_name_to_glfw(const char* name) {
    if (!name) return GLFW_KEY_UNKNOWN;

    size_t name_len = strlen(name);

    // Single character keys
    if (name_len == 1) {
        char c = name[0];
        if (c >= 'a' && c <= 'z') return GLFW_KEY_A + (c - 'a');
        if (c >= 'A' && c <= 'Z') return GLFW_KEY_A + (c - 'A');
        if (c >= '0' && c <= '9') return GLFW_KEY_0 + (c - '0');
    }

    // Special keys
    if (str_ieq_const(name, name_len, "space")) return GLFW_KEY_SPACE;
    if (str_ieq_const(name, name_len, "enter") || str_ieq_const(name, name_len, "return")) return GLFW_KEY_ENTER;
    if (str_ieq_const(name, name_len, "tab")) return GLFW_KEY_TAB;
    if (str_ieq_const(name, name_len, "backspace")) return GLFW_KEY_BACKSPACE;
    if (str_ieq_const(name, name_len, "delete")) return GLFW_KEY_DELETE;
    if (str_ieq_const(name, name_len, "escape") || str_ieq_const(name, name_len, "esc")) return GLFW_KEY_ESCAPE;
    if (str_ieq_const(name, name_len, "left")) return GLFW_KEY_LEFT;
    if (str_ieq_const(name, name_len, "right")) return GLFW_KEY_RIGHT;
    if (str_ieq_const(name, name_len, "up")) return GLFW_KEY_UP;
    if (str_ieq_const(name, name_len, "down")) return GLFW_KEY_DOWN;
    if (str_ieq_const(name, name_len, "home")) return GLFW_KEY_HOME;
    if (str_ieq_const(name, name_len, "end")) return GLFW_KEY_END;
    if (str_ieq_const(name, name_len, "pageup")) return GLFW_KEY_PAGE_UP;
    if (str_ieq_const(name, name_len, "pagedown")) return GLFW_KEY_PAGE_DOWN;
    if (str_ieq_const(name, name_len, "control") || str_ieq_const(name, name_len, "ctrl")) return GLFW_KEY_LEFT_CONTROL;
    if (str_ieq_const(name, name_len, "shift")) return GLFW_KEY_LEFT_SHIFT;
    if (str_ieq_const(name, name_len, "alt")) return GLFW_KEY_LEFT_ALT;
    if (str_ieq_const(name, name_len, "super") || str_ieq_const(name, name_len, "cmd") || str_ieq_const(name, name_len, "meta")) return GLFW_KEY_LEFT_SUPER;

    // Function keys
    if (name[0] == 'f' || name[0] == 'F') {
        int num = (int)str_to_int64_default(name + 1, strlen(name + 1), 0);
        if (num >= 1 && num <= 12) return GLFW_KEY_F1 + (num - 1);
    }

    return GLFW_KEY_UNKNOWN;
}

// Parse modifier string to RDT_MOD_* flags
static int parse_mods_string(const char* mods_str) {
    if (!mods_str) return 0;
    int mods = 0;
    if (strstr(mods_str, "shift") || strstr(mods_str, "Shift")) mods |= RDT_MOD_SHIFT;
    if (strstr(mods_str, "ctrl") || strstr(mods_str, "Ctrl") || strstr(mods_str, "control")) mods |= RDT_MOD_CTRL;
    if (strstr(mods_str, "alt") || strstr(mods_str, "Alt")) mods |= RDT_MOD_ALT;
    if (strstr(mods_str, "super") || strstr(mods_str, "Super") || strstr(mods_str, "cmd") || strstr(mods_str, "Cmd")) mods |= RDT_MOD_SUPER;
    return mods;
}

// Helper: recursively find text view containing target string and get its absolute position
// Returns true if found, sets out_x and out_y to center of first match
// block_abs_x/y tracks the absolute position of the nearest block ancestor,
// matching how target_text_view uses evcon->block.x/y + text_rect->x/y.
static bool find_text_position_recursive(View* view, const char* target_text,
                                         float block_abs_x, float block_abs_y,
                                         float* out_x, float* out_y) {
    if (!view || !target_text) return false;

    // Only block views contribute to the absolute offset.
    // Inline spans and text nodes have x/y that duplicate their TextRect positions
    // (both are relative to the containing block), so we must not accumulate them.
    float child_block_abs_x = block_abs_x;
    float child_block_abs_y = block_abs_y;
    if (view->is_block()) {
        child_block_abs_x = block_abs_x + view->x;
        child_block_abs_y = block_abs_y + view->y;
    }

    log_debug("find_text: view_type=%d, x=%.1f, y=%.1f, block_abs=(%.1f, %.1f)",
              view->view_type, view->x, view->y, child_block_abs_x, child_block_abs_y);

    // check if this is a text view with matching text
    if (view->view_type == RDT_VIEW_TEXT) {
        DomText* text_view = view->as_text();
        if (text_view && text_view->text) {
            log_debug("find_text: text='%.30s...', searching for '%s'", text_view->text, target_text);
            const char* match = strstr(text_view->text, target_text);
            if (match) {
                // Found a match - calculate position of the target text within the TextRect
                // TextRect x/y are relative to containing block, matching the hit test
                TextRect* rect = text_view->rect;
                if (rect) {
                    // Find the character offset of the match within the text
                    int match_offset = match - text_view->text;
                    int target_len = strlen(target_text);

                    // Walk through TextRects to find which one contains the match
                    while (rect) {
                        int rect_start = rect->start_index;
                        int rect_end = rect_start + rect->length;

                        if (match_offset >= rect_start && match_offset < rect_end) {
                            // Match is in this TextRect - calculate x position
                            // Use block_abs + rect position to match hit test coordinate system
                            float text_x = child_block_abs_x + rect->x;
                            int chars_before = match_offset - rect_start;

                            // Use average char width, but only to find the start position
                            // (not the center) to minimize error
                            float avg_char_width = rect->width / rect->length;
                            float match_start_x = text_x + chars_before * avg_char_width;

                            // Position a few pixels into the first character of target
                            // This ensures we hit the target text reliably
                            float click_x = match_start_x + 3.0f;  // 3 pixels into first char

                            *out_x = click_x;
                            *out_y = child_block_abs_y + rect->y + rect->height / 2;
                            log_info("event_sim: found target_text '%s' at (%.1f, %.1f), match_offset=%d, rect=(%.1f, %.1f, %.1f, %.1f)",
                                     target_text, *out_x, *out_y, match_offset, rect->x, rect->y, rect->width, rect->height);
                            return true;
                        }
                        rect = rect->next;
                    }
                    // Match not found in any rect - fallback to first rect center
                    rect = text_view->rect;
                    *out_x = child_block_abs_x + rect->x + rect->width / 2;
                    *out_y = child_block_abs_y + rect->y + rect->height / 2;
                    log_warn("event_sim: target_text '%s' found in text but not in any TextRect, using center", target_text);
                    return true;
                } else {
                    log_warn("event_sim: text found but no TextRect");
                }
            }
        }
    }

    // Pass block absolute position to children
    // Only block views shift the coordinate origin; inline views don't
    DomElement* elem = view->as_element();
    if (elem) {
        View* child = (View*)elem->first_child;
        while (child) {
            if (find_text_position_recursive(child, target_text, child_block_abs_x, child_block_abs_y, out_x, out_y)) {
                return true;
            }
            child = (View*)child->next_sibling;
        }
    }

    return false;
}

// Find position of text in document
static bool find_text_position(DomDocument* doc, const char* target_text, float* out_x, float* out_y) {
    if (!doc || !doc->view_tree || !doc->view_tree->root) return false;
    return find_text_position_recursive((View*)doc->view_tree->root, target_text, 0, 0, out_x, out_y);
}

// ============================================================================
// CSS Selector Element Finding
// ============================================================================

// Traverse view tree depth-first, calling visitor for each element
typedef bool (*SimViewVisitor)(View* view, void* udata);

static bool sim_traverse_views(View* view, SimViewVisitor visitor, void* udata) {
    if (!view) return true;
    if (view->is_element()) {
        if (!visitor(view, udata)) return false;
    }
    DomElement* elem = view->as_element();
    if (elem) {
        View* child = (View*)elem->first_child;
        while (child) {
            if (!sim_traverse_views(child, visitor, udata)) return false;
            child = (View*)child->next_sibling;
        }
    }
    return true;
}

typedef struct {
    CssSelector* selector;
    SelectorMatcher* matcher;
    View* result;
    int target_index;   // which match to return (0-based)
    int current_match;  // counter of matches found so far
} SimSelectorCtx;

static bool sim_selector_visitor(View* view, void* udata) {
    SimSelectorCtx* ctx = (SimSelectorCtx*)udata;
    if (!view->is_element()) return true;
    DomElement* dom_elem = (DomElement*)view;
    if (selector_matcher_matches(ctx->matcher, ctx->selector, dom_elem, NULL)) {
        if (ctx->current_match == ctx->target_index) {
            ctx->result = view;
            return false;  // stop on target match
        }
        ctx->current_match++;
    }
    return true;
}

// Find nth element matching a CSS selector in the document (0-based index)
static View* find_element_by_selector(DomDocument* doc, const char* selector_text, int index = 0) {
    if (!doc || !doc->view_tree || !doc->view_tree->root || !selector_text) return NULL;

    Pool* pool = doc->pool;
    if (!pool) return NULL;

    // Tokenize and parse the selector
    size_t token_count = 0;
    CssToken* tokens = css_tokenize(selector_text, strlen(selector_text), pool, &token_count);
    if (!tokens || token_count == 0) {
        log_error("event_sim: failed to tokenize selector '%s'", selector_text);
        return NULL;
    }
    int pos = 0;
    CssSelector* selector = css_parse_selector_with_combinators(tokens, &pos, (int)token_count, pool);
    if (!selector) {
        log_error("event_sim: failed to parse selector '%s'", selector_text);
        return NULL;
    }

    SelectorMatcher* matcher = selector_matcher_create(pool);
    if (!matcher) return NULL;

    SimSelectorCtx ctx = {0};
    ctx.selector = selector;
    ctx.matcher = matcher;
    ctx.result = NULL;
    ctx.target_index = index;
    ctx.current_match = 0;

    sim_traverse_views((View*)doc->view_tree->root, sim_selector_visitor, &ctx);
    return ctx.result;
}

// Count visitor context for assert_count
typedef struct {
    CssSelector* selector;
    SelectorMatcher* matcher;
    int count;
} SimCountCtx;

static bool sim_count_visitor(View* view, void* udata) {
    SimCountCtx* ctx = (SimCountCtx*)udata;
    if (!view->is_element()) return true;
    DomElement* dom_elem = (DomElement*)view;
    if (selector_matcher_matches(ctx->matcher, ctx->selector, dom_elem, NULL)) {
        ctx->count++;
    }
    return true;  // continue traversal (count all matches)
}

// Count elements matching a CSS selector in the document
static int count_elements_by_selector(DomDocument* doc, const char* selector_text) {
    if (!doc || !doc->view_tree || !doc->view_tree->root || !selector_text) return 0;

    Pool* pool = doc->pool;
    if (!pool) return 0;

    size_t token_count = 0;
    CssToken* tokens = css_tokenize(selector_text, strlen(selector_text), pool, &token_count);
    if (!tokens || token_count == 0) return 0;
    int pos = 0;
    CssSelector* selector = css_parse_selector_with_combinators(tokens, &pos, (int)token_count, pool);
    if (!selector) return 0;

    SelectorMatcher* matcher = selector_matcher_create(pool);
    if (!matcher) return 0;

    SimCountCtx ctx = {0};
    ctx.selector = selector;
    ctx.matcher = matcher;
    ctx.count = 0;

    sim_traverse_views((View*)doc->view_tree->root, sim_count_visitor, &ctx);
    return ctx.count;
}

// Get absolute position center of an element
static void get_element_center_abs(View* view, float* cx, float* cy) {
    if (!view) { *cx = 0; *cy = 0; return; }
    float abs_x = 0, abs_y = 0;
    View* current = view;
    while (current) {
        abs_x += current->x;
        abs_y += current->y;
        current = (View*)current->parent;
    }
    *cx = abs_x + view->width / 2;
    *cy = abs_y + view->height / 2;
}

// Get element absolute bounding rect (x, y, width, height)
static void get_element_rect_abs(View* view, float* out_x, float* out_y, float* out_w, float* out_h) {
    if (!view) { *out_x = 0; *out_y = 0; *out_w = 0; *out_h = 0; return; }
    float abs_x = 0, abs_y = 0;
    View* current = view;
    while (current) {
        abs_x += current->x;
        abs_y += current->y;
        current = (View*)current->parent;
    }
    *out_x = abs_x;
    *out_y = abs_y;
    *out_w = view->width;
    *out_h = view->height;
}

// Serialize a Color to "rgb(R, G, B)" or "rgba(R, G, B, A)" string
static void serialize_color(Color c, StrBuf* buf) {
    char tmp[64];
    if (c.a == 255 || c.a == 0) {
        snprintf(tmp, sizeof(tmp), "rgb(%d, %d, %d)", c.r, c.g, c.b);
    } else {
        float alpha = c.a / 255.0f;
        snprintf(tmp, sizeof(tmp), "rgba(%d, %d, %d, %.2g)", c.r, c.g, c.b, alpha);
    }
    strbuf_append_str(buf, tmp);
}

// Serialize a CssEnum value to its CSS keyword string
static const char* serialize_css_enum(CssEnum value) {
    if (value == CSS_VALUE_NONE) return "none";
    const CssEnumInfo* info = css_enum_info(value);
    return info ? info->name : "unknown";
}

// Get computed style value for a CSS property on an element
// Returns true if property was found, writes serialized value to buf
static bool get_computed_style(View* view, const char* property, StrBuf* buf) {
    if (!view || !property) return false;
    DomElement* elem = view->as_element();
    if (!elem) return false;

    char tmp[64]; // scratch buffer for numeric formatting

    // display
    if (strcmp(property, "display") == 0) {
        if (elem->display.outer == CSS_VALUE_NONE) {
            strbuf_append_str(buf, "none");
        } else {
            strbuf_append_str(buf, serialize_css_enum(elem->display.outer));
        }
        return true;
    }
    // position
    if (strcmp(property, "position") == 0) {
        if (elem->position) {
            strbuf_append_str(buf, serialize_css_enum(elem->position->position));
        } else {
            strbuf_append_str(buf, "static");
        }
        return true;
    }
    // font-size
    if (strcmp(property, "font-size") == 0) {
        if (elem->font) {
            snprintf(tmp, sizeof(tmp), "%.4gpx", elem->font->font_size);
            strbuf_append_str(buf, tmp);
        } else {
            strbuf_append_str(buf, "16px");
        }
        return true;
    }
    // font-weight
    if (strcmp(property, "font-weight") == 0) {
        if (elem->font) {
            if (elem->font->font_weight_numeric > 0) {
                snprintf(tmp, sizeof(tmp), "%d", (int)elem->font->font_weight_numeric);
                strbuf_append_str(buf, tmp);
            } else {
                strbuf_append_str(buf, serialize_css_enum(elem->font->font_weight));
            }
        } else {
            strbuf_append_str(buf, "400");
        }
        return true;
    }
    // font-family
    if (strcmp(property, "font-family") == 0) {
        if (elem->font && elem->font->family) {
            strbuf_append_str(buf, elem->font->family);
        }
        return true;
    }
    // font-style
    if (strcmp(property, "font-style") == 0) {
        if (elem->font) {
            strbuf_append_str(buf, serialize_css_enum(elem->font->font_style));
        } else {
            strbuf_append_str(buf, "normal");
        }
        return true;
    }
    // color (text color)
    if (strcmp(property, "color") == 0) {
        if (elem->in_line) {
            serialize_color(elem->in_line->color, buf);
        } else {
            strbuf_append_str(buf, "rgb(0, 0, 0)");
        }
        return true;
    }
    // background-color
    if (strcmp(property, "background-color") == 0) {
        if (elem->bound && elem->bound->background) {
            serialize_color(elem->bound->background->color, buf);
        } else {
            strbuf_append_str(buf, "rgba(0, 0, 0, 0)");
        }
        return true;
    }
    // text-align
    if (strcmp(property, "text-align") == 0) {
        if (elem->blk) {
            strbuf_append_str(buf, serialize_css_enum(elem->blk->text_align));
        } else {
            strbuf_append_str(buf, "start");
        }
        return true;
    }
    // box-sizing
    if (strcmp(property, "box-sizing") == 0) {
        if (elem->blk) {
            strbuf_append_str(buf, serialize_css_enum(elem->blk->box_sizing));
        } else {
            strbuf_append_str(buf, "content-box");
        }
        return true;
    }
    // opacity
    if (strcmp(property, "opacity") == 0) {
        if (elem->in_line) {
            snprintf(tmp, sizeof(tmp), "%.4g", elem->in_line->opacity);
            strbuf_append_str(buf, tmp);
        } else {
            strbuf_append_str(buf, "1");
        }
        return true;
    }
    // z-index
    if (strcmp(property, "z-index") == 0) {
        if (elem->position && elem->position->position != CSS_VALUE_STATIC) {
            snprintf(tmp, sizeof(tmp), "%d", elem->position->z_index);
            strbuf_append_str(buf, tmp);
        } else {
            strbuf_append_str(buf, "auto");
        }
        return true;
    }
    // overflow-x / overflow-y / overflow
    if (strcmp(property, "overflow") == 0 || strcmp(property, "overflow-x") == 0) {
        if (elem->scroller) {
            strbuf_append_str(buf, serialize_css_enum(elem->scroller->overflow_x));
        } else {
            strbuf_append_str(buf, "visible");
        }
        return true;
    }
    if (strcmp(property, "overflow-y") == 0) {
        if (elem->scroller) {
            strbuf_append_str(buf, serialize_css_enum(elem->scroller->overflow_y));
        } else {
            strbuf_append_str(buf, "visible");
        }
        return true;
    }
    // margin-top/right/bottom/left
    if (strcmp(property, "margin-top") == 0) {
        if (elem->bound) { snprintf(tmp, sizeof(tmp), "%.4gpx", elem->bound->margin.top); strbuf_append_str(buf, tmp); }
        else strbuf_append_str(buf, "0px");
        return true;
    }
    if (strcmp(property, "margin-right") == 0) {
        if (elem->bound) { snprintf(tmp, sizeof(tmp), "%.4gpx", elem->bound->margin.right); strbuf_append_str(buf, tmp); }
        else strbuf_append_str(buf, "0px");
        return true;
    }
    if (strcmp(property, "margin-bottom") == 0) {
        if (elem->bound) { snprintf(tmp, sizeof(tmp), "%.4gpx", elem->bound->margin.bottom); strbuf_append_str(buf, tmp); }
        else strbuf_append_str(buf, "0px");
        return true;
    }
    if (strcmp(property, "margin-left") == 0) {
        if (elem->bound) { snprintf(tmp, sizeof(tmp), "%.4gpx", elem->bound->margin.left); strbuf_append_str(buf, tmp); }
        else strbuf_append_str(buf, "0px");
        return true;
    }
    // padding-top/right/bottom/left
    if (strcmp(property, "padding-top") == 0) {
        if (elem->bound) { snprintf(tmp, sizeof(tmp), "%.4gpx", elem->bound->padding.top); strbuf_append_str(buf, tmp); }
        else strbuf_append_str(buf, "0px");
        return true;
    }
    if (strcmp(property, "padding-right") == 0) {
        if (elem->bound) { snprintf(tmp, sizeof(tmp), "%.4gpx", elem->bound->padding.right); strbuf_append_str(buf, tmp); }
        else strbuf_append_str(buf, "0px");
        return true;
    }
    if (strcmp(property, "padding-bottom") == 0) {
        if (elem->bound) { snprintf(tmp, sizeof(tmp), "%.4gpx", elem->bound->padding.bottom); strbuf_append_str(buf, tmp); }
        else strbuf_append_str(buf, "0px");
        return true;
    }
    if (strcmp(property, "padding-left") == 0) {
        if (elem->bound) { snprintf(tmp, sizeof(tmp), "%.4gpx", elem->bound->padding.left); strbuf_append_str(buf, tmp); }
        else strbuf_append_str(buf, "0px");
        return true;
    }
    // border-*-width
    if (strcmp(property, "border-top-width") == 0) {
        if (elem->bound && elem->bound->border) { snprintf(tmp, sizeof(tmp), "%.4gpx", elem->bound->border->width.top); strbuf_append_str(buf, tmp); }
        else strbuf_append_str(buf, "0px");
        return true;
    }
    if (strcmp(property, "border-right-width") == 0) {
        if (elem->bound && elem->bound->border) { snprintf(tmp, sizeof(tmp), "%.4gpx", elem->bound->border->width.right); strbuf_append_str(buf, tmp); }
        else strbuf_append_str(buf, "0px");
        return true;
    }
    if (strcmp(property, "border-bottom-width") == 0) {
        if (elem->bound && elem->bound->border) { snprintf(tmp, sizeof(tmp), "%.4gpx", elem->bound->border->width.bottom); strbuf_append_str(buf, tmp); }
        else strbuf_append_str(buf, "0px");
        return true;
    }
    if (strcmp(property, "border-left-width") == 0) {
        if (elem->bound && elem->bound->border) { snprintf(tmp, sizeof(tmp), "%.4gpx", elem->bound->border->width.left); strbuf_append_str(buf, tmp); }
        else strbuf_append_str(buf, "0px");
        return true;
    }
    // border-*-color
    if (strcmp(property, "border-top-color") == 0) {
        if (elem->bound && elem->bound->border) serialize_color(elem->bound->border->top_color, buf);
        else strbuf_append_str(buf, "rgb(0, 0, 0)");
        return true;
    }
    if (strcmp(property, "border-right-color") == 0) {
        if (elem->bound && elem->bound->border) serialize_color(elem->bound->border->right_color, buf);
        else strbuf_append_str(buf, "rgb(0, 0, 0)");
        return true;
    }
    if (strcmp(property, "border-bottom-color") == 0) {
        if (elem->bound && elem->bound->border) serialize_color(elem->bound->border->bottom_color, buf);
        else strbuf_append_str(buf, "rgb(0, 0, 0)");
        return true;
    }
    if (strcmp(property, "border-left-color") == 0) {
        if (elem->bound && elem->bound->border) serialize_color(elem->bound->border->left_color, buf);
        else strbuf_append_str(buf, "rgb(0, 0, 0)");
        return true;
    }
    // width / height (computed box size in px)
    if (strcmp(property, "width") == 0) {
        snprintf(tmp, sizeof(tmp), "%.4gpx", view->width);
        strbuf_append_str(buf, tmp);
        return true;
    }
    if (strcmp(property, "height") == 0) {
        snprintf(tmp, sizeof(tmp), "%.4gpx", view->height);
        strbuf_append_str(buf, tmp);
        return true;
    }
    // white-space
    if (strcmp(property, "white-space") == 0) {
        if (elem->blk) strbuf_append_str(buf, serialize_css_enum(elem->blk->white_space));
        else strbuf_append_str(buf, "normal");
        return true;
    }
    // letter-spacing
    if (strcmp(property, "letter-spacing") == 0) {
        if (elem->font && elem->font->letter_spacing != 0) {
            snprintf(tmp, sizeof(tmp), "%.4gpx", elem->font->letter_spacing);
            strbuf_append_str(buf, tmp);
        } else {
            strbuf_append_str(buf, "normal");
        }
        return true;
    }
    // word-spacing
    if (strcmp(property, "word-spacing") == 0) {
        if (elem->font && elem->font->word_spacing != 0) {
            snprintf(tmp, sizeof(tmp), "%.4gpx", elem->font->word_spacing);
            strbuf_append_str(buf, tmp);
        } else {
            strbuf_append_str(buf, "normal");
        }
        return true;
    }
    // vertical-align
    if (strcmp(property, "vertical-align") == 0) {
        if (elem->in_line) {
            strbuf_append_str(buf, serialize_css_enum(elem->in_line->vertical_align));
        } else {
            strbuf_append_str(buf, "baseline");
        }
        return true;
    }

    log_error("event_sim: get_computed_style - unsupported property '%s'", property);
    return false;
}

// Find element at absolute coordinates by hit-testing the view tree
static View* find_element_at(View* root, float abs_x, float abs_y, float parent_abs_x, float parent_abs_y) {
    if (!root) return nullptr;
    float view_abs_x = parent_abs_x + root->x;
    float view_abs_y = parent_abs_y + root->y;

    // Check if point is within this view's bounds
    if (abs_x < view_abs_x || abs_x > view_abs_x + root->width ||
        abs_y < view_abs_y || abs_y > view_abs_y + root->height) {
        return nullptr;
    }

    // Search children in reverse order (last child is visually on top)
    if (root->is_element()) {
        View* child = (View*)((DomElement*)root)->last_child;
        while (child) {
            View* found = find_element_at(child, abs_x, abs_y, view_abs_x, view_abs_y);
            if (found && found->is_element()) return found;
            child = (View*)child->prev_sibling;
        }
    }

    return root->is_element() ? root : nullptr;
}

// Check if an element is a checkbox/radio input
static bool sim_is_checkbox_or_radio(View* view) {
    if (!view || !view->is_element()) return false;
    ViewElement* elem = (ViewElement*)view;
    if (elem->tag() != HTM_TAG_INPUT) return false;
    const char* type = elem->get_attribute("type");
    return type && (strcmp(type, "checkbox") == 0 || strcmp(type, "radio") == 0);
}

// Direct toggle of checkbox/radio state (bypasses coordinate hit-testing)
// Used when event simulator clicks a selector-resolved form control.
static void sim_toggle_checkbox_radio(View* input, RadiantState* state) {
    if (!input || !input->is_element()) return;
    ViewElement* elem = (ViewElement*)input;
    const char* type = elem->get_attribute("type");
    if (!type) return;

    if (strcmp(type, "checkbox") == 0) {
        bool is_checked = dom_element_has_pseudo_state(elem, PSEUDO_STATE_CHECKED);
        bool new_state = !is_checked;
        if (new_state) {
            elem->pseudo_state |= PSEUDO_STATE_CHECKED;
        } else {
            elem->pseudo_state &= ~PSEUDO_STATE_CHECKED;
        }
        state->needs_repaint = true;
        log_info("event_sim: toggled checkbox to %s", new_state ? "checked" : "unchecked");
    }
    else if (strcmp(type, "radio") == 0) {
        bool is_checked = dom_element_has_pseudo_state(elem, PSEUDO_STATE_CHECKED);
        if (!is_checked) {
            // Uncheck other radios in the same group
            const char* name = elem->get_attribute("name");
            if (name) {
                View* root = input;
                while (root->parent) root = root->parent;
                // Traverse all views to find matching radios
                View* search = root;
                while (search) {
                    if (search != input && search->is_element()) {
                        ViewElement* se = (ViewElement*)search;
                        if (se->tag() == HTM_TAG_INPUT) {
                            const char* st = se->get_attribute("type");
                            const char* sn = se->get_attribute("name");
                            if (st && strcmp(st, "radio") == 0 && sn && strcmp(sn, name) == 0) {
                                if (dom_element_has_pseudo_state(se, PSEUDO_STATE_CHECKED)) {
                                    se->pseudo_state &= ~PSEUDO_STATE_CHECKED;
                                }
                            }
                        }
                    }
                    // Depth-first traversal
                    if (search->is_element()) {
                        DomElement* de = search->as_element();
                        if (de->first_child) { search = (View*)de->first_child; continue; }
                    }
                    if (search->next()) { search = search->next(); continue; }
                    search = search->parent;
                    while (search && !search->next()) search = search->parent;
                    if (search) search = search->next();
                }
            }
            // Check this radio
            elem->pseudo_state |= PSEUDO_STATE_CHECKED;
            state->needs_repaint = true;
            log_info("event_sim: checked radio button");
        }
    }
}

// Extract visible text from a view tree recursively
static void sim_extract_text(View* view, StrBuf* buf) {
    if (!view) return;
    if (view->view_type == RDT_VIEW_TEXT) {
        DomText* text = view->as_text();
        if (text && text->text) strbuf_append_str(buf, text->text);
        return;
    }
    DomElement* elem = view->as_element();
    if (elem) {
        DomNode* child = elem->first_child;
        while (child) {
            sim_extract_text((View*)child, buf);
            child = child->next_sibling;
        }
    }
}

// Resolve a target to (x,y) coordinates. Tries target_selector first, then target_text, then raw x/y.
// Returns true if resolved, sets out_x, out_y.
static bool resolve_target(SimEvent* ev, DomDocument* doc, int* out_x, int* out_y) {
    // Priority: selector > text > raw coordinates
    if (ev->target_selector && doc) {
        View* elem = find_element_by_selector(doc, ev->target_selector, ev->target_index);
        if (elem) {
            float fx, fy;
            get_element_center_abs(elem, &fx, &fy);
            *out_x = (int)fx;
            *out_y = (int)fy;
            log_info("event_sim: resolved selector '%s'[%d] to (%d, %d)", ev->target_selector, ev->target_index, *out_x, *out_y);
            return true;
        }
        log_error("event_sim: selector '%s'[%d] not found", ev->target_selector, ev->target_index);
        return false;
    }
    if (ev->target_text && doc) {
        float fx, fy;
        if (find_text_position(doc, ev->target_text, &fx, &fy)) {
            *out_x = (int)fx;
            *out_y = (int)fy;
            log_info("event_sim: resolved text '%s' to (%d, %d)", ev->target_text, *out_x, *out_y);
            return true;
        }
        log_error("event_sim: target_text '%s' not found", ev->target_text);
        return false;
    }
    *out_x = ev->x;
    *out_y = ev->y;
    return true;
}

// Resolve a target to a View* element (for assertions and actions that need the element)
static View* resolve_target_element(SimEvent* ev, DomDocument* doc) {
    if (ev->target_selector && doc) {
        return find_element_by_selector(doc, ev->target_selector, ev->target_index);
    }
    return NULL;
}

// Parse target object: {"selector": "...", "text": "..."}
// Also reads legacy top-level target_text for backward compat
static void parse_target(MapReader& reader, SimEvent* ev) {
    // New unified target object
    ItemReader target_item = reader.get("target");
    if (target_item.isMap()) {
        MapReader target_map = target_item.asMap();
        const char* sel = target_map.get("selector").cstring();
        if (sel) ev->target_selector = mem_strdup(sel, MEM_CAT_LAYOUT);
        const char* txt = target_map.get("text").cstring();
        if (txt) ev->target_text = mem_strdup(txt, MEM_CAT_LAYOUT);
        // target index: which nth matching element (0-based)
        if (target_map.has("index")) ev->target_index = target_map.get("index").asInt32();
        // target can also carry x,y
        if (target_map.has("x")) ev->x = target_map.get("x").asInt32();
        if (target_map.has("y")) ev->y = target_map.get("y").asInt32();
    }
    // Legacy target_text at top level
    if (!ev->target_text) {
        const char* target = reader.get("target_text").cstring();
        if (target) ev->target_text = mem_strdup(target, MEM_CAT_LAYOUT);
    }
}

// Parse a single event from MapReader
static SimEvent* parse_sim_event(MapReader& reader) {
    SimEvent* ev = (SimEvent*)mem_calloc(1, sizeof(SimEvent), MEM_CAT_LAYOUT);
    if (!ev) return NULL;

    ItemReader type_item = reader.get("type");
    const char* type_str = type_item.cstring();
    if (!type_str) {
        mem_free(ev);
        return NULL;
    }

    // Parse event type
    if (strcmp(type_str, "wait") == 0) {
        ev->type = SIM_EVENT_WAIT;
        ev->wait_ms = reader.get("ms").asInt32();
        if (ev->wait_ms <= 0) ev->wait_ms = 100;
    }
    else if (strcmp(type_str, "mouse_move") == 0) {
        ev->type = SIM_EVENT_MOUSE_MOVE;
        ev->x = reader.get("x").asInt32();
        ev->y = reader.get("y").asInt32();
        parse_target(reader, ev);
    }
    else if (strcmp(type_str, "mouse_down") == 0) {
        ev->type = SIM_EVENT_MOUSE_DOWN;
        ev->x = reader.get("x").asInt32();
        ev->y = reader.get("y").asInt32();
        ev->button = reader.get("button").asInt32();
        ev->mods = reader.get("mods").asInt32();
        const char* mods_str = reader.get("mods_str").cstring();
        if (mods_str) ev->mods = parse_mods_string(mods_str);
        parse_target(reader, ev);
    }
    else if (strcmp(type_str, "mouse_up") == 0) {
        ev->type = SIM_EVENT_MOUSE_UP;
        ev->x = reader.get("x").asInt32();
        ev->y = reader.get("y").asInt32();
        ev->button = reader.get("button").asInt32();
        ev->mods = reader.get("mods").asInt32();
    }
    else if (strcmp(type_str, "mouse_drag") == 0) {
        ev->type = SIM_EVENT_MOUSE_DRAG;
        ev->x = reader.get("from_x").asInt32();
        ev->y = reader.get("from_y").asInt32();
        ev->to_x = reader.get("to_x").asInt32();
        ev->to_y = reader.get("to_y").asInt32();
        ev->button = reader.get("button").asInt32();
        parse_target(reader, ev);
        // Parse to_target for destination selector/text
        ItemReader to_target_item = reader.get("to_target");
        if (to_target_item.isMap()) {
            MapReader to_map = to_target_item.asMap();
            const char* sel = to_map.get("selector").cstring();
            if (sel) ev->to_target_selector = mem_strdup(sel, MEM_CAT_LAYOUT);
            const char* txt = to_map.get("text").cstring();
            if (txt) ev->to_target_text = mem_strdup(txt, MEM_CAT_LAYOUT);
        }
    }
    else if (strcmp(type_str, "drag_and_drop") == 0) {
        ev->type = SIM_EVENT_DRAG_AND_DROP;
        parse_target(reader, ev);
        // Parse to_target for drop destination
        ItemReader to_target_item = reader.get("to_target");
        if (to_target_item.isMap()) {
            MapReader to_map = to_target_item.asMap();
            const char* sel = to_map.get("selector").cstring();
            if (sel) ev->to_target_selector = mem_strdup(sel, MEM_CAT_LAYOUT);
            const char* txt = to_map.get("text").cstring();
            if (txt) ev->to_target_text = mem_strdup(txt, MEM_CAT_LAYOUT);
        }
        int steps = reader.get("steps").asInt32();
        ev->drag_steps = steps > 0 ? steps : 5;
        if (!ev->target_selector && !ev->target_text) {
            log_error("event_sim: drag_and_drop missing 'target' (drag source)");
            mem_free(ev);
            return NULL;
        }
        if (!ev->to_target_selector && !ev->to_target_text) {
            log_error("event_sim: drag_and_drop missing 'to_target' (drop target)");
            mem_free(ev);
            return NULL;
        }
    }
    else if (strcmp(type_str, "key_press") == 0) {
        ev->type = SIM_EVENT_KEY_PRESS;
        const char* key_str = reader.get("key").cstring();
        ev->key = key_name_to_glfw(key_str);
        ev->mods = reader.get("mods").asInt32();
        const char* mods_str = reader.get("mods_str").cstring();
        if (mods_str) ev->mods = parse_mods_string(mods_str);
    }
    else if (strcmp(type_str, "key_down") == 0) {
        ev->type = SIM_EVENT_KEY_DOWN;
        const char* key_str = reader.get("key").cstring();
        ev->key = key_name_to_glfw(key_str);
    }
    else if (strcmp(type_str, "key_up") == 0) {
        ev->type = SIM_EVENT_KEY_UP;
        const char* key_str = reader.get("key").cstring();
        ev->key = key_name_to_glfw(key_str);
    }
    else if (strcmp(type_str, "key_combo") == 0) {
        ev->type = SIM_EVENT_KEY_COMBO;
        const char* key_str = reader.get("key").cstring();
        ev->key = key_name_to_glfw(key_str);
        const char* mods_str = reader.get("mods_str").cstring();
        if (mods_str) ev->mods = parse_mods_string(mods_str);
    }
    else if (strcmp(type_str, "scroll") == 0) {
        ev->type = SIM_EVENT_SCROLL;
        ev->x = reader.get("x").asInt32();
        ev->y = reader.get("y").asInt32();
        {
            ItemReader dx = reader.get("dx"); ev->scroll_dx = (float)(dx.isFloat() ? dx.asFloat() : dx.asInt());
            ItemReader dy = reader.get("dy"); ev->scroll_dy = (float)(dy.isFloat() ? dy.asFloat() : dy.asInt());
        }
    }
    else if (strcmp(type_str, "assert_caret") == 0) {
        ev->type = SIM_EVENT_ASSERT_CARET;
        ev->expected_view_type = reader.get("view_type").asInt32();
        ev->expected_char_offset = reader.get("char_offset").asInt32();
        if (ev->expected_view_type == 0) ev->expected_view_type = -1;  // treat 0 as "don't check"
        if (ev->expected_char_offset == 0 && !reader.has("char_offset")) ev->expected_char_offset = -1;
        int not_type = reader.get("view_type_not").asInt32();
        if (not_type > 0) {
            ev->expected_view_type = not_type;
            ev->negate_view_type = true;
        }
    }
    else if (strcmp(type_str, "assert_selection") == 0) {
        ev->type = SIM_EVENT_ASSERT_SELECTION;
        ev->expected_is_collapsed = reader.get("is_collapsed").asBool();
        ev->check_dom_selection = reader.get("check_dom").asBool();
    }
    else if (strcmp(type_str, "assert_target") == 0) {
        ev->type = SIM_EVENT_ASSERT_TARGET;
        ev->expected_view_type = reader.get("view_type").asInt32();
        if (ev->expected_view_type == 0) ev->expected_view_type = -1;
    }
    else if (strcmp(type_str, "click") == 0) {
        ev->type = SIM_EVENT_CLICK;
        ev->x = reader.get("x").asInt32();
        ev->y = reader.get("y").asInt32();
        ev->button = reader.get("button").asInt32();
        ev->mods = reader.get("mods").asInt32();
        const char* mods_str = reader.get("mods_str").cstring();
        if (mods_str) ev->mods = parse_mods_string(mods_str);
        parse_target(reader, ev);
    }
    else if (strcmp(type_str, "dblclick") == 0) {
        ev->type = SIM_EVENT_DBLCLICK;
        ev->x = reader.get("x").asInt32();
        ev->y = reader.get("y").asInt32();
        ev->button = reader.get("button").asInt32();
        parse_target(reader, ev);
    }
    else if (strcmp(type_str, "type") == 0) {
        ev->type = SIM_EVENT_TYPE;
        const char* text = reader.get("text").cstring();
        if (text) ev->input_text = mem_strdup(text, MEM_CAT_LAYOUT);
        ev->clear_first = reader.get("clear").asBool();
        parse_target(reader, ev);
    }
    else if (strcmp(type_str, "focus") == 0) {
        ev->type = SIM_EVENT_FOCUS;
        parse_target(reader, ev);
    }
    else if (strcmp(type_str, "check") == 0) {
        ev->type = SIM_EVENT_CHECK;
        ev->expected_checked = reader.get("checked").asBool();
        parse_target(reader, ev);
    }
    else if (strcmp(type_str, "select_option") == 0) {
        ev->type = SIM_EVENT_SELECT_OPTION;
        const char* val = reader.get("value").cstring();
        if (val) ev->option_value = mem_strdup(val, MEM_CAT_LAYOUT);
        const char* label = reader.get("label").cstring();
        if (label) ev->option_label = mem_strdup(label, MEM_CAT_LAYOUT);
        parse_target(reader, ev);
    }
    else if (strcmp(type_str, "resize") == 0) {
        ev->type = SIM_EVENT_RESIZE;
        ev->x = reader.get("width").asInt32();
        ev->y = reader.get("height").asInt32();
        if (ev->x <= 0 || ev->y <= 0) {
            log_error("event_sim: resize requires positive width and height");
            mem_free(ev);
            return NULL;
        }
    }
    else if (strcmp(type_str, "assert_text") == 0) {
        ev->type = SIM_EVENT_ASSERT_TEXT;
        const char* contains = reader.get("contains").cstring();
        if (contains) ev->assert_contains = mem_strdup(contains, MEM_CAT_LAYOUT);
        const char* equals = reader.get("equals").cstring();
        if (equals) ev->assert_equals = mem_strdup(equals, MEM_CAT_LAYOUT);
        parse_target(reader, ev);
    }
    else if (strcmp(type_str, "assert_value") == 0) {
        ev->type = SIM_EVENT_ASSERT_VALUE;
        const char* equals = reader.get("equals").cstring();
        if (equals) ev->assert_equals = mem_strdup(equals, MEM_CAT_LAYOUT);
        const char* contains = reader.get("contains").cstring();
        if (contains) ev->assert_contains = mem_strdup(contains, MEM_CAT_LAYOUT);
        parse_target(reader, ev);
    }
    else if (strcmp(type_str, "assert_checked") == 0) {
        ev->type = SIM_EVENT_ASSERT_CHECKED;
        ev->expected_checked = reader.get("checked").asBool();
        parse_target(reader, ev);
    }
    else if (strcmp(type_str, "assert_visible") == 0) {
        ev->type = SIM_EVENT_ASSERT_VISIBLE;
        ev->expected_visible = reader.get("visible").asBool();
        parse_target(reader, ev);
    }
    else if (strcmp(type_str, "assert_focus") == 0) {
        ev->type = SIM_EVENT_ASSERT_FOCUS;
        parse_target(reader, ev);
    }
    else if (strcmp(type_str, "assert_state") == 0) {
        ev->type = SIM_EVENT_ASSERT_STATE;
        const char* state = reader.get("state").cstring();
        if (state) ev->state_name = mem_strdup(state, MEM_CAT_LAYOUT);
        ev->expected_state_value = reader.get("value").asBool();
        parse_target(reader, ev);
    }
    else if (strcmp(type_str, "assert_scroll") == 0) {
        ev->type = SIM_EVENT_ASSERT_SCROLL;
        {
            ItemReader sx = reader.get("x"); ev->expected_scroll_x = (float)(sx.isFloat() ? sx.asFloat() : sx.asInt());
            ItemReader sy = reader.get("y"); ev->expected_scroll_y = (float)(sy.isFloat() ? sy.asFloat() : sy.asInt());
            ItemReader st = reader.get("tolerance"); ev->scroll_tolerance = (float)(st.isFloat() ? st.asFloat() : st.asInt());
        }
        if (ev->scroll_tolerance <= 0) ev->scroll_tolerance = 1.0f;
        ev->negate_scroll = reader.get("negate").asBool();
    }
    else if (strcmp(type_str, "assert_rect") == 0) {
        ev->type = SIM_EVENT_ASSERT_RECT;
        parse_target(reader, ev);
        if (reader.has("x")) { ev->expected_rect_x = (float)reader.get("x").asFloat(); ev->has_rect_x = true; }
        if (reader.has("y")) { ev->expected_rect_y = (float)reader.get("y").asFloat(); ev->has_rect_y = true; }
        if (reader.has("width")) { ev->expected_rect_w = (float)reader.get("width").asFloat(); ev->has_rect_w = true; }
        if (reader.has("height")) { ev->expected_rect_h = (float)reader.get("height").asFloat(); ev->has_rect_h = true; }
        {
            ItemReader tol = reader.get("tolerance");
            ev->rect_tolerance = tol.isFloat() ? (float)tol.asFloat() : (float)tol.asInt();
        }
        if (ev->rect_tolerance <= 0) ev->rect_tolerance = 1.0f;
    }
    else if (strcmp(type_str, "assert_style") == 0) {
        ev->type = SIM_EVENT_ASSERT_STYLE;
        parse_target(reader, ev);
        const char* prop = reader.get("property").cstring();
        if (prop) ev->style_property = mem_strdup(prop, MEM_CAT_LAYOUT);
        const char* equals = reader.get("equals").cstring();
        if (equals) ev->assert_equals = mem_strdup(equals, MEM_CAT_LAYOUT);
        const char* contains = reader.get("contains").cstring();
        if (contains) ev->assert_contains = mem_strdup(contains, MEM_CAT_LAYOUT);
        ev->style_animated = reader.get("animated").asBool();
        if (ev->style_animated) {
            ItemReader tol = reader.get("tolerance");
            ev->style_tolerance = tol.isFloat() ? (float)tol.asFloat() : (float)tol.asInt();
        }
    }
    else if (strcmp(type_str, "assert_position") == 0) {
        ev->type = SIM_EVENT_ASSERT_POSITION;
        // Parse element_a
        ItemReader a_item = reader.get("element_a");
        if (a_item.isMap()) {
            MapReader a_map = a_item.asMap();
            const char* sel = a_map.get("selector").cstring();
            if (sel) ev->element_a_selector = mem_strdup(sel, MEM_CAT_LAYOUT);
            const char* txt = a_map.get("text").cstring();
            if (txt) ev->element_a_text = mem_strdup(txt, MEM_CAT_LAYOUT);
        }
        // Parse element_b
        ItemReader b_item = reader.get("element_b");
        if (b_item.isMap()) {
            MapReader b_map = b_item.asMap();
            const char* sel = b_map.get("selector").cstring();
            if (sel) ev->element_b_selector = mem_strdup(sel, MEM_CAT_LAYOUT);
            const char* txt = b_map.get("text").cstring();
            if (txt) ev->element_b_text = mem_strdup(txt, MEM_CAT_LAYOUT);
        }
        const char* rel = reader.get("relation").cstring();
        if (rel) ev->position_relation = mem_strdup(rel, MEM_CAT_LAYOUT);
        {
            ItemReader gap = reader.get("gap");
            ev->position_gap = gap.isFloat() ? (float)gap.asFloat() : (float)gap.asInt();
            ItemReader tol = reader.get("tolerance");
            ev->position_tolerance = tol.isFloat() ? (float)tol.asFloat() : (float)tol.asInt();
        }
        if (ev->position_tolerance <= 0) ev->position_tolerance = 1.0f;
    }
    else if (strcmp(type_str, "assert_element_at") == 0) {
        ev->type = SIM_EVENT_ASSERT_ELEMENT_AT;
        ev->at_x = reader.get("x").asInt32();
        ev->at_y = reader.get("y").asInt32();
        const char* sel = reader.get("expected_selector").cstring();
        if (sel) ev->expected_at_selector = mem_strdup(sel, MEM_CAT_LAYOUT);
        const char* tag = reader.get("expected_tag").cstring();
        if (tag) ev->expected_at_tag = mem_strdup(tag, MEM_CAT_LAYOUT);
    }
    else if (strcmp(type_str, "assert_attribute") == 0) {
        ev->type = SIM_EVENT_ASSERT_ATTRIBUTE;
        parse_target(reader, ev);
        const char* attr = reader.get("attribute").cstring();
        if (attr) ev->attribute_name = mem_strdup(attr, MEM_CAT_LAYOUT);
        const char* eq = reader.get("equals").cstring();
        if (eq) ev->assert_equals = mem_strdup(eq, MEM_CAT_LAYOUT);
        const char* cont = reader.get("contains").cstring();
        if (cont) ev->assert_contains = mem_strdup(cont, MEM_CAT_LAYOUT);
        if (!ev->target_selector && !ev->target_text) {
            log_error("event_sim: assert_attribute missing 'target'");
            mem_free(ev);
            return NULL;
        }
        if (!attr) {
            log_error("event_sim: assert_attribute missing 'attribute' field");
            mem_free(ev);
            return NULL;
        }
    }
    else if (strcmp(type_str, "assert_count") == 0) {
        ev->type = SIM_EVENT_ASSERT_COUNT;
        parse_target(reader, ev);
        ev->assert_count_expected = -1;
        ev->assert_count_min = -1;
        ev->assert_count_max = -1;
        if (reader.has("count")) ev->assert_count_expected = reader.get("count").asInt32();
        if (reader.has("equals")) ev->assert_count_expected = reader.get("equals").asInt32();
        if (reader.has("min")) ev->assert_count_min = reader.get("min").asInt32();
        if (reader.has("max")) ev->assert_count_max = reader.get("max").asInt32();
        if (!ev->target_selector) {
            log_error("event_sim: assert_count requires 'target' CSS selector");
            mem_free(ev);
            return NULL;
        }
    }
    else if (strcmp(type_str, "navigate") == 0) {
        ev->type = SIM_EVENT_NAVIGATE;
        const char* url = reader.get("url").cstring();
        if (url) ev->navigate_url = mem_strdup(url, MEM_CAT_LAYOUT);
        else {
            log_error("event_sim: navigate event missing 'url' field");
            mem_free(ev);
            return NULL;
        }
    }
    else if (strcmp(type_str, "navigate_back") == 0) {
        ev->type = SIM_EVENT_NAVIGATE_BACK;
    }
    else if (strcmp(type_str, "switch_frame") == 0) {
        ev->type = SIM_EVENT_SWITCH_FRAME;
        const char* sel = reader.get("selector").cstring();
        if (sel) ev->frame_selector = mem_strdup(sel, MEM_CAT_LAYOUT);
        // NULL selector means switch back to main frame
    }
    else if (strcmp(type_str, "log") == 0) {
        ev->type = SIM_EVENT_LOG;
        const char* msg = reader.get("message").cstring();
        if (msg) ev->message = mem_strdup(msg, MEM_CAT_LAYOUT);
    }
    else if (strcmp(type_str, "render") == 0) {
        ev->type = SIM_EVENT_RENDER;
        const char* file = reader.get("file").cstring();
        if (file) ev->file_path = mem_strdup(file, MEM_CAT_LAYOUT);
        else {
            log_error("event_sim: render event missing 'file' field");
            mem_free(ev);
            return NULL;
        }
    }
    else if (strcmp(type_str, "dump_caret") == 0) {
        ev->type = SIM_EVENT_DUMP_CARET;
        const char* file = reader.get("file").cstring();
        if (file) ev->file_path = mem_strdup(file, MEM_CAT_LAYOUT);
        // file is optional, defaults to ./view_tree.txt
    }
    else if (strcmp(type_str, "assert_snapshot") == 0) {
        ev->type = SIM_EVENT_ASSERT_SNAPSHOT;
        const char* ref = reader.get("reference").cstring();
        if (ref) ev->snapshot_reference = mem_strdup(ref, MEM_CAT_LAYOUT);
        else {
            log_error("event_sim: assert_snapshot requires 'reference' field");
            mem_free(ev);
            return NULL;
        }
        {
            ItemReader th = reader.get("threshold");
            ev->snapshot_threshold = th.isFloat() ? (float)th.asFloat() : (float)th.asInt();
        }
        if (ev->snapshot_threshold <= 0) ev->snapshot_threshold = 1.0f;
        const char* diff = reader.get("save_diff").cstring();
        if (diff) ev->snapshot_diff_path = mem_strdup(diff, MEM_CAT_LAYOUT);
        const char* actual = reader.get("save_actual").cstring();
        if (actual) ev->snapshot_actual_path = mem_strdup(actual, MEM_CAT_LAYOUT);
    }
    else if (strcmp(type_str, "scroll_to") == 0) {
        ev->type = SIM_EVENT_SCROLL_TO;
        parse_target(reader, ev);
        {
            ItemReader sy = reader.get("y"); ev->expected_scroll_y = (float)(sy.isFloat() ? sy.asFloat() : sy.asInt());
            ItemReader sx = reader.get("x"); ev->expected_scroll_x = (float)(sx.isFloat() ? sx.asFloat() : sx.asInt());
        }
    }
    else if (strcmp(type_str, "advance_time") == 0) {
        ev->type = SIM_EVENT_ADVANCE_TIME;
        ev->wait_ms = reader.get("ms").asInt32();
        ev->advance_steps = reader.get("steps").asInt32();
        if (ev->wait_ms <= 0) {
            log_error("event_sim: advance_time requires positive 'ms' field");
            mem_free(ev);
            return NULL;
        }
    }
    else if (strcmp(type_str, "webview_eval_js") == 0) {
        ev->type = SIM_EVENT_WEBVIEW_EVAL_JS;
        parse_target(reader, ev);
        const char* js = reader.get("js").cstring();
        if (js) ev->js_code = mem_strdup(js, MEM_CAT_LAYOUT);
        else {
            log_error("event_sim: webview_eval_js requires 'js' field");
            mem_free(ev);
            return NULL;
        }
    }
    else if (strcmp(type_str, "webview_wait_load") == 0) {
        ev->type = SIM_EVENT_WEBVIEW_WAIT_LOAD;
        parse_target(reader, ev);
        ev->wait_ms = reader.get("timeout_ms").asInt32();
        if (ev->wait_ms <= 0) ev->wait_ms = 5000;  // default 5s timeout
    }
    else {
        log_error("event_sim: unknown event type '%s'", type_str);
        mem_free(ev);
        return NULL;
    }

    // Parse optional auto-waiting fields for assertion events
    if ((ev->type >= SIM_EVENT_ASSERT_CARET && ev->type <= SIM_EVENT_ASSERT_SNAPSHOT)) {
        ev->assert_timeout = reader.get("timeout").asInt32();
        ev->assert_interval = reader.get("interval").asInt32();
        if (ev->assert_interval <= 0) ev->assert_interval = 100; // default 100ms
    }

    return ev;
}

EventSimContext* event_sim_load(const char* json_file) {
    log_info("event_sim: loading event file '%s'", json_file);

    // Read JSON file content
    char* json_content = read_text_file(json_file);
    if (!json_content) {
        log_error("event_sim: failed to read JSON file '%s'", json_file);
        return NULL;
    }

    // Create input and parse JSON
    Url* url = url_parse(json_file);
    Input* input = InputManager::create_input(url);
    if (!input) {
        log_error("event_sim: failed to create input for JSON parsing");
        mem_free(json_content);
        return NULL;
    }

    parse_json(input, json_content);
    mem_free(json_content);

    // Check if parsing succeeded
    if (input->root.item == 0) {
        log_error("event_sim: failed to parse JSON file '%s'", json_file);
        return NULL;
    }

    // Use MarkReader to access the data
    MarkReader doc(input->root);
    ItemReader root = doc.getRoot();

    if (!root.isMap()) {
        log_error("event_sim: JSON root is not an object");
        return NULL;
    }

    MapReader root_map = root.asMap();
    ItemReader events_item = root_map.get("events");

    if (!events_item.isArray() && !events_item.isList()) {
        log_error("event_sim: JSON file missing 'events' array");
        return NULL;
    }

    ArrayReader events_arr = events_item.asArray();

    // Create context
    EventSimContext* ctx = (EventSimContext*)mem_calloc(1, sizeof(EventSimContext), MEM_CAT_LAYOUT);
    ctx->events = arraylist_new(16);
    ctx->current_index = 0;
    ctx->next_event_time = 0;
    ctx->is_running = true;
    ctx->auto_close = true;
    ctx->pass_count = 0;
    ctx->fail_count = 0;
    ctx->test_name = NULL;

    // Parse optional test name
    const char* name = root_map.get("name").cstring();
    if (name) ctx->test_name = mem_strdup(name, MEM_CAT_LAYOUT);

    // Parse optional viewport
    ItemReader viewport_item = root_map.get("viewport");
    if (viewport_item.isMap()) {
        MapReader vp = viewport_item.asMap();
        ctx->viewport_width = vp.get("width").asInt32();
        ctx->viewport_height = vp.get("height").asInt32();
        log_info("event_sim: viewport %dx%d", ctx->viewport_width, ctx->viewport_height);
    }

    // Parse optional default_timeout (ms) for auto-waiting assertions
    ctx->default_timeout = root_map.get("default_timeout").asInt32();
    if (ctx->default_timeout > 0) {
        log_info("event_sim: default_timeout %dms", ctx->default_timeout);
    }

    // Parse each event
    int count = (int)events_arr.length();
    log_info("event_sim: parsing %d events", count);

    for (int i = 0; i < count; i++) {
        ItemReader event_item = events_arr.get(i);
        if (!event_item.isMap()) {
            log_error("event_sim: event %d is not an object", i);
            continue;
        }
        MapReader event_map = event_item.asMap();
        SimEvent* ev = parse_sim_event(event_map);
        if (ev) {
            arraylist_append(ctx->events, ev);
        }
    }

    log_info("event_sim: loaded %d events successfully", ctx->events->length);

    return ctx;
}

void event_sim_free(EventSimContext* ctx) {
    if (!ctx) return;

    if (ctx->events) {
        for (int i = 0; i < ctx->events->length; i++) {
            SimEvent* ev = (SimEvent*)ctx->events->data[i];
            if (ev->message) mem_free(ev->message);
            if (ev->file_path) mem_free(ev->file_path);
            if (ev->target_text) mem_free(ev->target_text);
            if (ev->target_selector) mem_free(ev->target_selector);
            if (ev->to_target_selector) mem_free(ev->to_target_selector);
            if (ev->to_target_text) mem_free(ev->to_target_text);
            if (ev->input_text) mem_free(ev->input_text);
            if (ev->assert_contains) mem_free(ev->assert_contains);
            if (ev->assert_equals) mem_free(ev->assert_equals);
            if (ev->option_value) mem_free(ev->option_value);
            if (ev->option_label) mem_free(ev->option_label);
            if (ev->js_code) mem_free(ev->js_code);
            mem_free(ev);
        }
        arraylist_free(ctx->events);
    }

    if (ctx->test_name) mem_free(ctx->test_name);
    if (ctx->result_file) fclose(ctx->result_file);
    mem_free(ctx);
}

// Simulate a mouse move event
static void sim_mouse_move(UiContext* uicon, int x, int y) {
    RdtEvent event;
    event.mouse_position.type = RDT_EVENT_MOUSE_MOVE;
    event.mouse_position.timestamp = get_monotonic_time();
    event.mouse_position.x = x;
    event.mouse_position.y = y;
    handle_event(uicon, uicon->document, &event);
}

// Simulate a mouse button event
static void sim_mouse_button(UiContext* uicon, int x, int y, int button, int mods, bool is_down) {
    // First move to the position
    sim_mouse_move(uicon, x, y);

    // Then press/release
    RdtEvent event;
    event.mouse_button.type = is_down ? RDT_EVENT_MOUSE_DOWN : RDT_EVENT_MOUSE_UP;
    event.mouse_button.timestamp = get_monotonic_time();
    event.mouse_button.x = x;
    event.mouse_button.y = y;
    event.mouse_button.button = button;
    event.mouse_button.clicks = 1;
    event.mouse_button.mods = mods;
    handle_event(uicon, uicon->document, &event);
}

// Simulate a key event
static void sim_key(UiContext* uicon, int key, int mods, bool is_down) {
    RdtEvent event;
    event.key.type = is_down ? RDT_EVENT_KEY_DOWN : RDT_EVENT_KEY_UP;
    event.key.timestamp = get_monotonic_time();
    event.key.key = key;
    event.key.scancode = 0;
    event.key.mods = mods;
    handle_event(uicon, uicon->document, &event);
}

// Simulate a scroll event
static void sim_scroll(UiContext* uicon, int x, int y, float dx, float dy) {
    RdtEvent event;
    event.scroll.type = RDT_EVENT_SCROLL;
    event.scroll.timestamp = get_monotonic_time();
    event.scroll.x = x;
    event.scroll.y = y;
    event.scroll.xoffset = dx;
    event.scroll.yoffset = dy;
    handle_event(uicon, uicon->document, &event);
}

// Simulate a text input event (single Unicode codepoint)
static void sim_text_input(UiContext* uicon, uint32_t codepoint) {
    RdtEvent event;
    event.text_input.type = RDT_EVENT_TEXT_INPUT;
    event.text_input.timestamp = get_monotonic_time();
    event.text_input.codepoint = codepoint;
    handle_event(uicon, uicon->document, &event);
}

// Assert helper for caret state
static bool assert_caret(EventSimContext* ctx, UiContext* uicon, SimEvent* ev) {
    DomDocument* doc = uicon->document;
    if (!doc || !doc->state) {
        log_error("event_sim: assert_caret - no document or state");
        ctx->fail_count++;
        return false;
    }

    CaretState* caret = doc->state->caret;
    bool passed = true;

    if (ev->expected_view_type >= 0) {
        if (ev->negate_view_type) {
            // pass if there is no caret, or caret view type is not the rejected type
            bool has_forbidden = caret && caret->view && caret->view->view_type == ev->expected_view_type;
            if (has_forbidden) {
                log_error("event_sim: assert_caret - view_type should NOT be %d, but it is",
                         ev->expected_view_type);
                passed = false;
            }
        } else {
            // Check view type of caret view
            if (!caret || !caret->view) {
                log_error("event_sim: assert_caret - no caret view");
                passed = false;
            } else {
                ViewType actual_type = caret->view->view_type;
                if (actual_type != ev->expected_view_type) {
                    log_error("event_sim: assert_caret - view_type mismatch: expected %d, got %d",
                             ev->expected_view_type, actual_type);
                    passed = false;
                }
            }
        }
    }

    if (ev->expected_char_offset >= 0) {
        if (!caret || caret->char_offset != ev->expected_char_offset) {
            log_error("event_sim: assert_caret - char_offset mismatch: expected %d, got %d",
                     ev->expected_char_offset, caret ? caret->char_offset : -1);
            passed = false;
        }
    }

    if (passed) {
        log_info("event_sim: assert_caret PASS");
        ctx->pass_count++;
    } else {
        ctx->fail_count++;
    }

    return passed;
}

// Assert helper for selection state.
// Checks both legacy SelectionState (used by event/caret code) and the
// canonical DomSelection (used by the renderer to paint highlight rects).
// They MUST agree — disagreement means the selection is set internally but
// nothing visible is drawn (or vice-versa).
static bool assert_selection(EventSimContext* ctx, UiContext* uicon, SimEvent* ev) {
    DomDocument* doc = uicon->document;
    if (!doc || !doc->state) {
        log_error("event_sim: assert_selection - no document or state");
        ctx->fail_count++;
        return false;
    }

    SelectionState* sel = doc->state->selection;
    bool legacy_collapsed = sel ? sel->is_collapsed : true;

    DomSelection* ds = doc->state->dom_selection;
    bool dom_collapsed = (!ds || ds->range_count == 0) ? true : ds->is_collapsed;

    if (legacy_collapsed != ev->expected_is_collapsed) {
        log_error("event_sim: assert_selection - legacy is_collapsed mismatch: expected %s, got %s",
                 ev->expected_is_collapsed ? "true" : "false",
                 legacy_collapsed ? "true" : "false");
        ctx->fail_count++;
        return false;
    }

    if (ev->check_dom_selection && dom_collapsed != ev->expected_is_collapsed) {
        log_error("event_sim: assert_selection - DomSelection is_collapsed mismatch: expected %s, got %s "
                 "(range_count=%u). Highlight will not render.",
                 ev->expected_is_collapsed ? "true" : "false",
                 dom_collapsed ? "true" : "false",
                 ds ? ds->range_count : 0u);
        ctx->fail_count++;
        return false;
    }

    log_info("event_sim: assert_selection PASS (legacy=%s%s)",
             legacy_collapsed ? "collapsed" : "non-collapsed",
             ev->check_dom_selection ? (dom_collapsed ? ", DOM=collapsed" : ", DOM=non-collapsed") : "");
    ctx->pass_count++;
    return true;
}

// Assert helper for target view
static bool assert_target(EventSimContext* ctx, UiContext* uicon, SimEvent* ev) {
    DomDocument* doc = uicon->document;
    if (!doc || !doc->state) {
        log_error("event_sim: assert_target - no document or state");
        ctx->fail_count++;
        return false;
    }

    CaretState* caret = doc->state->caret;

    if (!caret || !caret->view) {
        log_error("event_sim: assert_target - no caret view");
        ctx->fail_count++;
        return false;
    }

    ViewType actual_type = caret->view->view_type;
    if (actual_type != ev->expected_view_type) {
        log_error("event_sim: assert_target - view_type mismatch: expected %d, got %d",
                 ev->expected_view_type, actual_type);
        ctx->fail_count++;
        return false;
    }

    log_info("event_sim: assert_target PASS (view_type=%d)", actual_type);
    ctx->pass_count++;
    return true;
}

// ===== Phase 7: assert_snapshot pixel comparison =====

// Helper: force render the current surface
static void force_render_surface(UiContext* uicon) {
    extern void render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file);
    if (uicon->document && uicon->document->view_tree) {
        RadiantState* state = (RadiantState*)uicon->document->state;
        if (state) state->is_dirty = true;
        render_html_doc(uicon, uicon->document->view_tree, nullptr);
        if (state) {
            state->is_dirty = false;
            state->needs_repaint = false;
            dirty_clear(&state->dirty_tracker);
        }
    }
}

// YIQ-based perceptual color distance (same algorithm as pixelmatch)
static float pixel_yiq_distance(uint32_t rgba1, uint32_t rgba2) {
    float r1 = (rgba1 & 0xFF) / 255.0f;
    float g1 = ((rgba1 >> 8) & 0xFF) / 255.0f;
    float b1 = ((rgba1 >> 16) & 0xFF) / 255.0f;
    float r2 = (rgba2 & 0xFF) / 255.0f;
    float g2 = ((rgba2 >> 8) & 0xFF) / 255.0f;
    float b2 = ((rgba2 >> 16) & 0xFF) / 255.0f;

    float y  = r1*0.29889531f + g1*0.58662247f + b1*0.11448223f;
    float i  = r1*0.59597799f - g1*0.27417610f - b1*0.32180189f;
    float q  = r1*0.21147017f - g1*0.52261711f + b1*0.31114694f;
    float y2 = r2*0.29889531f + g2*0.58662247f + b2*0.11448223f;
    float i2 = r2*0.59597799f - g2*0.27417610f - b2*0.32180189f;
    float q2 = r2*0.21147017f - g2*0.52261711f + b2*0.31114694f;

    float dy = y - y2, di = i - i2, dq = q - q2;
    return 0.5053f*dy*dy + 0.299f*di*di + 0.1957f*dq*dq;
}

static void assert_snapshot_impl(EventSimContext* ctx, UiContext* uicon, SimEvent* ev) {
    const char* ref_path = ev->snapshot_reference;
    float threshold_pct = ev->snapshot_threshold > 0 ? ev->snapshot_threshold : 1.0f;

    // Step 1: Ensure surface is rendered
    force_render_surface(uicon);

    ImageSurface* actual = uicon->surface;
    if (!actual || !actual->pixels) {
        log_error("event_sim: assert_snapshot FAIL - no rendered surface");
        ctx->fail_count++;
        return;
    }

    // Step 2: Optionally save actual PNG for debugging
    if (ev->snapshot_actual_path) {
        extern void save_surface_to_png(ImageSurface* surface, const char* filename);
        save_surface_to_png(actual, ev->snapshot_actual_path);
    }

    // Step 3: Load reference PNG
    int ref_w, ref_h, ref_channels;
    unsigned char* ref_pixels = image_load(ref_path, &ref_w, &ref_h,
                                            &ref_channels, 4);
    if (!ref_pixels) {
        log_error("event_sim: assert_snapshot FAIL - failed to load reference: %s", ref_path);
        ctx->fail_count++;
        return;
    }

    // Step 4: Dimension check
    if (ref_w != actual->width || ref_h != actual->height) {
        log_error("event_sim: assert_snapshot FAIL - size mismatch: actual %dx%d vs ref %dx%d",
                 actual->width, actual->height, ref_w, ref_h);
        image_free(ref_pixels);
        ctx->fail_count++;
        return;
    }

    // Step 5: Pixel comparison
    int total = ref_w * ref_h;
    int mismatched = 0;
    const float YIQ_THRESHOLD_SQ = 0.1f * 0.1f; // matches pixelmatch threshold=0.1

    uint32_t* actual_px = (uint32_t*)actual->pixels;
    uint32_t* ref_px = (uint32_t*)ref_pixels;
    int actual_stride = actual->pitch / 4; // INT_CAST_OK: pixel stride

    // Allocate diff buffer only when save_diff is requested
    uint32_t* diff_px = nullptr;
    if (ev->snapshot_diff_path) {
        diff_px = (uint32_t*)mem_calloc(total, sizeof(uint32_t), MEM_CAT_RENDER);
    }

    for (int y = 0; y < ref_h; y++) {
        for (int x = 0; x < ref_w; x++) {
            uint32_t a = actual_px[y * actual_stride + x];
            uint32_t r = ref_px[y * ref_w + x];
            if (a != r) {
                float dist = pixel_yiq_distance(a, r);
                if (dist > YIQ_THRESHOLD_SQ) {
                    mismatched++;
                    if (diff_px) diff_px[y * ref_w + x] = 0xFF0000FF; // red ABGR
                } else {
                    if (diff_px) diff_px[y * ref_w + x] = a;
                }
            } else {
                if (diff_px) diff_px[y * ref_w + x] = a;
            }
        }
    }

    float mismatch_pct = (float)mismatched / (float)total * 100.0f;

    // Step 6: Save diff image on mismatch
    if (diff_px && ev->snapshot_diff_path && mismatched > 0) {
        extern void save_surface_to_png(ImageSurface* surface, const char* filename);
        ImageSurface diff_surf = {};
        diff_surf.width = ref_w;
        diff_surf.height = ref_h;
        diff_surf.pitch = ref_w * 4;
        diff_surf.pixels = diff_px;
        save_surface_to_png(&diff_surf, ev->snapshot_diff_path);
    }
    if (diff_px) mem_free(diff_px);
    image_free(ref_pixels);

    // Step 7: Pass/fail
    if (mismatch_pct <= threshold_pct) {
        log_info("event_sim: assert_snapshot PASS - %.2f%% mismatch (threshold %.1f%%) ref=%s",
                 mismatch_pct, threshold_pct, ref_path);
        ctx->pass_count++;
    } else {
        log_error("event_sim: assert_snapshot FAIL - %.2f%% mismatch > threshold %.1f%% ref=%s",
                 mismatch_pct, threshold_pct, ref_path);
        ctx->fail_count++;
    }
}

// Process a single simulated event
static void process_sim_event(EventSimContext* ctx, SimEvent* ev, UiContext* uicon, GLFWwindow* window) {
    switch (ev->type) {
        case SIM_EVENT_WAIT:
            log_info("event_sim: wait %d ms", ev->wait_ms);
            break;

        case SIM_EVENT_MOUSE_MOVE: {
            int x, y;
            resolve_target(ev, uicon->document, &x, &y);
            log_info("event_sim: mouse_move to (%d, %d)", x, y);
            sim_mouse_move(uicon, x, y);
            break;
        }

        case SIM_EVENT_MOUSE_DOWN: {
            int x, y;
            resolve_target(ev, uicon->document, &x, &y);
            log_info("event_sim: mouse_down at (%d, %d) button=%d", x, y, ev->button);
            sim_mouse_button(uicon, x, y, ev->button, ev->mods, true);
            break;
        }

        case SIM_EVENT_MOUSE_UP: {
            int x, y;
            resolve_target(ev, uicon->document, &x, &y);
            log_info("event_sim: mouse_up at (%d, %d) button=%d", x, y, ev->button);
            sim_mouse_button(uicon, x, y, ev->button, ev->mods, false);
            break;
        }

        case SIM_EVENT_MOUSE_DRAG: {
            // Resolve start position from target selector/text or raw from_x/from_y
            int drag_x = ev->x, drag_y = ev->y;
            if (ev->target_selector || ev->target_text) {
                if (!resolve_target(ev, uicon->document, &drag_x, &drag_y)) break;
            }
            // Resolve end position from to_target or raw to_x/to_y
            int drag_to_x = ev->to_x, drag_to_y = ev->to_y;
            if (ev->to_target_selector && uicon->document) {
                View* to_elem = find_element_by_selector(uicon->document, ev->to_target_selector);
                if (to_elem) {
                    float fx, fy;
                    get_element_center_abs(to_elem, &fx, &fy);
                    drag_to_x = (int)fx;
                    drag_to_y = (int)fy;
                    log_info("event_sim: resolved to_target selector '%s' to (%d, %d)", ev->to_target_selector, drag_to_x, drag_to_y);
                } else {
                    log_error("event_sim: to_target selector '%s' not found", ev->to_target_selector);
                    break;
                }
            } else if (ev->to_target_text && uicon->document) {
                float fx, fy;
                if (find_text_position(uicon->document, ev->to_target_text, &fx, &fy)) {
                    drag_to_x = (int)fx;
                    drag_to_y = (int)fy;
                    log_info("event_sim: resolved to_target text '%s' to (%d, %d)", ev->to_target_text, drag_to_x, drag_to_y);
                } else {
                    log_error("event_sim: to_target text '%s' not found", ev->to_target_text);
                    break;
                }
            }
            log_info("event_sim: mouse_drag from (%d, %d) to (%d, %d)", drag_x, drag_y, drag_to_x, drag_to_y);
            sim_mouse_button(uicon, drag_x, drag_y, ev->button, ev->mods, true);
            for (int step = 1; step <= 5; step++) {
                int px = drag_x + (drag_to_x - drag_x) * step / 5;
                int py = drag_y + (drag_to_y - drag_y) * step / 5;
                sim_mouse_move(uicon, px, py);
            }
            sim_mouse_button(uicon, drag_to_x, drag_to_y, ev->button, ev->mods, false);
            break;
        }

        case SIM_EVENT_DRAG_AND_DROP: {
            DomDocument* doc = uicon->document;
            if (!doc) {
                log_error("event_sim: drag_and_drop - no document");
                ctx->fail_count++;
                break;
            }
            // Resolve drag source element
            View* src_view = nullptr;
            if (ev->target_selector) {
                src_view = find_element_by_selector(doc, ev->target_selector);
            }
            if (!src_view) {
                log_error("event_sim: drag_and_drop - source element '%s' not found",
                    ev->target_selector ? ev->target_selector : "(null)");
                ctx->fail_count++;
                break;
            }
            // Verify draggable attribute
            DomElement* src_dom = src_view->as_element();
            if (src_dom) {
                const char* draggable = dom_element_get_attribute(src_dom, "draggable");
                if (!draggable || strcmp(draggable, "true") != 0) {
                    log_error("event_sim: drag_and_drop - source '%s' is not draggable (draggable='%s')",
                        ev->target_selector, draggable ? draggable : "null");
                    ctx->fail_count++;
                    break;
                }
            }
            // Resolve drop target element
            View* dst_view = nullptr;
            if (ev->to_target_selector) {
                dst_view = find_element_by_selector(doc, ev->to_target_selector);
            }
            if (!dst_view) {
                log_error("event_sim: drag_and_drop - drop target '%s' not found",
                    ev->to_target_selector ? ev->to_target_selector : "(null)");
                ctx->fail_count++;
                break;
            }
            DomElement* dst_dom = dst_view->as_element();
            // Get source and destination centers
            float src_fx, src_fy, dst_fx, dst_fy;
            get_element_center_abs(src_view, &src_fx, &src_fy);
            get_element_center_abs(dst_view, &dst_fx, &dst_fy);
            int src_x = (int)src_fx, src_y = (int)src_fy;
            int dst_x = (int)dst_fx, dst_y = (int)dst_fy;
            log_info("event_sim: drag_and_drop from '%s' (%d,%d) to '%s' (%d,%d)",
                ev->target_selector, src_x, src_y, ev->to_target_selector, dst_x, dst_y);
            // Set drag pseudo-state on source
            if (src_dom) dom_element_set_pseudo_state(src_dom, PSEUDO_STATE_DRAG);
            // Dispatch: mouse_down on source
            sim_mouse_button(uicon, src_x, src_y, 0, 0, true);
            // Intermediate mouse_move steps
            int steps = ev->drag_steps > 0 ? ev->drag_steps : 5;
            for (int step = 1; step <= steps; step++) {
                int px = src_x + (dst_x - src_x) * step / steps;
                int py = src_y + (dst_y - src_y) * step / steps;
                sim_mouse_move(uicon, px, py);
            }
            // Set drag-over on destination
            if (dst_dom) dom_element_set_pseudo_state(dst_dom, PSEUDO_STATE_DRAG_OVER);
            // Drop: mouse_up on destination
            sim_mouse_button(uicon, dst_x, dst_y, 0, 0, false);
            // Clear drag pseudo-states
            if (src_dom) dom_element_clear_pseudo_state(src_dom, PSEUDO_STATE_DRAG);
            if (dst_dom) dom_element_clear_pseudo_state(dst_dom, PSEUDO_STATE_DRAG_OVER);
            log_info("event_sim: drag_and_drop completed");
            break;
        }

        case SIM_EVENT_KEY_PRESS:
            log_info("event_sim: key_press key=%d mods=%d", ev->key, ev->mods);
            sim_key(uicon, ev->key, ev->mods, true);
            sim_key(uicon, ev->key, ev->mods, false);
            break;

        case SIM_EVENT_KEY_DOWN:
            log_info("event_sim: key_down key=%d", ev->key);
            sim_key(uicon, ev->key, 0, true);
            break;

        case SIM_EVENT_KEY_UP:
            log_info("event_sim: key_up key=%d", ev->key);
            sim_key(uicon, ev->key, 0, false);
            break;

        case SIM_EVENT_KEY_COMBO:
            log_info("event_sim: key_combo key=%d mods=%d", ev->key, ev->mods);
            sim_key(uicon, ev->key, ev->mods, true);
            sim_key(uicon, ev->key, ev->mods, false);
            break;

        case SIM_EVENT_SCROLL:
            log_info("event_sim: scroll at (%d, %d) offset=(%.2f, %.2f)", ev->x, ev->y, ev->scroll_dx, ev->scroll_dy);
            sim_scroll(uicon, ev->x, ev->y, ev->scroll_dx, ev->scroll_dy);
            // Re-render after scroll to update surface pixels
            force_render_surface(uicon);
            break;

        // ===== High-level actions =====

        case SIM_EVENT_CLICK: {
            // For selector-targeted checkbox/radio, check state before click
            // to detect if the coordinate-based click already handled the toggle
            View* form_elem = nullptr;
            bool was_checked = false;
            if (ev->target_selector) {
                DomDocument* doc = uicon->document;
                form_elem = find_element_by_selector(doc, ev->target_selector, ev->target_index);
                if (form_elem && sim_is_checkbox_or_radio(form_elem)) {
                    was_checked = dom_element_has_pseudo_state((DomElement*)form_elem, PSEUDO_STATE_CHECKED);
                } else {
                    form_elem = nullptr;
                }
            }

            int x, y;
            if (!resolve_target(ev, uicon->document, &x, &y)) break;
            log_info("event_sim: click at (%d, %d) button=%d", x, y, ev->button);
            sim_mouse_button(uicon, x, y, ev->button, ev->mods, true);
            sim_mouse_button(uicon, x, y, ev->button, ev->mods, false);

            // If the coordinate click didn't toggle the checkbox/radio, do it directly
            if (form_elem) {
                bool is_checked_now = dom_element_has_pseudo_state((DomElement*)form_elem, PSEUDO_STATE_CHECKED);
                if (is_checked_now == was_checked) {
                    // State didn't change — coordinate click missed the element
                    RadiantState* state = (RadiantState*)uicon->document->state;
                    if (state && !dom_element_has_pseudo_state((DomElement*)form_elem, PSEUDO_STATE_DISABLED)) {
                        sim_toggle_checkbox_radio(form_elem, state);
                    }
                }
            }
            break;
        }

        case SIM_EVENT_DBLCLICK: {
            int x, y;
            if (!resolve_target(ev, uicon->document, &x, &y)) break;
            log_info("event_sim: dblclick at (%d, %d)", x, y);
            // First click
            sim_mouse_button(uicon, x, y, ev->button, ev->mods, true);
            sim_mouse_button(uicon, x, y, ev->button, ev->mods, false);
            // Second click with clicks=2
            sim_mouse_move(uicon, x, y);
            {
                RdtEvent event;
                event.mouse_button.type = RDT_EVENT_MOUSE_DOWN;
                event.mouse_button.timestamp = get_monotonic_time();
                event.mouse_button.x = x;
                event.mouse_button.y = y;
                event.mouse_button.button = ev->button;
                event.mouse_button.clicks = 2;
                event.mouse_button.mods = ev->mods;
                handle_event(uicon, uicon->document, &event);
                event.mouse_button.type = RDT_EVENT_MOUSE_UP;
                handle_event(uicon, uicon->document, &event);
            }
            break;
        }

        case SIM_EVENT_TYPE: {
            // Click target first to focus it
            if (ev->target_selector || ev->target_text) {
                int x, y;
                if (resolve_target(ev, uicon->document, &x, &y)) {
                    sim_mouse_button(uicon, x, y, 0, 0, true);
                    sim_mouse_button(uicon, x, y, 0, 0, false);
                }
            }
            // If clear_first, send Cmd+A then Delete
            if (ev->clear_first) {
                #ifdef __APPLE__
                sim_key(uicon, GLFW_KEY_A, RDT_MOD_SUPER, true);
                sim_key(uicon, GLFW_KEY_A, RDT_MOD_SUPER, false);
                #else
                sim_key(uicon, GLFW_KEY_A, RDT_MOD_CTRL, true);
                sim_key(uicon, GLFW_KEY_A, RDT_MOD_CTRL, false);
                #endif
                sim_key(uicon, GLFW_KEY_DELETE, 0, true);
                sim_key(uicon, GLFW_KEY_DELETE, 0, false);
            }
            // Type each character
            if (ev->input_text) {
                log_info("event_sim: type '%s'", ev->input_text);
                const char* p = ev->input_text;
                while (*p) {
                    // Simple ASCII handling - treat each byte as a codepoint
                    uint32_t cp = (uint32_t)(unsigned char)*p;
                    sim_text_input(uicon, cp);
                    p++;
                }
            }
            break;
        }

        case SIM_EVENT_FOCUS: {
            int x, y;
            if (!resolve_target(ev, uicon->document, &x, &y)) break;
            log_info("event_sim: focus via click at (%d, %d)", x, y);
            sim_mouse_button(uicon, x, y, 0, 0, true);
            sim_mouse_button(uicon, x, y, 0, 0, false);
            break;
        }

        case SIM_EVENT_CHECK: {
            // Set checkbox/radio to the desired checked state
            DomDocument* doc = uicon->document;
            View* elem = resolve_target_element(ev, doc);
            if (!elem) {
                log_error("event_sim: check - target element not found");
                break;
            }
            if (!sim_is_checkbox_or_radio(elem)) {
                log_error("event_sim: check - target is not a checkbox or radio");
                break;
            }
            bool is_checked = dom_element_has_pseudo_state((DomElement*)elem, PSEUDO_STATE_CHECKED);
            if (is_checked != ev->expected_checked) {
                RadiantState* state = (RadiantState*)doc->state;
                if (state && !dom_element_has_pseudo_state((DomElement*)elem, PSEUDO_STATE_DISABLED)) {
                    sim_toggle_checkbox_radio(elem, state);
                    log_info("event_sim: check - toggled to %s", ev->expected_checked ? "checked" : "unchecked");
                }
            } else {
                log_info("event_sim: check - already %s, no action needed", is_checked ? "checked" : "unchecked");
            }
            break;
        }

        case SIM_EVENT_SELECT_OPTION: {
            // Select an option from a <select> dropdown by value or label
            DomDocument* doc = uicon->document;
            View* elem = resolve_target_element(ev, doc);
            if (!elem) {
                log_error("event_sim: select_option - target element not found");
                break;
            }
            // Walk up to find the <select> element
            View* select_view = elem;
            while (select_view && select_view->is_element() &&
                   ((ViewElement*)select_view)->tag() != HTM_TAG_SELECT) {
                select_view = select_view->parent;
            }
            if (!select_view || !select_view->is_element() ||
                ((ViewElement*)select_view)->tag() != HTM_TAG_SELECT) {
                log_error("event_sim: select_option - target is not a <select> element");
                break;
            }
            ViewBlock* select = (ViewBlock*)select_view;
            if (!select->form) {
                log_error("event_sim: select_option - select has no form control prop");
                break;
            }
            // Find matching option by value or label
            int match_index = -1;
            int idx = 0;
            DomNode* child = select->first_child;
            while (child) {
                if (child->is_element()) {
                    DomElement* child_elem = (DomElement*)child;
                    if (child_elem->tag() == HTM_TAG_OPTION) {
                        if (ev->option_value) {
                            const char* val = dom_element_get_attribute(child_elem, "value");
                            if (val && strcmp(val, ev->option_value) == 0) {
                                match_index = idx;
                                break;
                            }
                        }
                        if (ev->option_label) {
                            // Match by visible text content
                            DomNode* text_child = child_elem->first_child;
                            while (text_child) {
                                if (text_child->is_text()) {
                                    DomText* text = (DomText*)text_child;
                                    if (text->text && strcmp(text->text, ev->option_label) == 0) {
                                        match_index = idx;
                                        break;
                                    }
                                }
                                text_child = text_child->next_sibling;
                            }
                            if (match_index >= 0) break;
                        }
                        idx++;
                    } else if (child_elem->tag() == HTM_TAG_OPTGROUP) {
                        // Search options inside optgroup
                        DomNode* opt_child = child_elem->first_child;
                        while (opt_child) {
                            if (opt_child->is_element()) {
                                DomElement* opt_elem = (DomElement*)opt_child;
                                if (opt_elem->tag() == HTM_TAG_OPTION) {
                                    if (ev->option_value) {
                                        const char* val = dom_element_get_attribute(opt_elem, "value");
                                        if (val && strcmp(val, ev->option_value) == 0) {
                                            match_index = idx;
                                            break;
                                        }
                                    }
                                    if (ev->option_label) {
                                        DomNode* text_child = opt_elem->first_child;
                                        while (text_child) {
                                            if (text_child->is_text()) {
                                                DomText* text = (DomText*)text_child;
                                                if (text->text && strcmp(text->text, ev->option_label) == 0) {
                                                    match_index = idx;
                                                    break;
                                                }
                                            }
                                            text_child = text_child->next_sibling;
                                        }
                                        if (match_index >= 0) break;
                                    }
                                    idx++;
                                }
                            }
                            opt_child = opt_child->next_sibling;
                        }
                        if (match_index >= 0) break;
                    }
                }
                child = child->next_sibling;
            }
            if (match_index < 0) {
                log_error("event_sim: select_option - no matching option found (value='%s', label='%s')",
                         ev->option_value ? ev->option_value : "(null)",
                         ev->option_label ? ev->option_label : "(null)");
                break;
            }
            select->form->selected_index = match_index;
            RadiantState* state = (RadiantState*)doc->state;
            if (state) {
                // Close dropdown if open
                if (state->open_dropdown == select_view) {
                    state->open_dropdown = nullptr;
                    select->form->dropdown_open = 0;
                }
                state->needs_repaint = true;
            }
            log_info("event_sim: select_option - selected index %d", match_index);
            break;
        }

        case SIM_EVENT_RESIZE: {
            // Resize viewport and trigger full relayout
            int new_css_w = ev->x;
            int new_css_h = ev->y;
            float pr = uicon->pixel_ratio > 0 ? uicon->pixel_ratio : 1.0f;
            int new_phys_w = (int)(new_css_w * pr);
            int new_phys_h = (int)(new_css_h * pr);
            log_info("event_sim: resize to %dx%d CSS pixels (%dx%d physical)", new_css_w, new_css_h, new_phys_w, new_phys_h);
            uicon->viewport_width = new_css_w;
            uicon->viewport_height = new_css_h;
            uicon->window_width = new_phys_w;
            uicon->window_height = new_phys_h;
            // Recreate surface and reflow
            extern void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);
            ui_context_create_surface(uicon, new_phys_w, new_phys_h);
            extern void reflow_html_doc(DomDocument* doc);
            if (uicon->document) {
                reflow_html_doc(uicon->document);
            }
            // Re-render after resize to update surface pixels
            force_render_surface(uicon);
            break;
        }

        // ===== Assertions =====

        case SIM_EVENT_ASSERT_CARET:
            log_info("event_sim: assert_caret view_type=%d char_offset=%d", ev->expected_view_type, ev->expected_char_offset);
            assert_caret(ctx, uicon, ev);
            break;

        case SIM_EVENT_ASSERT_SELECTION:
            log_info("event_sim: assert_selection is_collapsed=%s", ev->expected_is_collapsed ? "true" : "false");
            assert_selection(ctx, uicon, ev);
            break;

        case SIM_EVENT_ASSERT_TARGET:
            log_info("event_sim: assert_target view_type=%d", ev->expected_view_type);
            assert_target(ctx, uicon, ev);
            break;

        case SIM_EVENT_ASSERT_TEXT: {
            DomDocument* doc = uicon->document;
            View* elem = resolve_target_element(ev, doc);
            if (!elem) {
                log_error("event_sim: assert_text - target element not found");
                ctx->fail_count++;
                break;
            }
            StrBuf* buf = strbuf_new_cap(256);
            sim_extract_text(elem, buf);
            bool passed = true;
            if (ev->assert_contains) {
                if (!buf->str || !strstr(buf->str, ev->assert_contains)) {
                    log_error("event_sim: assert_text FAIL - expected to contain '%s', got '%s'",
                             ev->assert_contains, buf->str ? buf->str : "(empty)");
                    passed = false;
                }
            }
            if (ev->assert_equals) {
                if (!buf->str || strcmp(buf->str, ev->assert_equals) != 0) {
                    log_error("event_sim: assert_text FAIL - expected '%s', got '%s'",
                             ev->assert_equals, buf->str ? buf->str : "(empty)");
                    passed = false;
                }
            }
            if (passed) {
                log_info("event_sim: assert_text PASS");
                ctx->pass_count++;
            } else {
                ctx->fail_count++;
            }
            strbuf_free(buf);
            break;
        }

        case SIM_EVENT_ASSERT_VISIBLE: {
            DomDocument* doc = uicon->document;
            View* elem = resolve_target_element(ev, doc);
            if (!elem) {
                log_error("event_sim: assert_visible - target element not found");
                ctx->fail_count++;
                break;
            }
            bool is_visible = (elem->width > 0 && elem->height > 0);
            if (is_visible != ev->expected_visible) {
                log_error("event_sim: assert_visible FAIL - expected %s, element has size %.1fx%.1f",
                         ev->expected_visible ? "visible" : "hidden", elem->width, elem->height);
                ctx->fail_count++;
            } else {
                log_info("event_sim: assert_visible PASS");
                ctx->pass_count++;
            }
            break;
        }

        case SIM_EVENT_ASSERT_VALUE: {
            DomDocument* doc = uicon->document;
            View* elem = resolve_target_element(ev, doc);
            if (!elem || !elem->is_element()) {
                log_error("event_sim: assert_value - target element not found");
                ctx->fail_count++;
                break;
            }
            DomElement* dom_elem = (DomElement*)elem;
            const char* val = dom_element_get_attribute(dom_elem, "value");
            const char* actual = val ? val : "";
            bool passed = true;
            if (ev->assert_equals) {
                if (strcmp(actual, ev->assert_equals) != 0) {
                    log_error("event_sim: assert_value FAIL - expected '%s', got '%s'",
                             ev->assert_equals, actual);
                    passed = false;
                }
            }
            if (ev->assert_contains) {
                if (!strstr(actual, ev->assert_contains)) {
                    log_error("event_sim: assert_value FAIL - expected to contain '%s', got '%s'",
                             ev->assert_contains, actual);
                    passed = false;
                }
            }
            if (passed) {
                log_info("event_sim: assert_value PASS (value='%s')", actual);
                ctx->pass_count++;
            } else {
                ctx->fail_count++;
            }
            break;
        }

        case SIM_EVENT_ASSERT_CHECKED: {
            DomDocument* doc = uicon->document;
            View* elem = resolve_target_element(ev, doc);
            if (!elem || !elem->is_element()) {
                log_error("event_sim: assert_checked - target element not found");
                ctx->fail_count++;
                break;
            }
            DomElement* dom_elem = (DomElement*)elem;
            bool is_checked = (dom_elem->pseudo_state & PSEUDO_STATE_CHECKED) != 0;
            if (is_checked != ev->expected_checked) {
                log_error("event_sim: assert_checked FAIL - expected %s, got %s",
                         ev->expected_checked ? "checked" : "unchecked",
                         is_checked ? "checked" : "unchecked");
                ctx->fail_count++;
            } else {
                log_info("event_sim: assert_checked PASS (%s)", is_checked ? "checked" : "unchecked");
                ctx->pass_count++;
            }
            break;
        }

        case SIM_EVENT_ASSERT_STATE: {
            DomDocument* doc = uicon->document;
            View* elem = resolve_target_element(ev, doc);
            if (!elem || !elem->is_element()) {
                log_error("event_sim: assert_state - target element not found");
                ctx->fail_count++;
                break;
            }
            if (!ev->state_name) {
                log_error("event_sim: assert_state - missing 'state' field");
                ctx->fail_count++;
                break;
            }
            DomElement* dom_elem = (DomElement*)elem;
            uint32_t mask = 0;
            if (strcmp(ev->state_name, ":hover") == 0) mask = PSEUDO_STATE_HOVER;
            else if (strcmp(ev->state_name, ":active") == 0) mask = PSEUDO_STATE_ACTIVE;
            else if (strcmp(ev->state_name, ":focus") == 0) mask = PSEUDO_STATE_FOCUS;
            else if (strcmp(ev->state_name, ":visited") == 0) mask = PSEUDO_STATE_VISITED;
            else if (strcmp(ev->state_name, ":checked") == 0) mask = PSEUDO_STATE_CHECKED;
            else if (strcmp(ev->state_name, ":disabled") == 0) mask = PSEUDO_STATE_DISABLED;
            else if (strcmp(ev->state_name, ":enabled") == 0) mask = PSEUDO_STATE_ENABLED;
            else if (strcmp(ev->state_name, ":focus-visible") == 0) mask = PSEUDO_STATE_FOCUS_VISIBLE;
            else {
                log_error("event_sim: assert_state - unknown state '%s'", ev->state_name);
                ctx->fail_count++;
                break;
            }
            bool has_state = (dom_elem->pseudo_state & mask) != 0;
            if (has_state != ev->expected_state_value) {
                log_error("event_sim: assert_state FAIL - expected %s=%s, got %s",
                         ev->state_name,
                         ev->expected_state_value ? "true" : "false",
                         has_state ? "true" : "false");
                ctx->fail_count++;
            } else {
                log_info("event_sim: assert_state PASS (%s=%s)",
                        ev->state_name, has_state ? "true" : "false");
                ctx->pass_count++;
            }
            break;
        }

        case SIM_EVENT_ASSERT_FOCUS: {
            DomDocument* doc = uicon->document;
            if (!doc || !doc->state || !doc->state->focus) {
                log_error("event_sim: assert_focus - no document or focus state");
                ctx->fail_count++;
                break;
            }
            View* expected = resolve_target_element(ev, doc);
            View* actual = doc->state->focus->current;
            if (!expected) {
                log_error("event_sim: assert_focus - target element not found");
                ctx->fail_count++;
            } else if (actual == expected) {
                log_info("event_sim: assert_focus PASS");
                ctx->pass_count++;
            } else {
                // for replaced elements (input, textarea, etc.), focus may be on
                // an internal child view; check if actual is a descendant of expected
                bool match = false;
                if (actual) {
                    View* p = actual->parent;
                    while (p) {
                        if (p == expected) { match = true; break; }
                        p = p->parent;
                    }
                }
                if (match) {
                    log_info("event_sim: assert_focus PASS (child of expected)");
                    ctx->pass_count++;
                } else {
                    log_error("event_sim: assert_focus FAIL - focus not on expected element");
                    ctx->fail_count++;
                }
            }
            break;
        }

        case SIM_EVENT_ASSERT_SCROLL: {
            DomDocument* doc = uicon->document;
            if (!doc || !doc->view_tree || !doc->view_tree->root) {
                log_error("event_sim: assert_scroll - no document or view tree");
                ctx->fail_count++;
                break;
            }
            // Read scroll position from root block's scroller (the actual scroll source)
            float actual_x = 0, actual_y = 0;
            ViewBlock* root_block = (ViewBlock*)doc->view_tree->root;
            if (root_block && root_block->scroller && root_block->scroller->pane) {
                actual_x = root_block->scroller->pane->h_scroll_position;
                actual_y = root_block->scroller->pane->v_scroll_position;
            }
            float tol = ev->scroll_tolerance;
            bool pass_x = (ev->expected_scroll_x == 0 && !ev->expected_scroll_y) ||
                          (actual_x >= ev->expected_scroll_x - tol && actual_x <= ev->expected_scroll_x + tol);
            bool pass_y = (actual_y >= ev->expected_scroll_y - tol && actual_y <= ev->expected_scroll_y + tol);
            bool result = pass_x && pass_y;
            if (ev->negate_scroll) result = !result;
            if (!result) {
                log_error("event_sim: assert_scroll FAIL - expected%s (%.1f, %.1f) +/-%.1f, got (%.1f, %.1f)",
                         ev->negate_scroll ? " NOT" : "",
                         ev->expected_scroll_x, ev->expected_scroll_y, tol, actual_x, actual_y);
                ctx->fail_count++;
            } else {
                log_info("event_sim: assert_scroll PASS (%.1f, %.1f)%s", actual_x, actual_y,
                         ev->negate_scroll ? " [negated]" : "");
                ctx->pass_count++;
            }
            break;
        }

        // ===== Phase 5 Assertions =====

        case SIM_EVENT_ASSERT_RECT: {
            DomDocument* doc = uicon->document;
            View* elem = resolve_target_element(ev, doc);
            if (!elem) {
                log_error("event_sim: assert_rect - target element not found");
                ctx->fail_count++;
                break;
            }
            float ax, ay, aw, ah;
            get_element_rect_abs(elem, &ax, &ay, &aw, &ah);
            float tol = ev->rect_tolerance;
            bool passed = true;
            if (ev->has_rect_x && (ax < ev->expected_rect_x - tol || ax > ev->expected_rect_x + tol)) {
                log_error("event_sim: assert_rect FAIL - x: expected %.1f, got %.1f (tol=%.1f)", ev->expected_rect_x, ax, tol);
                passed = false;
            }
            if (ev->has_rect_y && (ay < ev->expected_rect_y - tol || ay > ev->expected_rect_y + tol)) {
                log_error("event_sim: assert_rect FAIL - y: expected %.1f, got %.1f (tol=%.1f)", ev->expected_rect_y, ay, tol);
                passed = false;
            }
            if (ev->has_rect_w && (aw < ev->expected_rect_w - tol || aw > ev->expected_rect_w + tol)) {
                log_error("event_sim: assert_rect FAIL - width: expected %.1f, got %.1f (tol=%.1f)", ev->expected_rect_w, aw, tol);
                passed = false;
            }
            if (ev->has_rect_h && (ah < ev->expected_rect_h - tol || ah > ev->expected_rect_h + tol)) {
                log_error("event_sim: assert_rect FAIL - height: expected %.1f, got %.1f (tol=%.1f)", ev->expected_rect_h, ah, tol);
                passed = false;
            }
            if (passed) {
                log_info("event_sim: assert_rect PASS (x=%.1f, y=%.1f, w=%.1f, h=%.1f)", ax, ay, aw, ah);
                ctx->pass_count++;
            } else {
                ctx->fail_count++;
            }
            break;
        }

        case SIM_EVENT_ASSERT_STYLE: {
            DomDocument* doc = uicon->document;
            View* elem = resolve_target_element(ev, doc);
            if (!elem) {
                log_error("event_sim: assert_style - target element not found");
                ctx->fail_count++;
                break;
            }
            if (!ev->style_property) {
                log_error("event_sim: assert_style - missing 'property' field");
                ctx->fail_count++;
                break;
            }
            StrBuf* val_buf = strbuf_new_cap(64);
            if (!get_computed_style(elem, ev->style_property, val_buf)) {
                log_error("event_sim: assert_style FAIL - unsupported property '%s'", ev->style_property);
                ctx->fail_count++;
                strbuf_free(val_buf);
                break;
            }
            const char* actual = val_buf->str ? val_buf->str : "";
            bool passed = true;
            if (ev->assert_equals) {
                if (ev->style_animated) {
                    // Animated tolerance comparison: parse both as float
                    float actual_f = (float)atof(actual);
                    float expected_f = (float)atof(ev->assert_equals);
                    float tol = ev->style_tolerance > 0 ? ev->style_tolerance : 0.05f;
                    if (actual_f < expected_f - tol || actual_f > expected_f + tol) {
                        log_error("event_sim: assert_style FAIL (animated) - %s: expected ~%.4g, got %.4g (tol=%.4g)",
                                 ev->style_property, expected_f, actual_f, tol);
                        passed = false;
                    }
                } else if (strcmp(actual, ev->assert_equals) != 0) {
                    log_error("event_sim: assert_style FAIL - %s: expected '%s', got '%s'",
                             ev->style_property, ev->assert_equals, actual);
                    passed = false;
                }
            }
            if (ev->assert_contains) {
                if (!strstr(actual, ev->assert_contains)) {
                    log_error("event_sim: assert_style FAIL - %s: expected to contain '%s', got '%s'",
                             ev->style_property, ev->assert_contains, actual);
                    passed = false;
                }
            }
            if (passed) {
                log_info("event_sim: assert_style PASS (%s = '%s')", ev->style_property, actual);
                ctx->pass_count++;
            } else {
                ctx->fail_count++;
            }
            strbuf_free(val_buf);
            break;
        }

        case SIM_EVENT_ASSERT_POSITION: {
            DomDocument* doc = uicon->document;
            // Resolve element A
            View* elem_a = nullptr;
            if (ev->element_a_selector && doc) {
                elem_a = find_element_by_selector(doc, ev->element_a_selector);
            }
            // Resolve element B
            View* elem_b = nullptr;
            if (ev->element_b_selector && doc) {
                elem_b = find_element_by_selector(doc, ev->element_b_selector);
            }
            if (!elem_a || !elem_b) {
                log_error("event_sim: assert_position - element(s) not found (a=%p, b=%p)", elem_a, elem_b);
                ctx->fail_count++;
                break;
            }
            if (!ev->position_relation) {
                log_error("event_sim: assert_position - missing 'relation' field");
                ctx->fail_count++;
                break;
            }
            float ax, ay, aw, ah, bx, by, bw, bh;
            get_element_rect_abs(elem_a, &ax, &ay, &aw, &ah);
            get_element_rect_abs(elem_b, &bx, &by, &bw, &bh);
            float tol = ev->position_tolerance;
            bool passed = false;
            const char* rel = ev->position_relation;
            if (strcmp(rel, "above") == 0) {
                // A.bottom <= B.top + tolerance
                passed = (ay + ah) <= (by + tol);
                if (passed && ev->position_gap > 0) {
                    float gap = by - (ay + ah);
                    passed = gap >= (ev->position_gap - tol);
                }
            } else if (strcmp(rel, "below") == 0) {
                // A.top >= B.bottom - tolerance
                passed = ay >= (by + bh - tol);
                if (passed && ev->position_gap > 0) {
                    float gap = ay - (by + bh);
                    passed = gap >= (ev->position_gap - tol);
                }
            } else if (strcmp(rel, "left_of") == 0) {
                // A.right <= B.left + tolerance
                passed = (ax + aw) <= (bx + tol);
                if (passed && ev->position_gap > 0) {
                    float gap = bx - (ax + aw);
                    passed = gap >= (ev->position_gap - tol);
                }
            } else if (strcmp(rel, "right_of") == 0) {
                // A.left >= B.right - tolerance
                passed = ax >= (bx + bw - tol);
                if (passed && ev->position_gap > 0) {
                    float gap = ax - (bx + bw);
                    passed = gap >= (ev->position_gap - tol);
                }
            } else if (strcmp(rel, "overlaps") == 0) {
                // bounding boxes intersect
                passed = !(ax + aw <= bx || bx + bw <= ax || ay + ah <= by || by + bh <= ay);
            } else if (strcmp(rel, "contains") == 0) {
                // A fully contains B
                passed = (ax <= bx + tol && ay <= by + tol &&
                          ax + aw >= bx + bw - tol && ay + ah >= by + bh - tol);
            } else if (strcmp(rel, "inside") == 0) {
                // A is fully inside B
                passed = (bx <= ax + tol && by <= ay + tol &&
                          bx + bw >= ax + aw - tol && by + bh >= ay + ah - tol);
            } else {
                log_error("event_sim: assert_position - unknown relation '%s'", rel);
                ctx->fail_count++;
                break;
            }
            if (passed) {
                log_info("event_sim: assert_position PASS (%s is %s %s)", 
                        ev->element_a_selector ? ev->element_a_selector : "A", rel,
                        ev->element_b_selector ? ev->element_b_selector : "B");
                ctx->pass_count++;
            } else {
                log_error("event_sim: assert_position FAIL - A(%.1f,%.1f,%.1f,%.1f) is NOT %s B(%.1f,%.1f,%.1f,%.1f)",
                         ax, ay, aw, ah, rel, bx, by, bw, bh);
                ctx->fail_count++;
            }
            break;
        }

        case SIM_EVENT_ASSERT_ELEMENT_AT: {
            DomDocument* doc = uicon->document;
            if (!doc || !doc->view_tree || !doc->view_tree->root) {
                log_error("event_sim: assert_element_at - no document/view tree");
                ctx->fail_count++;
                break;
            }
            View* root = (View*)doc->view_tree->root;
            View* found = find_element_at(root, (float)ev->at_x, (float)ev->at_y, 0, 0);
            if (!found) {
                log_error("event_sim: assert_element_at FAIL - no element found at (%d, %d)", ev->at_x, ev->at_y);
                ctx->fail_count++;
                break;
            }
            bool passed = true;
            DomElement* found_elem = found->as_element();
            if (ev->expected_at_tag && found_elem) {
                const char* tag = found_elem->tag_name;
                if (!tag || strcasecmp(tag, ev->expected_at_tag) != 0) {
                    log_error("event_sim: assert_element_at FAIL - expected tag '%s', got '%s' at (%d, %d)",
                             ev->expected_at_tag, tag ? tag : "(null)", ev->at_x, ev->at_y);
                    passed = false;
                }
            }
            if (ev->expected_at_selector && found_elem) {
                // Check if the found element or any of its ancestors match the selector
                View* check = found;
                bool selector_matched = false;
                while (check) {
                    if (check->is_element()) {
                        View* candidate = find_element_by_selector(doc, ev->expected_at_selector);
                        if (candidate == check) {
                            selector_matched = true;
                            break;
                        }
                    }
                    check = check->parent;
                }
                if (!selector_matched) {
                    log_error("event_sim: assert_element_at FAIL - element at (%d, %d) does not match selector '%s'",
                             ev->at_x, ev->at_y, ev->expected_at_selector);
                    passed = false;
                }
            }
            if (passed) {
                log_info("event_sim: assert_element_at PASS at (%d, %d)", ev->at_x, ev->at_y);
                ctx->pass_count++;
            } else {
                ctx->fail_count++;
            }
            break;
        }

        case SIM_EVENT_ASSERT_ATTRIBUTE: {
            DomDocument* doc = uicon->document;
            if (!doc) {
                log_error("event_sim: assert_attribute - no document");
                ctx->fail_count++;
                break;
            }
            View* elem = nullptr;
            if (ev->target_selector)
                elem = find_element_by_selector(doc, ev->target_selector);
            if (!elem) {
                log_error("event_sim: assert_attribute FAIL - target '%s' not found",
                    ev->target_selector ? ev->target_selector : "(null)");
                ctx->fail_count++;
                break;
            }
            DomElement* dom_elem = elem->as_element();
            if (!dom_elem) {
                log_error("event_sim: assert_attribute FAIL - target is not a DomElement");
                ctx->fail_count++;
                break;
            }
            const char* actual = dom_element_get_attribute(dom_elem, ev->attribute_name);
            if (ev->assert_equals) {
                if (actual && strcmp(actual, ev->assert_equals) == 0) {
                    log_info("event_sim: assert_attribute PASS '%s'='%s' on '%s'",
                        ev->attribute_name, ev->assert_equals, ev->target_selector);
                    ctx->pass_count++;
                } else {
                    log_error("event_sim: assert_attribute FAIL '%s' expected='%s', actual='%s' on '%s'",
                        ev->attribute_name, ev->assert_equals,
                        actual ? actual : "(null)", ev->target_selector);
                    ctx->fail_count++;
                }
            } else if (ev->assert_contains) {
                if (actual && strstr(actual, ev->assert_contains)) {
                    log_info("event_sim: assert_attribute PASS '%s' contains '%s' on '%s'",
                        ev->attribute_name, ev->assert_contains, ev->target_selector);
                    ctx->pass_count++;
                } else {
                    log_error("event_sim: assert_attribute FAIL '%s' does not contain '%s', actual='%s' on '%s'",
                        ev->attribute_name, ev->assert_contains,
                        actual ? actual : "(null)", ev->target_selector);
                    ctx->fail_count++;
                }
            } else {
                // Just check attribute exists
                if (actual) {
                    log_info("event_sim: assert_attribute PASS '%s' exists (='%s') on '%s'",
                        ev->attribute_name, actual, ev->target_selector);
                    ctx->pass_count++;
                } else {
                    log_error("event_sim: assert_attribute FAIL '%s' not found on '%s'",
                        ev->attribute_name, ev->target_selector);
                    ctx->fail_count++;
                }
            }
            break;
        }

        case SIM_EVENT_ASSERT_COUNT: {
            DomDocument* doc = uicon->document;
            if (!doc) {
                log_error("event_sim: assert_count - no document");
                ctx->fail_count++;
                break;
            }
            int actual = count_elements_by_selector(doc, ev->target_selector);
            bool passed = true;
            if (ev->assert_count_expected >= 0) {
                if (actual != ev->assert_count_expected) {
                    log_error("event_sim: assert_count FAIL - '%s' expected %d, got %d",
                        ev->target_selector, ev->assert_count_expected, actual);
                    passed = false;
                }
            }
            if (ev->assert_count_min >= 0 && actual < ev->assert_count_min) {
                log_error("event_sim: assert_count FAIL - '%s' expected min %d, got %d",
                    ev->target_selector, ev->assert_count_min, actual);
                passed = false;
            }
            if (ev->assert_count_max >= 0 && actual > ev->assert_count_max) {
                log_error("event_sim: assert_count FAIL - '%s' expected max %d, got %d",
                    ev->target_selector, ev->assert_count_max, actual);
                passed = false;
            }
            if (passed) {
                log_info("event_sim: assert_count PASS - '%s' count=%d", ev->target_selector, actual);
                ctx->pass_count++;
            } else {
                ctx->fail_count++;
            }
            break;
        }

        case SIM_EVENT_ASSERT_SNAPSHOT: {
            assert_snapshot_impl(ctx, uicon, ev);
            break;
        }

        // ===== Phase 7: Page Mutations =====

        case SIM_EVENT_SCROLL_TO: {
            DomDocument* doc = uicon->document;
            if (!doc || !doc->view_tree || !doc->view_tree->root) {
                log_error("event_sim: scroll_to - no document or view tree");
                break;
            }
            float target_x = ev->expected_scroll_x;
            float target_y = ev->expected_scroll_y;
            // If a target selector is given, scroll to that element's position
            if (ev->target_selector) {
                View* target_elem = find_element_by_selector(doc, ev->target_selector, ev->target_index);
                if (target_elem) {
                    float ax, ay, aw, ah;
                    get_element_rect_abs(target_elem, &ax, &ay, &aw, &ah);
                    target_x = ax;
                    target_y = ay;
                }
            }
            log_info("event_sim: scroll_to (%.1f, %.1f)", target_x, target_y);
            // Set scroll position on root block's scroller (the actual scroll mechanism)
            ViewBlock* root_block = (ViewBlock*)doc->view_tree->root;
            if (root_block && root_block->scroller && root_block->scroller->pane) {
                ScrollPane* pane = root_block->scroller->pane;
                pane->h_scroll_position = target_x < 0 ? 0 : (target_x > pane->h_max_scroll ? pane->h_max_scroll : target_x);
                pane->v_scroll_position = target_y < 0 ? 0 : (target_y > pane->v_max_scroll ? pane->v_max_scroll : target_y);
            }
            if (doc->state) doc->state->needs_repaint = true;
            force_render_surface(uicon);
            break;
        }

        case SIM_EVENT_ADVANCE_TIME: {
            DomDocument* doc = uicon->document;
            if (!doc || !doc->state) {
                log_error("event_sim: advance_time - no document state");
                break;
            }
            RadiantState* state = (RadiantState*)doc->state;
            AnimationScheduler* sched = state->animation_scheduler;
            if (!sched) {
                log_error("event_sim: advance_time - no animation scheduler");
                break;
            }
            float ms = (float)ev->wait_ms;  // advance_time stores ms in wait_ms
            int steps = ev->advance_steps > 0 ? ev->advance_steps : (int)(ms / 16.0f);
            if (steps < 1) steps = 1;
            float step_ms = ms / (float)steps;
            double base_time = sched->current_time;
            log_info("event_sim: advance_time %.1fms in %d steps (%.2fms each)", ms, steps, step_ms);
            for (int i = 1; i <= steps; i++) {
                double now = base_time + (step_ms * i) / 1000.0;
                animation_scheduler_tick(sched, now, &state->dirty_tracker);
            }
            // Re-render after advancing animation
            force_render_surface(uicon);
            break;
        }

        // ===== Navigation =====

        case SIM_EVENT_NAVIGATE: {
            if (!ev->navigate_url) {
                log_error("event_sim: navigate - missing 'url' field");
                ctx->fail_count++;
                break;
            }
            log_info("event_sim: navigating to '%s'", ev->navigate_url);
            extern DomDocument* load_html_doc(Url* base, char* doc_url, int viewport_width, int viewport_height, float pixel_ratio);
            extern void layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);
            extern void render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file);
            Url* base_url = uicon->document ? uicon->document->url : nullptr;
            if (!base_url) base_url = get_current_dir();
            DomDocument* new_doc = load_html_doc(base_url, ev->navigate_url,
                uicon->viewport_width, uicon->viewport_height, uicon->pixel_ratio);
            if (!new_doc) {
                log_error("event_sim: navigate FAIL - could not load '%s'", ev->navigate_url);
                ctx->fail_count++;
                break;
            }
            // push current document onto navigation history
            if (uicon->document && ctx->nav_history_depth < 16) {
                ctx->nav_history[ctx->nav_history_depth++] = uicon->document;
            }
            uicon->document = new_doc;
            layout_html_doc(uicon, new_doc, false);
            render_html_doc(uicon, new_doc->view_tree, nullptr);
            log_info("event_sim: navigated to '%s'", ev->navigate_url);
            break;
        }

        case SIM_EVENT_NAVIGATE_BACK: {
            if (ctx->nav_history_depth <= 0) {
                log_error("event_sim: navigate_back FAIL - no previous page in history");
                ctx->fail_count++;
                break;
            }
            extern void layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);
            extern void render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file);
            DomDocument* prev_doc = (DomDocument*)ctx->nav_history[--ctx->nav_history_depth];
            uicon->document = prev_doc;
            layout_html_doc(uicon, prev_doc, false);
            render_html_doc(uicon, prev_doc->view_tree, nullptr);
            log_info("event_sim: navigate_back to previous page (history depth now %d)", ctx->nav_history_depth);
            break;
        }

        case SIM_EVENT_SWITCH_FRAME: {
            if (!ev->frame_selector) {
                // switch back to main frame
                if (ctx->original_document) {
                    uicon->document = (DomDocument*)ctx->original_document;
                    ctx->frame_stack_depth = 0;
                    log_info("event_sim: switch_frame back to main");
                } else {
                    log_error("event_sim: switch_frame to main but no original document saved");
                    ctx->fail_count++;
                }
                break;
            }
            // save original document on first switch
            if (!ctx->original_document) {
                ctx->original_document = uicon->document;
            }
            // find iframe element in current document
            DomDocument* cur_doc = uicon->document;
            View* iframe_view = find_element_by_selector(cur_doc, ev->frame_selector);
            if (!iframe_view || !iframe_view->is_element()) {
                log_error("event_sim: switch_frame - iframe '%s' not found", ev->frame_selector);
                ctx->fail_count++;
                break;
            }
            DomElement* iframe_elem = (DomElement*)iframe_view;
            if (!iframe_elem->embed || !iframe_elem->embed->doc) {
                log_error("event_sim: switch_frame - '%s' has no embedded document", ev->frame_selector);
                ctx->fail_count++;
                break;
            }
            if (ctx->frame_stack_depth >= 8) {
                log_error("event_sim: switch_frame - frame stack overflow");
                ctx->fail_count++;
                break;
            }
            ctx->frame_stack[ctx->frame_stack_depth++] = cur_doc;
            uicon->document = iframe_elem->embed->doc;
            log_info("event_sim: switch_frame to '%s'", ev->frame_selector);
            break;
        }

        // ===== Utilities =====

        case SIM_EVENT_LOG:
            fprintf(stderr, "[EVENT_SIM] %s\\n", ev->message ? ev->message : "(no message)");
            break;

        case SIM_EVENT_RENDER:
            {
                log_info("event_sim: render to %s", ev->file_path);
                fprintf(stderr, "[EVENT_SIM] Rendering to: %s\n", ev->file_path);
                // Determine format from extension
                const char* ext = strrchr(ev->file_path, '.');
                if (ext && (strcmp(ext, ".svg") == 0 || strcmp(ext, ".SVG") == 0)) {
                    extern int render_uicontext_to_svg(UiContext* uicon, const char* svg_file);
                    render_uicontext_to_svg(uicon, ev->file_path);
                } else {
                    // Default to PNG
                    extern int render_uicontext_to_png(UiContext* uicon, const char* png_file);
                    render_uicontext_to_png(uicon, ev->file_path);
                }
            }
            break;

        case SIM_EVENT_DUMP_CARET:
            {
                const char* path = ev->file_path ? ev->file_path : "./view_tree.txt";
                log_info("event_sim: dump_caret to %s", path);
                DomDocument* doc = uicon->document;
                if (doc && doc->state) {
                    extern void print_caret_state(RadiantState* state, const char* output_path);
                    print_caret_state(doc->state, path);
                } else {
                    log_error("event_sim: dump_caret - no document state");
                }
            }
            break;

        // ===== Webview commands =====

        case SIM_EVENT_WEBVIEW_EVAL_JS: {
            // find the webview element by selector and execute JS in it
            DomDocument* doc = uicon->document;
            if (!doc) { log_warn("event_sim: webview_eval_js - no document (skipped)"); break; }
            View* view = ev->target_selector ? find_element_by_selector(doc, ev->target_selector, ev->target_index) : nullptr;
            if (!view || !view->is_element()) {
                log_warn("event_sim: webview_eval_js - element '%s' not found (skipped)", ev->target_selector);
                break;
            }
            ViewBlock* block = (ViewBlock*)view;
            if (block->tag_id != HTM_TAG_WEBVIEW || !block->embed || !block->embed->webview || !block->embed->webview->handle) {
                log_warn("event_sim: webview_eval_js - '%s' has no active webview handle (headless?), skipped", ev->target_selector);
                break;
            }
            webview_eval_js(block->embed->webview->handle, ev->js_code);
            log_info("event_sim: webview_eval_js on '%s': %s", ev->target_selector, ev->js_code);
            break;
        }

        case SIM_EVENT_WEBVIEW_WAIT_LOAD: {
            // wait for webview to finish loading (poll loaded flag)
            DomDocument* doc = uicon->document;
            if (!doc) { log_warn("event_sim: webview_wait_load - no document (skipped)"); break; }
            View* view = ev->target_selector ? find_element_by_selector(doc, ev->target_selector, ev->target_index) : nullptr;
            if (!view || !view->is_element()) {
                log_warn("event_sim: webview_wait_load - element '%s' not found (skipped)", ev->target_selector);
                break;
            }
            ViewBlock* block = (ViewBlock*)view;
            if (block->tag_id != HTM_TAG_WEBVIEW || !block->embed || !block->embed->webview) {
                log_warn("event_sim: webview_wait_load - '%s' has no active webview (headless?), skipped", ev->target_selector);
                break;
            }
            // for now, just log — actual load waiting requires async polling
            // which the event sim doesn't support (it's synchronous)
            log_info("event_sim: webview_wait_load for '%s' (timeout=%dms)", ev->target_selector, ev->wait_ms);
            break;
        }
    }
}

// Auto-waiting wrapper: retries assertion events until they pass or timeout expires
static void process_sim_event_with_retry(EventSimContext* ctx, SimEvent* ev, UiContext* uicon, GLFWwindow* window) {
    // Non-assertion events execute directly
    if (ev->type < SIM_EVENT_ASSERT_CARET || ev->type > SIM_EVENT_ASSERT_SNAPSHOT) {
        process_sim_event(ctx, ev, uicon, window);
        return;
    }

    // Determine effective timeout
    int timeout = ev->assert_timeout > 0 ? ev->assert_timeout : ctx->default_timeout;
    if (timeout <= 0) {
        process_sim_event(ctx, ev, uicon, window);
        return;
    }

    int interval = ev->assert_interval > 0 ? ev->assert_interval : 100;
    int elapsed = 0;

    while (true) {
        int saved_pass = ctx->pass_count;
        int saved_fail = ctx->fail_count;

        process_sim_event(ctx, ev, uicon, window);

        // Passed if pass_count increased without fail_count increasing
        if (ctx->pass_count > saved_pass && ctx->fail_count == saved_fail) {
            return;
        }

        // Timeout expired — keep the failure
        if (elapsed >= timeout) {
            return;
        }

        // Restore counts and retry after sleeping
        ctx->pass_count = saved_pass;
        ctx->fail_count = saved_fail;

        struct timespec ts;
        ts.tv_sec = interval / 1000;
        ts.tv_nsec = (interval % 1000) * 1000000L;
        nanosleep(&ts, NULL);
        elapsed += interval;

        log_info("event_sim: auto-wait retry after %dms (timeout=%dms)", elapsed, timeout);
    }
}

bool event_sim_update(EventSimContext* ctx, void* uicon_ptr, GLFWwindow* window, double current_time) {
    UiContext* uicon = (UiContext*)uicon_ptr;
    if (!ctx || !ctx->is_running) return false;
    if (ctx->current_index >= ctx->events->length) {
        ctx->is_running = false;
        return false;
    }

    // Check if it's time for the next event
    if (current_time < ctx->next_event_time) {
        return true;  // Still running, waiting
    }

    // Process current event (with auto-wait retry for assertions)
    SimEvent* ev = (SimEvent*)ctx->events->data[ctx->current_index];
    process_sim_event_with_retry(ctx, ev, uicon, window);

    // Calculate wait time for next event
    int wait_ms = 50;  // default 50ms between events
    if (ev->type == SIM_EVENT_WAIT) {
        wait_ms = ev->wait_ms;
    }

    ctx->next_event_time = current_time + (wait_ms / 1000.0);
    ctx->current_index++;

    // Check if done
    if (ctx->current_index >= ctx->events->length) {
        ctx->is_running = false;
        log_info("event_sim: simulation complete");
        event_sim_print_results(ctx);
        return false;
    }

    return true;
}

void event_sim_print_results(EventSimContext* ctx) {
    if (!ctx) return;

    int total = ctx->pass_count + ctx->fail_count;
    fprintf(stderr, "\n");
    fprintf(stderr, "========================================\n");
    fprintf(stderr, " EVENT SIMULATION RESULTS\n");
    if (ctx->test_name) {
        fprintf(stderr, " Test: %s\n", ctx->test_name);
    }
    fprintf(stderr, "========================================\n");
    fprintf(stderr, " Events executed: %d\n", ctx->current_index);
    fprintf(stderr, " Assertions: %d passed, %d failed\n", ctx->pass_count, ctx->fail_count);
    if (total > 0) {
        fprintf(stderr, " Result: %s\n", ctx->fail_count == 0 ? "PASS" : "FAIL");
    }
    fprintf(stderr, "========================================\n");

    // Log file as well
    log_info("event_sim: results - %d events, %d assertions passed, %d failed",
             ctx->current_index, ctx->pass_count, ctx->fail_count);
}
