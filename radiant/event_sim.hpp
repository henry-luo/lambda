/**
 * Event Simulation System for Radiant Viewer
 * 
 * Allows loading and replaying events from a JSON file for automated testing.
 * 
 * JSON Format:
 * {
 *   "events": [
 *     {"type": "wait", "ms": 500},
 *     {"type": "mouse_move", "x": 100, "y": 200},
 *     {"type": "mouse_down", "x": 100, "y": 200, "button": 0, "mods": 0},
 *     {"type": "mouse_down", "target_text": "Click here"},  // find text and click
 *     {"type": "mouse_up", "x": 100, "y": 200, "button": 0, "mods": 0},
 *     {"type": "mouse_drag", "from_x": 100, "from_y": 200, "to_x": 200, "to_y": 200},
 *     {"type": "key_press", "key": "a"},
 *     {"type": "key_down", "key": "Control"},
 *     {"type": "key_up", "key": "Control"},
 *     {"type": "key_combo", "key": "c", "mods": ["ctrl"]},
 *     {"type": "scroll", "x": 100, "y": 200, "dx": 0, "dy": -3},
 *     {"type": "assert_caret", "view_type": 4, "char_offset": 5},
 *     {"type": "assert_selection", "is_collapsed": false},
 *     {"type": "assert_target", "view_type": 4},
 *     {"type": "log", "message": "Test step completed"},
 *     {"type": "render", "file": "/tmp/output.png"},
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
    SIM_EVENT_ASSERT_CARET,
    SIM_EVENT_ASSERT_SELECTION,
    SIM_EVENT_ASSERT_TARGET,
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
    float scroll_dx, scroll_dy;  // scroll offsets
    char* message;               // for log events
    char* file_path;             // for render/dump_caret events
    char* target_text;           // for mouse events: find text and click on it
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
