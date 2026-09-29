// Representation-neutral editing descriptor registry (D7.2.5).
//
// This module is pure: it owns canonical command/input spellings and policy
// metadata, but imports neither a DOM session nor the editor model.

let base_registry = [
    { name: "bold", aliases: ["bold", "formatbold"], input_type: "formatBold", family: "format", format_tag: "b", css_property: "font-weight", css_value: "bold", requires_host: true, plaintext_rule: "disable", step_kind: "wrap_range", history_class: "format", event_contract: "beforeinput-input", state_kind: "boolean" },
    { name: "italic", aliases: ["italic", "formatitalic"], input_type: "formatItalic", family: "format", format_tag: "i", css_property: "font-style", css_value: "italic", requires_host: true, plaintext_rule: "disable", step_kind: "wrap_range", history_class: "format", event_contract: "beforeinput-input", state_kind: "boolean" },
    { name: "underline", aliases: ["underline", "formatunderline"], input_type: "formatUnderline", family: "format", format_tag: "u", css_property: "text-decoration", css_value: "underline", requires_host: true, plaintext_rule: "disable", step_kind: "wrap_range", history_class: "format", event_contract: "beforeinput-input", state_kind: "boolean" },
    { name: "strikethrough", aliases: ["strikethrough", "formatstrikethrough"], input_type: "formatStrikeThrough", family: "format", format_tag: "strike", css_property: "text-decoration", css_value: "line-through", requires_host: true, plaintext_rule: "disable", step_kind: "wrap_range", history_class: "format", event_contract: "beforeinput-input", state_kind: "boolean" },
    { name: "subscript", aliases: ["subscript", "formatsubscript"], input_type: "formatSubscript", family: "format", format_tag: "sub", exclusive_tag: "sup", requires_host: true, plaintext_rule: "disable", step_kind: "wrap_range", history_class: "format", event_contract: "beforeinput-input", state_kind: "boolean" },
    { name: "superscript", aliases: ["superscript", "formatsuperscript"], input_type: "formatSuperscript", family: "format", format_tag: "sup", exclusive_tag: "sub", requires_host: true, plaintext_rule: "disable", step_kind: "wrap_range", history_class: "format", event_contract: "beforeinput-input", state_kind: "boolean" },
    { name: "fontname", aliases: ["fontname"], input_type: "formatFontName", family: "format", format_tag: "font", attribute_name: "face", css_property: "font-family", api_data_kind: "value", requires_host: true, plaintext_rule: "disable", step_kind: "wrap_range", history_class: "format", event_contract: "beforeinput-input", state_kind: "value" },
    { name: "fontsize", aliases: ["fontsize"], input_type: "formatFontSize", family: "format", format_tag: "font", attribute_name: "size", css_property: "font-size", requires_host: true, plaintext_rule: "disable", step_kind: "wrap_range", history_class: "format", event_contract: "beforeinput-input", state_kind: "value" },
    { name: "forecolor", aliases: ["forecolor"], input_type: "formatFontColor", family: "format", format_tag: "font", attribute_name: "color", css_property: "color", api_data_kind: "color", requires_host: true, plaintext_rule: "disable", step_kind: "wrap_range", history_class: "format", event_contract: "beforeinput-input", state_kind: "value" },
    { name: "backcolor", aliases: ["backcolor"], input_type: "formatBackColor", family: "format", format_tag: "span", css_property: "background-color", legacy_style: true, api_data_kind: "color", requires_host: true, plaintext_rule: "disable", step_kind: "wrap_range", history_class: "format", event_contract: "beforeinput-input", state_kind: "value" },
    { name: "hilitecolor", aliases: ["hilitecolor"], input_type: "formatHiliteColor", exec_input_type: "formatBackColor", family: "format", format_tag: "span", css_property: "background-color", legacy_style: true, api_data_kind: "color", requires_host: true, plaintext_rule: "disable", step_kind: "wrap_range", history_class: "format", event_contract: "beforeinput-input", state_kind: "value" },
    { name: "removeformat", aliases: ["removeformat"], input_type: "formatRemove", family: "remove_format", format_tag: null, requires_host: true, plaintext_rule: "disable", step_kind: "unwrap_node", history_class: "format", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "createlink", aliases: ["createlink"], input_type: "insertLink", family: "create_link", format_tag: "a", api_data_kind: "value", requires_host: true, plaintext_rule: "disable", step_kind: "wrap_range", history_class: "format", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "unlink", aliases: ["unlink"], input_type: "removeLink", exec_input_type: "", family: "unlink", format_tag: "a", requires_host: true, plaintext_rule: "disable", step_kind: "unwrap_node", history_class: "format", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "insertimage", aliases: ["insertimage"], input_type: "insertImage", family: "insert_image", format_tag: null, requires_host: true, plaintext_rule: "disable", step_kind: "insert_fragment", history_class: "structural", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "inserthorizontalrule", aliases: ["inserthorizontalrule"], input_type: "insertHorizontalRule", family: "insert_horizontal_rule", format_tag: null, requires_host: true, plaintext_rule: "disable", step_kind: "insert_fragment", history_class: "structural", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "insertorderedlist", aliases: ["insertorderedlist"], input_type: "insertOrderedList", family: "list", format_tag: "ol", requires_host: true, plaintext_rule: "disable", step_kind: "move_node", history_class: "structural", event_contract: "beforeinput-input", state_kind: "boolean" },
    { name: "insertunorderedlist", aliases: ["insertunorderedlist"], input_type: "insertUnorderedList", family: "list", format_tag: "ul", requires_host: true, plaintext_rule: "disable", step_kind: "move_node", history_class: "structural", event_contract: "beforeinput-input", state_kind: "boolean" },
    { name: "indent", aliases: ["indent"], input_type: "formatIndent", family: "indent", format_tag: null, requires_host: true, plaintext_rule: "disable", step_kind: "move_node", history_class: "structural", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "outdent", aliases: ["outdent"], input_type: "formatOutdent", family: "outdent", format_tag: null, requires_host: true, plaintext_rule: "disable", step_kind: "move_node", history_class: "structural", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "formatblock", aliases: ["formatblock"], input_type: "formatBlock", family: "format_block", format_tag: null, requires_host: true, plaintext_rule: "disable", step_kind: "move_node", history_class: "structural", event_contract: "beforeinput-input", state_kind: "value" },
    { name: "justifyleft", aliases: ["justifyleft"], input_type: "formatJustifyLeft", family: "justify", format_value: "left", requires_host: true, plaintext_rule: "disable", step_kind: "set_style", history_class: "structural", event_contract: "beforeinput-input", state_kind: "boolean" },
    { name: "justifyright", aliases: ["justifyright"], input_type: "formatJustifyRight", family: "justify", format_value: "right", requires_host: true, plaintext_rule: "disable", step_kind: "set_style", history_class: "structural", event_contract: "beforeinput-input", state_kind: "boolean" },
    { name: "justifycenter", aliases: ["justifycenter"], input_type: "formatJustifyCenter", family: "justify", format_value: "center", requires_host: true, plaintext_rule: "disable", step_kind: "set_style", history_class: "structural", event_contract: "beforeinput-input", state_kind: "boolean" },
    { name: "justifyfull", aliases: ["justifyfull"], input_type: "formatJustifyFull", family: "justify", format_value: "justify", requires_host: true, plaintext_rule: "disable", step_kind: "set_style", history_class: "structural", event_contract: "beforeinput-input", state_kind: "boolean" },
    { name: "inserthtml", aliases: ["inserthtml"], input_type: "insertHTML", family: "insert_html", format_tag: null, requires_host: true, plaintext_rule: "disable", step_kind: "insert_fragment", history_class: "clipboard", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "inserttext", aliases: ["inserttext"], input_type: "insertText", input_aliases: ["insertReplacementText"], family: "replace", format_tag: null, api_data_kind: "value", requires_host: true, plaintext_rule: "allow", step_kind: "replace_text", history_class: "typing", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "delete", aliases: ["delete", "deletecontentbackward"], input_type: "deleteContentBackward", family: "delete", format_tag: null, requires_host: true, plaintext_rule: "allow", step_kind: "replace_text", history_class: "typing", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "forwarddelete", aliases: ["forwarddelete", "deletecontentforward"], input_type: "deleteContentForward", family: "delete", format_tag: null, requires_host: true, plaintext_rule: "allow", step_kind: "replace_text", history_class: "typing", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "deletewordbackward", aliases: ["deletewordbackward"], input_type: "deleteWordBackward", family: "delete", format_tag: null, requires_host: true, plaintext_rule: "allow", step_kind: "replace_text", history_class: "typing", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "deletewordforward", aliases: ["deletewordforward"], input_type: "deleteWordForward", family: "delete", format_tag: null, requires_host: true, plaintext_rule: "allow", step_kind: "replace_text", history_class: "typing", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "deletesoftlinebackward", aliases: [], input_type: "deleteSoftLineBackward", family: "delete", format_tag: null, requires_host: true, plaintext_rule: "allow", step_kind: "replace_text", history_class: "typing", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "deletesoftlineforward", aliases: [], input_type: "deleteSoftLineForward", family: "delete", format_tag: null, requires_host: true, plaintext_rule: "allow", step_kind: "replace_text", history_class: "typing", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "deletehardlinebackward", aliases: [], input_type: "deleteHardLineBackward", family: "delete", format_tag: null, requires_host: true, plaintext_rule: "allow", step_kind: "replace_text", history_class: "typing", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "deletehardlineforward", aliases: [], input_type: "deleteHardLineForward", family: "delete", format_tag: null, requires_host: true, plaintext_rule: "allow", step_kind: "replace_text", history_class: "typing", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "insertparagraph", aliases: ["insertparagraph"], input_type: "insertParagraph", family: "paragraph", format_tag: null, requires_host: true, plaintext_rule: "allow", step_kind: "split_element", history_class: "structural", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "insertlinebreak", aliases: ["insertlinebreak"], input_type: "insertLineBreak", family: "line_break", format_tag: null, requires_host: true, plaintext_rule: "allow", step_kind: "insert_node", history_class: "structural", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "compositionstart", aliases: [], input_type: "compositionStart", family: "composition_start", format_tag: null, requires_host: true, plaintext_rule: "allow", step_kind: "set_session", history_class: "composition", event_contract: "beforeinput", state_kind: "none" },
    { name: "composition", aliases: [], input_type: "insertCompositionText", input_aliases: ["insertFromComposition", "deleteCompositionText"], family: "composition", format_tag: null, requires_host: true, plaintext_rule: "allow", step_kind: "replace_text", history_class: "composition", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "insertfromdrop", aliases: [], input_type: "insertFromDrop", family: "drop", format_tag: null, requires_host: true, plaintext_rule: "allow", step_kind: "replace_text", history_class: "clipboard", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "deletebydrag", aliases: [], input_type: "deleteByDrag", family: "drag_delete", format_tag: null, requires_host: true, plaintext_rule: "allow", step_kind: "replace_text", history_class: "clipboard", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "selectall", aliases: ["selectall"], input_type: "selectAll", family: "selection", format_tag: null, requires_host: true, plaintext_rule: "allow", step_kind: "set_selection", history_class: "none", event_contract: "selectionchange", state_kind: "none" },
    { name: "copy", aliases: ["copy"], input_type: "copy", family: "copy", format_tag: null, requires_host: true, plaintext_rule: "allow", step_kind: "clipboard_write", history_class: "none", event_contract: "clipboard", state_kind: "none" },
    { name: "cut", aliases: ["cut"], input_type: "deleteByCut", family: "cut", format_tag: null, requires_host: true, plaintext_rule: "allow", step_kind: "clipboard_write", history_class: "clipboard", event_contract: "clipboard-beforeinput-input", state_kind: "none" },
    { name: "paste", aliases: ["paste"], input_type: "insertFromPaste", family: "paste", format_tag: null, requires_host: true, plaintext_rule: "allow", step_kind: "insert_fragment", history_class: "clipboard", event_contract: "clipboard-beforeinput-input", state_kind: "none" },
    { name: "undo", aliases: ["undo"], input_type: "historyUndo", family: "history_undo", format_tag: null, requires_host: true, plaintext_rule: "allow", step_kind: "replace_text", history_class: "none", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "redo", aliases: ["redo"], input_type: "historyRedo", family: "history_redo", format_tag: null, requires_host: true, plaintext_rule: "allow", step_kind: "replace_text", history_class: "none", event_contract: "beforeinput-input", state_kind: "none" },
    { name: "stylewithcss", aliases: ["stylewithcss"], input_type: "styleWithCSS", family: "setting", format_tag: null, requires_host: false, plaintext_rule: "allow", step_kind: "set_session", history_class: "none", event_contract: "none", state_kind: "boolean" },
    { name: "usecss", aliases: ["usecss"], input_type: "useCSS", family: "setting", format_tag: null, requires_host: false, plaintext_rule: "allow", step_kind: "set_session", history_class: "none", event_contract: "none", state_kind: "boolean" },
    { name: "defaultparagraphseparator", aliases: ["defaultparagraphseparator"], input_type: "defaultParagraphSeparator", family: "setting", format_tag: null, requires_host: false, plaintext_rule: "allow", step_kind: "set_session", history_class: "none", event_contract: "none", state_kind: "value" }
]

