// Package-owned structural editing policy (D7.2.5).  Native code receives
// only explicit containers/tags and performs generic Range and node mechanics.
import dom

fn same(a, b) {
    if (a == null or b == null) false else dom.same_node(a, b)
}

fn tag_of(node) {
    if (node == null or dom.node_type(node) != 1) null
    else lower(dom.node_name(node))
}

// The UA's current paragraph-like classification.  It is package data so a
// corpus-driven change cannot create a second native editing command table.
pub fn is_block(node) {
    let tag = tag_of(node);
    tag == "address" or tag == "article" or tag == "aside" or
    tag == "blockquote" or tag == "div" or tag == "figcaption" or
    tag == "figure" or tag == "footer" or tag == "header" or
    tag == "h1" or tag == "h2" or tag == "h3" or tag == "h4" or
    tag == "h5" or tag == "h6" or tag == "main" or tag == "nav" or
    tag == "li" or tag == "p" or tag == "pre" or tag == "section"
}

pub fn block_ancestor(host, node) {
    if (node == null or same(host, node)) null
    else if (is_block(node)) node
    else block_ancestor(host, dom.parent_node(node))
}

// Formatting state is an ordinary ancestor walk.  Keeping it here makes both
// command toggling and queryCommandState read the same live DOM fact.
pub fn ancestor_with_tag(host, node, tag) {
    if (node == null or same(host, node)) null
    else if (tag_of(node) == tag) node
    else ancestor_with_tag(host, dom.parent_node(node), tag)
}

fn selection_offset(node, codepoint_offset) {
    let value = dom.node_value(node);
    if (value == null) codepoint_offset
    else utf16_from_codepoint(value, codepoint_offset, 0, 0)
}

// Moving a selected node through ordinary DOM removal maps a live Range to its
// old parent. Restore the package's captured endpoints after structural moves
// so the next command still addresses the original content (D7.2.5).
pub pn restore_selection(host, edit_context) {
    let selection = dom.document_selection(dom.owner_document(host));
    let start_node = edit_context.start_container;
    let end_node = edit_context.end_container;
    if (selection == null or start_node == null or end_node == null) false
    else {
        dom.set_base_and_extent(selection, start_node,
                                selection_offset(start_node, edit_context.start),
                                end_node, selection_offset(end_node, edit_context.end));
        true
    }
}

// A root-inline selection has no movable endpoint of its own. Once policy
// wraps it in a structural container, select that container's contents so the
// following command classifies the same block rather than the edit host.
pub pn select_contents(host, target) {
    let selection = dom.document_selection(dom.owner_document(host));
    if (selection == null or target == null) false
    else {
        dom.set_base_and_extent(selection, target, 0, target,
                                len(dom.child_nodes(target)))
        true
    }
}

fn mergeable(first_block, second_block) {
    let a = tag_of(first_block);
    let b = tag_of(second_block);
    a != null and a == b and not same(first_block, second_block)
}

// Replacement does not imply a block join.  The planner decides compatibility
// from both pre-action ancestors, then gives the waist the exact pair to move.
pub pn replace_range(host, token, text) {
    let start_block = block_ancestor(host, dom.edit_start_container(host, token));
    let end_block = block_ancestor(host, dom.edit_end_container(host, token));
    if (not dom.dom_replace_dom_range(host, token, text)) false
    else {
        if (mergeable(start_block, end_block)) {
            dom.dom_merge_adjacent_blocks(host, token, start_block, end_block)
        }
        true
    }
}

// A paragraph split is a generic split of the package-selected container.  An
// inline/root selection chooses the document default tag here, not in C++.
pub pn insert_paragraph(host, token) {
    let block = block_ancestor(host, dom.edit_start_container(host, token));
    let source = if (block == null) host else block;
    let tag = if (block == null) "div" else tag_of(block);
    dom.edit_split_block(host, token, source, tag)
}
