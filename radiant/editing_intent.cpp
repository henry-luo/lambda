#include "event.hpp"

#include "../lib/memtrack.h"
#include "../lib/utf.h"

#include <string.h>

const char* clipboard_get_text();

InputIntent::InputIntent()
    : type(INPUT_INTENT_NONE),
      data(nullptr),
      html_data(nullptr),
      data_mime(nullptr),
      owned_data(nullptr),
      owned_html_data(nullptr),
      key(0),
      mods(0),
      is_composing(false),
      composition_caret(0),
      history_value(nullptr),
      history_sel_start(0),
      history_sel_end(0),
      option_index(-1),
      command(nullptr) {}

InputIntent::~InputIntent() {
    // paste/drop intents may own payload copies; scope cleanup keeps every
    // dispatch exit path from leaking or double-owning those buffers.
    input_intent_dispose(this);
}

void input_intent_dispose(InputIntent* intent) {
    if (!intent) return;
    mem_free(intent->owned_data);
    mem_free(intent->owned_html_data);
    intent->owned_data = nullptr;
    intent->owned_html_data = nullptr;
    intent->data = nullptr;
    intent->html_data = nullptr;
    intent->data_mime = nullptr;
}

bool input_intent_clone(const InputIntent* source, InputIntent* destination) {
    if (!source || !destination) return false;
    input_intent_dispose(destination);
    destination->type = source->type;
    destination->key = source->key;
    destination->mods = source->mods;
    destination->is_composing = source->is_composing;
    destination->composition_caret = source->composition_caret;
    destination->option_index = source->option_index;
    destination->command = source->command;
    if (source->data) {
        destination->owned_data = mem_strdup(source->data, MEM_CAT_TEMP);
        if (!destination->owned_data) goto fail;
        destination->data = destination->owned_data;
    }
    if (source->html_data) {
        destination->owned_html_data = mem_strdup(source->html_data, MEM_CAT_TEMP);
        if (!destination->owned_html_data) goto fail;
        destination->html_data = destination->owned_html_data;
    }
    destination->data_mime = source->data_mime;
    return true;

fail:
    // The edit dispatch owns its payload across arbitrary listeners; do not
    // leave a half-copied DataTransfer snapshot reachable on failure.
    input_intent_dispose(destination);
    destination->type = INPUT_INTENT_NONE;
    return false;
}

static void input_intent_reset(InputIntent* intent) {
    if (!intent) return;
    input_intent_dispose(intent);
    intent->type = INPUT_INTENT_NONE;
    intent->data_mime = nullptr;
    intent->key = 0;
    intent->mods = 0;
    intent->is_composing = false;
    intent->composition_caret = 0;
    intent->command = nullptr;
}

typedef struct InputIntentName {
    InputIntentType type;
    const char* name;
} InputIntentName;

