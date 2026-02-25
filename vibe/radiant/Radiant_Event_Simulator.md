# Radiant Event Simulator

## Overview

The Radiant Event Simulator provides automated testing capabilities for the `lambda view` command by simulating user input events (mouse clicks, keyboard input, scrolling) from a JSON configuration file. This enables reproducible testing of UI interactions without manual intervention.

## Features

- **Automated Event Playback**: Load and execute sequences of UI events from JSON files
- **Assertion Support**: Verify caret position, selection state, and target views
- **Timing Control**: Configure delays between events for realistic interaction patterns
- **Results Reporting**: Summary of passed/failed assertions with detailed error logging
- **Auto-close**: Window automatically closes when simulation completes
- **Render Capture**: Capture current view state to PNG/SVG including caret and selection
- **State Debugging**: Dump caret/selection state to text files for inspection

## Architecture

### Files

| File | Purpose |
|------|---------|
| `radiant/event_sim.hpp` | Header with type definitions and API declarations |
| `radiant/event_sim.cpp` | Implementation of event simulation logic |
| `radiant/window.cpp` | Integration with main event loop |
| `lambda/main.cpp` | CLI parameter handling (`--event-file`) |

### Data Structures

```cpp
// Event types supported by the simulator
enum SimEventType {
    SIM_EVENT_WAIT,           // Wait for specified duration
    SIM_EVENT_MOUSE_MOVE,     // Move cursor to position
    SIM_EVENT_MOUSE_DOWN,     // Press mouse button
    SIM_EVENT_MOUSE_UP,       // Release mouse button
    SIM_EVENT_MOUSE_DRAG,     // Drag from one position to another
    SIM_EVENT_KEY_PRESS,      // Press and release key
    SIM_EVENT_KEY_DOWN,       // Press key down
    SIM_EVENT_KEY_UP,         // Release key
    SIM_EVENT_KEY_COMBO,      // Key with modifiers (e.g., Ctrl+C)
    SIM_EVENT_SCROLL,         // Scroll event
    SIM_EVENT_ASSERT_CARET,   // Verify caret state
    SIM_EVENT_ASSERT_SELECTION, // Verify selection state
    SIM_EVENT_ASSERT_TARGET,  // Verify target view type
    SIM_EVENT_LOG,            // Log message to stderr
    SIM_EVENT_RENDER,         // Render to PNG/SVG file
    SIM_EVENT_DUMP_CARET      // Dump caret state to file
};

// Individual event command
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
};

// Simulation context
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
```

## CLI Usage

```bash
# Basic usage
./lambda.exe view <html-file> --event-file <events.json>

# Examples
./lambda.exe view test/html/table.html --event-file test/events/test_caret.json
./lambda.exe view index.html --event-file test/events/click_test.json
```

### Options

| Option | Description |
|--------|-------------|
| `--event-file <path>` | Path to JSON file containing events to simulate |

## JSON Event File Format

```json
{
  "description": "Optional description of the test",
  "events": [
    {"type": "event_type", ...event_params...},
    ...
  ]
}
```

## Supported Event Types

### wait
Wait for a specified duration before the next event.

```json
{"type": "wait", "ms": 500}
```

| Field | Type | Description |
|-------|------|-------------|
| `ms` | int | Milliseconds to wait (default: 100) |

### mouse_move
Move the mouse cursor to a position.

```json
{"type": "mouse_move", "x": 100, "y": 200}
```

Alternatively, use `target_text` to move to text content:

```json
{"type": "mouse_move", "target_text": "Hover here"}
```

| Field | Type | Description |
|-------|------|-------------|
| `x` | int | X coordinate (pixels from left) |
| `y` | int | Y coordinate (pixels from top) |
| `target_text` | string | Text content to find and move to (alternative to x/y) |

### mouse_down
Press a mouse button at a position.

```json
{"type": "mouse_down", "x": 100, "y": 200, "button": 0, "mods": 0}
```

Alternatively, use `target_text` to click on text content:

```json
{"type": "mouse_down", "target_text": "Click here"}
```

| Field | Type | Description |
|-------|------|-------------|
| `x` | int | X coordinate |
| `y` | int | Y coordinate |
| `button` | int | Button index: 0=left, 1=right, 2=middle |
| `mods` | int | Modifier flags (optional) |
| `mods_str` | string | Modifier string like "shift", "ctrl", "alt", "super" |
| `target_text` | string | Text content to find and click on (alternative to x/y) |

### mouse_up
Release a mouse button at a position.

```json
{"type": "mouse_up", "x": 100, "y": 200, "button": 0}
```

