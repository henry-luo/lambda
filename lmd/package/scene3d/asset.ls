// D7.2.4: the loader returns ordinary scene values; playback is owned by Radiant.
fn prefixed(value, prefix) => prefix ++ "-" ++ string(value)
fn rewrite(n, prefix, options) element {
    let kind = name(n)
    let attrs = map([for (k, v in n where k is string or k is symbol)
        (string(k), if (contains(["id", "geometry", "material", "texture", "skeleton"], string(k))) prefixed(v, prefix)
            else if (kind == 'keyframe-track' and string(k) == "path") prefixed(v, prefix) else v)])
    let children = [for (child in content(n)) rewrite(child, prefix, options)]
    // Lambda tags are static; share attribute/reference traversal across tag constructors.
    if (kind == 'group') <group *: attrs, *children>
    else if (kind == 'mesh') <mesh *: attrs, *children>
    else if (kind == 'resources') <resources *: attrs, *children>
    else if (kind == 'geometry') <geometry *: attrs>
    else if (kind == 'material') <material *: attrs, *children>
    else if (kind == 'texture') <texture *: attrs>
    else if (kind == 'skeleton') <skeleton *: attrs, *children>
    else if (kind == 'bone') <bone *: attrs, *children>
    else if (kind == 'animation-clip') <'animation-clip' *: attrs,
        autoplay: options.autoplay == true and n.id == options.clip_id, *children>
    else <'keyframe-track' *: attrs>
}
pub fn load(source, options = {}) element^ {
    let prefix = if (options.id == null) "model" else string(options.id)
    let checked_prefix = if (len(prefix) == 0 or len(prefix) > 48 or contains(prefix, "."))
        raise error("scene3d asset: id must be nonempty, at most 48 characters, and contain no dot")
    let source_format = if (options.format == null) 'auto' else options.format
    let raw = input(source, {type: 'scene3d-asset', flavor: source_format})^
    let clips = [for (c in content(raw) where name(c) == 'animation-clip') c]
    let matches = if (options.clip == null) clips else [for (c in clips where c.id == options.clip or c.label == options.clip) c]
    let checked_clip = if (options.clip != null and len(matches) != 1) raise error("scene3d asset: clip selection must match exactly one clip")
    let loaded = rewrite(raw, prefix, {*: options, clip_id: matches[0].id});
    <group *: (if (options.transform is map) options.transform else {}), id: prefix, loaded>
}
