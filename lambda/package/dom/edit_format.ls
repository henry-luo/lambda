// Package-owned inline-format policy (D7.2.5).  The waist receives only a
// wrapper tag and ordinary DOM mutations; choosing semantic versus CSS output
// and interpreting legacy command values remains here.
import dom
import session: lambda.dom.edit_session
import structure: lambda.dom.edit_structure

fn value_or_empty(value) {
    if (value == null) "" else string(value)
}

fn value_or_descriptor(value, descriptor) {
    if (value == null) descriptor.format_value else string(value)
}

fn style_entry(property, value) {
    property ++ ":" ++ value ++ ";"
}

fn style_value_at(parts, property, index) {
    if (index >= len(parts)) null
    else {
        let pair = split(parts[index], ":");
        if (len(pair) >= 2 and trim(lower(pair[0])) == property)
            trim(pair[1])
        else style_value_at(parts, property, index + 1)
    }
}

pub fn style_value(node, property) {
    if (node == null) null
    else style_value_at(split(value_or_empty(dom.get_attribute(node, "style")), ";"),
                        lower(property), 0)
}

fn wrapper_for(host, node, tag) {
    // A missing exclusive tag is not a match for text nodes: tag_of(text) is
    // null too. Without this guard ordinary formats incorrectly try to unwrap
    // the selected text node itself instead of creating their wrapper.
    if (tag == null) null else structure.ancestor_with_tag(host, node, tag)
}

fn wrapper_has_value(wrapper, descriptor) {
    if (wrapper == null) false
    else if (descriptor.attribute_name != null)
        lower(value_or_empty(dom.get_attribute(wrapper, descriptor.attribute_name))) ==
            lower(value_or_empty(descriptor.format_value))
    else lower(value_or_empty(style_value(wrapper, descriptor.css_property))) ==
        lower(value_or_empty(descriptor.format_value))
}

pub fn has_state(host, node, descriptor, use_css) {
    let tag = if (use_css and descriptor.css_property != null) "span"
              else descriptor.format_tag;
    let wrapper = wrapper_for(host, node, tag);
    if (descriptor.css_property == null and descriptor.attribute_name == null)
        wrapper != null
    else if (use_css and descriptor.css_property != null)
        descriptor.format_value == null and wrapper != null or
        lower(value_or_empty(style_value(wrapper, descriptor.css_property))) ==
            lower(value_or_empty(if (descriptor.css_value == null)
                                 descriptor.format_value else descriptor.css_value))
    else wrapper_has_value(wrapper, descriptor)
}

pub fn value(host, node, descriptor, use_css) {
    let tag = if (use_css and descriptor.css_property != null) "span"
              else descriptor.format_tag;
    let wrapper = wrapper_for(host, node, tag);
    if (wrapper == null) ""
    else if (use_css and descriptor.css_property != null)
        value_or_empty(style_value(wrapper, descriptor.css_property))
    else if (descriptor.attribute_name != null)
        value_or_empty(dom.get_attribute(wrapper, descriptor.attribute_name))
    else ""
}

// The wrapper is first created by the generic checked range operation.  Its
// selected text remains the invocation node, so policy can locate that wrapper
// with ordinary DOM traversal and decorate it using the core attribute API.
fn set_wrapper_value(host, token, node, descriptor, use_css) {
    let tag = if (use_css and descriptor.css_property != null) "span"
              else descriptor.format_tag;
    let wrapper = wrapper_for(host, node, tag);
    if (wrapper == null) false
    else if (use_css and descriptor.css_property != null) {
        dom.set_attribute(wrapper, "style", style_entry(
            descriptor.css_property,
            if (descriptor.css_value == null) descriptor.format_value
            else descriptor.css_value))
        true
    }
    else if (descriptor.attribute_name != null) {
        dom.set_attribute(wrapper, descriptor.attribute_name,
                          descriptor.format_value)
        true
    }
    else if (descriptor.legacy_style and descriptor.css_property != null) {
        // Legacy color commands still persist CSS where HTML has no matching
        // attribute; styleWithCSS only chooses the element spelling (D7.2.5).
        dom.set_attribute(wrapper, "style", style_entry(descriptor.css_property,
            if (descriptor.css_value == null) descriptor.format_value
            else descriptor.css_value))
        true
    }
    else true
}

