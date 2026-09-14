// Package-owned block conversion policy (D7.2.5).  It selects legal target
// tags and composes generic node creation/move/remove steps inside one checked
// transaction; native code never maps a command to a block tag.
import dom
import structure: lambda.dom.edit_structure

fn abort(host, token) {
    dom.edit_abort_transaction(host, token)
    false
}

fn canonical_tag(value) {
    let raw = lower(trim(if (value == null) "" else string(value)));
    let without_open = replace(replace(raw, "<", ""), ">", "");
    if (without_open == "div" or without_open == "p" or
        without_open == "pre" or without_open == "blockquote" or
        without_open == "h1" or without_open == "h2" or
        without_open == "h3" or without_open == "h4" or
        without_open == "h5" or without_open == "h6") without_open
    else null
}

fn copy_attributes(source, destination, names, index) {
    if (index >= len(names)) true
    else {
        let attr_name = names[index];
        dom.set_attribute(destination, attr_name,
                          value_or_empty(dom.get_attribute(source, attr_name)))
        copy_attributes(source, destination, names, index + 1)
    }
}

fn value_or_empty(value) {
    if (value == null) "" else value
}

// Capture the successor before reparenting. Live sibling links change during
// a core move, so this remains stable for root-inline content (D7.2.5).
fn move_children_from(child, destination) {
    if (child == null) true
    else {
        let next = dom.next_sibling(child);
        if (dom.append_child(destination, child) == null) false
        else move_children_from(next, destination)
    }
}

fn move_children(source, destination) {
    move_children_from(dom.first_child(source), destination)
}

fn style_without_property(parts, property, index) {
    if (index >= len(parts)) ""
    else {
        let entry = parts[index];
        let pair = split(entry, ":");
        let rest = style_without_property(parts, property, index + 1);
        if (len(pair) < 2 or trim(lower(pair[0])) == property) rest
        else trim(entry) ++ ";" ++ rest
    }
}

fn set_style_property(node, property, value) {
    let current = if (dom.get_attribute(node, "style") == null) ""
                  else dom.get_attribute(node, "style");
    let base = style_without_property(split(current, ";"), lower(property), 0);
    // CSSOM-style declarations serialize the property/value separator with a
    // space, matching browser-visible `innerHTML` (D7.2.5).
    dom.set_attribute(node, "style", base ++ property ++ ": " ++ value ++ ";")
    true
}

fn wrap_host_content(host, tag) {
    let first = dom.first_child(host);
    let destination = dom.create_node(dom.owner_document(host), 1, tag, null);
    if (first == null or destination == null) null
    else if (dom.insert_before(host, destination, first) == null) null
    else if (not move_children_after(destination, destination)) null
    else destination
}

fn move_children_after(marker, destination) {
    move_children_from(dom.next_sibling(marker), destination)
}

pub pn format_block(host, edit_context, value) {
    let token = edit_context.token;
    let source = structure.block_ancestor(host, edit_context.start_container);
    let tag = canonical_tag(value);
    let source_tag = if (source == null) null else lower(dom.node_name(source));
    if (source == null or tag == null or source_tag == tag) false
    else if (not dom.edit_begin_transaction(host, token)) false
    else {
        let parent = dom.parent_node(source);
        let destination = dom.create_node(dom.owner_document(host), 1, tag, null);
        if (parent == null or destination == null) abort(host, token)
        else if (dom.insert_before(parent, destination, source) == null) abort(host, token)
        else if (not copy_attributes(source, destination,
                                     dom.attribute_names(source), 0)) abort(host, token)
        else if (not move_children(source, destination)) abort(host, token)
        else if (dom.remove_child(parent, source) == null) abort(host, token)
        else if (not structure.select_contents(host, destination)) abort(host, token)
        else true
    }
}

pub pn justify(host, edit_context, alignment) {
    let token = edit_context.token;
    let block = structure.block_ancestor(host, edit_context.start_container);
    if (not dom.edit_begin_transaction(host, token)) false
    else {
        let target = if (block == null) wrap_host_content(host, "div") else block;
        if (target == null) abort(host, token)
        else if (not set_style_property(target, "text-align", alignment)) abort(host, token)
        else if (not structure.select_contents(host, target)) abort(host, token)
        else true
    }
}
