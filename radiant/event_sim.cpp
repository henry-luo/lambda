/**
 * Event Simulation Implementation
 */

#include "event_sim.hpp"
#include "event.hpp"
#include "event_state_log.hpp"
#include "state_store.hpp"
#include "state_machine.hpp"
#include "dom_range.hpp"
#include "editing.hpp"
#include "form_control.hpp"
#include "render.hpp"
#include "render_export_support.hpp"
#include "render_img.hpp"
#include "text_control.hpp"
#include "text_edit.hpp"
#include "view.hpp"
#include "webview.h"
#include "../lib/tagged.hpp"
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
#include "clipboard.hpp"
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <cstring>
#include <ctime>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef ERROR
#endif

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

static void sim_input_turn_yield() {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 16 * 1000 * 1000L;
    nanosleep(&ts, NULL);
}

// Forward declarations for callbacks (defined in window.cpp)
extern void handle_event(UiContext* uicon, DomDocument* doc, RdtEvent* event);
extern bool radiant_editing_animation_tick(UiContext* uicon, double timestamp);
extern "C" bool radiant_dispatch_editing_text_drag_drop(UiContext* uicon,
                                                         View* source,
                                                         uint32_t source_start,
                                                         uint32_t source_end,
                                                         View* target,
                                                         uint32_t target_start,
                                                         uint32_t target_end,
                                                         const char* payload,
                                                         const char* html_payload,
                                                         bool move);

// Forward declaration for parse_json
void parse_json(Input* input, const char* json_string);

static bool g_replay_assert_state = false;

void event_sim_set_replay_assert_state(bool assert_state) {
    g_replay_assert_state = assert_state;
}

static EventSimContext* event_sim_create_context() {
    EventSimContext* ctx = (EventSimContext*)mem_calloc(1, sizeof(EventSimContext), MEM_CAT_LAYOUT);
    if (!ctx) return NULL;
    ctx->events = arraylist_new(16);
    ctx->current_index = 0;
    ctx->next_event_time = 0;
    ctx->is_running = true;
    ctx->auto_close = true;
    ctx->pass_count = 0;
    ctx->fail_count = 0;
    ctx->test_name = NULL;
    ctx->default_timeout = 2000;
    ctx->replay_assert_state = g_replay_assert_state;
    ctx->replay_expected_focus_id = -1;
    ctx->replay_expected_caret_id = -1;
    ctx->replay_expected_caret_offset = -1;
    return ctx;
}

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
        View* child = static_cast<View*>(elem->first_child);
        while (child) {
            if (find_text_position_recursive(child, target_text, child_block_abs_x, child_block_abs_y, out_x, out_y)) {
                return true;
            }
            child = static_cast<View*>(child->next_sibling);
        }
    }

    return false;
}

// Find position of text in document
static bool find_text_position(DomDocument* doc, const char* target_text, float* out_x, float* out_y) {
    if (!doc || !doc->view_tree || !doc->view_tree->root) return false;
    return find_text_position_recursive(static_cast<View*>(doc->view_tree->root), target_text, 0, 0, out_x, out_y);
}

static View* find_text_view_recursive(View* view, const char* target_text) {
    if (!view || !target_text) return nullptr;
    if (view->view_type == RDT_VIEW_TEXT) {
        DomText* text_view = view->as_text();
        if (text_view && text_view->text && strstr(text_view->text, target_text)) {
            return view;
        }
    }
    DomElement* elem = view->as_element();
    if (elem) {
        View* child = static_cast<View*>(elem->first_child);
        while (child) {
            View* found = find_text_view_recursive(child, target_text);
            if (found) return found;
            child = static_cast<View*>(child->next_sibling);
        }
    }
    return nullptr;
}

static View* find_text_view(DomDocument* doc, const char* target_text) {
    if (!doc || !doc->view_tree || !doc->view_tree->root) return nullptr;
    return find_text_view_recursive(static_cast<View*>(doc->view_tree->root),
                                    target_text);
}

static View* find_element_by_selector(DomDocument* doc, const char* selector_text,
                                      int index);

static View* first_text_descendant(View* view) {
    if (!view) return nullptr;
    if (view->view_type == RDT_VIEW_TEXT) return view;
    DomElement* elem = view->as_element();
    if (elem) {
        DomNode* child = elem->first_child;
        while (child) {
            View* found = first_text_descendant(static_cast<View*>(child));
            if (found) return found;
            child = child->next_sibling;
        }
    }
    return nullptr;
}

