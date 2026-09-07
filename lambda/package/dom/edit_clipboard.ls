// Clipboard policy belongs to the DOM behavior package (D7.2.5).  The native
// helpers only serialize the current Selection and write/read platform data.
import dom
import structure: lambda.package.dom.edit_structure

pub pn copy(host, edit_context) {
    let token = edit_context.token;
    let text = dom.edit_selection_text(host, token);
    let html = dom.edit_selection_html(host, token);
    if (text == null or len(text) == 0) false
    else dom.edit_clipboard_write(host, token, html, text)
}

pub pn cut(host, edit_context) {
    if (not copy(host, edit_context)) false
    else structure.replace_range(host, edit_context.token, "")
}

// Rich payload selection is data-driven: platform input carries sanitised HTML
// when available, while legacy paste reads the ordinary clipboard mechanism.
pub pn paste(host, edit_context, html, text) {
    let markup = if (html == null) "" else html;
    let plain = if (text == null) "" else text;
    if (edit_context.plaintext_only) {
        if (len(plain) == 0) false else structure.replace_range(host, edit_context.token, plain)
    }
    else if (len(markup) > 0) dom.dom_insert_html(host, edit_context.token, markup)
    else if (len(plain) > 0) structure.replace_range(host, edit_context.token, plain)
    else false
}