static const InputIntentName s_input_intent_names[] = {
    {INPUT_INTENT_INSERT_TEXT, "insertText"},
    {INPUT_INTENT_INSERT_REPLACEMENT_TEXT, "insertReplacementText"},
    {INPUT_INTENT_INSERT_PARAGRAPH, "insertParagraph"},
    {INPUT_INTENT_INSERT_LINE_BREAK, "insertLineBreak"},
    {INPUT_INTENT_INSERT_HORIZONTAL_RULE, "insertHorizontalRule"},
    {INPUT_INTENT_INSERT_IMAGE, "insertImage"},
    {INPUT_INTENT_INSERT_LINK, "insertLink"},
    {INPUT_INTENT_INSERT_FROM_PASTE, "insertFromPaste"},
    {INPUT_INTENT_INSERT_FROM_PASTE_AS_QUOTATION, "insertFromPasteAsQuotation"},
    {INPUT_INTENT_INSERT_FROM_YANK, "insertFromYank"},
    {INPUT_INTENT_INSERT_FROM_DROP, "insertFromDrop"},
    {INPUT_INTENT_DELETE_CONTENT_BACKWARD, "deleteContentBackward"},
    {INPUT_INTENT_DELETE_CONTENT_FORWARD, "deleteContentForward"},
    {INPUT_INTENT_DELETE_WORD_BACKWARD, "deleteWordBackward"},
    {INPUT_INTENT_DELETE_WORD_FORWARD, "deleteWordForward"},
    {INPUT_INTENT_DELETE_SOFT_LINE_BACKWARD, "deleteSoftLineBackward"},
    {INPUT_INTENT_DELETE_SOFT_LINE_FORWARD, "deleteSoftLineForward"},
    {INPUT_INTENT_DELETE_HARD_LINE_BACKWARD, "deleteHardLineBackward"},
    {INPUT_INTENT_DELETE_HARD_LINE_FORWARD, "deleteHardLineForward"},
    {INPUT_INTENT_DELETE_BY_CUT, "deleteByCut"},
    {INPUT_INTENT_DELETE_BY_DRAG, "deleteByDrag"},
    {INPUT_INTENT_COMPOSITION_START, "compositionStart"},
    {INPUT_INTENT_INSERT_COMPOSITION_TEXT, "insertCompositionText"},
    {INPUT_INTENT_INSERT_FROM_COMPOSITION, "insertFromComposition"},
    {INPUT_INTENT_DELETE_COMPOSITION_TEXT, "deleteCompositionText"},
    {INPUT_INTENT_FORMAT_UNLINK, "unlink"},
    {INPUT_INTENT_FORMAT_BOLD, "formatBold"},
    {INPUT_INTENT_FORMAT_ITALIC, "formatItalic"},
    {INPUT_INTENT_FORMAT_UNDERLINE, "formatUnderline"},
    {INPUT_INTENT_FORMAT_STRIKETHROUGH, "formatStrikeThrough"},
    {INPUT_INTENT_FORMAT_SUBSCRIPT, "formatSubscript"},
    {INPUT_INTENT_FORMAT_SUPERSCRIPT, "formatSuperscript"},
    {INPUT_INTENT_FORMAT_FORE_COLOR, "formatForeColor"},
    {INPUT_INTENT_FORMAT_BACK_COLOR, "formatBackColor"},
    {INPUT_INTENT_FORMAT_HILITE_COLOR, "formatHiliteColor"},
    {INPUT_INTENT_FORMAT_FONT_NAME, "formatFontName"},
    {INPUT_INTENT_FORMAT_FONT_SIZE, "formatFontSize"},
    {INPUT_INTENT_FORMAT_REMOVE, "formatRemove"},
    {INPUT_INTENT_FORMAT_BLOCK, "formatBlock"},
    {INPUT_INTENT_FORMAT_JUSTIFY_LEFT, "formatJustifyLeft"},
    {INPUT_INTENT_FORMAT_JUSTIFY_CENTER, "formatJustifyCenter"},
    {INPUT_INTENT_FORMAT_JUSTIFY_RIGHT, "formatJustifyRight"},
    {INPUT_INTENT_FORMAT_JUSTIFY_FULL, "formatJustifyFull"},
    {INPUT_INTENT_FORMAT_ORDERED_LIST, "insertOrderedList"},
    {INPUT_INTENT_FORMAT_UNORDERED_LIST, "insertUnorderedList"},
    {INPUT_INTENT_FORMAT_INDENT, "formatIndent"},
    {INPUT_INTENT_FORMAT_OUTDENT, "formatOutdent"},
    {INPUT_INTENT_SELECT_ALL, "selectAll"},
    {INPUT_INTENT_HISTORY_UNDO, "historyUndo"},
    {INPUT_INTENT_HISTORY_REDO, "historyRedo"},
    {INPUT_INTENT_COPY, "copy"},
};

const char* input_intent_type_name(InputIntentType type) {
    for (size_t i = 0; i < sizeof(s_input_intent_names) / sizeof(s_input_intent_names[0]); i++) {
        if (s_input_intent_names[i].type == type) return s_input_intent_names[i].name;
    }
    return "";
}

bool input_intent_is_dispatchable(InputIntentType type) {
    // Formatting intents occupy one contiguous non-beforeinput enum range.
    return type != INPUT_INTENT_COMPOSITION_START &&
        type != INPUT_INTENT_INSERT_IMAGE &&
        type != INPUT_INTENT_SELECT_ALL &&
        type != INPUT_INTENT_COPY &&
        (type < INPUT_INTENT_FORMAT_UNLINK || type > INPUT_INTENT_FORMAT_OUTDENT);
}



// F11: the inverse of input_intent_type_name uses the same table, so the two
// cannot drift.
InputIntentType input_intent_type_from_name(const char* name) {
    if (!name) return INPUT_INTENT_NONE;
    for (size_t i = 0; i < sizeof(s_input_intent_names) / sizeof(s_input_intent_names[0]); i++) {
        if (strcmp(s_input_intent_names[i].name, name) == 0) return s_input_intent_names[i].type;
    }
    return INPUT_INTENT_NONE;
}