fn model_key(input_type) {
    if (input_type == "insertText" or input_type == "insertReplacementText") "insert_text"
    else if (input_type == "insertFromPaste") "paste"
    else if (input_type == "insertImage") "insert_image"
    else if (input_type == "insertLink") "insert_link"
    else if (input_type == "insertHorizontalRule") "insert_horizontal_rule"
    else if (input_type == "insertParagraph") "insert_paragraph"
    else if (input_type == "insertLineBreak") "insert_line_break"
    else if (input_type == "deleteContentBackward") "delete_backward"
    else if (input_type == "deleteContentForward" or input_type == "deleteByCut") "delete_forward"
    else if (input_type == "deleteWordBackward") "delete_word_backward"
    else if (input_type == "deleteWordForward" or input_type == "deleteSoftLineForward" or
             input_type == "deleteHardLineForward") "delete_forward"
    else if (input_type == "deleteSoftLineBackward" or
             input_type == "deleteHardLineBackward") "delete_backward"
    else if (input_type == "formatBold") "format_bold"
    else if (input_type == "formatItalic") "format_italic"
    else if (input_type == "formatUnderline") "format_underline"
    else if (input_type == "insertOrderedList" or input_type == "insertUnorderedList") "wrap_list"
    else if (input_type == "formatIndent") "indent_list_item"
    else if (input_type == "formatOutdent") "outdent_list_item"
    else if (input_type == "formatBlock") "set_block_type"
    else if (input_type == "selectAll") "select_all"
    else if (input_type == "historyUndo") "history_undo"
    else if (input_type == "historyRedo") "history_redo"
    else if (input_type == "compositionStart" or input_type == "insertCompositionText" or
             input_type == "insertFromComposition" or input_type == "deleteCompositionText") "composition"
    else if (input_type == "insertFromDrop") "drop"
    else null
}

