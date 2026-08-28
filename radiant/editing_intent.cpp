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

const char* input_intent_type_name(InputIntentType type) {
    switch (type) {
        case INPUT_INTENT_INSERT_TEXT:                  return "insertText";
        case INPUT_INTENT_INSERT_REPLACEMENT_TEXT:      return "insertReplacementText";
        case INPUT_INTENT_INSERT_PARAGRAPH:             return "insertParagraph";
        case INPUT_INTENT_INSERT_LINE_BREAK:            return "insertLineBreak";
        case INPUT_INTENT_INSERT_HORIZONTAL_RULE:       return "insertHorizontalRule";
        case INPUT_INTENT_INSERT_IMAGE:                 return "insertImage";
        case INPUT_INTENT_INSERT_LINK:                  return "insertLink";
        case INPUT_INTENT_INSERT_FROM_PASTE:            return "insertFromPaste";
        case INPUT_INTENT_INSERT_FROM_PASTE_AS_QUOTATION: return "insertFromPasteAsQuotation";
        case INPUT_INTENT_INSERT_FROM_YANK:             return "insertFromYank";
        case INPUT_INTENT_INSERT_FROM_DROP:             return "insertFromDrop";
        case INPUT_INTENT_DELETE_CONTENT_BACKWARD:      return "deleteContentBackward";
        case INPUT_INTENT_DELETE_CONTENT_FORWARD:       return "deleteContentForward";
        case INPUT_INTENT_DELETE_WORD_BACKWARD:         return "deleteWordBackward";
        case INPUT_INTENT_DELETE_WORD_FORWARD:          return "deleteWordForward";
        case INPUT_INTENT_DELETE_SOFT_LINE_BACKWARD:    return "deleteSoftLineBackward";
        case INPUT_INTENT_DELETE_SOFT_LINE_FORWARD:     return "deleteSoftLineForward";
        case INPUT_INTENT_DELETE_HARD_LINE_BACKWARD:    return "deleteHardLineBackward";
        case INPUT_INTENT_DELETE_HARD_LINE_FORWARD:     return "deleteHardLineForward";
        case INPUT_INTENT_DELETE_BY_CUT:                return "deleteByCut";
        case INPUT_INTENT_DELETE_BY_DRAG:               return "deleteByDrag";
        case INPUT_INTENT_COMPOSITION_START:            return "compositionStart";
        case INPUT_INTENT_INSERT_COMPOSITION_TEXT:      return "insertCompositionText";
        case INPUT_INTENT_INSERT_FROM_COMPOSITION:      return "insertFromComposition";
        case INPUT_INTENT_DELETE_COMPOSITION_TEXT:      return "deleteCompositionText";
        case INPUT_INTENT_FORMAT_UNLINK:                return "unlink";
        case INPUT_INTENT_FORMAT_BOLD:                  return "formatBold";
        case INPUT_INTENT_FORMAT_ITALIC:                return "formatItalic";
        case INPUT_INTENT_FORMAT_UNDERLINE:             return "formatUnderline";
        case INPUT_INTENT_FORMAT_STRIKETHROUGH:         return "formatStrikeThrough";
        case INPUT_INTENT_FORMAT_SUBSCRIPT:             return "formatSubscript";
        case INPUT_INTENT_FORMAT_SUPERSCRIPT:           return "formatSuperscript";
        case INPUT_INTENT_FORMAT_FORE_COLOR:            return "formatForeColor";
        case INPUT_INTENT_FORMAT_BACK_COLOR:            return "formatBackColor";
        case INPUT_INTENT_FORMAT_HILITE_COLOR:          return "formatHiliteColor";
        case INPUT_INTENT_FORMAT_FONT_NAME:             return "formatFontName";
        case INPUT_INTENT_FORMAT_FONT_SIZE:             return "formatFontSize";
        case INPUT_INTENT_FORMAT_REMOVE:                return "formatRemove";
        case INPUT_INTENT_FORMAT_BLOCK:                 return "formatBlock";
        case INPUT_INTENT_FORMAT_JUSTIFY_LEFT:          return "formatJustifyLeft";
        case INPUT_INTENT_FORMAT_JUSTIFY_CENTER:        return "formatJustifyCenter";
        case INPUT_INTENT_FORMAT_JUSTIFY_RIGHT:         return "formatJustifyRight";
        case INPUT_INTENT_FORMAT_JUSTIFY_FULL:          return "formatJustifyFull";
        case INPUT_INTENT_FORMAT_ORDERED_LIST:          return "insertOrderedList";
        case INPUT_INTENT_FORMAT_UNORDERED_LIST:        return "insertUnorderedList";
        case INPUT_INTENT_FORMAT_INDENT:                return "formatIndent";
        case INPUT_INTENT_FORMAT_OUTDENT:               return "formatOutdent";
        case INPUT_INTENT_SELECT_ALL:                   return "selectAll";
        case INPUT_INTENT_HISTORY_UNDO:                 return "historyUndo";
        case INPUT_INTENT_HISTORY_REDO:                 return "historyRedo";
        default:                                        return "";
    }
}

