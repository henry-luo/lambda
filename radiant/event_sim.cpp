/**
 * Event Simulation Implementation
 */

#include "event_sim.hpp"
#include "event.hpp"
#include "state_store.hpp"
#include "form_control.hpp"
#include "view.hpp"
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
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <cstring>

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
static bool find_text_position_recursive(View* view, const char* target_text,
                                         float parent_abs_x, float parent_abs_y,
                                         float* out_x, float* out_y) {
    if (!view || !target_text) return false;

    // calculate absolute position of this view
    // For text views, x/y are the same as the first TextRect's x/y (relative to parent block)
    // For block views, x/y are relative to parent block's border box
    float abs_x = parent_abs_x + view->x;
    float abs_y = parent_abs_y + view->y;

    log_debug("find_text: view_type=%d, x=%.1f, y=%.1f, parent_abs=(%.1f, %.1f), abs=(%.1f, %.1f)",
              view->view_type, view->x, view->y, parent_abs_x, parent_abs_y, abs_x, abs_y);

    // check if this is a text view with matching text
    if (view->view_type == RDT_VIEW_TEXT) {
        DomText* text_view = view->as_text();
        if (text_view && text_view->text) {
            log_debug("find_text: text='%.30s...', searching for '%s'", text_view->text, target_text);
            const char* match = strstr(text_view->text, target_text);
            if (match) {
                // Found a match - calculate position of the target text within the TextRect
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
                            // Since we can't get exact glyph widths here, position near
                            // the START of the target text with a small offset
                            float text_x = parent_abs_x + rect->x;
                            int chars_before = match_offset - rect_start;

                            // Use average char width, but only to find the start position
                            // (not the center) to minimize error
                            float avg_char_width = rect->width / rect->length;
                            float match_start_x = text_x + chars_before * avg_char_width;

                            // Position a few pixels into the first character of target
                            // This ensures we hit the target text reliably
                            float click_x = match_start_x + 3.0f;  // 3 pixels into first char

                            *out_x = click_x;
                            *out_y = parent_abs_y + rect->y + rect->height / 2;
                            log_info("event_sim: found target_text '%s' at (%.1f, %.1f), match_offset=%d, rect=(%.1f, %.1f, %.1f, %.1f)",
                                     target_text, *out_x, *out_y, match_offset, rect->x, rect->y, rect->width, rect->height);
                            return true;
                        }
                        rect = rect->next;
                    }
                    // Match not found in any rect - fallback to first rect center
                    rect = text_view->rect;
                    *out_x = parent_abs_x + rect->x + rect->width / 2;
                    *out_y = parent_abs_y + rect->y + rect->height / 2;
                    log_warn("event_sim: target_text '%s' found in text but not in any TextRect, using center", target_text);
                    return true;
                } else {
                    log_warn("event_sim: text found but no TextRect");
                }
            }
        }
    }

    // For block elements, pass absolute position to children
    // Note: text view children don't exist, only element children
    DomElement* elem = view->as_element();
    if (elem) {
        View* child = (View*)elem->first_child;
        while (child) {
            if (find_text_position_recursive(child, target_text, abs_x, abs_y, out_x, out_y)) {
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
} SimSelectorCtx;

static bool sim_selector_visitor(View* view, void* udata) {
    SimSelectorCtx* ctx = (SimSelectorCtx*)udata;
    if (!view->is_element()) return true;
    DomElement* dom_elem = (DomElement*)view;
    if (selector_matcher_matches(ctx->matcher, ctx->selector, dom_elem, NULL)) {
        ctx->result = view;
        return false;  // stop on first match
    }
    return true;
}

// Find first element matching a CSS selector in the document
static View* find_element_by_selector(DomDocument* doc, const char* selector_text) {
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

    sim_traverse_views((View*)doc->view_tree->root, sim_selector_visitor, &ctx);
    return ctx.result;
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
        View* elem = find_element_by_selector(doc, ev->target_selector);
        if (elem) {
            float fx, fy;
            get_element_center_abs(elem, &fx, &fy);
            *out_x = (int)fx;
            *out_y = (int)fy;
            log_info("event_sim: resolved selector '%s' to (%d, %d)", ev->target_selector, *out_x, *out_y);
            return true;
        }
        log_error("event_sim: selector '%s' not found", ev->target_selector);
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
        return find_element_by_selector(doc, ev->target_selector);
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
    else {
        log_error("event_sim: unknown event type '%s'", type_str);
        mem_free(ev);
        return NULL;
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
        free(json_content);
        return NULL;
    }

    parse_json(input, json_content);
    free(json_content);

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
            if (ev->input_text) mem_free(ev->input_text);
            if (ev->assert_contains) mem_free(ev->assert_contains);
            if (ev->assert_equals) mem_free(ev->assert_equals);
            if (ev->option_value) mem_free(ev->option_value);
            if (ev->option_label) mem_free(ev->option_label);
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
    event.mouse_position.timestamp = glfwGetTime();
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
    event.mouse_button.timestamp = glfwGetTime();
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
    event.key.timestamp = glfwGetTime();
    event.key.key = key;
    event.key.scancode = 0;
    event.key.mods = mods;
    handle_event(uicon, uicon->document, &event);
}

// Simulate a scroll event
static void sim_scroll(UiContext* uicon, int x, int y, float dx, float dy) {
    RdtEvent event;
    event.scroll.type = RDT_EVENT_SCROLL;
    event.scroll.timestamp = glfwGetTime();
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
    event.text_input.timestamp = glfwGetTime();
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

// Assert helper for selection state
static bool assert_selection(EventSimContext* ctx, UiContext* uicon, SimEvent* ev) {
    DomDocument* doc = uicon->document;
    if (!doc || !doc->state) {
        log_error("event_sim: assert_selection - no document or state");
        ctx->fail_count++;
        return false;
    }

    SelectionState* sel = doc->state->selection;
    bool is_collapsed = sel ? sel->is_collapsed : true;

    if (is_collapsed != ev->expected_is_collapsed) {
        log_error("event_sim: assert_selection - is_collapsed mismatch: expected %s, got %s",
                 ev->expected_is_collapsed ? "true" : "false",
                 is_collapsed ? "true" : "false");
        ctx->fail_count++;
        return false;
    }

    log_info("event_sim: assert_selection PASS");
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

        case SIM_EVENT_MOUSE_DRAG:
            log_info("event_sim: mouse_drag from (%d, %d) to (%d, %d)", ev->x, ev->y, ev->to_x, ev->to_y);
            sim_mouse_button(uicon, ev->x, ev->y, ev->button, ev->mods, true);
            for (int step = 1; step <= 5; step++) {
                int px = ev->x + (ev->to_x - ev->x) * step / 5;
                int py = ev->y + (ev->to_y - ev->y) * step / 5;
                sim_mouse_move(uicon, px, py);
            }
            sim_mouse_button(uicon, ev->to_x, ev->to_y, ev->button, ev->mods, false);
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
            break;

        // ===== High-level actions =====

        case SIM_EVENT_CLICK: {
            // For selector-targeted checkbox/radio, check state before click
            // to detect if the coordinate-based click already handled the toggle
            View* form_elem = nullptr;
            bool was_checked = false;
            if (ev->target_selector) {
                DomDocument* doc = uicon->document;
                form_elem = find_element_by_selector(doc, ev->target_selector);
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
                event.mouse_button.timestamp = glfwGetTime();
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
            } else if (actual != expected) {
                log_error("event_sim: assert_focus FAIL - focus not on expected element");
                ctx->fail_count++;
            } else {
                log_info("event_sim: assert_focus PASS");
                ctx->pass_count++;
            }
            break;
        }

        case SIM_EVENT_ASSERT_SCROLL: {
            DomDocument* doc = uicon->document;
            if (!doc || !doc->state) {
                log_error("event_sim: assert_scroll - no document state");
                ctx->fail_count++;
                break;
            }
            float actual_x = doc->state->scroll_x;
            float actual_y = doc->state->scroll_y;
            float tol = ev->scroll_tolerance;
            bool pass_x = (ev->expected_scroll_x == 0 && !ev->expected_scroll_y) ||
                          (actual_x >= ev->expected_scroll_x - tol && actual_x <= ev->expected_scroll_x + tol);
            bool pass_y = (actual_y >= ev->expected_scroll_y - tol && actual_y <= ev->expected_scroll_y + tol);
            if (!pass_x || !pass_y) {
                log_error("event_sim: assert_scroll FAIL - expected (%.1f, %.1f) +/-%.1f, got (%.1f, %.1f)",
                         ev->expected_scroll_x, ev->expected_scroll_y, tol, actual_x, actual_y);
                ctx->fail_count++;
            } else {
                log_info("event_sim: assert_scroll PASS (%.1f, %.1f)", actual_x, actual_y);
                ctx->pass_count++;
            }
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
                fprintf(stderr, "[EVENT_SIM] Dumping caret state to: %s\n", path);
                DomDocument* doc = uicon->document;
                if (doc && doc->state) {
                    extern void print_caret_state(RadiantState* state, const char* output_path);
                    print_caret_state(doc->state, path);
                } else {
                    log_error("event_sim: dump_caret - no document state");
                }
            }
            break;
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

    // Process current event
    SimEvent* ev = (SimEvent*)ctx->events->data[ctx->current_index];
    process_sim_event(ctx, ev, uicon, window);

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
