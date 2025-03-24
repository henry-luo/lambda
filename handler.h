#include "view.h"
#include "event.h"

typedef struct EventContext {
    RdtEvent event;
    View* target;

    // style context
    BlockBlot block;
    FontBox font;  // current font style

    // effects fields
    PropValue new_cursor;
    char* new_uri;
    bool need_repaint;
    
    UiContext* ui_context;
} EventContext;