bool input_intent_is_dispatchable(InputIntentType type) {
    switch (type) {
        case INPUT_INTENT_COMPOSITION_START:
        case INPUT_INTENT_INSERT_IMAGE:
        case INPUT_INTENT_FORMAT_UNLINK:
        case INPUT_INTENT_FORMAT_BOLD:
        case INPUT_INTENT_FORMAT_ITALIC:
        case INPUT_INTENT_FORMAT_UNDERLINE:
        case INPUT_INTENT_FORMAT_STRIKETHROUGH:
        case INPUT_INTENT_FORMAT_SUBSCRIPT:
        case INPUT_INTENT_FORMAT_SUPERSCRIPT:
        case INPUT_INTENT_FORMAT_FORE_COLOR:
        case INPUT_INTENT_FORMAT_BACK_COLOR:
        case INPUT_INTENT_FORMAT_HILITE_COLOR:
        case INPUT_INTENT_FORMAT_FONT_NAME:
        case INPUT_INTENT_FORMAT_FONT_SIZE:
        case INPUT_INTENT_FORMAT_REMOVE:
        case INPUT_INTENT_FORMAT_BLOCK:
        case INPUT_INTENT_FORMAT_JUSTIFY_LEFT:
        case INPUT_INTENT_FORMAT_JUSTIFY_CENTER:
        case INPUT_INTENT_FORMAT_JUSTIFY_RIGHT:
        case INPUT_INTENT_FORMAT_JUSTIFY_FULL:
        case INPUT_INTENT_FORMAT_ORDERED_LIST:
        case INPUT_INTENT_FORMAT_UNORDERED_LIST:
        case INPUT_INTENT_FORMAT_INDENT:
        case INPUT_INTENT_FORMAT_OUTDENT:
        case INPUT_INTENT_SELECT_ALL:
            return false;
        default:
            return true;
    }
}



