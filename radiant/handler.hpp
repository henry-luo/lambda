#include "view.hpp"
#include "event.hpp"

// Forward declaration
struct ViewText;
struct TextRect;

typedef struct EventContext {
    RdtEvent event;
    View* target;
    TextRect* target_text_rect;
    float offset_x, offset_y;  // mouse offset from target view

    // style context
    BlockBlot block;
    FontBox font;  // current font style

    // effects fields
    CssEnum new_cursor;
    char* new_url;
    char* new_target;
    bool need_repaint;

    UiContext* ui_context;
} EventContext;

/**
 * Calculate character offset from mouse click position within a text rect
 * Returns the character offset closest to the click position
 */
int calculate_char_offset_from_position(EventContext* evcon, ViewText* text, 
    TextRect* rect, int mouse_x, int mouse_y);
