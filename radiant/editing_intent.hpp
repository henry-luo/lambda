#ifndef RADIANT_EDITING_INTENT_HPP
#define RADIANT_EDITING_INTENT_HPP

// shared input intent model for form text controls and contenteditable.
// canonical design: vibe/radiant/Radiant_Design_Editing2.md (§6.1).
// vibe/radiant/Radiant_Design_Editing.md E2 is phased out (historical record).

#include "event.hpp"

#include <stddef.h>
#include <stdint.h>

// CE-3 (Radiant_Design_Content_Editable.md §6.2): complete §6.2 inputType
// coverage. Entries marked "consumer-issued only" are NOT synthesized by
// Radiant; they exist so consumers can emit them through the same dispatcher.
typedef enum InputIntentType {
    INPUT_INTENT_NONE = 0,
    INPUT_INTENT_INSERT_TEXT,
    INPUT_INTENT_INSERT_REPLACEMENT_TEXT,
    INPUT_INTENT_INSERT_PARAGRAPH,
    INPUT_INTENT_INSERT_LINE_BREAK,
    INPUT_INTENT_INSERT_HORIZONTAL_RULE,
    INPUT_INTENT_INSERT_LINK,
    INPUT_INTENT_INSERT_FROM_PASTE,
    INPUT_INTENT_INSERT_FROM_PASTE_AS_QUOTATION,
    INPUT_INTENT_INSERT_FROM_YANK,
    INPUT_INTENT_INSERT_FROM_DROP,
    INPUT_INTENT_DELETE_CONTENT_BACKWARD,
    INPUT_INTENT_DELETE_CONTENT_FORWARD,
    INPUT_INTENT_DELETE_WORD_BACKWARD,
    INPUT_INTENT_DELETE_WORD_FORWARD,
    INPUT_INTENT_DELETE_SOFT_LINE_BACKWARD,
    INPUT_INTENT_DELETE_SOFT_LINE_FORWARD,
    INPUT_INTENT_DELETE_HARD_LINE_BACKWARD,
    INPUT_INTENT_DELETE_HARD_LINE_FORWARD,
    INPUT_INTENT_DELETE_BY_CUT,
    INPUT_INTENT_DELETE_BY_DRAG,
    INPUT_INTENT_COMPOSITION_START,
    INPUT_INTENT_INSERT_COMPOSITION_TEXT,
    INPUT_INTENT_INSERT_FROM_COMPOSITION,
    INPUT_INTENT_DELETE_COMPOSITION_TEXT,
    INPUT_INTENT_FORMAT_BOLD,
    INPUT_INTENT_FORMAT_ITALIC,
    INPUT_INTENT_FORMAT_UNDERLINE,
    INPUT_INTENT_FORMAT_STRIKETHROUGH,
    INPUT_INTENT_FORMAT_SUBSCRIPT,
    INPUT_INTENT_FORMAT_SUPERSCRIPT,
    INPUT_INTENT_FORMAT_BLOCK,
    INPUT_INTENT_FORMAT_JUSTIFY_LEFT,
    INPUT_INTENT_FORMAT_JUSTIFY_CENTER,
    INPUT_INTENT_FORMAT_JUSTIFY_RIGHT,
    INPUT_INTENT_FORMAT_JUSTIFY_FULL,
    INPUT_INTENT_FORMAT_ORDERED_LIST,
    INPUT_INTENT_FORMAT_UNORDERED_LIST,
    INPUT_INTENT_FORMAT_INDENT,
    INPUT_INTENT_FORMAT_OUTDENT,
    INPUT_INTENT_SELECT_ALL,
    INPUT_INTENT_HISTORY_UNDO,
    INPUT_INTENT_HISTORY_REDO,
} InputIntentType;

typedef struct InputIntent {
    InputIntentType type;
    const char* data;
    const char* html_data;
    const char* data_mime;
    char* owned_data;
    char* owned_html_data;
    int key;
    int mods;
    bool is_composing;
    uint32_t composition_caret;
} InputIntent;

typedef InputIntent EditingIntent;

void input_intent_dispose(InputIntent* intent);

const char* input_intent_type_name(InputIntentType type);
bool input_intent_is_dispatchable(InputIntentType type);

bool input_intent_from_key_event(const KeyEvent* key_event, InputIntent* out);
bool input_intent_from_text_input(uint32_t codepoint, InputIntent* out,
                                  char* utf8_buf, size_t utf8_buf_size);
bool input_intent_from_composition_event(const CompositionEvent* comp_event,
                                         InputIntent* out);

#endif // RADIANT_EDITING_INTENT_HPP