// F11: the inverse of input_intent_type_name, generated from the same rows so
// the two cannot drift.
InputIntentType input_intent_type_from_name(const char* name) {
    if (!name) return INPUT_INTENT_NONE;
    if (strcmp(name, "insertText") == 0) return INPUT_INTENT_INSERT_TEXT;
    if (strcmp(name, "insertReplacementText") == 0) return INPUT_INTENT_INSERT_REPLACEMENT_TEXT;
    if (strcmp(name, "insertParagraph") == 0) return INPUT_INTENT_INSERT_PARAGRAPH;
    if (strcmp(name, "insertLineBreak") == 0) return INPUT_INTENT_INSERT_LINE_BREAK;
    if (strcmp(name, "insertHorizontalRule") == 0) return INPUT_INTENT_INSERT_HORIZONTAL_RULE;
    if (strcmp(name, "insertImage") == 0) return INPUT_INTENT_INSERT_IMAGE;
    if (strcmp(name, "insertLink") == 0) return INPUT_INTENT_INSERT_LINK;
    if (strcmp(name, "insertFromPaste") == 0) return INPUT_INTENT_INSERT_FROM_PASTE;
    if (strcmp(name, "insertFromPasteAsQuotation") == 0) return INPUT_INTENT_INSERT_FROM_PASTE_AS_QUOTATION;
    if (strcmp(name, "insertFromYank") == 0) return INPUT_INTENT_INSERT_FROM_YANK;
    if (strcmp(name, "insertFromDrop") == 0) return INPUT_INTENT_INSERT_FROM_DROP;
    if (strcmp(name, "deleteContentBackward") == 0) return INPUT_INTENT_DELETE_CONTENT_BACKWARD;
    if (strcmp(name, "deleteContentForward") == 0) return INPUT_INTENT_DELETE_CONTENT_FORWARD;
    if (strcmp(name, "deleteWordBackward") == 0) return INPUT_INTENT_DELETE_WORD_BACKWARD;
    if (strcmp(name, "deleteWordForward") == 0) return INPUT_INTENT_DELETE_WORD_FORWARD;
    if (strcmp(name, "deleteSoftLineBackward") == 0) return INPUT_INTENT_DELETE_SOFT_LINE_BACKWARD;
    if (strcmp(name, "deleteSoftLineForward") == 0) return INPUT_INTENT_DELETE_SOFT_LINE_FORWARD;
    if (strcmp(name, "deleteHardLineBackward") == 0) return INPUT_INTENT_DELETE_HARD_LINE_BACKWARD;
    if (strcmp(name, "deleteHardLineForward") == 0) return INPUT_INTENT_DELETE_HARD_LINE_FORWARD;
    if (strcmp(name, "deleteByCut") == 0) return INPUT_INTENT_DELETE_BY_CUT;
    if (strcmp(name, "deleteByDrag") == 0) return INPUT_INTENT_DELETE_BY_DRAG;
    if (strcmp(name, "compositionStart") == 0) return INPUT_INTENT_COMPOSITION_START;
    if (strcmp(name, "insertCompositionText") == 0) return INPUT_INTENT_INSERT_COMPOSITION_TEXT;
    if (strcmp(name, "insertFromComposition") == 0) return INPUT_INTENT_INSERT_FROM_COMPOSITION;
    if (strcmp(name, "deleteCompositionText") == 0) return INPUT_INTENT_DELETE_COMPOSITION_TEXT;
    if (strcmp(name, "unlink") == 0) return INPUT_INTENT_FORMAT_UNLINK;
    if (strcmp(name, "formatBold") == 0) return INPUT_INTENT_FORMAT_BOLD;
    if (strcmp(name, "formatItalic") == 0) return INPUT_INTENT_FORMAT_ITALIC;
    if (strcmp(name, "formatUnderline") == 0) return INPUT_INTENT_FORMAT_UNDERLINE;
    if (strcmp(name, "formatStrikeThrough") == 0) return INPUT_INTENT_FORMAT_STRIKETHROUGH;
    if (strcmp(name, "formatSubscript") == 0) return INPUT_INTENT_FORMAT_SUBSCRIPT;
    if (strcmp(name, "formatSuperscript") == 0) return INPUT_INTENT_FORMAT_SUPERSCRIPT;
    if (strcmp(name, "formatForeColor") == 0) return INPUT_INTENT_FORMAT_FORE_COLOR;
    if (strcmp(name, "formatBackColor") == 0) return INPUT_INTENT_FORMAT_BACK_COLOR;
    if (strcmp(name, "formatHiliteColor") == 0) return INPUT_INTENT_FORMAT_HILITE_COLOR;
    if (strcmp(name, "formatFontName") == 0) return INPUT_INTENT_FORMAT_FONT_NAME;
    if (strcmp(name, "formatFontSize") == 0) return INPUT_INTENT_FORMAT_FONT_SIZE;
    if (strcmp(name, "formatRemove") == 0) return INPUT_INTENT_FORMAT_REMOVE;
    if (strcmp(name, "formatBlock") == 0) return INPUT_INTENT_FORMAT_BLOCK;
    if (strcmp(name, "formatJustifyLeft") == 0) return INPUT_INTENT_FORMAT_JUSTIFY_LEFT;
    if (strcmp(name, "formatJustifyCenter") == 0) return INPUT_INTENT_FORMAT_JUSTIFY_CENTER;
    if (strcmp(name, "formatJustifyRight") == 0) return INPUT_INTENT_FORMAT_JUSTIFY_RIGHT;
    if (strcmp(name, "formatJustifyFull") == 0) return INPUT_INTENT_FORMAT_JUSTIFY_FULL;
    if (strcmp(name, "insertOrderedList") == 0) return INPUT_INTENT_FORMAT_ORDERED_LIST;
    if (strcmp(name, "insertUnorderedList") == 0) return INPUT_INTENT_FORMAT_UNORDERED_LIST;
    if (strcmp(name, "formatIndent") == 0) return INPUT_INTENT_FORMAT_INDENT;
    if (strcmp(name, "formatOutdent") == 0) return INPUT_INTENT_FORMAT_OUTDENT;
    if (strcmp(name, "selectAll") == 0) return INPUT_INTENT_SELECT_ALL;
    if (strcmp(name, "historyUndo") == 0) return INPUT_INTENT_HISTORY_UNDO;
    if (strcmp(name, "historyRedo") == 0) return INPUT_INTENT_HISTORY_REDO;
    return INPUT_INTENT_NONE;
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
    DomNode* root = doc && doc->root ? static_cast<DomNode*>(doc->root) : nullptr;
    DomElement* body = nullptr;
    for (DomNode* n = root; n && !body; n = n->next_sibling) {
        if (n->node_type != DOM_NODE_ELEMENT) continue;
        DomElement* e = static_cast<DomElement*>(n);
        if (e->tag() == MARKUP_NAME_BODY) { body = e; break; }
        for (DomNode* c = e->first_child; c; c = c->next_sibling) {
            if (c->node_type != DOM_NODE_ELEMENT) continue;
            DomElement* ce = static_cast<DomElement*>(c);
            if (ce->tag() == MARKUP_NAME_BODY) { body = ce; break; }
        }
    }
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

    InputIntentType type = input_intent_type_from_name(radiant_key_intent_name());
    if (type == INPUT_INTENT_NONE) return false;

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
        out->data_mime = (out->html_data && out->html_data[0]) ? "text/html" : "text/plain";
    }
    out->type = type;
    return true;
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