| Field | Type | Description |
|-------|------|-------------|
| `x` | int | X coordinate |
| `y` | int | Y coordinate |
| `button` | int | Button index |
| `mods` | int | Modifier flags (optional) |

### mouse_drag
Drag from one position to another (combines down, move, up).

```json
{"type": "mouse_drag", "from_x": 100, "from_y": 200, "to_x": 300, "to_y": 200, "button": 0}
```

| Field | Type | Description |
|-------|------|-------------|
| `from_x`, `from_y` | int | Starting position |
| `to_x`, `to_y` | int | Ending position |
| `button` | int | Button index (default: 0) |

### key_press
Press and release a key (generates both key_down and key_up).

```json
{"type": "key_press", "key": "a", "mods_str": "ctrl"}
```

| Field | Type | Description |
|-------|------|-------------|
| `key` | string | Key name (see Key Names below) |
| `mods` | int | Modifier flags (optional) |
| `mods_str` | string | Modifier string (optional) |

### key_down
Press a key without releasing.

```json
{"type": "key_down", "key": "shift"}
```

| Field | Type | Description |
|-------|------|-------------|
| `key` | string | Key name |

### key_up
Release a previously pressed key.

```json
{"type": "key_up", "key": "shift"}
```

| Field | Type | Description |
|-------|------|-------------|
| `key` | string | Key name |

### key_combo
Press a key with modifiers (common shortcut pattern).

```json
{"type": "key_combo", "key": "c", "mods_str": "ctrl"}
```

| Field | Type | Description |
|-------|------|-------------|
| `key` | string | Key name |
| `mods_str` | string | Modifier string: "shift", "ctrl", "alt", "super/cmd" |

### scroll
Scroll at a position.

```json
{"type": "scroll", "x": 100, "y": 200, "dx": 0, "dy": -3}
```

| Field | Type | Description |
|-------|------|-------------|
| `x`, `y` | int | Scroll position |
| `dx` | float | Horizontal scroll offset |
| `dy` | float | Vertical scroll offset (negative = scroll down) |

### assert_caret
Verify the caret (text cursor) state.

```json
{"type": "assert_caret", "view_type": 1, "char_offset": 5}
```

| Field | Type | Description |
|-------|------|-------------|
| `view_type` | int | Expected ViewType of caret's view (-1 to skip check) |
| `char_offset` | int | Expected character offset (-1 to skip check) |

### assert_selection
Verify the selection state.

```json
{"type": "assert_selection", "is_collapsed": false}
```

| Field | Type | Description |
|-------|------|-------------|
| `is_collapsed` | bool | Whether selection should be collapsed (no selection) |

### assert_target
Verify the target view type.

```json
{"type": "assert_target", "view_type": 1}
```

| Field | Type | Description |
|-------|------|-------------|
| `view_type` | int | Expected ViewType of the current target |

### log
Print a message to stderr.

```json
{"type": "log", "message": "Step 1 complete"}
```

| Field | Type | Description |
|-------|------|-------------|
| `message` | string | Message to print |

### render
Render the current view (including caret and selection) to an image file.

```json
{"type": "render", "file": "/tmp/output.png"}
```

| Field | Type | Description |
|-------|------|-------------|
| `file` | string | Path to output file (.png or .svg) |

**Supported formats:**
- `.png` - PNG image (includes caret/selection overlays)
- `.svg` - SVG vector output

### dump_caret
Dump the current caret and selection state to a text file for debugging.

```json
{"type": "dump_caret", "file": "./caret_state.txt"}
```

| Field | Type | Description |
|-------|------|-------------|
| `file` | string | Path to output file (optional, defaults to `./view_tree.txt`) |

The output includes:
- Caret view pointer and type
- Character offset, line, column
- Visual position (x, y) and height
- Visibility state
- Selection anchor/focus offsets
- Selection collapsed state

## Key Names

The simulator recognizes the following key names:

### Single Characters
- `a` - `z` (case-insensitive)
- `0` - `9`

### Special Keys
| Key Name | Aliases |
|----------|---------|
| `space` | |
| `enter` | `return` |
| `tab` | |
| `backspace` | |
| `delete` | |
| `escape` | `esc` |
| `left`, `right`, `up`, `down` | Arrow keys |
| `home`, `end` | |
| `pageup`, `pagedown` | |
| `control` | `ctrl` |
| `shift` | |
| `alt` | |
| `super` | `cmd`, `meta` |
| `f1` - `f12` | Function keys |

## View Types

The `view_type` field in assertions corresponds to the `ViewType` enum:

| Value | Name | Description |
|-------|------|-------------|
| 0 | `RDT_VIEW_NONE` | No view |
| 1 | `RDT_VIEW_TEXT` | Text content |
| 2 | `RDT_VIEW_BR` | Line break |
| 3 | `RDT_VIEW_MARKER` | List marker |
| 4 | `RDT_VIEW_INLINE` | Inline element |
| 5 | `RDT_VIEW_MATH` | Math content |
| 6 | `RDT_VIEW_INLINE_BLOCK` | Inline-block element |
| 7 | `RDT_VIEW_BLOCK` | Block element |
| 8 | `RDT_VIEW_LIST_ITEM` | List item |
| 9 | `RDT_VIEW_TABLE` | Table |
| 10 | `RDT_VIEW_TABLE_ROW_GROUP` | Table row group (thead/tbody/tfoot) |
| 11 | `RDT_VIEW_TABLE_ROW` | Table row |
| 12 | `RDT_VIEW_TABLE_CELL` | Table cell |

## Example Test File

```json
{
  "description": "Test caret positioning when clicking on text",
  "events": [
    {"type": "log", "message": "=== Starting caret test ==="},
    {"type": "wait", "ms": 200},
    
    {"type": "log", "message": "Step 1: Click on text at position (200, 200)"},
    {"type": "mouse_down", "x": 200, "y": 200, "button": 0, "mods": 0},
    {"type": "mouse_up", "x": 200, "y": 200, "button": 0, "mods": 0},
    {"type": "wait", "ms": 100},
    
    {"type": "log", "message": "Step 2: Verify caret was set to TEXT view (view_type=1)"},
    {"type": "assert_caret", "view_type": 1, "char_offset": -1},
    
    {"type": "log", "message": "=== Caret test complete ==="},
    {"type": "wait", "ms": 500}
  ]
}
```

## Output

The simulator produces output in several forms:

### stderr Output
Log messages and assertion results are printed to stderr:
```
[EVENT_SIM] === Starting caret test ===
[EVENT_SIM] Step 1: Click on text at position (200, 200)
[EVENT_SIM] Step 2: Verify caret was set to TEXT view (view_type=1)
```

### Results Summary
After simulation completes, a summary is printed:
```
========================================
 EVENT SIMULATION RESULTS
========================================
 Events executed: 10
 Assertions: 1 passed, 0 failed
 Result: PASS
========================================
```

### Log File
Detailed event processing is logged to `log.txt`:
```
12:48:26 [INFO] event_sim: loading event file 'test/events/test_caret.json'
12:48:26 [INFO] event_sim: parsing 10 events
12:48:26 [INFO] event_sim: loaded 10 events successfully
12:48:26 [INFO] event_sim: mouse_down at (200, 200) button=0
12:48:26 [INFO] event_sim: assert_caret PASS
```

## API Reference

### Functions

```cpp
// Load events from JSON file
// Returns NULL on error
EventSimContext* event_sim_load(const char* json_file);

// Free simulation context
void event_sim_free(EventSimContext* ctx);

// Process next event if ready
// Returns true if simulation is still running
bool event_sim_update(EventSimContext* ctx, void* uicon, GLFWwindow* window, double current_time);

// Print simulation results summary
void event_sim_print_results(EventSimContext* ctx);
```

### Integration Example

```cpp
// In window.cpp main loop
EventSimContext* sim_ctx = NULL;
if (event_file) {
    sim_ctx = event_sim_load(event_file);
}

while (!glfwWindowShouldClose(window)) {
    // ... render frame ...
    
    if (sim_ctx) {
        double currentTime = glfwGetTime();
        bool sim_running = event_sim_update(sim_ctx, &ui_context, window, currentTime);
        if (!sim_running && sim_ctx->auto_close) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }
    
    glfwPollEvents();
}

if (sim_ctx) {
    event_sim_free(sim_ctx);
}
```

## Best Practices

1. **Add wait events** between mouse down and up to simulate realistic click timing
2. **Use log events** to mark test phases for easier debugging
3. **Test coordinates carefully** - use browser dev tools to find correct positions
4. **Start with simple tests** before building complex interaction sequences
5. **Check view types** using the debug output before writing assertions

## Troubleshooting

### Assertions Failing
- Check that click coordinates actually hit the expected element
- Verify view_type values match the ViewType enum (TEXT=1, not 4)
- Add `log` events to trace execution

### Events Not Firing
- Ensure the event file path is correct
- Check log.txt for parsing errors
- Verify JSON syntax is valid

### Window Not Auto-closing
- Check if `auto_close` is set in context
- Look for assertion failures that might indicate incomplete simulation

## Future Enhancements

- [ ] Text input simulation (character input events)
- [ ] Screenshot capture at specific points
- [ ] Comparison with reference images
- [ ] Conditional event execution
- [ ] Loop and branching support
- [ ] Record mode to capture real interactions
