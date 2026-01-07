#pragma once

typedef enum  {
    RDT_EVENT_NIL = 0,
    RDT_EVENT_MOUSE_DOWN,
    RDT_EVENT_MOUSE_UP,
    RDT_EVENT_MOUSE_MOVE,
    RDT_EVENT_MOUSE_DRAG,
    RDT_EVENT_SCROLL,
    RDT_EVENT_KEY_DOWN,
    RDT_EVENT_KEY_UP,
    RDT_EVENT_TEXT_INPUT,
    RDT_EVENT_FOCUS_IN,
    RDT_EVENT_FOCUS_OUT,
    RDT_EVENT_CLICK,
    RDT_EVENT_DBL_CLICK,
} EventType;

typedef struct Event {
    EventType type;
    double timestamp;  // in seconds, populated using glfwGetTime()
} Event;

// mouse/pointer motion event
typedef struct MousePositionEvent : Event {
    int x;      // X coordinate, relative to window
    int y;      // Y coordinate, relative to window
} MousePositionEvent;

// mouse click events
typedef struct MouseButtonEvent : MousePositionEvent {
    uint8_t button;     // mouse button index
    uint8_t clicks;     // 1 for single-click, 2 for double-click, etc.
} MouseButtonEvent;

// mouse/touchpad scroll event
typedef struct ScrollEvent : MousePositionEvent{
    float xoffset;        // horizontal scroll offset, can have fractional value
    float yoffset;        // vertical scroll offset, can have fractional value
} ScrollEvent;

// Keyboard modifier flags
#define RDT_MOD_SHIFT   (1 << 0)
#define RDT_MOD_CTRL    (1 << 1)
#define RDT_MOD_ALT     (1 << 2)
#define RDT_MOD_SUPER   (1 << 3)  // Cmd on macOS, Win on Windows

// Virtual key codes (subset of common keys)
typedef enum {
    RDT_KEY_UNKNOWN = 0,
    // Navigation keys
    RDT_KEY_LEFT = 263,
    RDT_KEY_RIGHT = 262,
    RDT_KEY_UP = 265,
    RDT_KEY_DOWN = 264,
    RDT_KEY_HOME = 268,
    RDT_KEY_END = 269,
    RDT_KEY_PAGE_UP = 266,
    RDT_KEY_PAGE_DOWN = 267,
    // Editing keys
    RDT_KEY_BACKSPACE = 259,
    RDT_KEY_DELETE = 261,
    RDT_KEY_ENTER = 257,
    RDT_KEY_TAB = 258,
    RDT_KEY_ESCAPE = 256,
    // Clipboard keys (A, C, V, X, Z)
    RDT_KEY_A = 65,
    RDT_KEY_C = 67,
    RDT_KEY_V = 86,
    RDT_KEY_X = 88,
    RDT_KEY_Z = 90,
} RdtKeyCode;

// Keyboard key event
typedef struct KeyEvent : Event {
    int key;              // virtual key code (RdtKeyCode)
    int scancode;         // platform-specific scancode
    int mods;             // modifier flags (RDT_MOD_*)
} KeyEvent;

// Text input event (Unicode character input)
typedef struct TextInputEvent : Event {
    uint32_t codepoint;   // Unicode codepoint (UTF-32)
} TextInputEvent;

// Focus change event
typedef struct FocusEvent : Event {
    void* target;         // element gaining/losing focus (View*)
    void* related;        // element losing/gaining focus (View*)
} FocusEvent;

typedef union RdtEvent {
    struct {
        EventType type;
        double timestamp;  // in seconds, populated using glfwGetTime()
    };
    MousePositionEvent mouse_position;
    MouseButtonEvent mouse_button;
    ScrollEvent scroll;
    KeyEvent key;
    TextInputEvent text_input;
    FocusEvent focus;
} RdtEvent;

typedef struct {
    bool is_mouse_down;
    float down_x, down_y;  // mouse position when mouse down
    CssEnum cursor;  // current cursor style
    GLFWcursor* sys_cursor;
} MouseState;
