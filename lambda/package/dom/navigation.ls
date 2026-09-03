// Link navigation policy (ES31). This module resolves DOM-visible browsing
// context targets and fragment elements; native executes the validated request.
import radiant
import dom

fn is_iframe(node) {
    node != null and dom.closest(node, "iframe") == node
}

fn find_named_frame(node, target_name) {
    if (node == null) { null }
    else if (is_iframe(node)) {
        if (dom.get_attribute(node, "name") == target_name) { node }
        else {
            // An iframe's active nested document is a separate browsing
            // context, not part of its fallback-child DOM traversal.
            let inside = find_named_frame(radiant.embedded_document_root(node), target_name);
            if (inside != null) { inside }
            else { find_named_frame(dom.next_element_sibling(node), target_name) }
        }
    }
    else {
        let inside = find_named_frame(dom.first_element_child(node), target_name);
        if (inside != null) { inside }
        else { find_named_frame(dom.next_element_sibling(node), target_name) }
    }
}

fn find_id(node, fragment) {
    if (node == null) { null }
    else if (dom.get_attribute(node, "id") == fragment) { node }
    else {
        let inside = find_id(dom.first_element_child(node), fragment);
        if (inside != null) { inside }
        else { find_id(dom.next_element_sibling(node), fragment) }
    }
}

fn is_named_anchor(node) {
    node != null and dom.closest(node, "a") == node
}

fn find_named_anchor(node, fragment) {
    if (node == null) { null }
    else if (is_named_anchor(node) and dom.get_attribute(node, "name") == fragment) { node }
    else {
        let inside = find_named_anchor(dom.first_element_child(node), fragment);
        if (inside != null) { inside }
        else { find_named_anchor(dom.next_element_sibling(node), fragment) }
    }
}

fn fragment_target(root, fragment) {
    let by_id = find_id(root, fragment);
    if (by_id != null) { by_id } else { find_named_anchor(root, fragment) }
}

fn parent_root(root) {
    let iframe = radiant.embedding_element(root);
    if (iframe == null) { root } else { dom.document_element(iframe) }
}

fn top_root(root) {
    let iframe = radiant.embedding_element(root);
    if (iframe == null) { root } else { top_root(dom.document_element(iframe)) }
}

fn resolve_target(source, raw_target) {
    let source_root = dom.document_element(source);
    let target_name = if (raw_target == null) "" else raw_target;
    let reserved = lower(target_name);
    if (target_name == "" or reserved == "_self") {
        {target: source_root, target_root: source_root, target_kind: "existing", target_name: null}
    }
    else if (reserved == "_parent") {
        let root = parent_root(source_root);
        {target: root, target_root: root, target_kind: "existing", target_name: null}
    }
    else if (reserved == "_top") {
        let root = top_root(source_root);
        {target: root, target_root: root, target_kind: "existing", target_name: null}
    }
    else if (reserved == "_blank") {
        {target: null, target_root: null, target_kind: "new", target_name: null}
    }
    else {
        let frame = find_named_frame(top_root(source_root), target_name);
        let frame_root = if (frame == null) null else radiant.embedded_document_root(frame);
        if (frame == null) {
            {target: null, target_root: null, target_kind: "new", target_name: target_name}
        }
        else {
            {target: frame, target_root: frame_root, target_kind: "existing", target_name: null}
        }
    }
}

pub pn activate(anchor) {
    let href = dom.get_attribute(anchor, "href");
    if (href == null) { 'pass' }
    else {
        let resolved = resolve_target(anchor, dom.get_attribute(anchor, "target"));
        let destination = if (resolved.target_root == null) null
            else dom.navigation_destination(anchor, href, resolved.target_root);
        let fragment = if (destination == null or destination.kind != "fragment") null
            else fragment_target(resolved.target_root, destination.fragment);
        if (dom.request_navigation({
            source: anchor, url: href,
            target: resolved.target, target_kind: resolved.target_kind,
            target_name: resolved.target_name, fragment_target: fragment
        })) { 'prevent-default' }
        else { 'pass' }
    }
}

// `linkactivation` is behavior-only. The ordinary click remains the author
// event; native calls this only after cancellation is settled.
view <a> {}
on linkactivation(evt) { activate(~) }
