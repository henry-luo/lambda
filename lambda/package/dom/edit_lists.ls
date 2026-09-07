// Package-owned single-block list policy (D7.2.5).  The native waist only
// opens the generic transaction; this module supplies every list/container
// choice and composes ordinary DOM node primitives.
import dom
import structure: lambda.package.dom.edit_structure

fn tag_of(node) {
    if (node == null or dom.node_type(node) != 1) ""
    else lower(dom.node_name(node))
}

fn item_ancestor(host, node) {
    if (node == null or dom.same_node(host, node)) null
    else if (tag_of(node) == "li") node
    else item_ancestor(host, dom.parent_node(node))
}

fn abort(host, token) {
    dom.edit_abort_transaction(host, token)
    false
}

fn list_parent(block) {
    let item = if (tag_of(block) == "li") block
               else if (block == null) null else dom.parent_node(block);
    let list_node = if (item == null) null else dom.parent_node(item);
    if (tag_of(item) == "li" and
        (tag_of(list_node) == "ul" or tag_of(list_node) == "ol")) list_node else null
}

fn list_item(block) {
    let item = if (tag_of(block) == "li") block
               else if (block == null) null else dom.parent_node(block);
    if (tag_of(item) == "li") item else null
}

fn list_for_item(item) {
    let list_node = if (item == null) null else dom.parent_node(item);
    if (tag_of(list_node) == "ul" or tag_of(list_node) == "ol") list_node
    else null
}

// Capture the successor before reparenting. The core move changes the live
// sibling chain, so asking the marker for its successor after the move can
// revisit the child and make the checked transaction roll back (D7.2.5).
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

// A host may legally contain an inline run directly. The list policy turns
// that run into one list item; native only carries out these ordinary moves.
fn wrap_host_content(host, list_tag) {
    let first = dom.first_child(host);
    let document = dom.owner_document(host);
    let list_node = dom.create_node(document, 1, list_tag, null);
    let item = dom.create_node(document, 1, "li", null);
    if (first == null or list_node == null or item == null) false
    else if (dom.insert_before(host, list_node, first) == null) false
    else if (dom.append_child(list_node, item) == null) false
    else move_children_after(list_node, item)
}

fn move_children_after(marker, destination) {
    move_children_from(dom.next_sibling(marker), destination)
}

fn replace_list_tag(list_node, list_tag) {
    let parent = dom.parent_node(list_node);
    let replacement = dom.create_node(dom.owner_document(list_node), 1, list_tag, null);
    if (parent == null or replacement == null) false
    else if (dom.insert_before(parent, replacement, list_node) == null) false
    else if (not move_children(list_node, replacement)) false
    else dom.remove_child(parent, list_node) != null
}

// Indentation changes only an existing list item's parentage.  The selected
// previous item chooses the nested list; ordinary core tree operations carry
// out the plan, so no list command shape reaches native code.
pub pn indent(host, edit_context) {
    let token = edit_context.token;
    let block = item_ancestor(host, edit_context.start_container);
    let item = list_item(block);
    let list_node = list_for_item(item);
    let previous = if (item == null) null else dom.previous_sibling(item);
    let nested = if (previous == null) null else dom.last_child(previous);
    let list_tag = tag_of(list_node);
    if (item == null or list_node == null or tag_of(previous) != "li") false
    else if (not dom.edit_begin_transaction(host, token)) false
    else {
        let child_list = if (tag_of(nested) == list_tag) nested
                         else dom.create_node(dom.owner_document(host), 1,
                                              list_tag, null);
        if (child_list == null) abort(host, token)
        else if (not dom.same_node(child_list, nested) and
                 dom.append_child(previous, child_list) == null) abort(host, token)
        else if (dom.append_child(child_list, item) == null) abort(host, token)
        else true
    }
}

// Outdent is the inverse tree move.  Empty nested lists disappear in the
// same generic transaction, which keeps observers from seeing a partial move.
pub pn outdent(host, edit_context) {
    let token = edit_context.token;
    let block = item_ancestor(host, edit_context.start_container);
    let item = list_item(block);
    let list_node = list_for_item(item);
    let outer_item = if (list_node == null) null else dom.parent_node(list_node);
    let outer_list = list_for_item(outer_item);
    if (item == null or list_node == null or outer_item == null or outer_list == null) false
    else if (not dom.edit_begin_transaction(host, token)) false
    else {
        let after_outer = dom.next_sibling(outer_item);
        if (dom.insert_before(outer_list, item, after_outer) == null) abort(host, token)
        else if (len(dom.child_nodes(list_node)) == 0 and
                 dom.remove_child(outer_item, list_node) == null) abort(host, token)
        else true
    }
}

pub pn toggle(host, edit_context, list_tag) {
    let token = edit_context.token;
    let block = structure.block_ancestor(host, edit_context.start_container);
    let current_list = list_parent(block);
    if (not dom.edit_begin_transaction(host, token)) false
    else {
        let changed = if (block == null) wrap_host_content(host, list_tag)
        else if (current_list != null and tag_of(current_list) == list_tag) {
            let parent = dom.parent_node(current_list);
            let item = dom.parent_node(block);
            if (parent == null or dom.insert_before(parent, block, current_list) == null)
                false
            else if (item != null and len(dom.child_nodes(item)) == 0 and
                     dom.remove_child(current_list, item) == null)
                false
            else if (len(dom.child_nodes(current_list)) == 0 and
                     dom.remove_child(parent, current_list) == null)
                false
            else true
        }
        else if (current_list != null) replace_list_tag(current_list, list_tag)
        else {
            let parent = dom.parent_node(block);
            let document = dom.owner_document(host);
            let list_node = dom.create_node(document, 1, list_tag, null);
            let item = dom.create_node(document, 1, "li", null);
            if (parent == null or list_node == null or item == null) false
            else if (dom.insert_before(parent, list_node, block) == null) false
            else if (dom.append_child(list_node, item) == null) false
            else if (dom.append_child(item, block) == null) false
            else true
        };
        if (not changed) abort(host, token)
        else if (not structure.restore_selection(host, edit_context)) abort(host, token)
        else true
    }
}