fn target_rule(input_type) {
    if (input_type == "deleteWordBackward") "word-backward"
    else if (input_type == "deleteWordForward") "word-forward"
    else if (input_type == "deleteSoftLineBackward") "soft-line-backward"
    else if (input_type == "deleteSoftLineForward") "soft-line-forward"
    else if (input_type == "deleteHardLineBackward") "hard-line-backward"
    else if (input_type == "deleteHardLineForward") "hard-line-forward"
    else if (input_type == "deleteContentBackward") "character-backward"
    else if (input_type == "deleteContentForward") "character-forward"
    else "selection"
}

fn payload_kind(descriptor) {
    if (descriptor.family == "paste" or descriptor.family == "insert_html") "clipboard"
    else if (descriptor.family == "drop" or descriptor.family == "drag_delete") "drag"
    else if (descriptor.family == "composition" or descriptor.family == "composition_start") "composition"
    else if (descriptor.api_data_kind != null) descriptor.api_data_kind
    else "none"
}

fn decorate(descriptor) {
    let key = model_key(descriptor.input_type);
    { *: descriptor,
      payload_kind: payload_kind(descriptor),
      target_rule: target_rule(descriptor.input_type),
      dom: {supported: true, planner_key: descriptor.family, query_key: descriptor.state_kind},
      model: if (key == null) null else {supported: true, command_key: key, query_key: descriptor.state_kind} }
}

