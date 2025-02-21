#pragma once

// typedef enum  {
//     RDT_EVENT_NIL = 0,
//     RDT_EVENT_MOUSE_DOWN,
//     RDT_EVENT_MOUSE_UP,
//     RDT_EVENT_MOUSE_MOVE,
//     RDT_EVENT_MOUSE_SCROLL,
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
// } EventType;

typedef union RdtEvent {
    struct {
        Uint32 type;        // SDL_EventType
        Uint32 timestamp;   /**< In milliseconds, populated using SDL_GetTicks() */
    };
    SDL_MouseMotionEvent mouse_motion;
    SDL_MouseButtonEvent mouse_button;
    SDL_MouseWheelEvent mouse_wheel;
    SDL_KeyboardEvent key;
    SDL_WindowEvent window;
    SDL_TextEditingEvent text_edit;
    SDL_TextInputEvent text_input;
} RdtEvent;

typedef struct {
    bool is_mouse_down;
    float down_x, down_y;  // mouse position when mouse down
    PropValue cursor;  // current cursor style
    SDL_Cursor* sdl_cursor;
} MouseState;