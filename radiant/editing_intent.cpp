#include "editing_intent.hpp"

#include "clipboard.hpp"
#include "../lib/memtrack.h"
#include "../lib/utf.h"

#include <string.h>

const char* clipboard_get_text();

void input_intent_dispose(InputIntent* intent) {
    if (!intent) return;
    mem_free(intent->owned_data);
    mem_free(intent->owned_html_data);
    intent->owned_data = nullptr;
    intent->owned_html_data = nullptr;
    intent->data = nullptr;
    intent->html_data = nullptr;
}

const char* input_intent_type_name(InputIntentType type) {
    switch (type) {
        case INPUT_INTENT_INSERT_TEXT:                  return "insertText";
        case INPUT_INTENT_INSERT_REPLACEMENT_TEXT:      return "insertReplacementText";
        case INPUT_INTENT_INSERT_PARAGRAPH:             return "insertParagraph";
        case INPUT_INTENT_INSERT_LINE_BREAK:            return "insertLineBreak";
        case INPUT_INTENT_INSERT_HORIZONTAL_RULE:       return "insertHorizontalRule";
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
        case INPUT_INTENT_FORMAT_BOLD:                  return "formatBold";
        case INPUT_INTENT_FORMAT_ITALIC:                return "formatItalic";
        case INPUT_INTENT_FORMAT_UNDERLINE:             return "formatUnderline";
        case INPUT_INTENT_FORMAT_STRIKETHROUGH:         return "formatStrikeThrough";
        case INPUT_INTENT_FORMAT_SUBSCRIPT:             return "formatSubscript";
        case INPUT_INTENT_FORMAT_SUPERSCRIPT:           return "formatSuperscript";
        case INPUT_INTENT_FORMAT_BLOCK:                 return "formatBlock";
        case INPUT_INTENT_FORMAT_JUSTIFY_LEFT:          return "formatJustifyLeft";
        case INPUT_INTENT_FORMAT_JUSTIFY_CENTER:        return "formatJustifyCenter";
        case INPUT_INTENT_FORMAT_JUSTIFY_RIGHT:         return "formatJustifyRight";
        case INPUT_INTENT_FORMAT_JUSTIFY_FULL:          return "formatJustifyFull";
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
        case INPUT_INTENT_FORMAT_BOLD:
        case INPUT_INTENT_FORMAT_ITALIC:
        case INPUT_INTENT_FORMAT_UNDERLINE:
        case INPUT_INTENT_FORMAT_STRIKETHROUGH:
        case INPUT_INTENT_FORMAT_SUBSCRIPT:
        case INPUT_INTENT_FORMAT_SUPERSCRIPT:
        case INPUT_INTENT_FORMAT_BLOCK:
        case INPUT_INTENT_FORMAT_JUSTIFY_LEFT:
        case INPUT_INTENT_FORMAT_JUSTIFY_CENTER:
        case INPUT_INTENT_FORMAT_JUSTIFY_RIGHT:
        case INPUT_INTENT_FORMAT_JUSTIFY_FULL:
        case INPUT_INTENT_FORMAT_INDENT:
        case INPUT_INTENT_FORMAT_OUTDENT:
        case INPUT_INTENT_SELECT_ALL:
            return false;
        default:
            return true;
    }
}

bool input_intent_from_key_event(const KeyEvent* key_event, InputIntent* out) {
    if (!key_event || !out) return false;
    memset(out, 0, sizeof(*out));
    out->key = key_event->key;
    out->mods = key_event->mods;

    bool shift = (key_event->mods & RDT_MOD_SHIFT) != 0;
    bool alt = (key_event->mods & RDT_MOD_ALT) != 0;
    bool ctrl = (key_event->mods & RDT_MOD_CTRL) != 0;
    bool cmd = (key_event->mods & RDT_MOD_SUPER) != 0;
    bool primary = cmd || ctrl;

    if (primary && key_event->key == RDT_KEY_Z) {
        out->type = shift ? INPUT_INTENT_HISTORY_REDO : INPUT_INTENT_HISTORY_UNDO;
        return true;
    }
    if (primary && key_event->key == RDT_KEY_Y) {
        out->type = INPUT_INTENT_HISTORY_REDO;
        return true;
    }
    if (primary && key_event->key == RDT_KEY_B) {
        out->type = INPUT_INTENT_FORMAT_BOLD;
        return true;
    }
    if (primary && key_event->key == RDT_KEY_I) {
        out->type = INPUT_INTENT_FORMAT_ITALIC;
        return true;
    }
    if (primary && key_event->key == RDT_KEY_U) {
        out->type = INPUT_INTENT_FORMAT_UNDERLINE;
        return true;
    }
    if (primary && key_event->key == RDT_KEY_V) {
        const char* html = clipboard_store_read_mime("text/html");
        if (html && html[0]) {
            out->owned_html_data = mem_strdup(html, MEM_CAT_TEMP);
            out->html_data = out->owned_html_data;
        }
        const char* clip = clipboard_get_text();
        if ((!clip || !clip[0]) && (!html || !html[0])) return false;
        out->type = INPUT_INTENT_INSERT_FROM_PASTE;
        out->owned_data = mem_strdup(clip ? clip : "", MEM_CAT_TEMP);
        out->data = out->owned_data;
        out->data_mime = (out->html_data && out->html_data[0]) ? "text/html" : "text/plain";
        return true;
    }
    if (primary && key_event->key == RDT_KEY_X) {
        out->type = INPUT_INTENT_DELETE_BY_CUT;
        return true;
    }
    if (primary && key_event->key == RDT_KEY_A) {
        out->type = INPUT_INTENT_SELECT_ALL;
        return true;
    }
    if (key_event->key == RDT_KEY_ENTER) {
        out->type = shift ? INPUT_INTENT_INSERT_LINE_BREAK : INPUT_INTENT_INSERT_PARAGRAPH;
        return true;
    }
    if (key_event->key == RDT_KEY_TAB) {
        out->type = shift ? INPUT_INTENT_FORMAT_OUTDENT : INPUT_INTENT_FORMAT_INDENT;
        return true;
    }
    if (key_event->key == RDT_KEY_BACKSPACE) {
        out->type = (alt || ctrl) ? INPUT_INTENT_DELETE_WORD_BACKWARD
                                  : INPUT_INTENT_DELETE_CONTENT_BACKWARD;
        return true;
    }
    if (key_event->key == RDT_KEY_DELETE) {
        out->type = INPUT_INTENT_DELETE_CONTENT_FORWARD;
        return true;
    }

    return false;
}

bool input_intent_from_text_input(uint32_t codepoint, InputIntent* out,
                                  char* utf8_buf, size_t utf8_buf_size) {
    if (!out || !utf8_buf || utf8_buf_size < 5 || codepoint == 0) return false;
    memset(out, 0, sizeof(*out));
    size_t utf8_len = utf8_encode_z(codepoint, utf8_buf);
    if (utf8_len == 0) return false;
    out->type = INPUT_INTENT_INSERT_TEXT;
    out->data = utf8_buf;
    return true;
}

bool input_intent_from_composition_event(const CompositionEvent* comp_event,
                                         InputIntent* out) {
    if (!comp_event || !out) return false;
    memset(out, 0, sizeof(*out));
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
