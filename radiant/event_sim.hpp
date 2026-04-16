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
 *     {"type": "mouse_drag", "target": {"selector": "#start"}, "to_target": {"selector": "#end"}},
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
 *     {"type": "resize", "width": 800, "height": 600},
 *     {"type": "navigate", "url": "test/ui/page2.html"},
 *     {"type": "assert_rect", "target": {"selector": "#box"}, "x": 0, "y": 0, "width": 200, "height": 100, "tolerance": 2},
 *     {"type": "assert_style", "target": {"selector": "h1"}, "property": "font-size", "equals": "32px"},
 *     {"type": "assert_position", "element_a": {"selector": "#header"}, "element_b": {"selector": "#content"}, "relation": "above"},
 *     {"type": "assert_element_at", "x": 100, "y": 50, "expected_selector": "#header"},
 *     {"type": "switch_frame", "selector": "iframe#myframe"},
 *     {"type": "switch_frame"},
 *     {"type": "drag_and_drop", "target": {"selector": "#src"}, "to_target": {"selector": "#dest"}},
 *     {"type": "assert_attribute", "target": {"selector": "#box"}, "attribute": "draggable", "equals": "true"},
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
    SIM_EVENT_RESIZE,          // resize viewport and trigger relayout
    SIM_EVENT_DRAG_AND_DROP,   // HTML5 drag-and-drop from source to target
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
    SIM_EVENT_ASSERT_RECT,     // verify element bounding box (x, y, width, height)
    SIM_EVENT_ASSERT_STYLE,    // verify computed CSS property value
    SIM_EVENT_ASSERT_POSITION, // verify spatial relation between two elements
    SIM_EVENT_ASSERT_ELEMENT_AT, // verify element at given coordinates
    SIM_EVENT_ASSERT_ATTRIBUTE,  // verify HTML attribute value
    SIM_EVENT_ASSERT_COUNT,      // verify number of elements matching a selector
    SIM_EVENT_ASSERT_SNAPSHOT,   // pixel-compare rendered surface against browser reference PNG
    // Mutation helpers
    SIM_EVENT_SCROLL_TO,         // scroll to absolute position or element
    SIM_EVENT_ADVANCE_TIME,      // advance animation scheduler clock deterministically
    // Navigation
    SIM_EVENT_NAVIGATE,        // load a new HTML document
    SIM_EVENT_NAVIGATE_BACK,   // go back to previous document
    // Frame switching
    SIM_EVENT_SWITCH_FRAME,    // switch to iframe document (or back to main)
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
    int target_index;            // 0-based index: which matching element (default 0 = first)
    char* to_target_selector;    // for mouse_drag: destination CSS selector
    char* to_target_text;        // for mouse_drag: destination text target
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
    // Phase 5: assert_rect fields
    float expected_rect_x, expected_rect_y;     // expected position
    float expected_rect_w, expected_rect_h;     // expected size
    float rect_tolerance;                       // allowed deviation (default 1px)
    bool has_rect_x, has_rect_y, has_rect_w, has_rect_h; // which fields to check
    // Phase 5: assert_style fields
    char* style_property;        // CSS property name
    // Phase 5: assert_position fields
    char* element_a_selector;    // first element selector
    char* element_a_text;        // first element text target
    char* element_b_selector;    // second element selector
    char* element_b_text;        // second element text target
    char* position_relation;     // "above", "below", "left_of", "right_of", "overlaps", "contains", "inside"
    float position_gap;          // expected minimum gap between elements
    float position_tolerance;    // allowed deviation (default 1px)
    // Phase 5: navigate fields
    char* navigate_url;          // path to HTML file
    // Phase 5: assert_element_at fields
    char* expected_at_selector;  // expected element selector at coords
    char* expected_at_tag;       // expected tag name at coords
    int at_x, at_y;             // coordinates to test
    // Phase 5c: auto-waiting on assertions
    int assert_timeout;          // max wait time in ms (0 = no retry, default)
    int assert_interval;         // retry interval in ms (default 100)
    // Phase 5f: switch_frame fields
    char* frame_selector;        // CSS selector for iframe element (NULL = switch to main)
    // drag_and_drop / assert_attribute fields
    char* attribute_name;        // HTML attribute name (for assert_attribute)
    int drag_steps;              // number of intermediate mouse_move steps (default 5)
    // assert_count fields
    int assert_count_expected;   // exact expected count (-1 = not set)
    int assert_count_min;        // minimum expected count (-1 = not set)
    int assert_count_max;        // maximum expected count (-1 = not set)
    // Phase 7: assert_snapshot fields
    char* snapshot_reference;    // path to reference PNG
    float snapshot_threshold;    // max mismatch %, default 1.0
    char* snapshot_diff_path;    // optional: save diff image on failure
    char* snapshot_actual_path;  // optional: save actual image for debugging
    // Phase 7: advance_time fields
    int advance_steps;           // number of tick steps (0 = auto from ms/16)
    // Phase 7: assert_style animated flag
    bool style_animated;         // read from live ViewSpan instead of CSS cascade
    float style_tolerance;       // tolerance for animated float comparison (default 0.05)
    // Phase 7: assert_scroll negate flag
    bool negate_scroll;          // invert assertion (pass when NOT at expected position)
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
    int default_timeout;         // default assertion timeout in ms (0 = no retry)
    // Phase 5b: navigation history stack
    void* nav_history[16];       // stack of DomDocument* for navigate_back
    int nav_history_depth;       // current depth in nav_history
    // Phase 5f: iframe frame stack
    void* original_document;     // main document (DomDocument*) before any switch_frame
    void* frame_stack[8];        // stack of DomDocument* for nested switch_frame
    int frame_stack_depth;       // current depth in frame_stack
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
