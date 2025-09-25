#pragma once

typedef enum  {
    RDT_EVENT_NIL = 0,
    RDT_EVENT_MOUSE_DOWN,
    RDT_EVENT_MOUSE_UP,
    RDT_EVENT_MOUSE_MOVE,
    RDT_EVENT_MOUSE_DRAG,
    RDT_EVENT_SCROLL,
//     RDT_EVENT_KEY_DOWN,
//     RDT_EVENT_KEY_UP,
//     RDT_EVENT_KEY_PRESS,
//     RDT_EVENT_FOCUS_IN,
//     RDT_EVENT_FOCUS_OUT,
//     RDT_EVENT_CLICK,
//     RDT_EVENT_DBL_CLICK,
//     RDT_EVENT_DRAG_START,
//     RDT_EVENT_DRAG_END,
//     RDT_EVENT_DRAG_ENTER,
//     RDT_EVENT_DRAG_LEAVE,
//     RDT_EVENT_DRAG_OVER,
//     RDT_EVENT_DROP,
//     RDT_EVENT_SCROLL,
//     RDT_EVENT_RESIZE,
//     RDT_EVENT_LOAD,
//     RDT_EVENT_UNLOAD,
//     RDT_EVENT_INPUT,
//     RDT_EVENT_SUBMIT,
//     RDT_EVENT_CHANGE,
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

typedef union RdtEvent {
    struct {
        EventType type;
        double timestamp;  // in seconds, populated using glfwGetTime()
    };
    MousePositionEvent mouse_position;
    MouseButtonEvent mouse_button;
    ScrollEvent scroll;
    // SDL_KeyboardEvent key;
    // SDL_WindowEvent window;
    // SDL_TextEditingEvent text_edit;
    // SDL_TextInputEvent text_input;
} RdtEvent;

typedef struct {
    bool is_mouse_down;
    float down_x, down_y;  // mouse position when mouse down
    PropValue cursor;  // current cursor style
    GLFWcursor* sys_cursor;
} MouseState;
