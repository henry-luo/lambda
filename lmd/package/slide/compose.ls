// masters and named layouts lower to ordinary slides before cue compilation (D7.2.4).
import c: .common
import theme: .theme
import scene: .scene

let slide_attrs = ["id", "title", "background", "transition", "transition_duration", "direction", "layout", "theme", "master"]
fn fields(node, excluded = []) => map([for (key, value in node where (key is symbol or key is string) and
    not contains(excluded, string(key))) (string(key), value)])

pub fn definitions(deck, tag) array^ {
    let entries = c.children(deck, tag)
    let ids = [for (entry in entries) {id: c.as_text(entry.id)}]
    let checked_ids = if (any([for (entry in entries) not c.text_id(entry.id)])) raise c.fail(string(tag), "definition requires an ID")
    let checked_unique = c.check_unique(ids, string(tag))^;
    entries
}

fn definition(entries, id, path) element^ {
    let matches = [for (entry in entries where c.as_text(entry.id) == c.as_text(id)) entry]
    if (len(matches) != 1) raise c.fail(path, "unknown definition " ++ c.as_text(id)) else matches[0]
}

fn master(entries, id, seen = []) element^ {
    let path = "master." ++ c.as_text(id)
    let cycle = if (contains(seen, c.as_text(id))) raise c.fail(path, "master inheritance cycle")
    let node = definition(entries, id, path)^;
    let checked_attrs = c.attributes(node, [*slide_attrs, "extends"], path)^;
    let checked_children = if (len(c.objects(node)) != len(content(node))) raise c.fail(path, "master accepts only slide objects")
    let checked_objects = c.check_unique([for (obj in c.objects(node) where obj.id != null) {id: c.as_text(obj.id)}], path)^;
    let parent = if (node.extends != null) master(entries, node.extends, [*seen, c.as_text(id)])^ else <master>;
    <master *: {*: fields(parent), *: fields(node, ["extends"])}, *merge_objects(c.objects(parent), c.objects(node))>
}

// an explicit object ID replaces its inherited slot; other objects keep their order.
fn merge_objects(inherited, local) => [
    *[for (obj in inherited) {
        let replacements = [for (next in local where obj.id != null and c.as_text(next.id) == c.as_text(obj.id)) next]
        if (len(replacements) > 0) replacements[0] else obj
    }],
    *[for (obj in local where obj.id == null or not contains([for (prior in inherited where prior.id != null) c.as_text(prior.id)], c.as_text(obj.id))) obj]]

pub fn layouts(deck, width, height) map^ {
    let entries = definitions(deck, 'layout')^;
    {*: map([for (node in entries) {
        let path = "layout." ++ c.as_text(node.id)
        let checked_attrs = c.attributes(node, ["id"], path)^;
        let checked_name = if (contains(theme.layouts, symbol(c.as_text(node.id)))) raise c.fail(path, "layout shadows a built-in")
        let children = c.children(node, 'placeholder')
        let checked_children = if (len(children) == 0 or len(children) != len(content(node))) raise c.fail(path, "layout requires placeholders")
        let checked_roles = c.check_unique([for (slot in children) {id: c.as_text(slot.role)}], path)^;
        (c.as_text(node.id), {*: map([for (slot in children) {
            let checked_slot = c.attributes(slot, ["role", "x", "y", "width", "height"], path)^;
            let checked_role = if (not c.text_id(slot.role)) raise c.fail(path, "placeholder requires a role")
            let checked_content = if (len(content(slot)) > 0) raise c.fail(path, "placeholder cannot have content")
            let checked_xy = c.numbers(slot, ["x", "y"], false, path)^;
            let checked_wh = c.numbers(slot, ["width", "height"], true, path)^;
            (c.as_text(slot.role), {x: c.num(slot, "x", 0.0), y: c.num(slot, "y", 0.0),
                width: c.num(slot, "width", width), height: c.num(slot, "height", height)})
        }])})
    }])}
}

pub fn slides(deck, width, height, palette, layouts) array^ {
    let masters = definitions(deck, 'master')^;
    // validate unused definitions too: authoring errors must not depend on navigation.
    let checked_masters = [for (node in masters) master(masters, node.id)^];
    let checked_scenes = [for (node in checked_masters) scene.build(<slide *: fields(node, ["master"]), *content(node)>,
        0, width, height, palette, layouts, "master." ++ c.as_text(node.id))^];
    [for (node in c.children(deck, 'slide')) {
        let checked = c.attributes(node, slide_attrs, "slide")^;
        let checked_objects = c.check_unique([for (obj in c.objects(node) where obj.id != null) {id: c.as_text(obj.id)}], "slide")^;
        let selected = c.value(node.master, deck.master)
        let base = if (selected == null or selected == false) <master> else definition(checked_masters, selected, "slide.master")^;
        let attrs = {*: fields(base, ["id", "master"]), *: fields(node, ["master"])};
        <slide *: attrs, *[*merge_objects(c.objects(base), c.objects(node)),
            *[for (child in content(node) where not (child is element) or not contains(['text', 'shape', 'image', 'content', 'group'], name(child))) child]]>
    }]
}