bool input_intent_from_name(const char* name, InputIntent* out) {
    if (!out) return false;
    input_intent_reset(out);

    InputIntentType type = input_intent_type_from_name(name);
    if (type == INPUT_INTENT_NONE) return false;

    // Clipboard snapshots belong to the execution waist, not to key policy.
    if (type == INPUT_INTENT_INSERT_FROM_PASTE) {
        const char* html = clipboard_store_read_mime("text/html");
        if (html && html[0]) {
            out->owned_html_data = mem_strdup(html, MEM_CAT_TEMP);
            out->html_data = out->owned_html_data;
        }
        const char* clip = clipboard_get_text();
        if ((!clip || !clip[0]) && (!html || !html[0])) return false;
        out->owned_data = mem_strdup(clip ? clip : "", MEM_CAT_TEMP);
        out->data = out->owned_data;
        out->data_mime = (out->html_data && out->html_data[0])
            ? "text/html" : "text/plain";
    }
    out->type = type;
    return true;
}

extern "C" bool radiant_dispatch_behavior_key_intent(View* target, const InputIntent* intent);
extern "C" uint64_t radiant_key_intent_epoch(void);
extern "C" const char* radiant_key_intent_name(void);

// F11: which key means which edit intent is policy, and it lived here as a
// twelve-branch table while the text-control path carried its own copy. The
// table is now one rule set in the dom package; this resolves the name the
// template returns back to a type and fills the payload, which was never policy.
//
// Dispatched from the **caret view**, not from the editing surface. On a
// template-rendered page `active_surface.view` can hand back a node from a
// superseded render generation whose parent link has been cleared, and a walk up
// a detached chain never reaches <body> — it just looks like "no template wanted
// this event". That silent failure is what sank the first attempt; the caret
// view is resolved against the live tree.
bool input_intent_from_key_event(DocState* state, const KeyEvent* key_event,
                                 InputIntent* out) {
    if (!key_event || !out) return false;
    input_intent_reset(out);
    out->key = key_event->key;
    out->mods = key_event->mods;
    if (!state) return false;

    // Dispatched at the document's <body>, not at the caret's element. Both the
    // editing surface and the caret view can be nodes from a superseded render
    // generation this early in the keydown — their parent chain dead-ends on a
    // detached div and never reaches <body>, which reads as "no template wanted
    // this" rather than as an error. Walking *down* to the body from the
    // document sidesteps that entirely, and costs nothing in fidelity: this
    // mapping reads only the key and modifiers, never the target element.
    DomDocument* doc = state->owner_store ? state->owner_store->document : nullptr;
    DomElement* body = radiant_document_body_element(doc);
    if (!body) return false;
    View* dispatch_target = static_cast<View*>(body);

    uint64_t epoch_before = radiant_key_intent_epoch();
    InputIntent key_carrier;
    key_carrier.key = key_event->key;
    key_carrier.mods = key_event->mods;
    // No EventContext by design: every other behavior hook is a default action
    // and ES5 suppresses those after preventDefault, but this one is a
    // *translation* — a JS editor that prevents the keydown still relies on the
    // intent, and passing evcon here silenced every such editor in the suite.
    radiant_dispatch_behavior_key_intent(dispatch_target, &key_carrier);
    if (radiant_key_intent_epoch() == epoch_before) return false;

    return input_intent_from_name(radiant_key_intent_name(), out);
}

bool input_intent_from_text_input(uint32_t codepoint, InputIntent* out,
                                  char* utf8_buf, size_t utf8_buf_size) {
    if (!out || !utf8_buf || utf8_buf_size < 5 || codepoint == 0) return false;
    input_intent_reset(out);
    size_t utf8_len = utf8_encode_z(codepoint, utf8_buf);
    if (utf8_len == 0) return false;
    out->type = INPUT_INTENT_INSERT_TEXT;
    out->data = utf8_buf;
    return true;
}

bool input_intent_from_composition_event(const CompositionEvent* comp_event,
                                         InputIntent* out) {
    if (!comp_event || !out) return false;
    input_intent_reset(out);
    out->data = comp_event->text ? comp_event->text : "";
    out->composition_caret = comp_event->preedit_caret;

    if (comp_event->type == RDT_EVENT_COMPOSITION_START) {
        out->type = INPUT_INTENT_COMPOSITION_START;
        out->is_composing = true;
        return true;
    }
    if (comp_event->type == RDT_EVENT_COMPOSITION_UPDATE) {
        out->type = INPUT_INTENT_INSERT_COMPOSITION_TEXT;
        out->is_composing = true;
        return true;
    }
    if (comp_event->type == RDT_EVENT_COMPOSITION_END) {
        out->type = comp_event->text && comp_event->text[0]
            ? INPUT_INTENT_INSERT_FROM_COMPOSITION
            : INPUT_INTENT_DELETE_COMPOSITION_TEXT;
        out->is_composing = false;
        return true;
    }
    return false;
}