static View* resolve_editing_range_view(DomDocument* doc, SimEvent* ev,
                                        EditingSurface* out_surface) {
    if (out_surface) editing_surface_clear(out_surface);
    if (!doc || !ev) return nullptr;
    View* view = nullptr;
    if (ev->target_selector) {
        view = find_element_by_selector(doc, ev->target_selector,
                                        ev->target_index);
    } else if (ev->target_text) {
        view = find_text_view(doc, ev->target_text);
    }
    if (!view) return nullptr;

    EditingSurface surface;
    if (!editing_surface_from_target(view, &surface)) return nullptr;
    if (out_surface) *out_surface = surface;
    if (editing_surface_is_text_control(&surface)) {
        tc_ensure_init(surface.owner);
        return surface.view ? surface.view : view;
    }
    if (editing_surface_is_rich(&surface)) {
        if (view->view_type == RDT_VIEW_TEXT) return view;
        View* text = first_text_descendant(view);
        return text ? text : view;
    }
    return view;
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
        View* child = static_cast<View*>(elem->first_child);
        while (child) {
            if (!sim_traverse_views(child, visitor, udata)) return false;
            child = static_cast<View*>(child->next_sibling);
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
    DomElement* dom_elem = lam::dom_require_element(view);
    if (selector_matcher_matches(ctx->matcher, ctx->selector, dom_elem, NULL)) {
        if (ctx->current_match == ctx->target_index) {
            ctx->result = view;
            return false;  // stop on target match
        }
        ctx->current_match++;
    }
    return true;
}

static void sim_flush_pending_reflow(DomDocument* doc) {
    if (!doc || !doc->state) return;
    DocState* state = (DocState*)doc->state;
    if (!state->needs_reflow) return;
    reflow_process_pending(state);
    if (state->needs_reflow) {
        extern void reflow_html_doc(DomDocument* doc);
        reflow_html_doc(doc);
        doc_state_clear_reflow(state);
    }
}

// Find nth element matching a CSS selector in the document (0-based index)
static View* find_element_by_selector(DomDocument* doc, const char* selector_text, int index = 0) {
    if (!doc || !doc->view_tree || !doc->view_tree->root || !selector_text) return NULL;
    sim_flush_pending_reflow(doc);

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
    state_configure_selector_matcher((DocState*)doc->state, matcher);

    SimSelectorCtx ctx = {0};
    ctx.selector = selector;
    ctx.matcher = matcher;
    ctx.result = NULL;
    ctx.target_index = index;
    ctx.current_match = 0;

    sim_traverse_views(static_cast<View*>(doc->view_tree->root), sim_selector_visitor, &ctx);
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
    DomElement* dom_elem = lam::dom_require_element(view);
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
    state_configure_selector_matcher((DocState*)doc->state, matcher);

    SimCountCtx ctx = {0};
    ctx.selector = selector;
    ctx.matcher = matcher;
    ctx.count = 0;

    sim_traverse_views(static_cast<View*>(doc->view_tree->root), sim_count_visitor, &ctx);
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
        current = static_cast<View*>(current->parent);
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
        current = static_cast<View*>(current->parent);
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
        if (elem->in_line && elem->in_line->has_color) {
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
    // outline-* 
    if (strcmp(property, "outline-style") == 0) {
        if (elem->bound && elem->bound->outline) {
            strbuf_append_str(buf, serialize_css_enum(elem->bound->outline->style));
        } else {
            strbuf_append_str(buf, "none");
        }
        return true;
    }
    if (strcmp(property, "outline-width") == 0) {
        if (elem->bound && elem->bound->outline) {
            snprintf(tmp, sizeof(tmp), "%.4gpx", elem->bound->outline->width);
            strbuf_append_str(buf, tmp);
        } else {
            strbuf_append_str(buf, "0px");
        }
        return true;
    }
    if (strcmp(property, "outline-color") == 0) {
        if (elem->bound && elem->bound->outline) {
            serialize_color(elem->bound->outline->color, buf);
        } else {
            strbuf_append_str(buf, "rgba(0, 0, 0, 0)");
        }
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
        View* child = static_cast<View*>(lam::dom_require_element(root)->last_child);
        while (child) {
            View* found = find_element_at(child, abs_x, abs_y, view_abs_x, view_abs_y);
            if (found && found->is_element()) return found;
            child = static_cast<View*>(child->prev_sibling);
        }
    }

    return root->is_element() ? root : nullptr;
}

// Check if an element is a checkbox/radio input
static bool sim_is_checkbox_or_radio(View* view) {
    if (!view || !view->is_element()) return false;
    ViewElement* elem = lam::view_require_element(view);
    if (elem->tag() != HTM_TAG_INPUT) return false;
    const char* type = elem->get_attribute("type");
    return type && (strcmp(type, "checkbox") == 0 || strcmp(type, "radio") == 0);
}

// writer-backed toggle of checkbox/radio state for selector-resolved controls.
// Used when event simulator clicks a selector-resolved form control.
static void sim_toggle_checkbox_radio(View* input, DocState* state) {
    if (!input || !input->is_element()) return;
    ViewElement* elem = lam::view_require_element(input);
    const char* type = elem->get_attribute("type");
    if (!type) return;

    if (strcmp(type, "checkbox") == 0) {
        bool is_checked = form_control_get_checked(state, input);
        bool new_state = !is_checked;
        form_control_set_checked(state, input, new_state);
        log_info("event_sim: toggled checkbox to %s", new_state ? "checked" : "unchecked");
    }
    else if (strcmp(type, "radio") == 0) {
        bool is_checked = form_control_get_checked(state, input);
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
                        ViewElement* se = lam::view_require_element(search);
                        if (se->tag() == HTM_TAG_INPUT) {
                            const char* st = se->get_attribute("type");
                            const char* sn = se->get_attribute("name");
                            if (st && strcmp(st, "radio") == 0 && sn && strcmp(sn, name) == 0) {
                                if (form_control_get_checked(state, search)) {
                                    form_control_uncheck_radio_group_peer(state, search);
                                }
                            }
                        }
                    }
                    // Depth-first traversal
                    if (search->is_element()) {
                        DomElement* de = search->as_element();
                        if (de->first_child) { search = static_cast<View*>(de->first_child); continue; }
                    }
                    if (search->next()) { search = search->next(); continue; }
                    search = search->parent;
                    while (search && !search->next()) search = search->parent;
                    if (search) search = search->next();
                }
            }
            // Check this radio
            form_control_set_checked(state, input, true);
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
            sim_extract_text(static_cast<View*>(child), buf);
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
            if (ev->has_target_offset) {
                float ex, ey, ew, eh;
                get_element_rect_abs(elem, &ex, &ey, &ew, &eh);
                *out_x = (int)(ex + (float)ev->target_offset_x);
                *out_y = (int)(ey + (float)ev->target_offset_y);
                log_info("event_sim: resolved selector '%s'[%d] + offset (%d,%d) to (%d, %d)",
                         ev->target_selector, ev->target_index,
                         ev->target_offset_x, ev->target_offset_y, *out_x, *out_y);
                return true;
            }
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
        // optional pixel offset from element top-left, applied when a
        // selector resolves (so tests can target a specific spot inside
        // a wide element such as <input type="text">).
        if (target_map.has("offset_x") || target_map.has("offset_y")) {
            ev->target_offset_x = target_map.get("offset_x").asInt32();
            ev->target_offset_y = target_map.get("offset_y").asInt32();
            ev->has_target_offset = true;
        }
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
        parse_target(reader, ev);
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
    else if (strcmp(type_str, "editing_text_drag_drop") == 0) {
        ev->type = SIM_EVENT_EDITING_TEXT_DRAG_DROP;
        parse_target(reader, ev);
        ItemReader target_item = reader.get("target");
        if (target_item.isMap()) {
            MapReader target_map = target_item.asMap();
            ev->drag_source_start = target_map.get("start").asInt32();
            ev->drag_source_end = target_map.has("end")
                ? target_map.get("end").asInt32()
                : ev->drag_source_start;
        }
        ev->drag_source_start = reader.has("source_start")
            ? reader.get("source_start").asInt32()
            : ev->drag_source_start;
        ev->drag_source_end = reader.has("source_end")
            ? reader.get("source_end").asInt32()
            : ev->drag_source_end;
        ItemReader to_target_item = reader.get("to_target");
        if (to_target_item.isMap()) {
            MapReader to_map = to_target_item.asMap();
            const char* sel = to_map.get("selector").cstring();
            if (sel) ev->to_target_selector = mem_strdup(sel, MEM_CAT_LAYOUT);
            const char* txt = to_map.get("text").cstring();
            if (txt) ev->to_target_text = mem_strdup(txt, MEM_CAT_LAYOUT);
            ev->drag_target_start = to_map.get("start").asInt32();
            ev->drag_target_end = to_map.has("end")
                ? to_map.get("end").asInt32()
                : ev->drag_target_start;
        }
        ev->drag_target_start = reader.has("target_start")
            ? reader.get("target_start").asInt32()
            : ev->drag_target_start;
        ev->drag_target_end = reader.has("target_end")
            ? reader.get("target_end").asInt32()
            : ev->drag_target_end;
        const char* text = reader.get("text").cstring();
        if (text) ev->input_text = mem_strdup(text, MEM_CAT_LAYOUT);
        const char* html = reader.get("html").cstring();
        if (html) ev->clipboard_html = mem_strdup(html, MEM_CAT_LAYOUT);
        ev->drag_move = reader.has("move") ? reader.get("move").asBool() : true;
        if (!ev->target_selector && !ev->target_text) {
            log_error("event_sim: editing_text_drag_drop missing 'target'");
            mem_free(ev);
            return NULL;
        }
        if (!ev->to_target_selector && !ev->to_target_text) {
            log_error("event_sim: editing_text_drag_drop missing 'to_target'");
            mem_free(ev);
            return NULL;
        }
    }
    else if (strcmp(type_str, "fuzz_schema") == 0) {
        ev->type = SIM_EVENT_FUZZ_SCHEMA;
        if (reader.has("steps")) ev->fuzz_steps = reader.get("steps").asInt32();
        if (reader.has("seed")) {
            const char* seed_text = reader.get("seed").cstring();
            if (seed_text &&
                (strcmp(seed_text, "random") == 0 || strcmp(seed_text, "time") == 0)) {
                ev->fuzz_seed = 0;
            } else {
                int seed = reader.get("seed").asInt32();
                ev->fuzz_seed = (uint32_t)seed; // INT_CAST_OK: optional replay seed is stored as unsigned LCG state.
            }
        }
        if (ev->fuzz_steps <= 0) ev->fuzz_steps = 32;
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
    else if (strcmp(type_str, "assert_form_selection") == 0) {
        ev->type = SIM_EVENT_ASSERT_FORM_SELECTION;
        ev->expected_char_offset = reader.get("start").asInt32();
        ev->expected_selection_end = reader.get("end").asInt32();
        parse_target(reader, ev);
    }
    else if (strcmp(type_str, "assert_editing_selection") == 0) {
        ev->type = SIM_EVENT_ASSERT_EDITING_SELECTION;
        ev->expected_char_offset = reader.get("start").asInt32();
        ev->expected_selection_end = reader.has("end")
            ? reader.get("end").asInt32()
            : ev->expected_char_offset;
        parse_target(reader, ev);
        if (!ev->target_selector && !ev->target_text) {
            log_error("event_sim: assert_editing_selection requires target");
            mem_free(ev);
            return NULL;
        }
    }
    else if (strcmp(type_str, "assert_preedit") == 0) {
        ev->type = SIM_EVENT_ASSERT_PREEDIT;
        const char* equals = reader.get("equals").cstring();
        if (equals) ev->assert_equals = mem_strdup(equals, MEM_CAT_LAYOUT);
        const char* contains = reader.get("contains").cstring();
        if (contains) ev->assert_contains = mem_strdup(contains, MEM_CAT_LAYOUT);
        parse_target(reader, ev);
    }
    else if (strcmp(type_str, "assert_password_reveal") == 0) {
        ev->type = SIM_EVENT_ASSERT_PASSWORD_REVEAL;
        ev->expected_password_reveal_active = reader.get("active").asBool();
        ev->expected_char_offset = reader.has("start") ? reader.get("start").asInt32() : -1;
        ev->expected_selection_end = reader.has("end") ? reader.get("end").asInt32() : -1;
        parse_target(reader, ev);
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
    else if (strcmp(type_str, "tripleclick") == 0) {
        // F2 (Radiant_Design_Form_Input.md §4.1): three rapid clicks at the
        // same location. Reuses SIM_EVENT_DBLCLICK plumbing with click_count=3.
        ev->type = SIM_EVENT_DBLCLICK;
        ev->x = reader.get("x").asInt32();
        ev->y = reader.get("y").asInt32();
        ev->button = reader.get("button").asInt32();
        ev->click_count = 3;
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
    // F6 (Radiant_Design_Form_Input.md §3.6): clipboard helpers.
    else if (strcmp(type_str, "paste_text") == 0) {
        ev->type = SIM_EVENT_PASTE_TEXT;
        const char* text = reader.get("text").cstring();
        if (text) ev->input_text = mem_strdup(text, MEM_CAT_LAYOUT);
        const char* html = reader.get("html").cstring();
        if (html) ev->clipboard_html = mem_strdup(html, MEM_CAT_LAYOUT);
        parse_target(reader, ev);
    }
    else if (strcmp(type_str, "assert_clipboard") == 0) {
        ev->type = SIM_EVENT_ASSERT_CLIPBOARD;
        const char* equals = reader.get("equals").cstring();
        if (equals) ev->assert_equals = mem_strdup(equals, MEM_CAT_LAYOUT);
        const char* contains = reader.get("contains").cstring();
        if (contains) ev->assert_contains = mem_strdup(contains, MEM_CAT_LAYOUT);
        const char* mime = reader.get("mime").cstring();
        if (mime) ev->clipboard_mime = mem_strdup(mime, MEM_CAT_LAYOUT);
    }
    // F7 (Radiant_Design_Form_Input.md §4.1): IME composition driver.
    //   {"type":"ime_compose","phase":"begin"}
    //   {"type":"ime_compose","phase":"update","preedit":"か","caret":1}
    //   {"type":"ime_compose","phase":"commit","commit":"日本"}
    //   {"type":"ime_compose","phase":"cancel"}
    // Optional `target` selects/focuses an input first.
    else if (strcmp(type_str, "ime_compose") == 0) {
        ev->type = SIM_EVENT_IME_COMPOSE;
        const char* phase = reader.get("phase").cstring();
        ev->ime_phase = mem_strdup(phase ? phase : "update", MEM_CAT_LAYOUT);
        const char* preedit = reader.get("preedit").cstring();
        if (preedit) ev->input_text = mem_strdup(preedit, MEM_CAT_LAYOUT);
        const char* commit = reader.get("commit").cstring();
        if (commit && !ev->input_text) {
            ev->input_text = mem_strdup(commit, MEM_CAT_LAYOUT);
        }
        ev->expected_char_offset = reader.get("caret").asInt32();
        parse_target(reader, ev);
    }
    else if (strcmp(type_str, "set_editing_selection") == 0) {
        ev->type = SIM_EVENT_SET_EDITING_SELECTION;
        ev->editing_selection_start = reader.get("start").asInt32();
        ev->editing_selection_end = reader.has("end")
            ? reader.get("end").asInt32()
            : ev->editing_selection_start;
        parse_target(reader, ev);
        if (!ev->target_selector && !ev->target_text) {
            log_error("event_sim: set_editing_selection requires target");
            mem_free(ev);
            return NULL;
        }
    }
    else if (strcmp(type_str, "set_editing_value") == 0) {
        ev->type = SIM_EVENT_SET_EDITING_VALUE;
        const char* text = reader.get("text").cstring();
        ev->input_text = mem_strdup(text ? text : "", MEM_CAT_LAYOUT);
        parse_target(reader, ev);
        if (!ev->target_selector && !ev->target_text) {
            log_error("event_sim: set_editing_value requires target");
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
    else if (strcmp(type_str, "assert_editing_value") == 0) {
        ev->type = SIM_EVENT_ASSERT_EDITING_VALUE;
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
        if (reader.has("x")) {
            ItemReader value = reader.get("x");
            ev->expected_rect_x = (float)(value.isFloat() ? value.asFloat() : value.asInt());
            ev->has_rect_x = true;
        }
        if (reader.has("y")) {
            ItemReader value = reader.get("y");
            ev->expected_rect_y = (float)(value.isFloat() ? value.asFloat() : value.asInt());
            ev->has_rect_y = true;
        }
        if (reader.has("width")) {
            ItemReader value = reader.get("width");
            ev->expected_rect_w = (float)(value.isFloat() ? value.asFloat() : value.asInt());
            ev->has_rect_w = true;
        }
        if (reader.has("height")) {
            ItemReader value = reader.get("height");
            ev->expected_rect_h = (float)(value.isFloat() ? value.asFloat() : value.asInt());
            ev->has_rect_h = true;
        }
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
    else if (strcmp(type_str, "assert_state_store") == 0) {
        ev->type = SIM_EVENT_ASSERT_STATE_STORE;
        parse_target(reader, ev);
        if (reader.has("view_state")) {
            ev->has_expected_view_state = true;
            ev->expected_view_state_exists = reader.get("view_state").asBool();
        }
        if (reader.has("view_state_count")) {
            ev->has_expected_view_state_count = true;
            ev->expected_view_state_count = reader.get("view_state_count").asInt32();
        }
        const char* kind = reader.get("kind").cstring();
        if (kind) ev->expected_view_state_kind = mem_strdup(kind, MEM_CAT_LAYOUT);
        if (reader.has("weak_ref")) {
            ev->has_expected_weak_ref = true;
            ev->expected_weak_ref = reader.get("weak_ref").asBool();
        }
        if (reader.has("active_target")) {
            ev->has_expected_active_target = true;
            ev->expected_active_target = reader.get("active_target").asBool();
        }
        if (reader.has("drag_target")) {
            ev->has_expected_drag_target = true;
            ev->expected_drag_target = reader.get("drag_target").asBool();
        }
        if (reader.has("drag_active")) {
            ev->has_expected_drag_active = true;
            ev->expected_drag_active = reader.get("drag_active").asBool();
        }
        if (reader.has("drag_drop")) {
            ev->has_expected_drag_drop = true;
            ev->expected_drag_drop = reader.get("drag_drop").asBool();
        }
        if (reader.has("drag_drop_pending")) {
            ev->has_expected_drag_drop_pending = true;
            ev->expected_drag_drop_pending = reader.get("drag_drop_pending").asBool();
        }
        if (reader.has("drag_drop_active")) {
            ev->has_expected_drag_drop_active = true;
            ev->expected_drag_drop_active = reader.get("drag_drop_active").asBool();
        }
        if (reader.has("drag_drop_source")) {
            ev->has_expected_drag_drop_source = true;
            ev->expected_drag_drop_source = reader.get("drag_drop_source").asBool();
        }
        if (reader.has("drag_drop_target")) {
            ev->has_expected_drag_drop_target = true;
            ev->expected_drag_drop_target = reader.get("drag_drop_target").asBool();
        }
        if (reader.has("open_dropdown")) {
            ev->has_expected_open_dropdown = true;
            ev->expected_open_dropdown = reader.get("open_dropdown").asBool();
        }
        if (reader.has("scrollbar_h_hovered")) {
            ev->has_expected_scrollbar_h_hovered = true;
            ev->expected_scrollbar_h_hovered = reader.get("scrollbar_h_hovered").asBool();
        }
        if (reader.has("scrollbar_v_hovered")) {
            ev->has_expected_scrollbar_v_hovered = true;
            ev->expected_scrollbar_v_hovered = reader.get("scrollbar_v_hovered").asBool();
        }
        if (reader.has("scrollbar_h_dragging")) {
            ev->has_expected_scrollbar_h_dragging = true;
            ev->expected_scrollbar_h_dragging = reader.get("scrollbar_h_dragging").asBool();
        }
        if (reader.has("scrollbar_v_dragging")) {
            ev->has_expected_scrollbar_v_dragging = true;
            ev->expected_scrollbar_v_dragging = reader.get("scrollbar_v_dragging").asBool();
        }
        if (reader.has("doc_scroll_x")) {
            ev->has_expected_doc_scroll_x = true;
            ItemReader sx = reader.get("doc_scroll_x");
            ev->expected_doc_scroll_x = (float)(sx.isFloat() ? sx.asFloat() : sx.asInt());
        }
        if (reader.has("doc_scroll_y")) {
            ev->has_expected_doc_scroll_y = true;
            ItemReader sy = reader.get("doc_scroll_y");
            ev->expected_doc_scroll_y = (float)(sy.isFloat() ? sy.asFloat() : sy.asInt());
        }
        if (reader.has("view_scroll_x")) {
            ev->has_expected_view_scroll_x = true;
            ItemReader sx = reader.get("view_scroll_x");
            ev->expected_view_scroll_x = (float)(sx.isFloat() ? sx.asFloat() : sx.asInt());
        }
        if (reader.has("view_scroll_y")) {
            ev->has_expected_view_scroll_y = true;
            ItemReader sy = reader.get("view_scroll_y");
            ev->expected_view_scroll_y = (float)(sy.isFloat() ? sy.asFloat() : sy.asInt());
        }
        if (reader.has("dropdown_x")) {
            ev->has_expected_dropdown_x = true;
            ItemReader sx = reader.get("dropdown_x");
            ev->expected_dropdown_x = (float)(sx.isFloat() ? sx.asFloat() : sx.asInt());
        }
        if (reader.has("dropdown_y")) {
            ev->has_expected_dropdown_y = true;
            ItemReader sy = reader.get("dropdown_y");
            ev->expected_dropdown_y = (float)(sy.isFloat() ? sy.asFloat() : sy.asInt());
        }
        if (reader.has("dropdown_width")) {
            ev->has_expected_dropdown_width = true;
            ItemReader sw = reader.get("dropdown_width");
            ev->expected_dropdown_width = (float)(sw.isFloat() ? sw.asFloat() : sw.asInt());
        }
        if (reader.has("dropdown_height")) {
            ev->has_expected_dropdown_height = true;
            ItemReader sh = reader.get("dropdown_height");
            ev->expected_dropdown_height = (float)(sh.isFloat() ? sh.asFloat() : sh.asInt());
        }
        {
            ItemReader st = reader.get("tolerance");
            ev->scroll_tolerance = (float)(st.isFloat() ? st.asFloat() : st.asInt());
        }
        if (ev->scroll_tolerance <= 0) ev->scroll_tolerance = 1.0f;
    }
    else if (strcmp(type_str, "assert_event_log") == 0) {
        ev->type = SIM_EVENT_ASSERT_EVENT_LOG;
        const char* contains = reader.get("contains").cstring();
        if (contains) ev->assert_contains = mem_strdup(contains, MEM_CAT_LAYOUT);
        ev->assert_count_expected = -1;
        ev->assert_count_min = -1;
        ev->assert_count_max = -1;
        if (reader.has("count")) ev->assert_count_expected = reader.get("count").asInt32();
        if (reader.has("equals")) ev->assert_count_expected = reader.get("equals").asInt32();
        if (reader.has("min")) ev->assert_count_min = reader.get("min").asInt32();
        if (reader.has("max")) ev->assert_count_max = reader.get("max").asInt32();
        if (!ev->assert_contains) {
            log_error("event_sim: assert_event_log requires 'contains'");
            mem_free(ev);
            return NULL;
        }
    }
    else if (strcmp(type_str, "assert_state_dump") == 0) {
        ev->type = SIM_EVENT_ASSERT_STATE_DUMP;
        const char* ref = reader.get("reference").cstring();
        if (ref) ev->state_dump_reference = mem_strdup(ref, MEM_CAT_LAYOUT);
        else {
            log_error("event_sim: assert_state_dump requires 'reference' field");
            mem_free(ev);
            return NULL;
        }
    }
    else if (strcmp(type_str, "assert_editing_event") == 0) {
        ev->type = SIM_EVENT_ASSERT_EDITING_EVENT;
        const char* event_type = reader.get("event").cstring();
        if (!event_type) event_type = reader.get("record").cstring();
        if (event_type) ev->editing_event_type = mem_strdup(event_type, MEM_CAT_LAYOUT);
        const char* input_type = reader.get("inputType").cstring();
        if (!input_type) input_type = reader.get("input_type").cstring();
        if (input_type) ev->editing_input_type = mem_strdup(input_type, MEM_CAT_LAYOUT);
        const char* surface = reader.get("surface").cstring();
        if (!surface) surface = reader.get("surface_kind").cstring();
        if (surface) ev->editing_surface_kind = mem_strdup(surface, MEM_CAT_LAYOUT);
        const char* mode = reader.get("mode").cstring();
        if (!mode) mode = reader.get("surface_mode").cstring();
        if (mode) ev->editing_surface_mode = mem_strdup(mode, MEM_CAT_LAYOUT);
        const char* operation = reader.get("operation").cstring();
        if (operation) ev->editing_operation = mem_strdup(operation, MEM_CAT_LAYOUT);
        const char* owned_by = reader.get("owned_by").cstring();
        if (owned_by) ev->editing_owned_by = mem_strdup(owned_by, MEM_CAT_LAYOUT);
        if (reader.has("prevented")) {
            ev->has_expected_prevented = true;
            ev->expected_prevented = reader.get("prevented").asBool();
        }
        if (reader.has("redacted")) {
            ev->has_expected_redacted = true;
            ev->expected_redacted = reader.get("redacted").asBool();
        }
        ev->assert_count_expected = -1;
        ev->assert_count_min = -1;
        ev->assert_count_max = -1;
        if (reader.has("count")) ev->assert_count_expected = reader.get("count").asInt32();
        if (reader.has("equals")) ev->assert_count_expected = reader.get("equals").asInt32();
        if (reader.has("min")) ev->assert_count_min = reader.get("min").asInt32();
        if (reader.has("max")) ev->assert_count_max = reader.get("max").asInt32();
        if (!ev->editing_event_type) {
            log_error("event_sim: assert_editing_event requires 'event'");
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
    else if (strcmp(type_str, "render_pending") == 0) {
        ev->type = SIM_EVENT_RENDER_PENDING;
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
    else if (strcmp(type_str, "assert_pixel") == 0) {
        ev->type = SIM_EVENT_ASSERT_PIXEL;
        parse_target(reader, ev);
        if (reader.has("x")) ev->x = reader.get("x").asInt32();
        if (reader.has("y")) ev->y = reader.get("y").asInt32();
        ev->pixel_min_r = ev->pixel_min_g = ev->pixel_min_b = ev->pixel_min_a = -1;
        ev->pixel_max_r = ev->pixel_max_g = ev->pixel_max_b = ev->pixel_max_a = -1;
        ev->pixel_force_render = reader.has("force_render") ? reader.get("force_render").asBool() : true;
        if (reader.has("min_r")) ev->pixel_min_r = reader.get("min_r").asInt32();
        if (reader.has("min_g")) ev->pixel_min_g = reader.get("min_g").asInt32();
        if (reader.has("min_b")) ev->pixel_min_b = reader.get("min_b").asInt32();
        if (reader.has("min_a")) ev->pixel_min_a = reader.get("min_a").asInt32();
        if (reader.has("max_r")) ev->pixel_max_r = reader.get("max_r").asInt32();
        if (reader.has("max_g")) ev->pixel_max_g = reader.get("max_g").asInt32();
        if (reader.has("max_b")) ev->pixel_max_b = reader.get("max_b").asInt32();
        if (reader.has("max_a")) ev->pixel_max_a = reader.get("max_a").asInt32();
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
    EventSimContext* ctx = event_sim_create_context();
    if (!ctx) return NULL;

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

    // Parse optional default_timeout (ms) for auto-waiting assertions.
    // Default 500ms ensures assertions reliably auto-wait for the prior input
    // event to propagate, even under heavy parallel CPU load (e.g. when the
    // gtest runner spawns many lambda.exe processes concurrently).
    ctx->default_timeout = root_map.get("default_timeout").asInt32();
    if (ctx->default_timeout <= 0) {
        ctx->default_timeout = 2000;
    }
    log_info("event_sim: default_timeout %dms", ctx->default_timeout);

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

static char* replay_document_path_from_url(const char* url) {
    if (!url) return NULL;
    if (strncmp(url, "file://", 7) == 0) return mem_strdup(url + 7, MEM_CAT_LAYOUT);
    if (strncmp(url, "file:", 5) == 0) return mem_strdup(url + 5, MEM_CAT_LAYOUT);
    return mem_strdup(url, MEM_CAT_LAYOUT);
}

static bool replay_parse_json_line(const char* jsonl_file, const char* line,
                                   ItemReader* out_root, MarkReader** out_doc) {
    if (!line || !line[0]) return false;
    Url* url = url_parse(jsonl_file);
    Input* input = InputManager::create_input(url);
    if (!input) return false;
    parse_json(input, line);
    if (input->root.item == 0) return false;
    MarkReader* doc = new MarkReader(input->root);
    ItemReader root = doc->getRoot();
    if (!root.isMap()) {
        delete doc;
        return false;
    }
    *out_root = root;
    *out_doc = doc;
    return true;
}

static void replay_parse_expected_snapshot(EventSimContext* ctx, MapReader& root_map) {
    if (!ctx) return;
    ItemReader data_item = root_map.get("data");
    if (!data_item.isMap()) return;
    MapReader data = data_item.asMap();
    ctx->replay_has_expected_state = true;

    ItemReader focus_item = data.get("focus");
    if (focus_item.isMap()) {
        MapReader focus = focus_item.asMap();
        ItemReader target_item = focus.get("target");
        ctx->replay_expected_focus_id = 0;
        if (target_item.isMap()) {
            MapReader target = target_item.asMap();
            ctx->replay_expected_focus_id = target.get("id").asInt32();
        }
    }

    ItemReader caret_item = data.get("caret");
    if (caret_item.isMap()) {
        MapReader caret = caret_item.asMap();
        ItemReader target_item = caret.get("target");
        ctx->replay_expected_caret_id = 0;
        if (target_item.isMap()) {
            MapReader target = target_item.asMap();
            ctx->replay_expected_caret_id = target.get("id").asInt32();
        }
        if (caret.has("offset")) {
            ctx->replay_expected_caret_offset = caret.get("offset").asInt32();
            ctx->replay_has_caret_offset = true;
        } else {
            ctx->replay_expected_caret_offset = -1;
            ctx->replay_has_caret_offset = false;
        }
    }

    ItemReader selection_item = data.get("selection");
    if (selection_item.isMap()) {
        MapReader selection = selection_item.asMap();
        if (selection.has("is_collapsed")) {
            ctx->replay_expected_selection_collapsed = selection.get("is_collapsed").asBool();
            ctx->replay_has_selection_collapsed = true;
        }
    }

    ItemReader doc_state_item = data.get("document_state");
    if (doc_state_item.isMap()) {
        MapReader doc_state = doc_state_item.asMap();
        ctx->replay_expected_scroll_x = (float)doc_state.get("scroll_x").asFloat();
        ctx->replay_expected_scroll_y = (float)doc_state.get("scroll_y").asFloat();
        ctx->replay_has_scroll = true;
    }
}

static SimEvent* replay_input_raw_to_event(MapReader& data) {
    const char* event_name = data.get("event").cstring();
    if (!event_name) return NULL;
    SimEvent* ev = (SimEvent*)mem_calloc(1, sizeof(SimEvent), MEM_CAT_LAYOUT);
    if (!ev) return NULL;
    ev->type = SIM_EVENT_REPLAY_INPUT;
    ev->replay_event_name = mem_strdup(event_name, MEM_CAT_LAYOUT);
    ev->x = data.get("x").asInt32();
    ev->y = data.get("y").asInt32();
    ev->button = data.has("button") ? data.get("button").asInt32() : 0;
    ev->mods = data.has("mods") ? data.get("mods").asInt32() : 0;
    ev->click_count = data.has("clicks") ? data.get("clicks").asInt32() : 1;
    ev->key = data.get("key").asInt32();
    ev->replay_scancode = data.get("scancode").asInt32();
    ev->scroll_dx = (float)data.get("xoffset").asFloat();
    ev->scroll_dy = (float)data.get("yoffset").asFloat();
    ev->replay_codepoint = (uint32_t)data.get("codepoint").asInt32();
    ev->replay_preedit_caret = (uint32_t)data.get("preedit_caret").asInt32();
    const char* text = data.get("text").cstring();
    if (text) ev->input_text = mem_strdup(text, MEM_CAT_LAYOUT);
    return ev;
}

static void replay_attach_hit_target(SimEvent* ev, MapReader& data) {
    if (!ev) return;
    ItemReader target_item = data.get("target");
    if (!target_item.isMap()) return;
    MapReader target = target_item.asMap();
    const char* author_id = target.get("author_id").cstring();
    const char* stable_id = target.get("stable_id").cstring();
    const char* selector_id = author_id;
    if (!selector_id && stable_id && strncmp(stable_id, "id:", 3) == 0) selector_id = stable_id + 3;
    if (selector_id && !ev->target_selector) {
        size_t len = strlen(selector_id) + 2;
        char* selector = (char*)mem_alloc(len, MEM_CAT_LAYOUT);
        if (selector) {
            snprintf(selector, len, "#%s", selector_id);
            ev->target_selector = selector;
        }
    }
    if (data.has("offset_x") || data.has("offset_y")) {
        ev->target_offset_x = (int)data.get("offset_x").asFloat();
        ev->target_offset_y = (int)data.get("offset_y").asFloat();
        ev->has_target_offset = true;
    }
}

char* event_sim_replay_document_path(const char* jsonl_file) {
    char* content = read_text_file(jsonl_file);
    if (!content) return NULL;
    char* line = content;
    char* doc_path = NULL;
    while (line && *line && !doc_path) {
        char* next = strchr(line, '\n');
        if (next) *next = '\0';
        ItemReader root;
        MarkReader* doc = NULL;
        if (replay_parse_json_line(jsonl_file, line, &root, &doc)) {
            MapReader root_map = root.asMap();
            const char* type = root_map.get("type").cstring();
            if (type && strcmp(type, "session_start") == 0) {
                ItemReader document_item = root_map.get("document");
                if (document_item.isMap()) {
                    MapReader document = document_item.asMap();
                    doc_path = replay_document_path_from_url(document.get("url").cstring());
                }
            }
            delete doc;
        }
        line = next ? next + 1 : NULL;
    }
    mem_free(content);
    return doc_path;
}

EventSimContext* event_sim_load_replay_log(const char* jsonl_file) {
    log_info("event_sim: loading replay log '%s'", jsonl_file);
    char* content = read_text_file(jsonl_file);
    if (!content) {
        log_error("event_sim: failed to read replay log '%s'", jsonl_file);
        return NULL;
    }

    EventSimContext* ctx = event_sim_create_context();
    if (!ctx) {
        mem_free(content);
        return NULL;
    }
    ctx->test_name = mem_strdup("Replay log", MEM_CAT_LAYOUT);

    SimEvent* last_input = NULL;
    int line_no = 0;
    for (char* line = content; line && *line; ) {
        char* next = strchr(line, '\n');
        if (next) *next = '\0';
        line_no++;
        if (line[0]) {
            ItemReader root;
            MarkReader* doc = NULL;
            if (replay_parse_json_line(jsonl_file, line, &root, &doc)) {
                MapReader root_map = root.asMap();
                const char* type = root_map.get("type").cstring();
                if (type && strcmp(type, "session_start") == 0) {
                    ItemReader viewport_item = root_map.get("viewport");
                    if (viewport_item.isMap()) {
                        MapReader viewport = viewport_item.asMap();
                        ctx->viewport_width = viewport.get("w").asInt32();
                        ctx->viewport_height = viewport.get("h").asInt32();
                    }
                } else if (type && strcmp(type, "input.raw") == 0) {
                    ItemReader data_item = root_map.get("data");
                    if (data_item.isMap()) {
                        MapReader data = data_item.asMap();
                        SimEvent* ev = replay_input_raw_to_event(data);
                        if (ev) {
                            arraylist_append(ctx->events, ev);
                            last_input = ev;
                        }
                    }
                } else if (type && strcmp(type, "hit.target") == 0) {
                    ItemReader data_item = root_map.get("data");
                    if (data_item.isMap()) {
                        MapReader data = data_item.asMap();
                        replay_attach_hit_target(last_input, data);
                    }
                } else if (type && strcmp(type, "state.snapshot") == 0) {
                    replay_parse_expected_snapshot(ctx, root_map);
                }
                delete doc;
            } else {
                log_warn("event_sim: skipped invalid replay JSONL line %d", line_no);
            }
        }
        line = next ? next + 1 : NULL;
    }

    mem_free(content);
    log_info("event_sim: loaded %d replay input events from %s", ctx->events->length, jsonl_file);
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
            if (ev->clipboard_mime) mem_free(ev->clipboard_mime);
            if (ev->clipboard_html) mem_free(ev->clipboard_html);
            if (ev->option_value) mem_free(ev->option_value);
            if (ev->option_label) mem_free(ev->option_label);
            if (ev->state_name) mem_free(ev->state_name);
            if (ev->style_property) mem_free(ev->style_property);
            if (ev->element_a_selector) mem_free(ev->element_a_selector);
            if (ev->element_a_text) mem_free(ev->element_a_text);
            if (ev->element_b_selector) mem_free(ev->element_b_selector);
            if (ev->element_b_text) mem_free(ev->element_b_text);
            if (ev->position_relation) mem_free(ev->position_relation);
            if (ev->navigate_url) mem_free(ev->navigate_url);
            if (ev->expected_at_selector) mem_free(ev->expected_at_selector);
            if (ev->expected_at_tag) mem_free(ev->expected_at_tag);
            if (ev->attribute_name) mem_free(ev->attribute_name);
            if (ev->expected_view_state_kind) mem_free(ev->expected_view_state_kind);
            if (ev->snapshot_reference) mem_free(ev->snapshot_reference);
            if (ev->snapshot_diff_path) mem_free(ev->snapshot_diff_path);
            if (ev->snapshot_actual_path) mem_free(ev->snapshot_actual_path);
            if (ev->js_code) mem_free(ev->js_code);
            if (ev->frame_selector) mem_free(ev->frame_selector);
            if (ev->ime_phase) mem_free(ev->ime_phase);
            if (ev->editing_event_type) mem_free(ev->editing_event_type);
            if (ev->editing_input_type) mem_free(ev->editing_input_type);
            if (ev->editing_surface_kind) mem_free(ev->editing_surface_kind);
            if (ev->editing_surface_mode) mem_free(ev->editing_surface_mode);
            if (ev->editing_operation) mem_free(ev->editing_operation);
            if (ev->editing_owned_by) mem_free(ev->editing_owned_by);
            if (ev->replay_event_name) mem_free(ev->replay_event_name);
            if (ev->state_dump_reference) mem_free(ev->state_dump_reference);
            mem_free(ev);
        }
        arraylist_free(ctx->events);
    }

    if (ctx->test_name) mem_free(ctx->test_name);
    if (ctx->result_file) fclose(ctx->result_file);
    mem_free(ctx);
}

static int count_substring_occurrences(const char* haystack, const char* needle) {
    if (!haystack || !needle || !needle[0]) return 0;
    int count = 0;
    size_t needle_len = strlen(needle);
    const char* scan = haystack;
    while ((scan = strstr(scan, needle)) != NULL) {
        count++;
        scan += needle_len;
    }
    return count;
}

static bool sim_line_contains(const char* line, size_t len, const char* needle) {
    if (!line || !needle) return false;
    size_t needle_len = strlen(needle);
    if (needle_len == 0) return true;
    if (needle_len > len) return false;
    size_t limit = len - needle_len;
    for (size_t i = 0; i <= limit; i++) {
        if (line[i] == needle[0] && strncmp(line + i, needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
}

static bool sim_line_contains_str_field(const char* line, size_t len,
                                        const char* key,
                                        const char* value) {
    if (!value) return true;
    char needle[256];
    snprintf(needle, sizeof(needle), "\"%s\":\"%s\"", key, value);
    return sim_line_contains(line, len, needle);
}

static bool sim_line_contains_bool_field(const char* line, size_t len,
                                         const char* key,
                                         bool value) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":%s",
             key, value ? "true" : "false");
    return sim_line_contains(line, len, needle);
}

static bool sim_editing_event_line_matches(const char* line, size_t len,
                                           SimEvent* ev) {
    if (!sim_line_contains_str_field(line, len, "type",
                                     ev->editing_event_type)) {
        return false;
    }
    if (ev->editing_input_type) {
        bool has_snake = sim_line_contains_str_field(line, len, "input_type",
                                                     ev->editing_input_type);
        bool has_camel = sim_line_contains_str_field(line, len, "inputType",
                                                     ev->editing_input_type);
        if (!has_snake && !has_camel) return false;
    }
    if (!sim_line_contains_str_field(line, len, "kind",
                                     ev->editing_surface_kind)) {
        return false;
    }
    if (!sim_line_contains_str_field(line, len, "mode",
                                     ev->editing_surface_mode)) {
        return false;
    }
    if (!sim_line_contains_str_field(line, len, "operation",
                                     ev->editing_operation)) {
        return false;
    }
    if (!sim_line_contains_str_field(line, len, "owned_by",
                                     ev->editing_owned_by)) {
        return false;
    }
    if (ev->has_expected_prevented &&
        !sim_line_contains_bool_field(line, len, "prevented",
                                      ev->expected_prevented)) {
        return false;
    }
    if (ev->has_expected_redacted &&
        !sim_line_contains_bool_field(line, len, "redacted",
                                      ev->expected_redacted)) {
        return false;
    }
    return true;
}

static int count_editing_event_matches(const char* content, SimEvent* ev) {
    if (!content || !ev) return 0;
    int count = 0;
    const char* line = content;
    while (*line) {
        const char* next = strchr(line, '\n');
        size_t len = next ? (size_t)(next - line) : strlen(line);
        if (sim_editing_event_line_matches(line, len, ev)) count++;
        if (!next) break;
        line = next + 1;
    }
    return count;
}

static void assert_event_log_impl(EventSimContext* ctx, UiContext* uicon, SimEvent* ev) {
    if (!ctx || !uicon || !ev) return;
    EventStateLog* event_log = uicon->event_log;
    if (!event_state_log_enabled(event_log)) {
        log_info("event_sim: assert_event_log SKIP - event log disabled");
        ctx->pass_count++;
        return;
    }

    const char* path = event_state_log_path(event_log);
    if (!path) {
        log_error("event_sim: assert_event_log FAIL - event log has no path");
        ctx->fail_count++;
        return;
    }

    char* content = read_text_file(path);
    if (!content) {
        log_error("event_sim: assert_event_log FAIL - cannot read '%s'", path);
        ctx->fail_count++;
        return;
    }

    int actual = count_substring_occurrences(content, ev->assert_contains);
    int expected = ev->assert_count_expected;
    int min_count = ev->assert_count_min;
    int max_count = ev->assert_count_max;
    if (expected < 0 && min_count < 0 && max_count < 0) min_count = 1;

    bool passed = true;
    if (expected >= 0 && actual != expected) {
        log_error("event_sim: assert_event_log FAIL - '%s' expected %d, got %d",
            ev->assert_contains, expected, actual);
        passed = false;
    }
    if (min_count >= 0 && actual < min_count) {
        log_error("event_sim: assert_event_log FAIL - '%s' expected min %d, got %d",
            ev->assert_contains, min_count, actual);
        passed = false;
    }
    if (max_count >= 0 && actual > max_count) {
        log_error("event_sim: assert_event_log FAIL - '%s' expected max %d, got %d",
            ev->assert_contains, max_count, actual);
        passed = false;
    }

    if (passed) {
        log_info("event_sim: assert_event_log PASS - '%s' count=%d",
            ev->assert_contains, actual);
        ctx->pass_count++;
    } else {
        ctx->fail_count++;
    }

    mem_free(content);
}

static bool sim_state_dump_update_enabled(void) {
    const char* env = getenv("RADIANT_UPDATE_STATE_DUMPS");
    return env && (strcmp(env, "1") == 0 || strcasecmp(env, "true") == 0 ||
                   strcasecmp(env, "yes") == 0);
}

static StrBuf* sim_normalize_state_dump_text(const char* text) {
    StrBuf* out = strbuf_new_cap(text ? strlen(text) + 1 : 1);
    if (!out) return NULL;
    if (!text) return out;

    const char* p = text;
    while (*p) {
        const char* end = strchr(p, '\n');
        if (!end) end = p + strlen(p);

        const char* trim = end;
        if (trim > p && trim[-1] == '\r') trim--;
        while (trim > p && (trim[-1] == ' ' || trim[-1] == '\t')) trim--;

        for (const char* q = p; q < trim; q++) {
            if (*q != '\r') strbuf_append_char(out, *q);
        }
        if (*end == '\n') {
            strbuf_append_char(out, '\n');
            p = end + 1;
        } else {
            p = end;
        }
    }

    while (out->length > 0) {
        char c = out->str[out->length - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        out->length--;
    }
    if (out->str) out->str[out->length] = '\0';
    return out;
}

static size_t sim_first_diff_offset(const char* a, const char* b) {
    if (!a) a = "";
    if (!b) b = "";
    size_t i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return i;
}

static int sim_line_number_at(const char* text, size_t offset) {
    if (!text) return 1;
    int line = 1;
    for (size_t i = 0; text[i] && i < offset; i++) {
        if (text[i] == '\n') line++;
    }
    return line;
}

static void sim_copy_line_at(const char* text, size_t offset, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!text) return;

    size_t len = strlen(text);
    if (offset > len) offset = len;
    size_t start = offset;
    while (start > 0 && text[start - 1] != '\n') start--;
    size_t end = offset;
    while (text[end] && text[end] != '\n') end++;

    size_t copy_len = end > start ? end - start : 0;
    if (copy_len >= out_sz) copy_len = out_sz - 1;
    memcpy(out, text + start, copy_len);
    out[copy_len] = '\0';
}

static void assert_state_dump_impl(EventSimContext* ctx, UiContext* uicon, SimEvent* ev) {
    if (!ctx || !uicon || !ev) return;
    DomDocument* doc = uicon->document;
    if (!doc) {
        log_error("event_sim: assert_state_dump FAIL - no document");
        ctx->fail_count++;
        return;
    }

    DocState* state = doc->state ? (DocState*)doc->state :
        radiant_document_ensure_state(doc, "event_sim:assert_state_dump");
    if (!state) {
        log_error("event_sim: assert_state_dump FAIL - document has no DocState");
        ctx->fail_count++;
        return;
    }

    StrBuf* actual = radiant_state_dump_mark(state);
    if (!actual) {
        log_error("event_sim: assert_state_dump FAIL - could not build state dump");
        ctx->fail_count++;
        return;
    }

    StrBuf* actual_norm = sim_normalize_state_dump_text(actual->str ? actual->str : "");
    strbuf_free(actual);
    if (!actual_norm) {
        log_error("event_sim: assert_state_dump FAIL - could not normalize actual dump");
        ctx->fail_count++;
        return;
    }

    const char* ref_path = ev->state_dump_reference;
    bool update = sim_state_dump_update_enabled();
    char* expected_text = file_exists(ref_path) ? read_text_file(ref_path) : NULL;
    if (!expected_text) {
        if (update) {
            int rc = write_text_file_atomic(ref_path, actual_norm->str ? actual_norm->str : "");
            if (rc == 0) {
                log_info("event_sim: assert_state_dump UPDATE - wrote %s", ref_path);
                ctx->pass_count++;
            } else {
                log_error("event_sim: assert_state_dump FAIL - could not write %s", ref_path);
                ctx->fail_count++;
            }
        } else {
            log_error("event_sim: assert_state_dump FAIL - cannot read '%s'", ref_path);
            ctx->fail_count++;
        }
        strbuf_free(actual_norm);
        return;
    }

    StrBuf* expected_norm = sim_normalize_state_dump_text(expected_text);
    mem_free(expected_text);
    if (!expected_norm) {
        log_error("event_sim: assert_state_dump FAIL - could not normalize fixture '%s'", ref_path);
        strbuf_free(actual_norm);
        ctx->fail_count++;
        return;
    }

    const char* actual_str = actual_norm->str ? actual_norm->str : "";
    const char* expected_str = expected_norm->str ? expected_norm->str : "";
    bool same = strcmp(actual_str, expected_str) == 0;
    if (!same && update) {
        int rc = write_text_file_atomic(ref_path, actual_str);
        if (rc == 0) {
            log_info("event_sim: assert_state_dump UPDATE - refreshed %s", ref_path);
            same = true;
        } else {
            log_error("event_sim: assert_state_dump FAIL - could not refresh %s", ref_path);
        }
    }

    if (same) {
        log_info("event_sim: assert_state_dump PASS - %s", ref_path);
        ctx->pass_count++;
    } else {
        size_t offset = sim_first_diff_offset(expected_str, actual_str);
        int line = sim_line_number_at(expected_str, offset);
        char expected_line[256];
        char actual_line[256];
        sim_copy_line_at(expected_str, offset, expected_line, sizeof(expected_line));
        sim_copy_line_at(actual_str, offset, actual_line, sizeof(actual_line));
        log_error("event_sim: assert_state_dump FAIL - mismatch in %s at line %d",
                  ref_path, line);
        log_error("event_sim: assert_state_dump expected: %s", expected_line);
        log_error("event_sim: assert_state_dump actual:   %s", actual_line);
        ctx->fail_count++;
    }

    strbuf_free(expected_norm);
    strbuf_free(actual_norm);
}

static void assert_editing_event_impl(EventSimContext* ctx, UiContext* uicon,
                                      SimEvent* ev) {
    if (!ctx || !uicon || !ev) return;
    EventStateLog* event_log = uicon->event_log;
    if (!event_state_log_enabled(event_log)) {
        log_info("event_sim: assert_editing_event SKIP - event log disabled");
        ctx->pass_count++;
        return;
    }

    const char* path = event_state_log_path(event_log);
    if (!path) {
        log_error("event_sim: assert_editing_event FAIL - event log has no path");
        ctx->fail_count++;
        return;
    }

    char* content = read_text_file(path);
    if (!content) {
        log_error("event_sim: assert_editing_event FAIL - cannot read '%s'", path);
        ctx->fail_count++;
        return;
    }

    int actual = count_editing_event_matches(content, ev);
    int expected = ev->assert_count_expected;
    int min_count = ev->assert_count_min;
    int max_count = ev->assert_count_max;
    if (expected < 0 && min_count < 0 && max_count < 0) min_count = 1;

    bool passed = true;
    if (expected >= 0 && actual != expected) {
        log_error("event_sim: assert_editing_event FAIL - event '%s' expected %d, got %d",
                  ev->editing_event_type, expected, actual);
        passed = false;
    }
    if (min_count >= 0 && actual < min_count) {
        log_error("event_sim: assert_editing_event FAIL - event '%s' expected min %d, got %d",
                  ev->editing_event_type, min_count, actual);
        passed = false;
    }
    if (max_count >= 0 && actual > max_count) {
        log_error("event_sim: assert_editing_event FAIL - event '%s' expected max %d, got %d",
                  ev->editing_event_type, max_count, actual);
        passed = false;
    }

    if (passed) {
        log_info("event_sim: assert_editing_event PASS - event '%s' count=%d",
                 ev->editing_event_type, actual);
        ctx->pass_count++;
    } else {
        ctx->fail_count++;
    }
    mem_free(content);
}

// Simulate a mouse move event
static void sim_mouse_move(UiContext* uicon, int x, int y) {
    RdtEvent event;
    event.mouse_position.type = RDT_EVENT_MOUSE_MOVE;
    event.mouse_position.timestamp = get_monotonic_time();
    event.mouse_position.x = x;
    event.mouse_position.y = y;
    handle_event(uicon, uicon->document, &event);
    sim_input_turn_yield();
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
    sim_input_turn_yield();
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
    sim_input_turn_yield();
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
    sim_input_turn_yield();
}

static void sim_replay_input(UiContext* uicon, SimEvent* ev) {
    if (!uicon || !ev || !ev->replay_event_name) return;
    RdtEvent event;
    memset(&event, 0, sizeof(event));
    double now = get_monotonic_time();

    if (strcmp(ev->replay_event_name, "mouse_down") == 0 ||
        strcmp(ev->replay_event_name, "mouse_up") == 0) {
        int x, y;
        if (!resolve_target(ev, uicon->document, &x, &y)) return;
        event.mouse_button.type = strcmp(ev->replay_event_name, "mouse_down") == 0 ?
            RDT_EVENT_MOUSE_DOWN : RDT_EVENT_MOUSE_UP;
        event.mouse_button.timestamp = now;
        event.mouse_button.x = x;
        event.mouse_button.y = y;
        event.mouse_button.button = (uint8_t)ev->button;
        event.mouse_button.clicks = (uint8_t)(ev->click_count > 0 ? ev->click_count : 1);
        event.mouse_button.mods = ev->mods;
    } else if (strcmp(ev->replay_event_name, "mouse_move") == 0 ||
               strcmp(ev->replay_event_name, "mouse_drag") == 0) {
        int x, y;
        if (!resolve_target(ev, uicon->document, &x, &y)) return;
        event.mouse_position.type = strcmp(ev->replay_event_name, "mouse_drag") == 0 ?
            RDT_EVENT_MOUSE_DRAG : RDT_EVENT_MOUSE_MOVE;
        event.mouse_position.timestamp = now;
        event.mouse_position.x = x;
        event.mouse_position.y = y;
    } else if (strcmp(ev->replay_event_name, "scroll") == 0) {
        event.scroll.type = RDT_EVENT_SCROLL;
        event.scroll.timestamp = now;
        event.scroll.x = ev->x;
        event.scroll.y = ev->y;
        event.scroll.xoffset = ev->scroll_dx;
        event.scroll.yoffset = ev->scroll_dy;
    } else if (strcmp(ev->replay_event_name, "key_down") == 0 ||
               strcmp(ev->replay_event_name, "key_up") == 0) {
        event.key.type = strcmp(ev->replay_event_name, "key_down") == 0 ?
            RDT_EVENT_KEY_DOWN : RDT_EVENT_KEY_UP;
        event.key.timestamp = now;
        event.key.key = ev->key;
        event.key.scancode = ev->replay_scancode;
        event.key.mods = ev->mods;
    } else if (strcmp(ev->replay_event_name, "text_input") == 0) {
        event.text_input.type = RDT_EVENT_TEXT_INPUT;
        event.text_input.timestamp = now;
        event.text_input.codepoint = ev->replay_codepoint;
    } else if (strcmp(ev->replay_event_name, "composition_start") == 0 ||
               strcmp(ev->replay_event_name, "composition_update") == 0 ||
               strcmp(ev->replay_event_name, "composition_end") == 0) {
        if (strcmp(ev->replay_event_name, "composition_start") == 0) {
            event.composition.type = RDT_EVENT_COMPOSITION_START;
        } else if (strcmp(ev->replay_event_name, "composition_update") == 0) {
            event.composition.type = RDT_EVENT_COMPOSITION_UPDATE;
        } else {
            event.composition.type = RDT_EVENT_COMPOSITION_END;
        }
        event.composition.timestamp = now;
        event.composition.text = ev->input_text ? ev->input_text : "";
        event.composition.preedit_caret = ev->replay_preedit_caret;
    } else {
        log_warn("event_sim: unsupported replay input.raw event '%s'", ev->replay_event_name);
        return;
    }

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

    bool passed = true;
    View* caret_view = caret_get_view(doc->state);

    if (ev->expected_view_type >= 0) {
        if (ev->negate_view_type) {
            // pass if there is no caret, or caret view type is not the rejected type
            bool has_forbidden = caret_view && caret_view->view_type == ev->expected_view_type;
            if (has_forbidden) {
                log_error("event_sim: assert_caret - view_type should NOT be %d, but it is",
                         ev->expected_view_type);
                passed = false;
            }
        } else {
            // Check view type of caret view
            if (!caret_view) {
                log_error("event_sim: assert_caret - no caret view");
                passed = false;
            } else {
                ViewType actual_type = caret_view->view_type;
                if (actual_type != ev->expected_view_type) {
                    log_error("event_sim: assert_caret - view_type mismatch: expected %d, got %d",
                             ev->expected_view_type, actual_type);
                    passed = false;
                }
            }
        }
    }

    if (ev->expected_char_offset >= 0) {
        int caret_offset = 0;
        bool has_caret_offset = caret_get_offset(doc->state, &caret_offset);
        if (!has_caret_offset || caret_offset != ev->expected_char_offset) {
            log_error("event_sim: assert_caret - char_offset mismatch: expected %d, got %d",
                     ev->expected_char_offset, has_caret_offset ? caret_offset : -1);
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
// Checks StateStore's compatibility projection plus the canonical render
// selection. Text controls use EditingSelection; rich/document ranges use
// DomSelection.
static bool assert_selection(EventSimContext* ctx, UiContext* uicon, SimEvent* ev) {
    DomDocument* doc = uicon->document;
    if (!doc || !doc->state) {
        log_error("event_sim: assert_selection - no document or state");
        ctx->fail_count++;
        return false;
    }

    DocState* state = doc->state;
    bool legacy_collapsed = !selection_has(state);
    bool text_control_selection = state->sel.kind == EDIT_SEL_TEXT_CONTROL;
    bool canonical_collapsed = legacy_collapsed;
    if (text_control_selection) {
        canonical_collapsed = state->sel.start_u16 == state->sel.end_u16;
    } else {
        DomSelection* ds = state->dom_selection;
        canonical_collapsed = dom_selection_is_collapsed(ds);
    }


    if (legacy_collapsed != ev->expected_is_collapsed) {
        log_error("event_sim: assert_selection - legacy is_collapsed mismatch: expected %s, got %s",
                 ev->expected_is_collapsed ? "true" : "false",
                 legacy_collapsed ? "true" : "false");
        ctx->fail_count++;
        return false;
    }

    if (ev->check_dom_selection && canonical_collapsed != ev->expected_is_collapsed) {
        DomSelection* ds = state->dom_selection;
        log_error("event_sim: assert_selection - canonical is_collapsed mismatch: expected %s, got %s "
                 "(source=%s range_count=%u). Highlight will not render.",
                 ev->expected_is_collapsed ? "true" : "false",
                 canonical_collapsed ? "true" : "false",
                 text_control_selection ? "text-control" : "dom",
                 ds ? ds->range_count : 0u);
        ctx->fail_count++;
        return false;
    }

    log_info("event_sim: assert_selection PASS (legacy=%s%s)",
             legacy_collapsed ? "collapsed" : "non-collapsed",
             ev->check_dom_selection ?
                (canonical_collapsed ? ", canonical=collapsed" : ", canonical=non-collapsed") : "");
    ctx->pass_count++;
    return true;
}

static bool assert_form_selection(EventSimContext* ctx, UiContext* uicon, SimEvent* ev) {
    DomDocument* doc = uicon ? uicon->document : nullptr;
    if (!doc || !doc->state) {
        log_error("event_sim: assert_form_selection - no document or state");
        ctx->fail_count++;
        return false;
    }

    View* view = resolve_target_element(ev, doc);
    if (!view || !view->is_element()) {
        log_error("event_sim: assert_form_selection - target element not found");
        ctx->fail_count++;
        return false;
    }
    DomElement* elem = lam::dom_require_element(view);
    if (!tc_is_text_control(elem)) {
        log_error("event_sim: assert_form_selection - target is not a text control");
        ctx->fail_count++;
        return false;
    }

    uint32_t start = 0, end = 0;
    form_control_get_selection(doc->state, view, &start, &end, NULL);
    uint32_t expected_start = ev->expected_char_offset < 0 ? 0 : (uint32_t)ev->expected_char_offset;
    uint32_t expected_end = ev->expected_selection_end < 0 ? 0 : (uint32_t)ev->expected_selection_end;
    if (start != expected_start || end != expected_end) {
        log_error("event_sim: assert_form_selection - mismatch: expected [%u..%u], got [%u..%u]",
                  expected_start, expected_end, start, end);
        ctx->fail_count++;
        return false;
    }

    log_info("event_sim: assert_form_selection PASS [%u..%u]", start, end);
    ctx->pass_count++;
    return true;
}

static bool assert_editing_selection(EventSimContext* ctx, UiContext* uicon, SimEvent* ev) {
    DomDocument* doc = uicon ? uicon->document : nullptr;
    if (!doc || !doc->state) {
        log_error("event_sim: assert_editing_selection - no document or state");
        ctx->fail_count++;
        return false;
    }

    EditingSurface surface;
    View* range_view = resolve_editing_range_view(doc, ev, &surface);
    if (!range_view) {
        log_error("event_sim: assert_editing_selection - target editing range not found");
        ctx->fail_count++;
        return false;
    }

    uint32_t expected_start = ev->expected_char_offset < 0
        ? 0
        : (uint32_t)ev->expected_char_offset;
    uint32_t expected_end = ev->expected_selection_end < 0
        ? expected_start
        : (uint32_t)ev->expected_selection_end;

    uint32_t start = 0;
    uint32_t end = 0;
    if (editing_surface_is_text_control(&surface)) {
        form_control_get_selection(doc->state, range_view, &start, &end, NULL);
    } else {
        int rich_start = 0;
        int rich_end = 0;
        selection_get_range(doc->state, &rich_start, &rich_end);
        start = rich_start < 0 ? 0 : (uint32_t)rich_start;
        end = rich_end < 0 ? 0 : (uint32_t)rich_end;
    }

    if (start != expected_start || end != expected_end) {
        log_error("event_sim: assert_editing_selection - mismatch: expected [%u..%u], got [%u..%u]",
                  expected_start, expected_end, start, end);
        ctx->fail_count++;
        return false;
    }

    log_info("event_sim: assert_editing_selection PASS [%u..%u]", start, end);
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

    View* caret_view = caret_get_view(doc->state);

    if (!caret_view) {
        log_error("event_sim: assert_target - no caret view");
        ctx->fail_count++;
        return false;
    }

    ViewType actual_type = caret_view->view_type;
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
    if (uicon->document && uicon->document->view_tree) {
        DocState* state = (DocState*)uicon->document->state;
        if (state) doc_state_mark_dirty(state);
        render_html_doc(uicon, uicon->document->view_tree, nullptr);
        if (state) doc_state_clear_render_flags(state);
    }
}

static void render_pending_surface(UiContext* uicon) {
    if (!uicon || !uicon->document || !uicon->document->view_tree) return;
    DocState* state = (DocState*)uicon->document->state;
    if (!state) return;
    bool pending = state->is_dirty || state->needs_repaint ||
        state->needs_reflow || state->dirty_tracker.full_repaint ||
        state->dirty_tracker.full_reflow || dirty_has_regions(&state->dirty_tracker);
    if (!pending) return;
    if (state->needs_reflow) {
        reflow_process_pending(state);
        if (state->needs_reflow) {
            extern void reflow_html_doc(DomDocument* doc);
            reflow_html_doc(uicon->document);
            doc_state_clear_reflow(state);
        }
    }
    render_html_doc(uicon, uicon->document->view_tree, nullptr);
    doc_state_clear_render_flags(state);
}

static bool pixel_component_in_range(const char* name, int value, int min_value, int max_value) {
    if (min_value >= 0 && value < min_value) {
        log_error("event_sim: assert_pixel FAIL - %s=%d below min %d", name, value, min_value);
        return false;
    }
    if (max_value >= 0 && value > max_value) {
        log_error("event_sim: assert_pixel FAIL - %s=%d above max %d", name, value, max_value);
        return false;
    }
    return true;
}

static void assert_pixel_impl(EventSimContext* ctx, UiContext* uicon, SimEvent* ev) {
    DomDocument* doc = uicon ? uicon->document : NULL;
    if (ev->pixel_force_render && uicon) force_render_surface(uicon);

    ImageSurface* actual = uicon ? uicon->surface : NULL;
    if (!actual || !actual->pixels) {
        log_error("event_sim: assert_pixel FAIL - no rendered surface");
        ctx->fail_count++;
        return;
    }

    int x = 0, y = 0;
    if (!resolve_target(ev, doc, &x, &y)) {
        log_error("event_sim: assert_pixel FAIL - could not resolve target");
        ctx->fail_count++;
        return;
    }
    if (x < 0 || y < 0 || x >= actual->width || y >= actual->height) {
        log_error("event_sim: assert_pixel FAIL - coordinate (%d,%d) outside surface %dx%d",
                  x, y, actual->width, actual->height);
        ctx->fail_count++;
        return;
    }

    int stride = actual->pitch / 4; // INT_CAST_OK: pitch is bytes, pixel rows are uint32_t
    uint32_t* pixels = (uint32_t*)actual->pixels;
    uint32_t px = pixels[y * stride + x];
    int r = px & 0xFF;
    int g = (px >> 8) & 0xFF;
    int b = (px >> 16) & 0xFF;
    int a = (px >> 24) & 0xFF;

    bool ok = true;
    ok = pixel_component_in_range("r", r, ev->pixel_min_r, ev->pixel_max_r) && ok;
    ok = pixel_component_in_range("g", g, ev->pixel_min_g, ev->pixel_max_g) && ok;
    ok = pixel_component_in_range("b", b, ev->pixel_min_b, ev->pixel_max_b) && ok;
    ok = pixel_component_in_range("a", a, ev->pixel_min_a, ev->pixel_max_a) && ok;

    if (ok) {
        log_info("event_sim: assert_pixel PASS at (%d,%d) rgba=(%d,%d,%d,%d)",
                 x, y, r, g, b, a);
        ctx->pass_count++;
    } else {
        log_error("event_sim: assert_pixel FAIL at (%d,%d) rgba=(%d,%d,%d,%d)",
                  x, y, r, g, b, a);
        ctx->fail_count++;
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

static uint32_t sim_fuzz_next(uint32_t* state) {
    *state = (*state * 1664525u) + 1013904223u;
    return *state;
}

static uint32_t sim_fuzz_time_seed() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint32_t seed = (uint32_t)time(NULL); // INT_CAST_OK: fuzz seed intentionally folds wall-clock seconds.
    seed ^= (uint32_t)ts.tv_sec; // INT_CAST_OK: fuzz seed intentionally folds monotonic seconds.
    seed ^= (uint32_t)ts.tv_nsec; // INT_CAST_OK: fuzz seed intentionally folds monotonic nanoseconds.
    if (seed == 0) seed = 0x5eed1234u;
    return seed;
}

static bool event_sim_assert_schema(EventSimContext* ctx, UiContext* uicon,
                                    const char* phase) {
    DomDocument* doc = uicon ? uicon->document : NULL;
    DocState* state = doc ? doc->state : NULL;
    if (!state) {
        log_error("event_sim: schema conformance FAIL - no document state (%s)",
                  phase ? phase : "unknown");
        if (ctx) ctx->fail_count++;
        return false;
    }
    StateValidationReport report;
    bool ok = radiant_state_validate_interaction(state, &report);
    if (!ok) {
        log_error("event_sim: schema conformance FAIL after %s: %s",
                  phase ? phase : "event",
                  report.message[0] ? report.message : "interaction invariant failed");
        if (ctx) ctx->fail_count++;
        return false;
    }
    log_info("event_sim: schema conformance PASS after %s", phase ? phase : "event");
    if (ctx) ctx->pass_count++;
    return true;
}

static void event_sim_fuzz_schema(EventSimContext* ctx, UiContext* uicon, SimEvent* ev) {
    if (!ctx || !uicon || !ev) return;
    DomDocument* doc = uicon->document;
    if (!doc || !doc->state) {
        log_error("event_sim: fuzz_schema FAIL - no document state");
        ctx->fail_count++;
        return;
    }

    uint32_t rng = ev->fuzz_seed ? ev->fuzz_seed : sim_fuzz_time_seed();
    int steps = ev->fuzz_steps > 0 ? ev->fuzz_steps : 32;
    int width = uicon->viewport_width > 0 ? uicon->viewport_width : 800;
    int height = uicon->viewport_height > 0 ? uicon->viewport_height : 600;
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    log_info("event_sim: fuzz_schema begin steps=%d seed=%u viewport=%dx%d",
             steps, rng, width, height);
    for (int step = 0; step < steps; step++) {
        uint32_t op = sim_fuzz_next(&rng) % 7u;
        int x = (int)(sim_fuzz_next(&rng) % (uint32_t)width); // INT_CAST_OK: event coordinates are integer CSS pixels.
        int y = (int)(sim_fuzz_next(&rng) % (uint32_t)height); // INT_CAST_OK: event coordinates are integer CSS pixels.

        switch (op) {
            case 0:
                log_info("event_sim: fuzz_schema[%d] mouse_move (%d,%d)", step, x, y);
                sim_mouse_move(uicon, x, y);
                break;
            case 1:
                log_info("event_sim: fuzz_schema[%d] click (%d,%d)", step, x, y);
                sim_mouse_move(uicon, x, y);
                sim_mouse_button(uicon, x, y, 0, 0, true);
                sim_mouse_button(uicon, x, y, 0, 0, false);
                break;
            case 2: {
                float dy = (sim_fuzz_next(&rng) & 1u) ? 3.0f : -3.0f;
                log_info("event_sim: fuzz_schema[%d] scroll (%d,%d) dy=%.1f", step, x, y, dy);
                sim_scroll(uicon, x, y, 0.0f, dy);
                break;
            }
            case 3:
                log_info("event_sim: fuzz_schema[%d] key_tab", step);
                sim_key(uicon, GLFW_KEY_TAB, 0, true);
                sim_key(uicon, GLFW_KEY_TAB, 0, false);
                break;
            case 4:
                log_info("event_sim: fuzz_schema[%d] key_right", step);
                sim_key(uicon, GLFW_KEY_RIGHT, 0, true);
                sim_key(uicon, GLFW_KEY_RIGHT, 0, false);
                break;
            case 5:
                log_info("event_sim: fuzz_schema[%d] text_input", step);
                sim_text_input(uicon, (uint32_t)('a' + (sim_fuzz_next(&rng) % 26u)));
                break;
            default:
                log_info("event_sim: fuzz_schema[%d] key_backspace", step);
                sim_key(uicon, GLFW_KEY_BACKSPACE, 0, true);
                sim_key(uicon, GLFW_KEY_BACKSPACE, 0, false);
                break;
        }

        if (!event_sim_assert_schema(ctx, uicon, "fuzz_schema")) return;
    }
}

// Process a single simulated event
static void process_sim_event(EventSimContext* ctx, SimEvent* ev, UiContext* uicon, GLFWwindow* window) {
    switch (ev->type) {
        case SIM_EVENT_WAIT:
            log_info("event_sim: wait %d ms", ev->wait_ms);
            if (ev->wait_ms > 0) {
                struct timespec ts;
                ts.tv_sec = ev->wait_ms / 1000;
                ts.tv_nsec = (ev->wait_ms % 1000) * 1000000L;
                nanosleep(&ts, NULL);
            }
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
            // Get source and destination centers
            float src_fx, src_fy, dst_fx, dst_fy;
            get_element_center_abs(src_view, &src_fx, &src_fy);
            get_element_center_abs(dst_view, &dst_fx, &dst_fy);
            int src_x = (int)src_fx, src_y = (int)src_fy;
            int dst_x = (int)dst_fx, dst_y = (int)dst_fy;
            log_info("event_sim: drag_and_drop from '%s' (%d,%d) to '%s' (%d,%d)",
                ev->target_selector, src_x, src_y, ev->to_target_selector, dst_x, dst_y);
            // Dispatch: mouse_down on source
            sim_mouse_button(uicon, src_x, src_y, 0, 0, true);
            // Intermediate mouse_move steps
            int steps = ev->drag_steps > 0 ? ev->drag_steps : 5;
            for (int step = 1; step <= steps; step++) {
                int px = src_x + (dst_x - src_x) * step / steps;
                int py = src_y + (dst_y - src_y) * step / steps;
                sim_mouse_move(uicon, px, py);
            }
            // Drop: mouse_up on destination
            sim_mouse_button(uicon, dst_x, dst_y, 0, 0, false);
            log_info("event_sim: drag_and_drop completed");
            break;
        }

        case SIM_EVENT_EDITING_TEXT_DRAG_DROP: {
            DomDocument* doc = uicon->document;
            if (!doc) {
                log_error("event_sim: editing_text_drag_drop - no document");
                ctx->fail_count++;
                break;
            }
            View* src_view = nullptr;
            if (ev->target_selector) {
                src_view = find_element_by_selector(doc, ev->target_selector,
                                                    ev->target_index);
            } else if (ev->target_text) {
                src_view = find_text_view(doc, ev->target_text);
            }
            if (!src_view) {
                log_error("event_sim: editing_text_drag_drop - source not found");
                ctx->fail_count++;
                break;
            }
            View* dst_view = nullptr;
            if (ev->to_target_selector) {
                dst_view = find_element_by_selector(doc, ev->to_target_selector);
            } else if (ev->to_target_text) {
                dst_view = find_text_view(doc, ev->to_target_text);
            }
            if (!dst_view) {
                log_error("event_sim: editing_text_drag_drop - target not found");
                ctx->fail_count++;
                break;
            }
            uint32_t source_start = ev->drag_source_start < 0
                ? 0
                : (uint32_t)ev->drag_source_start;
            uint32_t source_end = ev->drag_source_end < 0
                ? source_start
                : (uint32_t)ev->drag_source_end;
            uint32_t target_start = ev->drag_target_start < 0
                ? 0
                : (uint32_t)ev->drag_target_start;
            uint32_t target_end = ev->drag_target_end < 0
                ? target_start
                : (uint32_t)ev->drag_target_end;
            log_info("event_sim: editing_text_drag_drop source=[%u..%u] target=[%u..%u] move=%d text_len=%zu html_len=%zu",
                     source_start, source_end, target_start, target_end,
                     ev->drag_move ? 1 : 0,
                     ev->input_text ? strlen(ev->input_text) : 0,
                     ev->clipboard_html ? strlen(ev->clipboard_html) : 0);
            bool ok = radiant_dispatch_editing_text_drag_drop(
                uicon, src_view, source_start, source_end,
                dst_view, target_start, target_end,
                ev->input_text, ev->clipboard_html, ev->drag_move);
            if (!ok) {
                log_error("event_sim: editing_text_drag_drop failed");
                ctx->fail_count++;
            }
            force_render_surface(uicon);
            break;
        }

        case SIM_EVENT_FUZZ_SCHEMA:
            event_sim_fuzz_schema(ctx, uicon, ev);
            break;

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

        case SIM_EVENT_REPLAY_INPUT:
            log_info("event_sim: replay input.raw '%s'", ev->replay_event_name ? ev->replay_event_name : "unknown");
            sim_replay_input(uicon, ev);
            if (ev->replay_event_name && strcmp(ev->replay_event_name, "scroll") == 0) force_render_surface(uicon);
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
                    DocState* state = doc ? (DocState*)doc->state : nullptr;
                    was_checked = state_get_pseudo_state(state, form_elem, PSEUDO_STATE_CHECKED);
                } else {
                    form_elem = nullptr;
                }
            }

            int x, y;
            if (!resolve_target(ev, uicon->document, &x, &y)) break;
            log_info("event_sim: click at (%d, %d) button=%d", x, y, ev->button);
            sim_mouse_move(uicon, x, y);
            sim_mouse_button(uicon, x, y, ev->button, ev->mods, true);
            sim_mouse_button(uicon, x, y, ev->button, ev->mods, false);

            // If the coordinate click didn't toggle the checkbox/radio, do it directly
            if (form_elem) {
                DocState* state = (DocState*)uicon->document->state;
                bool is_checked_now = state_get_pseudo_state(state, form_elem, PSEUDO_STATE_CHECKED);
                if (is_checked_now == was_checked) {
                    bool has_inline_click_handler = false;
                    if (form_elem->is_element()) {
                        DomElement* elem = lam::dom_require_element(form_elem);
                        has_inline_click_handler = dom_element_has_attribute(elem, "onclick");
                    }
                    // State didn't change — coordinate click may have missed the
                    // element. Do not synthesize a fallback toggle when an inline
                    // click handler is present; `return false` intentionally keeps
                    // checkbox/radio state unchanged.
                    if (!has_inline_click_handler && state &&
                        !state_get_pseudo_state(state, form_elem, PSEUDO_STATE_DISABLED)) {
                        sim_toggle_checkbox_radio(form_elem, state);
                    }
                }
            }
            break;
        }

        case SIM_EVENT_DBLCLICK: {
            int x, y;
            if (!resolve_target(ev, uicon->document, &x, &y)) break;
            // F2: click_count default = 2 (dblclick). 3 = tripleclick.
            int total_clicks = ev->click_count > 0 ? ev->click_count : 2;
            if (total_clicks < 2) total_clicks = 2;
            log_info("event_sim: %sclick at (%d, %d) total_clicks=%d",
                     total_clicks >= 3 ? "triple" : "dbl", x, y, total_clicks);
            // First (total_clicks - 1) plain clicks via sim_mouse_button.
            for (int c = 1; c < total_clicks; c++) {
                sim_mouse_button(uicon, x, y, ev->button, ev->mods, true);
                sim_mouse_button(uicon, x, y, ev->button, ev->mods, false);
            }
            // Final click carries clicks=total_clicks so handle_event can
            // dispatch dblclick/tripleclick semantics.
            sim_mouse_move(uicon, x, y);
            {
                RdtEvent event;
                event.mouse_button.type = RDT_EVENT_MOUSE_DOWN;
                event.mouse_button.timestamp = get_monotonic_time();
                event.mouse_button.x = x;
                event.mouse_button.y = y;
                event.mouse_button.button = ev->button;
                event.mouse_button.clicks = (uint8_t)total_clicks;
                event.mouse_button.mods = ev->mods;
                handle_event(uicon, uicon->document, &event);
                event.mouse_button.type = RDT_EVENT_MOUSE_UP;
                handle_event(uicon, uicon->document, &event);
            }
            break;
        }

        case SIM_EVENT_TYPE: {
            // Click target first to focus it — but skip the click if the target
            // is already focused, otherwise the click would collapse any existing
            // text selection (e.g. from a preceding tripleclick).
            if (ev->target_selector || ev->target_text) {
                View* target_elem = resolve_target_element(ev, uicon->document);
                DocState* state = uicon->document ? (DocState*)uicon->document->state : nullptr;
                bool already_focused = target_elem && target_elem->is_element() &&
                    state_get_pseudo_state(state, target_elem, PSEUDO_STATE_FOCUS);
                if (!already_focused) {
                    int x, y;
                    if (resolve_target(ev, uicon->document, &x, &y)) {
                        sim_mouse_button(uicon, x, y, 0, 0, true);
                        sim_mouse_button(uicon, x, y, 0, 0, false);
                    }
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
            DocState* state = (DocState*)doc->state;
            bool is_checked = state_get_pseudo_state(state, elem, PSEUDO_STATE_CHECKED);
            if (is_checked != ev->expected_checked) {
                if (state && !state_get_pseudo_state(state, elem, PSEUDO_STATE_DISABLED)) {
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
                   (lam::view_require_element(select_view))->tag() != HTM_TAG_SELECT) {
                select_view = select_view->parent;
            }
            if (!select_view || !select_view->is_element() ||
                (lam::view_require_element(select_view))->tag() != HTM_TAG_SELECT) {
                log_error("event_sim: select_option - target is not a <select> element");
                break;
            }
            ViewBlock* select = lam::view_require_block(select_view);
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
                    DomElement* child_elem = lam::dom_require_element(child);
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
                                    DomText* text = lam::dom_require_text(text_child);
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
                                DomElement* opt_elem = lam::dom_require_element(opt_child);
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
                                                DomText* text = lam::dom_require_text(text_child);
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
            DocState* state = (DocState*)doc->state;
            form_control_set_selected_index(state, static_cast<View*>(select), match_index);
            if (state) {
                // Close dropdown if open
                if (state->open_dropdown == select_view) {
                    doc_state_close_dropdown(state, static_cast<View*>(select));
                }
                doc_state_request_repaint(state);
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
            ui_context_create_surface(uicon, new_phys_w, new_phys_h);
            extern void reflow_html_doc(DomDocument* doc);
            if (uicon->document) {
                reflow_html_doc(uicon->document);
            }
            // Re-render after resize to update surface pixels
            force_render_surface(uicon);
            break;
        }

        // F6: paste sanitized text into the focused control. Mirrors a
        // user pressing Cmd+V after the OS clipboard has the given text.
        case SIM_EVENT_PASTE_TEXT: {
            if (ev->target_selector || ev->target_text) {
                int x, y;
                if (resolve_target(ev, uicon->document, &x, &y)) {
                    sim_mouse_button(uicon, x, y, 0, 0, true);
                    sim_mouse_button(uicon, x, y, 0, 0, false);
                }
            }
            const char* text = ev->input_text ? ev->input_text : "";
            log_info("event_sim: paste_text len=%zu", strlen(text));
            if (ev->clipboard_html) {
                clipboard_store_write_html(ev->clipboard_html, text);
            } else {
                clipboard_copy_text(text);
            }
            #ifdef __APPLE__
            sim_key(uicon, GLFW_KEY_V, RDT_MOD_SUPER, true);
            sim_key(uicon, GLFW_KEY_V, RDT_MOD_SUPER, false);
            #else
            sim_key(uicon, GLFW_KEY_V, RDT_MOD_CTRL, true);
            sim_key(uicon, GLFW_KEY_V, RDT_MOD_CTRL, false);
            #endif
            break;
        }

        case SIM_EVENT_ASSERT_CLIPBOARD: {
            const char* clip = ev->clipboard_mime
                ? clipboard_store_read_mime(ev->clipboard_mime)
                : clipboard_get_text();
            if (!clip) clip = "";
            bool passed = true;
            if (ev->assert_equals && strcmp(clip, ev->assert_equals) != 0) {
                log_error("event_sim: assert_clipboard equals fail: expected '%s', got '%s'",
                          ev->assert_equals, clip);
                passed = false;
            }
            if (ev->assert_contains && !strstr(clip, ev->assert_contains)) {
                log_error("event_sim: assert_clipboard contains fail: '%s' not in '%s'",
                          ev->assert_contains, clip);
                passed = false;
            }
            if (passed) {
                log_info("event_sim: assert_clipboard PASS");
                ctx->pass_count++;
            } else {
                ctx->fail_count++;
            }
            break;
        }

        // F7: drive the shared composition event path against the focused
        // editing surface. Mirrors the OS shim path without requiring
        // NSTextInputClient / IMM in the test runner.
        case SIM_EVENT_IME_COMPOSE: {
            DomDocument* doc = uicon->document;
            if (!doc || !doc->state) {
                log_error("event_sim: ime_compose - no document/state");
                ctx->fail_count++;
                break;
            }
            // Optional target focus: click to focus first.
            if (ev->target_selector || ev->target_text) {
                int x, y;
                if (resolve_target(ev, doc, &x, &y)) {
                    sim_mouse_button(uicon, x, y, 0, 0, true);
                    sim_mouse_button(uicon, x, y, 0, 0, false);
                }
            }
            DocState* state = (DocState*)doc->state;
            View* focused = focus_get(state);
            View* intent_target = focused ? focused : caret_get_view(state);
            EditingSurface surface;
            editing_surface_clear(&surface);
            if (state->editing.composition.active &&
                state->editing.composition.surface.kind != EDIT_SURFACE_NONE) {
                surface = state->editing.composition.surface;
            } else if (!intent_target ||
                !editing_surface_from_target(intent_target, &surface)) {
                log_error("event_sim: ime_compose - no focused editing surface");
                ctx->fail_count++;
                break;
            }
            if (!editing_surface_is_text_control(&surface) &&
                !editing_surface_is_rich(&surface)) {
                log_error("event_sim: ime_compose - no focused editing surface");
                ctx->fail_count++;
                break;
            }
            const char* phase = ev->ime_phase ? ev->ime_phase : "update";
            const char* data  = ev->input_text ? ev->input_text : "";
            RdtEvent event;
            memset(&event, 0, sizeof(event));
            event.composition.timestamp = get_monotonic_time();
            event.composition.text = data;
            event.composition.preedit_caret = (uint32_t)ev->expected_char_offset;
            if (strcmp(phase, "begin") == 0) {
                event.composition.type = RDT_EVENT_COMPOSITION_START;
                handle_event(uicon, doc, &event);
                log_info("event_sim: ime_compose begin");
            } else if (strcmp(phase, "update") == 0) {
                event.composition.type = RDT_EVENT_COMPOSITION_UPDATE;
                handle_event(uicon, doc, &event);
                log_info("event_sim: ime_compose update '%s'", data);
            } else if (strcmp(phase, "commit") == 0) {
                event.composition.type = RDT_EVENT_COMPOSITION_END;
                handle_event(uicon, doc, &event);
                log_info("event_sim: ime_compose commit '%s'", data);
            } else if (strcmp(phase, "cancel") == 0) {
                event.composition.type = RDT_EVENT_COMPOSITION_END;
                event.composition.text = "";
                handle_event(uicon, doc, &event);
                log_info("event_sim: ime_compose cancel");
            } else {
                log_error("event_sim: ime_compose - unknown phase '%s'", phase);
                ctx->fail_count++;
                break;
            }
            doc_state_request_repaint(state);
            break;
        }

        case SIM_EVENT_SET_EDITING_SELECTION: {
            DomDocument* doc = uicon->document;
            if (!doc || !doc->state) {
                log_error("event_sim: set_editing_selection - no document/state");
                ctx->fail_count++;
                break;
            }
            EditingSurface surface;
            View* range_view = resolve_editing_range_view(doc, ev, &surface);
            if (!range_view) {
                log_error("event_sim: set_editing_selection - target editing range not found");
                ctx->fail_count++;
                break;
            }
            uint32_t start = ev->editing_selection_start < 0
                ? 0
                : (uint32_t)ev->editing_selection_start;
            uint32_t end = ev->editing_selection_end < 0
                ? start
                : (uint32_t)ev->editing_selection_end;
            uint32_t len = 0;
            if (editing_surface_is_text_control(&surface)) {
                tc_ensure_init(surface.owner);
                len = surface.owner && surface.owner->form
                    ? surface.owner->form->current_value_len
                    : 0;
            } else if (range_view->view_type == RDT_VIEW_TEXT) {
                DomText* text = range_view->as_text();
                len = text && text->text ? (uint32_t)strlen(text->text) : 0;
            }
            if (start > len) start = len;
            if (end > len) end = len;
            View* focus_view = nullptr;
            if (editing_surface_is_text_control(&surface)) {
                focus_view = surface.owner;
            } else if (editing_surface_is_rich(&surface)) {
                focus_view = surface.owner ? surface.owner : range_view;
            }
            if (focus_view) {
                focus_set((DocState*)doc->state, focus_view, true);
            }
            if (start == end) {
                if (selection_has_projection((DocState*)doc->state)) {
                    selection_clear((DocState*)doc->state);
                }
                caret_set((DocState*)doc->state, range_view, (int)start); // INT_CAST_OK: StateStore caret API uses int offsets.
            } else {
                selection_set((DocState*)doc->state, range_view,
                              (int)start, (int)end); // INT_CAST_OK: StateStore selection API uses int offsets.
            }
            log_info("event_sim: set_editing_selection [%u..%u]", start, end);
            doc_state_request_repaint((DocState*)doc->state);
            break;
        }

        case SIM_EVENT_SET_EDITING_VALUE: {
            DomDocument* doc = uicon->document;
            if (!doc || !doc->state) {
                log_error("event_sim: set_editing_value - no document/state");
                ctx->fail_count++;
                break;
            }
            View* elem = resolve_target_element(ev, doc);
            if (!elem || !elem->is_element()) {
                log_error("event_sim: set_editing_value - target element not found");
                ctx->fail_count++;
                break;
            }
            EditingSurface surface;
            editing_surface_clear(&surface);
            if (!editing_surface_from_target(elem, &surface) ||
                !editing_surface_is_text_control(&surface)) {
                log_error("event_sim: set_editing_value - target is not a text control");
                ctx->fail_count++;
                break;
            }
            DomElement* owner = surface.owner;
            tc_ensure_init(owner);
            uint32_t old_len = owner && owner->form
                ? owner->form->current_value_len
                : 0;
            const char* text = ev->input_text ? ev->input_text : "";
            uint32_t text_len = (uint32_t)strlen(text);
            bool ok = te_replace_byte_range_no_events(owner, (DocState*)doc->state,
                                                      surface.view, 0, old_len,
                                                      text, text_len);
            if (!ok) {
                log_error("event_sim: set_editing_value - replace failed");
                ctx->fail_count++;
                break;
            }
            dom_element_set_attribute(owner, "value", text);
            caret_set((DocState*)doc->state, surface.view, (int)text_len);
            log_info("event_sim: set_editing_value len=%u", text_len);
            doc_state_request_repaint((DocState*)doc->state);
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

        case SIM_EVENT_ASSERT_FORM_SELECTION:
            log_info("event_sim: assert_form_selection start=%d end=%d",
                     ev->expected_char_offset, ev->expected_selection_end);
            assert_form_selection(ctx, uicon, ev);
            break;

        case SIM_EVENT_ASSERT_EDITING_SELECTION:
            log_info("event_sim: assert_editing_selection start=%d end=%d",
                     ev->expected_char_offset, ev->expected_selection_end);
            assert_editing_selection(ctx, uicon, ev);
            break;

        case SIM_EVENT_ASSERT_PIXEL:
            log_info("event_sim: assert_pixel");
            assert_pixel_impl(ctx, uicon, ev);
            break;

        case SIM_EVENT_ASSERT_PREEDIT: {
            DomDocument* doc = uicon->document;
            View* elem = resolve_target_element(ev, doc);
            if (!elem || !elem->is_element()) {
                log_error("event_sim: assert_preedit - target element not found");
                ctx->fail_count++;
                break;
            }
            DomElement* dom_elem = lam::dom_require_element(elem);
            if (!tc_is_text_control(dom_elem)) {
                log_error("event_sim: assert_preedit - target is not a text control");
                ctx->fail_count++;
                break;
            }
            tc_ensure_init(dom_elem);
            const char* actual = dom_elem->form && dom_elem->form->preedit_utf8
                ? dom_elem->form->preedit_utf8 : "";
            bool passed = true;
            if (ev->assert_equals && strcmp(actual, ev->assert_equals) != 0) {
                log_error("event_sim: assert_preedit FAIL - expected '%s', got '%s'",
                          ev->assert_equals, actual);
                passed = false;
            }
            if (ev->assert_contains && !strstr(actual, ev->assert_contains)) {
                log_error("event_sim: assert_preedit FAIL - expected to contain '%s', got '%s'",
                          ev->assert_contains, actual);
                passed = false;
            }
            if (passed) {
                log_info("event_sim: assert_preedit PASS (preedit='%s')", actual);
                ctx->pass_count++;
            } else {
                ctx->fail_count++;
            }
            break;
        }

        case SIM_EVENT_ASSERT_PASSWORD_REVEAL: {
            DomDocument* doc = uicon->document;
            View* elem = resolve_target_element(ev, doc);
            if (!elem || !elem->is_element()) {
                log_error("event_sim: assert_password_reveal - target element not found");
                ctx->fail_count++;
                break;
            }
            DomElement* dom_elem = lam::dom_require_element(elem);
            if (!tc_is_text_control(dom_elem) || !dom_elem->form) {
                log_error("event_sim: assert_password_reveal - target is not a text control");
                ctx->fail_count++;
                break;
            }
            bool actual_active = dom_elem->form->password_reveal_active != 0;
            bool passed = true;
            if (actual_active != ev->expected_password_reveal_active) {
                log_error("event_sim: assert_password_reveal FAIL - active expected %s, got %s",
                          ev->expected_password_reveal_active ? "true" : "false",
                          actual_active ? "true" : "false");
                passed = false;
            }
            if (ev->expected_char_offset >= 0 &&
                dom_elem->form->password_reveal_start != (uint32_t)ev->expected_char_offset) {
                log_error("event_sim: assert_password_reveal FAIL - start expected %d, got %u",
                          ev->expected_char_offset,
                          dom_elem->form->password_reveal_start);
                passed = false;
            }
            if (ev->expected_selection_end >= 0 &&
                dom_elem->form->password_reveal_end != (uint32_t)ev->expected_selection_end) {
                log_error("event_sim: assert_password_reveal FAIL - end expected %d, got %u",
                          ev->expected_selection_end,
                          dom_elem->form->password_reveal_end);
                passed = false;
            }
            if (passed) {
                log_info("event_sim: assert_password_reveal PASS");
                ctx->pass_count++;
            } else {
                ctx->fail_count++;
            }
            break;
        }

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
            DomElement* dom_elem = lam::dom_require_element(elem);
            // Prefer live edit buffer (FormControlProp::current_value) when
            // present — the "value" attribute only carries the initial default
            // for HTML <input>/<textarea>. Falling back to the attribute keeps
            // legacy non-FormControl widgets (e.g. todo.ls) working.
            const char* actual = nullptr;
            if (dom_elem->item_prop_type == DomElement::ITEM_PROP_FORM &&
                dom_elem->form && dom_elem->form->current_value) {
                actual = dom_elem->form->current_value;
            }
            if (!actual) {
                const char* val = dom_element_get_attribute(dom_elem, "value");
                actual = val ? val : "";
            }
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

        case SIM_EVENT_ASSERT_EDITING_VALUE: {
            DomDocument* doc = uicon->document;
            View* elem = resolve_target_element(ev, doc);
            if (!elem) {
                log_error("event_sim: assert_editing_value - target element not found");
                ctx->fail_count++;
                break;
            }
            const char* actual = nullptr;
            StrBuf* rich_text = nullptr;
            EditingSurface surface;
            editing_surface_clear(&surface);
            if (editing_surface_from_target(elem, &surface) &&
                editing_surface_is_text_control(&surface)) {
                tc_ensure_init(surface.owner);
                actual = surface.owner && surface.owner->form &&
                    surface.owner->form->current_value
                    ? surface.owner->form->current_value
                    : "";
            } else {
                rich_text = strbuf_new_cap(256);
                sim_extract_text(elem, rich_text);
                actual = rich_text && rich_text->str ? rich_text->str : "";
            }
            bool passed = true;
            if (ev->assert_equals && strcmp(actual, ev->assert_equals) != 0) {
                log_error("event_sim: assert_editing_value FAIL - expected '%s', got '%s'",
                          ev->assert_equals, actual);
                passed = false;
            }
            if (ev->assert_contains && !strstr(actual, ev->assert_contains)) {
                log_error("event_sim: assert_editing_value FAIL - expected to contain '%s', got '%s'",
                          ev->assert_contains, actual);
                passed = false;
            }
            if (passed) {
                log_info("event_sim: assert_editing_value PASS (value='%s')", actual);
                ctx->pass_count++;
            } else {
                ctx->fail_count++;
            }
            if (rich_text) strbuf_free(rich_text);
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
            DocState* state = doc ? (DocState*)doc->state : nullptr;
            bool is_checked = form_control_get_checked(state, elem);
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
            DocState* state = doc ? (DocState*)doc->state : nullptr;
            bool has_state = state_get_pseudo_state(state, elem, mask);
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
            if (!doc || !doc->state) {
                log_error("event_sim: assert_focus - no document or focus state");
                ctx->fail_count++;
                break;
            }
            View* expected = resolve_target_element(ev, doc);
            View* actual = focus_get(doc->state);
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
            ViewBlock* root_block = lam::view_require_block(doc->view_tree->root);
            if (root_block && root_block->scroller && root_block->scroller->pane) {
                scroll_state_get_position_for_view((DocState*)doc->state, static_cast<View*>(root_block),
                    root_block->scroller->pane, &actual_x, &actual_y, NULL, NULL);
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
            DocState* state = doc ? (DocState*)doc->state : nullptr;
            if (state && state->needs_reflow) {
                reflow_process_pending(state);
                if (state->needs_reflow) {
                    extern void reflow_html_doc(DomDocument* doc);
                    reflow_html_doc(doc);
                    doc_state_clear_reflow(state);
                }
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

            DocState* state = doc ? (DocState*)doc->state : nullptr;
            if (state && state->needs_reflow) {
                reflow_process_pending(state);
                if (state->needs_reflow) {
                    extern void reflow_html_doc(DomDocument* doc);
                    reflow_html_doc(doc);
                    doc_state_clear_reflow(state);
                }
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
            View* root = static_cast<View*>(doc->view_tree->root);
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

        case SIM_EVENT_ASSERT_STATE_STORE: {
            DomDocument* doc = uicon->document;
            if (!doc) {
                log_error("event_sim: assert_state_store - no document");
                ctx->fail_count++;
                break;
            }

            DocState* state = (DocState*)doc->state;
            bool passed = true;
            if (!doc->state_store) {
                log_error("event_sim: assert_state_store FAIL - document has no StateStore");
                passed = false;
            }
            if (!state) {
                log_error("event_sim: assert_state_store FAIL - document has no DocState projection");
                passed = false;
            }
            if (doc->state_store && doc->state_store->doc_state != state) {
                log_error("event_sim: assert_state_store FAIL - StateStore DocState does not match document projection");
                passed = false;
            }
            if (state && ev->has_expected_view_state_count) {
                int actual_count = state->view_state_map ? (int)hashmap_count(state->view_state_map) : 0; // INT_CAST_OK: UI test assertion count
                if (actual_count != ev->expected_view_state_count) {
                    log_error("event_sim: assert_state_store FAIL - expected ViewState count %d, got %d",
                        ev->expected_view_state_count, actual_count);
                    passed = false;
                }
            }

            View* elem = NULL;
            if (ev->target_selector || ev->target_text) {
                elem = resolve_target_element(ev, doc);
                if (!elem) {
                    log_error("event_sim: assert_state_store FAIL - target element not found");
                    passed = false;
                }
            }

            ViewState* view_state = (state && elem) ? view_state_get(state, elem) : NULL;
            if (elem && ev->has_expected_view_state && ((view_state != NULL) != ev->expected_view_state_exists)) {
                log_error("event_sim: assert_state_store FAIL - expected ViewState %s, got %s",
                    ev->expected_view_state_exists ? "present" : "absent",
                    view_state ? "present" : "absent");
                passed = false;
            }
            if (elem && ev->has_expected_weak_ref && ((elem->view_state_ref == view_state && view_state != NULL) != ev->expected_weak_ref)) {
                log_error("event_sim: assert_state_store FAIL - weak ViewState ref expectation mismatch");
                passed = false;
            }
            if (state && elem && ev->has_expected_active_target && ((state->active_target == elem) != ev->expected_active_target)) {
                log_error("event_sim: assert_state_store FAIL - active target expectation mismatch");
                passed = false;
            }
            if (state && elem && ev->has_expected_drag_target && ((state->drag_target == elem) != ev->expected_drag_target)) {
                log_error("event_sim: assert_state_store FAIL - drag target expectation mismatch");
                passed = false;
            }
            if (state && ev->has_expected_drag_active && (state->is_dragging != ev->expected_drag_active)) {
                log_error("event_sim: assert_state_store FAIL - drag active expected %s, got %s",
                    ev->expected_drag_active ? "true" : "false",
                    state->is_dragging ? "true" : "false");
                passed = false;
            }
            if (state && ev->has_expected_drag_drop) {
                bool has_drag_drop = state->drag_drop && (state->drag_drop->pending || state->drag_drop->active || state->drag_drop->source_view);
                if (has_drag_drop != ev->expected_drag_drop) {
                    log_error("event_sim: assert_state_store FAIL - drag_drop expected %s, got %s",
                        ev->expected_drag_drop ? "present" : "absent",
                        has_drag_drop ? "present" : "absent");
                    passed = false;
                }
            }
            DragDropState* drag_drop = state ? state->drag_drop : NULL;
            if (drag_drop && ev->has_expected_drag_drop_pending && (drag_drop->pending != ev->expected_drag_drop_pending)) {
                log_error("event_sim: assert_state_store FAIL - drag_drop pending expectation mismatch");
                passed = false;
            }
            if (drag_drop && ev->has_expected_drag_drop_active && (drag_drop->active != ev->expected_drag_drop_active)) {
                log_error("event_sim: assert_state_store FAIL - drag_drop active expectation mismatch");
                passed = false;
            }
            if (drag_drop && elem && ev->has_expected_drag_drop_source && ((drag_drop->source_view == elem) != ev->expected_drag_drop_source)) {
                log_error("event_sim: assert_state_store FAIL - drag_drop source expectation mismatch");
                passed = false;
            }
            if (drag_drop && elem && ev->has_expected_drag_drop_target && ((drag_drop->drop_target == elem) != ev->expected_drag_drop_target)) {
                log_error("event_sim: assert_state_store FAIL - drag_drop target expectation mismatch");
                passed = false;
            }
            if (state && elem && ev->has_expected_open_dropdown && ((state->open_dropdown == elem) != ev->expected_open_dropdown)) {
                log_error("event_sim: assert_state_store FAIL - open dropdown expectation mismatch");
                passed = false;
            }
            float tol = ev->scroll_tolerance > 0.0f ? ev->scroll_tolerance : 1.0f;
            if (state && (ev->has_expected_dropdown_x || ev->has_expected_dropdown_y ||
                          ev->has_expected_dropdown_width || ev->has_expected_dropdown_height)) {
                if (ev->has_expected_dropdown_x &&
                    (state->dropdown_x < ev->expected_dropdown_x - tol || state->dropdown_x > ev->expected_dropdown_x + tol)) {
                    log_error("event_sim: assert_state_store FAIL - dropdown_x expected %.1f, got %.1f",
                        ev->expected_dropdown_x, state->dropdown_x);
                    passed = false;
                }
                if (ev->has_expected_dropdown_y &&
                    (state->dropdown_y < ev->expected_dropdown_y - tol || state->dropdown_y > ev->expected_dropdown_y + tol)) {
                    log_error("event_sim: assert_state_store FAIL - dropdown_y expected %.1f, got %.1f",
                        ev->expected_dropdown_y, state->dropdown_y);
                    passed = false;
                }
                if (ev->has_expected_dropdown_width &&
                    (state->dropdown_width < ev->expected_dropdown_width - tol || state->dropdown_width > ev->expected_dropdown_width + tol)) {
                    log_error("event_sim: assert_state_store FAIL - dropdown_width expected %.1f, got %.1f",
                        ev->expected_dropdown_width, state->dropdown_width);
                    passed = false;
                }
                if (ev->has_expected_dropdown_height &&
                    (state->dropdown_height < ev->expected_dropdown_height - tol || state->dropdown_height > ev->expected_dropdown_height + tol)) {
                    log_error("event_sim: assert_state_store FAIL - dropdown_height expected %.1f, got %.1f",
                        ev->expected_dropdown_height, state->dropdown_height);
                    passed = false;
                }
            }
            if (elem && ev->expected_view_state_kind) {
                const char* actual_kind = "none";
                if (view_state) {
                    switch (view_state->kind) {
                        case VIEW_STATE_BASE: actual_kind = "base"; break;
                        case VIEW_STATE_SCROLL: actual_kind = "scroll"; break;
                        case VIEW_STATE_FORM_CONTROL: actual_kind = "form"; break;
                        case VIEW_STATE_CUSTOM: actual_kind = "custom"; break;
                        default: actual_kind = "unknown"; break;
                    }
                }
                if (strcmp(actual_kind, ev->expected_view_state_kind) != 0) {
                    log_error("event_sim: assert_state_store FAIL - expected kind '%s', got '%s'",
                        ev->expected_view_state_kind, actual_kind);
                    passed = false;
                }
            }

            if (state && (ev->has_expected_doc_scroll_x || ev->has_expected_doc_scroll_y)) {
                if (ev->has_expected_doc_scroll_x &&
                    (state->scroll_x < ev->expected_doc_scroll_x - tol || state->scroll_x > ev->expected_doc_scroll_x + tol)) {
                    log_error("event_sim: assert_state_store FAIL - doc scroll_x expected %.1f, got %.1f",
                        ev->expected_doc_scroll_x, state->scroll_x);
                    passed = false;
                }
                if (ev->has_expected_doc_scroll_y &&
                    (state->scroll_y < ev->expected_doc_scroll_y - tol || state->scroll_y > ev->expected_doc_scroll_y + tol)) {
                    log_error("event_sim: assert_state_store FAIL - doc scroll_y expected %.1f, got %.1f",
                        ev->expected_doc_scroll_y, state->scroll_y);
                    passed = false;
                }
            }

            if (elem && (ev->has_expected_view_scroll_x || ev->has_expected_view_scroll_y)) {
                if (!elem->is_block()) {
                    log_error("event_sim: assert_state_store FAIL - scroll target is not a block view");
                    passed = false;
                } else {
                    ViewBlock* block = lam::view_require_block(elem);
                    float actual_x = 0.0f, actual_y = 0.0f;
                    void* pane = block->scroller ? (void*)block->scroller->pane : NULL;
                    scroll_state_get_position_for_view(state, elem, pane, &actual_x, &actual_y, NULL, NULL);
                    if (ev->has_expected_view_scroll_x &&
                        (actual_x < ev->expected_view_scroll_x - tol || actual_x > ev->expected_view_scroll_x + tol)) {
                        log_error("event_sim: assert_state_store FAIL - view scroll_x expected %.1f, got %.1f",
                            ev->expected_view_scroll_x, actual_x);
                        passed = false;
                    }
                    if (ev->has_expected_view_scroll_y &&
                        (actual_y < ev->expected_view_scroll_y - tol || actual_y > ev->expected_view_scroll_y + tol)) {
                        log_error("event_sim: assert_state_store FAIL - view scroll_y expected %.1f, got %.1f",
                            ev->expected_view_scroll_y, actual_y);
                        passed = false;
                    }
                }
            }

            if (elem && (ev->has_expected_scrollbar_h_hovered || ev->has_expected_scrollbar_v_hovered ||
                         ev->has_expected_scrollbar_h_dragging || ev->has_expected_scrollbar_v_dragging)) {
                ScrollInteractionState interaction;
                scroll_state_get_interaction_for_view(state, elem, &interaction);
                if (ev->has_expected_scrollbar_h_hovered && interaction.h_hovered != ev->expected_scrollbar_h_hovered) {
                    log_error("event_sim: assert_state_store FAIL - scrollbar_h_hovered expected %s, got %s",
                        ev->expected_scrollbar_h_hovered ? "true" : "false",
                        interaction.h_hovered ? "true" : "false");
                    passed = false;
                }
                if (ev->has_expected_scrollbar_v_hovered && interaction.v_hovered != ev->expected_scrollbar_v_hovered) {
                    log_error("event_sim: assert_state_store FAIL - scrollbar_v_hovered expected %s, got %s",
                        ev->expected_scrollbar_v_hovered ? "true" : "false",
                        interaction.v_hovered ? "true" : "false");
                    passed = false;
                }
                if (ev->has_expected_scrollbar_h_dragging && interaction.h_dragging != ev->expected_scrollbar_h_dragging) {
                    log_error("event_sim: assert_state_store FAIL - scrollbar_h_dragging expected %s, got %s",
                        ev->expected_scrollbar_h_dragging ? "true" : "false",
                        interaction.h_dragging ? "true" : "false");
                    passed = false;
                }
                if (ev->has_expected_scrollbar_v_dragging && interaction.v_dragging != ev->expected_scrollbar_v_dragging) {
                    log_error("event_sim: assert_state_store FAIL - scrollbar_v_dragging expected %s, got %s",
                        ev->expected_scrollbar_v_dragging ? "true" : "false",
                        interaction.v_dragging ? "true" : "false");
                    passed = false;
                }
            }

            if (passed) {
                log_info("event_sim: assert_state_store PASS");
                ctx->pass_count++;
            } else {
                ctx->fail_count++;
            }
            break;
        }

        case SIM_EVENT_ASSERT_EVENT_LOG: {
            assert_event_log_impl(ctx, uicon, ev);
            break;
        }

        case SIM_EVENT_ASSERT_EDITING_EVENT: {
            assert_editing_event_impl(ctx, uicon, ev);
            break;
        }

        case SIM_EVENT_ASSERT_STATE_DUMP: {
            assert_state_dump_impl(ctx, uicon, ev);
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
            // Set scroll position through centralized API
            ViewBlock* root_block = lam::view_require_block(doc->view_tree->root);
            if (root_block && root_block->scroller && root_block->scroller->pane) {
                ScrollPane* pane = root_block->scroller->pane;
                DocState* state = (DocState*)doc->state;
                scroll_state_set_position_for_view(state, static_cast<View*>(root_block), pane,
                                                   target_x, target_y, true);
                scroll_state_get_position_for_view(state, static_cast<View*>(root_block), pane,
                                                   &target_x, &target_y, NULL, NULL);
                doc_state_sync_viewport_scroll(state, doc, target_x, target_y);
            }
            if (doc->state) doc_state_request_repaint(doc->state);
            force_render_surface(uicon);
            break;
        }

        case SIM_EVENT_ADVANCE_TIME: {
            DomDocument* doc = uicon->document;
            if (!doc || !doc->state) {
                log_error("event_sim: advance_time - no document state");
                break;
            }
            DocState* state = (DocState*)doc->state;
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
                radiant_editing_animation_tick(uicon, now);
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
            if (!radiant_document_ensure_state(new_doc, "event_sim:navigate")) {
                log_error("event_sim: navigate FAIL - could not create DocState");
                ctx->fail_count++;
                break;
            }
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
            DomElement* iframe_elem = lam::dom_require_element(iframe_view);
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
                // Determine format from extension
                const char* ext = strrchr(ev->file_path, '.');
                if (ext && (strcmp(ext, ".svg") == 0 || strcmp(ext, ".SVG") == 0)) {
                    render_uicontext_to_svg(uicon, ev->file_path);
                } else {
                    // Default to PNG
                    render_uicontext_to_png(uicon, ev->file_path);
                }
            }
            break;

        case SIM_EVENT_RENDER_PENDING:
            log_info("event_sim: render_pending");
            render_pending_surface(uicon);
            break;

        case SIM_EVENT_DUMP_CARET:
            {
                const char* path = ev->file_path ? ev->file_path : "./view_tree.txt";
                log_info("event_sim: dump_caret to %s", path);
                DomDocument* doc = uicon->document;
                if (doc && doc->state) {
                    extern void print_caret_state(DocState* state, const char* output_path);
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
            ViewBlock* block = lam::view_require_block(view);
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
            ViewBlock* block = lam::view_require_block(view);
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

static void replay_emit_mismatch(EventSimContext* ctx, UiContext* uicon,
                                 const char* field, const char* expected,
                                 const char* actual) {
    log_error("event_sim: replay mismatch %s expected=%s actual=%s",
              field ? field : "unknown", expected ? expected : "(null)",
              actual ? actual : "(null)");
    if (ctx) ctx->fail_count++;
    EventStateLog* event_log = uicon ? uicon->event_log : NULL;
    if (event_state_log_enabled(event_log)) {
        char buf[1024];
        JsonWriter w;
        event_state_log_begin_record(event_log, &w, buf, sizeof(buf), "replay.mismatch", 0);
        jw_key(&w, "data");
        jw_obj_begin(&w);
            jw_kv_str(&w, "field", field ? field : "unknown");
            jw_kv_str(&w, "expected", expected ? expected : "(null)");
            jw_kv_str(&w, "actual", actual ? actual : "(null)");
        jw_obj_end(&w);
        event_state_log_finish_record(event_log, &w);
    }
}

static void replay_check_final_state(EventSimContext* ctx, UiContext* uicon) {
    if (!ctx || !ctx->replay_assert_state || ctx->replay_state_checked) return;
    ctx->replay_state_checked = true;
    if (!ctx->replay_has_expected_state) {
        replay_emit_mismatch(ctx, uicon, "state.snapshot", "present", "missing");
        return;
    }
    DomDocument* doc = uicon ? uicon->document : NULL;
    DocState* state = doc ? doc->state : NULL;
    if (!state) {
        replay_emit_mismatch(ctx, uicon, "state", "present", "missing");
        return;
    }

    char expected[64];
    char actual[64];
    View* focus = focus_get(state);
    int actual_focus_id = focus ? (int)(static_cast<DomNode*>(focus))->id : 0;
    if (ctx->replay_expected_focus_id >= 0 && actual_focus_id != ctx->replay_expected_focus_id) {
        snprintf(expected, sizeof(expected), "%d", ctx->replay_expected_focus_id);
        snprintf(actual, sizeof(actual), "%d", actual_focus_id);
        replay_emit_mismatch(ctx, uicon, "focus.target.id", expected, actual);
    }

    View* caret = caret_get_view(state);
    int actual_caret_id = caret ? (int)(static_cast<DomNode*>(caret))->id : 0;
    if (ctx->replay_expected_caret_id >= 0 && actual_caret_id != ctx->replay_expected_caret_id) {
        snprintf(expected, sizeof(expected), "%d", ctx->replay_expected_caret_id);
        snprintf(actual, sizeof(actual), "%d", actual_caret_id);
        replay_emit_mismatch(ctx, uicon, "caret.target.id", expected, actual);
    }

    if (ctx->replay_has_caret_offset) {
        int actual_offset = -1;
        caret_get_offset(state, &actual_offset);
        if (actual_offset != ctx->replay_expected_caret_offset) {
            snprintf(expected, sizeof(expected), "%d", ctx->replay_expected_caret_offset);
            snprintf(actual, sizeof(actual), "%d", actual_offset);
            replay_emit_mismatch(ctx, uicon, "caret.offset", expected, actual);
        }
    }

    if (ctx->replay_has_selection_collapsed) {
        bool actual_collapsed = !selection_has(state);
        if (actual_collapsed != ctx->replay_expected_selection_collapsed) {
            replay_emit_mismatch(ctx, uicon, "selection.is_collapsed",
                ctx->replay_expected_selection_collapsed ? "true" : "false",
                actual_collapsed ? "true" : "false");
        }
    }

    if (ctx->replay_has_scroll) {
        float dx = state->scroll_x - ctx->replay_expected_scroll_x;
        if (dx < 0) dx = -dx;
        float dy = state->scroll_y - ctx->replay_expected_scroll_y;
        if (dy < 0) dy = -dy;
        if (dx > 1.0f || dy > 1.0f) {
            snprintf(expected, sizeof(expected), "%.2f,%.2f", ctx->replay_expected_scroll_x, ctx->replay_expected_scroll_y);
            snprintf(actual, sizeof(actual), "%.2f,%.2f", state->scroll_x, state->scroll_y);
            replay_emit_mismatch(ctx, uicon, "document_state.scroll", expected, actual);
        }
    }

    if (ctx->fail_count == 0) {
        ctx->pass_count++;
        log_info("event_sim: replay final state PASS");
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
    if (ctx->replay_assert_state && ev->type == SIM_EVENT_REPLAY_INPUT) {
        event_sim_assert_schema(ctx, uicon, "replay_input");
    }

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
        replay_check_final_state(ctx, uicon);
        if (ctx->original_document) {
            uicon->document = (DomDocument*)ctx->original_document;
            ctx->frame_stack_depth = 0;
        }
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