// Text replacement preserves the resolved text node.  Reuse that stable node
// to attach a collapsed typing mark through the same generic wrapper path.
pub pn apply_typing_mark(host, token, node, start_offset, length) {
    let mark = session.typing_format(host);
    if (mark == null or length == 0) true
    else if (not dom.dom_wrap_range(host, token, start_offset, start_offset + length,
                                    mark.format_tag)) false
    else {
        let wrapper = wrapper_for(host, node, mark.format_tag);
        if (wrapper == null) false
        else if (mark.attribute_name == null) true
        else {
            dom.set_attribute(wrapper, mark.attribute_name, mark.attribute_value)
            true
        }
    }
}

pub pn apply_format(host, edit_context, descriptor, value) {
    let token = edit_context.token;
    let node = dom.edit_node(host, token);
    let use_css = session.style_with_css(host);
    let resolved = {
        *: descriptor,
        format_value: value_or_descriptor(value, descriptor)
    };
    if (edit_context.collapsed)
        session.set_typing_format(host, resolved, use_css) != null
    else if (node == null) false
    else {
        let tag = if (use_css and resolved.css_property != null) "span"
                  else resolved.format_tag;
        let wrapper = wrapper_for(host, node, tag);
        let exclusive = wrapper_for(host, node, resolved.exclusive_tag);
        if (has_state(host, node, resolved, use_css) and wrapper != null)
            dom.dom_unwrap_range(host, token, dom.edit_start(host, token),
                                 dom.edit_end(host, token), wrapper)
        else if (exclusive != null and
                 not dom.dom_unwrap_range(host, token, dom.edit_start(host, token),
                                          dom.edit_end(host, token), exclusive)) false
        else if (not dom.dom_wrap_range(host, token, dom.edit_start(host, token),
                                        dom.edit_end(host, token), tag)) false
        else set_wrapper_value(host, token, dom.edit_node(host, token), resolved, use_css)
    }
}

fn direct_format_wrapper(host, node) {
    let parent = if (node == null) null else dom.parent_node(node);
    let tag = if (parent == null or dom.node_type(parent) != 1) ""
              else lower(dom.node_name(parent));
    if (contains(["b", "i", "u", "s", "sub", "sup", "font", "span"], tag))
        parent
    else null
}

fn unwrap_direct_formats(host, token, node, start_offset, end_offset, changed) {
    let wrapper = direct_format_wrapper(host, node);
    if (wrapper == null) changed
    else if (not dom.dom_unwrap_range(host, token, start_offset, end_offset, wrapper)) false
    else unwrap_direct_formats(host, token, node, start_offset, end_offset, true)
}

fn clear_block_style(host, node) {
    let block = structure.block_ancestor(host, node);
    if (block == null or dom.get_attribute(block, "style") == null) false
    else { dom.set_attribute(block, "style", ""); true }
}

// A collapsed removeFormat affects only future typing. Non-collapsed input
// removes every direct formatting shell around the selected run while leaving
// links and other authored non-format attributes untouched (D7.2.5).
pub pn remove(host, edit_context) {
    let token = edit_context.token;
    let node = dom.edit_node(host, token);
    if (edit_context.collapsed) session.clear_typing_format(host) != null
    else if (node == null) false
    else {
        let inline_changed = unwrap_direct_formats(host, token, node,
                                                   dom.edit_start(host, token),
                                                   dom.edit_end(host, token), false);
        let block_changed = clear_block_style(host, node);
        inline_changed or block_changed
    }
}
