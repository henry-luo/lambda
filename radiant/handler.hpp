#include "view.hpp"
#include "event.hpp"

typedef struct EventContext {
    RdtEvent event;
    View* target;
    TextRect* target_text_rect;
    float offset_x, offset_y;  // mouse offset from target view

    // style context
    BlockBlot block;
    FontBox font;  // current font style

    // effects fields
    PropValue new_cursor;
    char* new_url;
    char* new_target;
    bool need_repaint;

    UiContext* ui_context;
} EventContext;
