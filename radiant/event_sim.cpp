/**
 * Event Simulation Implementation
 */

#include "event_sim.hpp"
#include "event.hpp"
#include "state_store.hpp"
#include "view.hpp"
#include "../lib/log.h"
#include "../lib/strbuf.h"
#include "../lib/file.h"
#include "../lib/memtrack.h"
#include "../lambda/lambda-data.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lambda/input/input.hpp"
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

    // Single character keys
    if (strlen(name) == 1) {
        char c = name[0];
        if (c >= 'a' && c <= 'z') return GLFW_KEY_A + (c - 'a');
        if (c >= 'A' && c <= 'Z') return GLFW_KEY_A + (c - 'A');
        if (c >= '0' && c <= '9') return GLFW_KEY_0 + (c - '0');
    }

    // Special keys
    if (strcasecmp(name, "space") == 0) return GLFW_KEY_SPACE;
    if (strcasecmp(name, "enter") == 0 || strcasecmp(name, "return") == 0) return GLFW_KEY_ENTER;
    if (strcasecmp(name, "tab") == 0) return GLFW_KEY_TAB;
    if (strcasecmp(name, "backspace") == 0) return GLFW_KEY_BACKSPACE;
    if (strcasecmp(name, "delete") == 0) return GLFW_KEY_DELETE;
    if (strcasecmp(name, "escape") == 0 || strcasecmp(name, "esc") == 0) return GLFW_KEY_ESCAPE;
    if (strcasecmp(name, "left") == 0) return GLFW_KEY_LEFT;
    if (strcasecmp(name, "right") == 0) return GLFW_KEY_RIGHT;
    if (strcasecmp(name, "up") == 0) return GLFW_KEY_UP;
    if (strcasecmp(name, "down") == 0) return GLFW_KEY_DOWN;
    if (strcasecmp(name, "home") == 0) return GLFW_KEY_HOME;
    if (strcasecmp(name, "end") == 0) return GLFW_KEY_END;
    if (strcasecmp(name, "pageup") == 0) return GLFW_KEY_PAGE_UP;
    if (strcasecmp(name, "pagedown") == 0) return GLFW_KEY_PAGE_DOWN;
    if (strcasecmp(name, "control") == 0 || strcasecmp(name, "ctrl") == 0) return GLFW_KEY_LEFT_CONTROL;
    if (strcasecmp(name, "shift") == 0) return GLFW_KEY_LEFT_SHIFT;
    if (strcasecmp(name, "alt") == 0) return GLFW_KEY_LEFT_ALT;
    if (strcasecmp(name, "super") == 0 || strcasecmp(name, "cmd") == 0 || strcasecmp(name, "meta") == 0) return GLFW_KEY_LEFT_SUPER;

    // Function keys
    if (name[0] == 'f' || name[0] == 'F') {
        int num = atoi(name + 1);
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
    }
    else if (strcmp(type_str, "mouse_down") == 0) {
        ev->type = SIM_EVENT_MOUSE_DOWN;
        ev->x = reader.get("x").asInt32();
        ev->y = reader.get("y").asInt32();
        ev->button = reader.get("button").asInt32();
        ev->mods = reader.get("mods").asInt32();
        const char* mods_str = reader.get("mods_str").cstring();
        if (mods_str) ev->mods = parse_mods_string(mods_str);
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
        ev->scroll_dx = (float)reader.get("dx").asFloat();
        ev->scroll_dy = (float)reader.get("dy").asFloat();
    }
    else if (strcmp(type_str, "assert_caret") == 0) {
        ev->type = SIM_EVENT_ASSERT_CARET;
        ev->expected_view_type = reader.get("view_type").asInt32();
        ev->expected_char_offset = reader.get("char_offset").asInt32();
        if (ev->expected_view_type == 0) ev->expected_view_type = -1;  // treat 0 as "don't check"
        if (ev->expected_char_offset == 0 && !reader.has("char_offset")) ev->expected_char_offset = -1;
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
    else if (strcmp(type_str, "log") == 0) {
        ev->type = SIM_EVENT_LOG;
        const char* msg = reader.get("message").cstring();
        if (msg) ev->message = mem_strdup(msg, MEM_CAT_LAYOUT);
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
            mem_free(ev);
        }
        arraylist_free(ctx->events);
    }

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

        case SIM_EVENT_MOUSE_MOVE:
            log_info("event_sim: mouse_move to (%d, %d)", ev->x, ev->y);
            sim_mouse_move(uicon, ev->x, ev->y);
            break;

        case SIM_EVENT_MOUSE_DOWN:
            log_info("event_sim: mouse_down at (%d, %d) button=%d", ev->x, ev->y, ev->button);
            sim_mouse_button(uicon, ev->x, ev->y, ev->button, ev->mods, true);
            break;

        case SIM_EVENT_MOUSE_UP:
            log_info("event_sim: mouse_up at (%d, %d) button=%d", ev->x, ev->y, ev->button);
            sim_mouse_button(uicon, ev->x, ev->y, ev->button, ev->mods, false);
            break;

        case SIM_EVENT_MOUSE_DRAG:
            log_info("event_sim: mouse_drag from (%d, %d) to (%d, %d)", ev->x, ev->y, ev->to_x, ev->to_y);
            // Simulate drag: down at start, move to end, up at end
            sim_mouse_button(uicon, ev->x, ev->y, ev->button, ev->mods, true);
            // Interpolate some positions
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

        case SIM_EVENT_LOG:
            fprintf(stderr, "[EVENT_SIM] %s\n", ev->message ? ev->message : "(no message)");
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
