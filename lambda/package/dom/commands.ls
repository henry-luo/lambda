// The legacy command surface (D7.2.5, ES20).
//
// Keyboard input and execCommand both resolve through this immutable package
// registry. Rows carry policy facts only; the native waist sees just the
// generic DOM operation selected by the resulting plan.
import dom
import context: lambda.package.dom.edit_context
import plan: lambda.package.dom.edit_plan
import result: lambda.package.dom.edit_result
import session: lambda.package.dom.edit_session
import structure: lambda.package.dom.edit_structure
import format: lambda.package.dom.edit_format
import objects: lambda.package.dom.edit_objects
import lists: lambda.package.dom.edit_lists
import blocks: lambda.package.dom.edit_blocks
import clipboard: lambda.package.dom.edit_clipboard
import text: lambda.package.dom.edit_text
import history: lambda.package.dom.edit_history

let registry = [
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

fn has_alias(descriptor, spelling) {
    any([for (alias in descriptor.aliases) alias == spelling])
}

fn descriptor_at_name(spelling, index) {
    if (index >= len(registry)) null
    else {
        let descriptor = registry[index];
        if (has_alias(descriptor, spelling)) descriptor
        else descriptor_at_name(spelling, index + 1)
    }
}

fn has_input_alias(aliases, intent) {
    if (aliases == null) false
    else any([for (alias in aliases) alias == intent])
}

fn matches_input_intent(descriptor, intent) {
    if (descriptor.input_type == intent) true
    else has_input_alias(descriptor.input_aliases, intent)
}

fn descriptor_at_intent(intent, index) {
    if (index >= len(registry)) null
    else {
        let descriptor = registry[index];
        if (matches_input_intent(descriptor, intent)) descriptor
        else descriptor_at_intent(intent, index + 1)
    }
}

// `descriptor` is the only spelling resolver. Query and execute calls share
// it, so adding a command cannot accidentally add query-only policy.
pub fn descriptor(spelling) {
    if (spelling == null) null else descriptor_at_name(lower(spelling), 0)
}

pub fn canonical(spelling) {
    let descriptor = descriptor(spelling);
    if (descriptor == null) null else descriptor.input_type
}

// The document behavior template is attached at an ancestor, while an edit
// invocation names its concrete host. Keep that handoff explicit rather than
// treating the template receiver as the editing surface (D7.2.5).
fn invocation_host(receiver, evt, descriptor) {
    // Document-scoped settings have no Range or editable host. They share the
    // same package session through the body receiver without minting a handle.
    if (descriptor != null and not descriptor.requires_host) receiver
    else if (evt.edit_token == null or evt.edit_token == 0) null
    else dom.edit_target(receiver, evt.edit_token)
}

// Platform input uses the same immutable rows as the legacy spelling path.
// Exposing this pure lookup lets the package oracle pin that invariant without
// constructing a live DOM invocation.
pub fn descriptor_for_intent(intent) {
    if (intent == null) null else descriptor_at_intent(intent, 0)
}

pub fn is_format(intent) {
    let descriptor = descriptor_at_intent(intent, 0);
    descriptor != null and descriptor.family == "format"
}

fn format_state(host, edit_context, descriptor) {
    if (descriptor.format_tag == null or edit_context.token == null) false
    else if (edit_context.collapsed)
        session.typing_format_tag(host) ==
            if (session.style_with_css(host) and descriptor.css_property != null)
                "span"
            else descriptor.format_tag
    else format.has_state(host, dom.edit_node(host, edit_context.token), descriptor,
                          session.style_with_css(host))
}

fn setting_state(host, descriptor) {
    if (descriptor.name == "stylewithcss") session.style_with_css(host)
    else if (descriptor.name == "usecss") not session.style_with_css(host)
    else false
}

fn setting_value(host, descriptor) {
    if (descriptor.name == "defaultparagraphseparator")
        session.default_paragraph_separator(host)
    else ""
}

fn history_enabled(host, descriptor) {
    if (descriptor.family == "history_undo") history.can_undo(host)
    else if (descriptor.family == "history_redo") history.can_redo(host)
    else true
}

// A query only derives facts from its context. It never creates a range,
// changes the selection, records history, or opens a mutation transaction.
pub fn query(host, evt) {
    let descriptor = descriptor(evt.command);
    let target = invocation_host(host, evt, descriptor);
    let active_host = if (target == null) host else target;
    let edit_context = context.build(active_host, evt, descriptor);
    let kind = evt.edit_query;
    if (kind == "supported") edit_context.supported
    else if (kind == "enabled") edit_context.enabled and
                                    (descriptor == null or history_enabled(active_host, descriptor))
    else if (kind == "state") {
        if (not edit_context.enabled) false
        else if (descriptor == null) false
        else if (descriptor.family == "setting") setting_state(active_host, descriptor)
        else format_state(active_host, edit_context, descriptor)
    }
    else if (kind == "indeterm") false
    else if (kind == "value") {
        if (descriptor == null) ""
        else if (descriptor.family == "setting") setting_value(active_host, descriptor)
        else if (descriptor.family == "format" and edit_context.token != null)
            format.value(active_host, dom.edit_node(active_host, edit_context.token), descriptor,
                         session.style_with_css(active_host))
        else ""
    }
    else false
}

// A formatting command delegates semantic-versus-CSS output and legacy value
// handling to the family module; this registry only chooses that family.
pn apply_format(host, edit_context, descriptor) {
    format.apply_format(host, edit_context, descriptor, edit_context.value)
}

fn setting_bool(value) {
    value == "true" or value == "1"
}

pn apply_setting(host, descriptor, value) {
    if (descriptor.name == "stylewithcss") {
        let enabled = setting_bool(value);
        let changed = session.style_with_css(host) != enabled;
        let updated = session.set_style_with_css(host, enabled);
        changed and updated != null
    }
    else if (descriptor.name == "usecss") {
        let enabled = not setting_bool(value);
        let changed = session.style_with_css(host) != enabled;
        let updated = session.set_style_with_css(host, enabled);
        changed and updated != null
    }
    else if (descriptor.name == "defaultparagraphseparator") {
        let tag = lower(if (value == null) "" else value);
        if (tag != "div" and tag != "p") false
        else {
            let changed = session.default_paragraph_separator(host) != tag;
            let updated = session.set_default_paragraph_separator(host, tag);
            changed and updated != null
        }
    }
    else false
}

fn outcome(changed, selection_changed) {
    { changed: changed, selection_changed: selection_changed }
}

// API commands notify only after their package action. The descriptor owns
// the vocabulary; `beforeinput` is reserved for physical edit transactions.
fn api_emits_input(descriptor) {
    descriptor.event_contract == "beforeinput-input" or
    descriptor.event_contract == "clipboard-beforeinput-input"
}

fn api_input_type(descriptor) {
    if (descriptor.exec_input_type != null) descriptor.exec_input_type
    else descriptor.input_type
}

fn api_input_color(host, edit_context, descriptor, value) {
    let raw = if (value == null) "" else string(value);
    let keyword = lower(trim(raw));
    if (keyword == "inherit" or keyword == "initial" or keyword == "reset" or
        keyword == "currentcolor") keyword
    else if (not dom.css_color_valid(raw)) raw
    else {
        let node = dom.edit_node(host, edit_context.token);
        let wrapper = structure.ancestor_with_tag(host, node, descriptor.format_tag);
        if (wrapper == null) raw else dom.computed_style(wrapper, descriptor.css_property)
    }
}

fn api_input_data(host, edit_context, descriptor, value) {
    if (descriptor.api_data_kind == "value") value
    else if (descriptor.api_data_kind == "color")
        api_input_color(host, edit_context, descriptor, value)
    else null
}

// The plan records the selected generic shape, then this adapter applies it.
// It is keyed by descriptor family, never raw caller spelling, so aliases
// cannot form a second execution implementation.
pn apply_plan(host, edit_context, descriptor, edit_plan, evt) {
    let token = edit_context.token;
    let changed = if (edit_plan.family == "format") apply_format(host, edit_context, descriptor)
    else if (edit_plan.family == "remove_format") format.remove(host, edit_context)
    else if (edit_plan.family == "create_link") objects.create_link(host, edit_context,
                                                                       edit_plan.value)
    else if (edit_plan.family == "unlink") objects.unlink(host, edit_context)
    else if (edit_plan.family == "insert_image") objects.insert_image(host, edit_context,
                                                                         edit_plan.value)
    else if (edit_plan.family == "insert_horizontal_rule")
        objects.insert_horizontal_rule(host, edit_context)
    else if (edit_plan.family == "list") lists.toggle(host, edit_context,
                                                        descriptor.format_tag)
    else if (edit_plan.family == "indent") lists.indent(host, edit_context)
    else if (edit_plan.family == "outdent") lists.outdent(host, edit_context)
    else if (edit_plan.family == "format_block") blocks.format_block(host,
                                                                        edit_context,
                                                                        edit_plan.value)
    else if (edit_plan.family == "justify") blocks.justify(host, edit_context,
                                                             descriptor.format_value)
    else if (edit_plan.family == "copy") clipboard.copy(host, edit_context)
    else if (edit_plan.family == "cut") clipboard.cut(host, edit_context)
    else if (edit_plan.family == "paste") clipboard.paste(host, edit_context,
                                                             edit_plan.html,
                                                             if (edit_plan.value == null)
                                                                dom.clipboard_text()
                                                             else edit_plan.value)
    else if (edit_plan.family == "insert_html") {
        let html = if (edit_plan.value == null) "" else edit_plan.value;
        len(html) > 0 and dom.dom_insert_html(host, token, html)
    }
    else if (edit_plan.family == "replace") text.replace_range(host, edit_context, edit_plan.value)
    else if (edit_plan.family == "delete") text.delete(host, edit_context,
                                                          descriptor.name == "delete")
    else if (edit_plan.family == "composition_start") false
    else if (edit_plan.family == "composition") text.composition(host, edit_context,
                                                                     edit_plan.value,
        // Only provisional updates carry an IME-relative caret. Commit and
        // cancel replace the provisional span and conventionally land at its
        // end, so an incidental zero field must not reset the caret to start.
        if (evt.input_type == "insertCompositionText") evt.composition_caret
        else null)
    else if (edit_plan.family == "drop") text.insert_drop(host, edit_context, edit_plan.value)
    else if (edit_plan.family == "drag_delete") text.delete(host, edit_context, false)
    else if (edit_plan.family == "history_undo") history.run_undo(host, edit_context)
    else if (edit_plan.family == "history_redo") history.run_redo(host, edit_context)
    else if (edit_plan.family == "paragraph") structure.insert_paragraph(host, token)
    else if (edit_plan.family == "line_break") dom.edit_insert_break(host, token)
    else if (edit_plan.family == "selection") false
    else if (edit_plan.family == "setting") apply_setting(host, descriptor, edit_plan.value)
    else false;
    if (edit_plan.family == "selection")
        outcome(false, dom.edit_select_host(host, token))
    else outcome(changed, changed)
}

// Structured execution result for both legacy command and keyboard adapters.
pub pn execute(host, evt, intent, value) {
    let descriptor = descriptor_at_intent(intent, 0);
    let edit_context = context.build(host, evt, descriptor);
    if (descriptor == null) result.decline(false, false, "unsupported", 0)
    else if (not edit_context.enabled) result.decline(true, false, "disabled", 0)
    else if (not history_enabled(host, descriptor)) result.decline(true, false, "disabled", 0)
    else {
        let edit_session = session.ensure(host);
        if (edit_session == null) result.decline(true, true, "session-unavailable", 0)
        else {
        // HTML is transport data, not context/planner policy. Keep it on the
        // invocation plan only for the shared paste family so ordinary typing
        // and model-owned input do not acquire an absent event member.
        let edit_plan = {
            *: plan.make(edit_context, descriptor, value, edit_session),
            html: if (evt.html == null) null else evt.html
        };
        // The snapshot is package state, not a native pending channel. It is
        // published only after this plan changed one eligible text node.
        let history_snapshot = history.capture(host, edit_context, descriptor);
        let applied = apply_plan(host, edit_context, descriptor, edit_plan, evt);
        let history_recorded = if (applied.changed)
            history.record(host, history_snapshot)
        else false;
        let completed = result.applied(true, true, applied.changed,
                                       applied.selection_changed,
                                       history_recorded, 0,
                                       descriptor.history_class,
                                       api_emits_input(descriptor),
                                       api_input_type(descriptor),
                                       api_input_data(host, edit_context,
                                                      descriptor, value));
        completed
        }
    }
}

// Compatibility helper for callers that require only a mutation bit.
pub pn run(host, token, intent, value) {
    let event = { edit_token: token, command: null, value: value, edit_query: null };
    execute(host, event, intent, value).changed
}

// The execCommand bridge receives this rooted EditResult directly and derives
// its IDL Boolean from `claimed`; ordinary input still uses its verdict adapter.
pub pn exec(host, evt) {
    let intent = canonical(evt.command);
    if (intent == null) result.decline(false, false, "unsupported", 0)
    else {
        let descriptor = descriptor(evt.command);
        let target = invocation_host(host, evt, descriptor);
        if (target == null) result.decline(true, false, "disabled", 0)
        else execute(target, evt, intent, evt.value)
    }
}