pub let registry = [for (descriptor in base_registry) decorate(descriptor)]

fn has_alias(descriptor, spelling) =>
    descriptor.name == spelling or
    (descriptor.aliases != null and
     any([for (alias in descriptor.aliases) alias == spelling]))
fn has_input_alias(aliases, intent) {
    if (aliases == null) false
    else any([for (alias in aliases) alias == intent])
}

fn matches_input_intent(descriptor, intent) {
    if (descriptor.input_type == intent) true
    else has_input_alias(descriptor.input_aliases, intent)
}

fn descriptor_at_name(rows, spelling, index) {
    if (index >= len(rows)) null
    else if (has_alias(rows[index], spelling)) rows[index]
    else descriptor_at_name(rows, spelling, index + 1)
}

fn descriptor_at_intent(rows, intent, index) {
    if (index >= len(rows)) null
    else if (matches_input_intent(rows[index], intent)) rows[index]
    else descriptor_at_intent(rows, intent, index + 1)
}

pub fn descriptor_in(rows, spelling) =>
    if (rows == null or spelling == null) null
    else descriptor_at_name(rows, lower(spelling), 0)

pub fn descriptor(spelling) => descriptor_in(registry, spelling)

pub fn descriptor_for_intent_in(rows, intent) =>
    if (rows == null or intent == null) null
    else descriptor_at_intent(rows, intent, 0)

