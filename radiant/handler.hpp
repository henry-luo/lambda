#include "view.hpp"
#include "event.hpp"

// Forward declaration
struct ViewText;
struct TextRect;
struct RadiantState;

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

/**
 * Calculate visual position (x, y, height) from byte offset within a text rect
 * The target_offset is a byte offset aligned to UTF-8 character boundaries
 * Returns the x position relative to the text rect's origin
 */
void calculate_position_from_char_offset(EventContext* evcon, ViewText* text,
    TextRect* rect, int target_offset, float* out_x, float* out_y, float* out_height);

/**
 * Find the TextRect containing a given character offset
 * Returns the TextRect and updates the rect pointer, or NULL if not found
 */
TextRect* find_text_rect_for_offset(ViewText* text, int char_offset);

/**
 * Update caret visual position after movement operations
 * Must be called after caret_move, caret_move_line, caret_move_to
 */
void update_caret_visual_position(UiContext* uicon, RadiantState* state);
