#pragma once

typedef enum  {
    RDT_EVENT_NIL = 0,
    RDT_EVENT_MOUSE_DOWN,
    RDT_EVENT_MOUSE_UP,
    RDT_EVENT_MOUSE_MOVE,
    RDT_EVENT_MOUSE_SCROLL,
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

typedef struct MouseMotionEvent{
    EventType type;        // RDT_EVENT_MOUSE_MOVE
    double timestamp;       // in seconds, populated using glfwGetTime()
    // Uint32 windowID;    /**< The window with mouse focus, if any */
    // Uint32 which;       /**< The mouse instance id, or SDL_TOUCH_MOUSEID */
    // Uint32 state;       /**< The current button state */
    int x;           /**< X coordinate, relative to window */
    int y;           /**< Y coordinate, relative to window */
    // Sint32 xrel;        /**< The relative motion in the X direction */
    // Sint32 yrel;        /**< The relative motion in the Y direction */
} MouseMotionEvent;

typedef struct MouseButtonEvent {
    EventType type;        // RDT_EVENT_MOUSE_DOWN or RDT_EVENT_MOUSE_UP
    double timestamp;   // in seconds, populated using glfwGetTime()
    // Uint32 windowID;    /**< The window with mouse focus, if any */
    // Uint32 which;       /**< The mouse instance id, or SDL_TOUCH_MOUSEID */
    uint8_t button;       /**< The mouse button index */
    // Uint8 state;        /**< SDL_PRESSED or SDL_RELEASED */
    uint8_t clicks;       /**< 1 for single-click, 2 for double-click, etc. */
    // Uint8 padding1;
    int x;           /**< X coordinate, relative to window */
    int y;           /**< Y coordinate, relative to window */
} MouseButtonEvent;

typedef union RdtEvent {
    struct {
        EventType type;
        uint32_t timestamp;   /**< In milliseconds, populated using SDL_GetTicks() */
    };
    MouseMotionEvent mouse_motion;
    MouseButtonEvent mouse_button;
    // SDL_MouseWheelEvent mouse_wheel;
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