pub fn descriptor_for_intent(intent) => descriptor_for_intent_in(registry, intent)

pub fn canonical(spelling) {
    let found = descriptor(spelling);
    if (found == null) null else found.input_type
}

fn aliases_hit(aliases, descriptor) {
    if (aliases == null) false
    else any([for (alias in aliases) has_alias(descriptor, alias)])
}

fn aliases_overlap(left, right) =>
    has_alias(left, right.name) or has_alias(right, left.name) or
    aliases_hit(left.aliases, right) or aliases_hit(right.aliases, left) or false

fn intents_overlap(left, right) {
    if (matches_input_intent(left, right.input_type)) true
    else matches_input_intent(right, left.input_type)
}

fn target_rule_valid(rule) =>
    rule == "selection" or rule == "character-backward" or
    rule == "character-forward" or rule == "word-backward" or
    rule == "word-forward" or rule == "soft-line-backward" or
    rule == "soft-line-forward" or rule == "hard-line-backward" or
    rule == "hard-line-forward"

fn row_shape_valid(row) {
    if (row.name == null or row.input_type == null or row.family == null) false
    else if (not target_rule_valid(row.target_rule)) false
    else if (row.dom == null and row.model == null) false
    else true
}

fn row_unique(rows, row, index, other) {
    if (other >= len(rows)) true
    else if (other == index) row_unique(rows, row, index, other + 1)
    else if (aliases_overlap(row, rows[other]) or intents_overlap(row, rows[other])) false
    else row_unique(rows, row, index, other + 1)
}

fn validate_at(rows, index) {
    if (index >= len(rows)) true
    else if (not row_shape_valid(rows[index])) false
    else if (not row_unique(rows, rows[index], index, 0)) false
    else validate_at(rows, index + 1)
}

pub fn validate(rows) {
    if (rows == null) false else validate_at(rows, 0)
}

pub fn merge_extensions(extensions) {
    let merged = [*registry, *extensions];
    if (validate(merged)) merged else null
}
