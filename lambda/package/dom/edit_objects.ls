// Package-owned object and markup command policy (D7.2.5).  It composes the
// existing checked range wrapper and contextual-fragment mechanisms without
// giving native code any object-command spelling.
import dom
import structure: lambda.package.dom.edit_structure

fn html_escape(value) {
    replace(replace(replace(replace(replace(string(value), "&", "&amp;"),
                                    "\"", "&quot;"), "<", "&lt;"),
                            ">", "&gt;"), "'", "&#39;")
}

fn wrap_with_attribute(host, edit_context, tag, attribute_name, value) {
    let token = edit_context.token;
    let node = dom.edit_node(host, token);
    if (node == null or edit_context.collapsed) false
    else if (not dom.dom_wrap_range(host, token, dom.edit_start(host, token),
                                    dom.edit_end(host, token), tag)) false
    else {
        // Splitting a partial text range replaces the original text wrapper.
        // Read the generic operation's mapped selection node before finding
        // the newly-created object shell (D7.2.5).
        let wrapped_node = dom.edit_node(host, token);
        let wrapper = structure.ancestor_with_tag(host, wrapped_node, tag);
        if (wrapper == null) false
        else {
            dom.set_attribute(wrapper, attribute_name, value)
            true
        }
    }
}

pub pn create_link(host, edit_context, value) {
    let href = if (value == null) "" else trim(string(value));
    if (href == "") false
    else wrap_with_attribute(host, edit_context, "a", "href", href)
}

pub pn unlink(host, edit_context) {
    let token = edit_context.token;
    let node = dom.edit_node(host, token);
    if (node == null or edit_context.collapsed) false
    else {
        let link = structure.ancestor_with_tag(host, node, "a");
        if (link == null) false
        else dom.dom_unwrap_range(host, token, dom.edit_start(host, token),
                                  dom.edit_end(host, token), link)
    }
}

pub pn insert_image(host, edit_context, value) {
    let src = if (value == null) "" else trim(string(value));
    if (src == "") false
    else dom.dom_insert_html(host, edit_context.token,
                             "<img src=\"" ++ html_escape(src) ++ "\">")
}

pub pn insert_horizontal_rule(host, edit_context) {
    dom.dom_insert_html(host, edit_context.token, "<hr>")
}
