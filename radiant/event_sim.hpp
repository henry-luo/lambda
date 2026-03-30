/**
 * Event Simulation System for Radiant Viewer
 * 
 * Allows loading and replaying events from a JSON file for automated testing.
 * Supports primitive events (mouse_down, key_press, etc.) and high-level
 * actions (click, dblclick, type) plus assertions for verification.
 * 
 * JSON Format:
 * {
 *   "name": "Test name",
 *   "events": [
 *     {"type": "wait", "ms": 500},
 *     {"type": "click", "target": {"selector": "#btn"}},
 *     {"type": "click", "target": {"text": "Click here"}},
 *     {"type": "dblclick", "target": {"text": "Select word"}},
 *     {"type": "type", "target": {"selector": "input"}, "text": "hello"},
 *     {"type": "mouse_move", "x": 100, "y": 200},
 *     {"type": "mouse_down", "x": 100, "y": 200, "button": 0, "mods": 0},
 *     {"type": "mouse_down", "target_text": "Click here"},
 *     {"type": "mouse_up", "x": 100, "y": 200, "button": 0, "mods": 0},
 *     {"type": "mouse_drag", "from_x": 100, "from_y": 200, "to_x": 200, "to_y": 200},
 *     {"type": "key_press", "key": "a"},
 *     {"type": "key_down", "key": "Control"},
 *     {"type": "key_up", "key": "Control"},
 *     {"type": "key_combo", "key": "c", "mods": ["ctrl"]},
 *     {"type": "scroll", "x": 100, "y": 200, "dx": 0, "dy": -3},
 *     {"type": "assert_caret", "view_type": 1, "char_offset": 5},
 *     {"type": "assert_selection", "is_collapsed": false},
 *     {"type": "assert_target", "view_type": 1},
 *     {"type": "assert_text", "target": {"selector": "h1"}, "contains": "Hello"},
 *     {"type": "assert_value", "target": {"selector": "input#email"}, "equals": "user@test.com"},
 *     {"type": "assert_checked", "target": {"selector": "input#agree"}, "checked": true},
 *     {"type": "assert_visible", "target": {"selector": ".modal"}, "visible": true},
 *     {"type": "assert_focus", "target": {"selector": "input#email"}},
 *     {"type": "assert_state", "target": {"selector": "button"}, "state": ":hover", "value": true},
 *     {"type": "assert_scroll", "y": 500, "tolerance": 10},
 *     {"type": "check", "target": {"selector": "input#agree"}, "checked": true},
 *     {"type": "select_option", "target": {"selector": "select#country"}, "value": "us"},
 *     {"type": "select_option", "target": {"selector": "select#color"}, "label": "Blue"},
 *     {"type": "log", "message": "Test step completed"},
 *     {"type": "render", "file": "./temp/output.png"},
 *     {"type": "dump_caret", "file": "./caret_state.txt"}
 *   ]
 * }
 */

#ifndef RADIANT_EVENT_SIM_HPP
#define RADIANT_EVENT_SIM_HPP

#include <cstdio>
#include <cstdint>
#include "../lib/arraylist.h"

// Forward declarations
struct GLFWwindow;

// Event simulation command types
enum SimEventType {
    // Primitive events
    SIM_EVENT_WAIT,
    SIM_EVENT_MOUSE_MOVE,
    SIM_EVENT_MOUSE_DOWN,
    SIM_EVENT_MOUSE_UP,
    SIM_EVENT_MOUSE_DRAG,
    SIM_EVENT_KEY_PRESS,
    SIM_EVENT_KEY_DOWN,
    SIM_EVENT_KEY_UP,
    SIM_EVENT_KEY_COMBO,
    SIM_EVENT_SCROLL,
    // High-level actions
    SIM_EVENT_CLICK,           // click (mouse_down + mouse_up)
    SIM_EVENT_DBLCLICK,        // double-click
    SIM_EVENT_TYPE,            // type text into focused element
    SIM_EVENT_FOCUS,           // focus an element (via click)
    SIM_EVENT_CHECK,           // toggle checkbox/radio to desired state
    SIM_EVENT_SELECT_OPTION,   // select an option from a <select> dropdown
    // Assertions
    SIM_EVENT_ASSERT_CARET,
    SIM_EVENT_ASSERT_SELECTION,
    SIM_EVENT_ASSERT_TARGET,
    SIM_EVENT_ASSERT_TEXT,     // verify element text content
    SIM_EVENT_ASSERT_VALUE,    // verify form field value
    SIM_EVENT_ASSERT_CHECKED,  // verify checkbox/radio state
    SIM_EVENT_ASSERT_VISIBLE,  // verify element visibility
    SIM_EVENT_ASSERT_FOCUS,    // verify focused element
    SIM_EVENT_ASSERT_STATE,    // verify pseudo-state (:hover, :active, etc.)
    SIM_EVENT_ASSERT_SCROLL,   // verify scroll position
    // Utilities
    SIM_EVENT_LOG,
    SIM_EVENT_RENDER,          // render current view to PNG/SVG
    SIM_EVENT_DUMP_CARET       // dump caret state to file
};

// Simulated event command
struct SimEvent {
    SimEventType type;
    int x, y;                    // mouse position
    int to_x, to_y;              // for drag: destination
    int button;                  // mouse button (0=left, 1=right, 2=middle)
    int mods;                    // modifier keys (RDT_MOD_*)
    int key;                     // GLFW key code
    int wait_ms;                 // wait duration in milliseconds
    int expected_view_type;      // for assertions
    int expected_char_offset;    // for assertions
    bool expected_is_collapsed;  // for assertions
    bool negate_view_type;       // for assert_caret: assert view_type != expected_view_type
    float scroll_dx, scroll_dy;  // scroll offsets
    char* message;               // for log events
    char* file_path;             // for render/dump_caret events
    char* target_text;           // for mouse events: find text and click on it
    char* target_selector;       // CSS selector for targeting elements
    char* input_text;            // for type action: text to type
    bool clear_first;            // for type action: select-all + delete before typing
    char* assert_contains;       // for assert_text: substring match
    char* assert_equals;         // for assert_text: exact match
    bool expected_visible;       // for assert_visible
    bool expected_checked;       // for assert_checked
    char* state_name;            // for assert_state: e.g. ":hover", ":active"
    bool expected_state_value;   // for assert_state: expected boolean
    float expected_scroll_x;     // for assert_scroll
    float expected_scroll_y;     // for assert_scroll
    float scroll_tolerance;      // for assert_scroll
    char* option_value;          // for select_option: match by value attribute
    char* option_label;          // for select_option: match by visible text
};

// Event simulation context
struct EventSimContext {
    ArrayList* events;           // list of SimEvent*
    int current_index;           // current event being processed
    double next_event_time;      // when to process next event
    bool is_running;             // simulation in progress
    bool auto_close;             // close window when done
    int pass_count;              // assertions passed
    int fail_count;              // assertions failed
    FILE* result_file;           // optional result output file
    char* test_name;             // optional test name from JSON
    int viewport_width;          // 0 = use default (1200)
    int viewport_height;         // 0 = use default (800)
};

// Load events from JSON file
// Returns NULL on error
EventSimContext* event_sim_load(const char* json_file);

// Free simulation context
void event_sim_free(EventSimContext* ctx);

// Process next event if ready
// Returns true if simulation is still running
// uicon is cast to UiContext* internally
bool event_sim_update(EventSimContext* ctx, void* uicon, GLFWwindow* window, double current_time);

// Get simulation results summary
void event_sim_print_results(EventSimContext* ctx);

#endif // RADIANT_EVENT_SIM_HPP
