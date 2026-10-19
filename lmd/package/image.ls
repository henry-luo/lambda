import sequence: .editor.mod_doc

// D7.2.4v3 / S9.1.2: Raster storage is native; document composition stays in Lambda values.
pub fn blank(options) element^ => lambda.sys.image_raster(null, options)^
pub fn from_array(samples, options = {}) element^ => lambda.sys.image_raster(samples, options)^
pub fn channel(raster, channel_name) array^ => lambda.sys.image_region(raster, null, channel_name)^
fn checked_options(options, allowed) map^ {
    if (not (options is map) or len([for (key, value in options where not (string(key) in allowed)) key]) > 0)
        raise error("image: unknown option or options are not a map")
    else options
}
pub fn read_region(raster, rect, options = {}) array^ {
    let checked = checked_options(options, ["channel"])^
    lambda.sys.image_region(raster, rect, checked.channel)^
}

pub fn materialize(raster) element =>
    <raster width: raster.width, height: raster.height, sample_type: raster.sample_type,
        channels: raster.channels, color: raster.color, alpha_mode: raster.alpha_mode,
        pixels: raster.pixels>

pub fn raster_of(layer) element? => [for (child in content(layer) where name(child) == 'raster') child][0]
pub fn layer(raster, options = {}) element => <layer kind: 'raster', *: options, raster>
pub fn stack(layers, options = {}) element => <stack *: options, *layers>
pub fn frame(composition, duration, options = {}) element =>
    <frame duration: duration, *: options, composition>
pub fn document(composition, options = {}) element => <image *: options, composition>
pub fn frames(image) array => [for (child in content(image) where name(child) == 'frame') child]
pub fn layers(composition) array =>
    [for (child in content(composition) where name(child) in ['layer', 'stack']) child]
pub fn assets(image) element? => [for (child in content(image) where name(child) == 'assets') child][0]
pub fn composition(image, frame_index = 0) element? {
    let direct = [for (child in content(image) where name(child) == 'stack') child]
    if (len(direct) == 1) direct[0] else frames(image)[frame_index][0]
}

fn psd_node(record) element {
    let attrs = {id: record.id, name: record.name, opacity: record.opacity,
        visible: record.visible, source_index: record.source_index,
        unsupported: record.unsupported};
    if (record.kind == "group")
        <stack *: attrs, blend: if (record.blend == "pass") 'normal' else record.blend,
            isolation: record.isolation, *[for (child in record.children) psd_node(child)]>
    else
        <layer *: attrs, kind: record.kind, position: record.position, blend: record.blend,
            *: if (record.clipping) {clipping: true} else {},
            record.raster;
            if (record.mask != null)
                <mask role: 'alpha', position: record.mask.position, default: record.mask.default,
                    enabled: record.mask.enabled, invert: record.mask.invert, record.mask.raster>
        >
}

pub fn of_raster(raster, options = {}) element =>
    <image width: raster.width, height: raster.height, *: options,
        <stack <layer kind: 'raster', raster>>>

pub fn decode(bytes, options = {}) element^ {
    let decoded = lambda.sys.image_decode(bytes, options)^;
    <image width: decoded.width, height: decoded.height,
        plays: if (decoded.format == "psd") 1 else decoded.plays,
        unsupported: decoded.unsupported,
        <assets
            <asset id: "source", format: decoded.format, data: bytes>
            if (decoded.profile != null) <profile id: "icc", format: 'icc', data: decoded.profile>
        >
        if (decoded.preview != null) <preview decoded.preview>
        if (decoded.format == "psd")
            <stack
                if (len(decoded.records) > 0) for (record in decoded.records) psd_node(record)
                else <layer kind: 'raster', decoded.preview>
            >
        else if (decoded.animated)
            for (entry in decoded.frames)
                <frame duration: entry.duration, <stack <layer kind: 'raster', entry.raster>>>
        else <stack <layer kind: 'raster', decoded.frames[0].raster>>
    >
}
pub pn read(target, options = {}) element^ {
    let bytes = input(target, 'binary')^
    decode(bytes, options)^
}

pub fn fill(raster, color) element^ =>
    blank({width: raster.width, height: raster.height, channels: len(raster.channels),
        color_space: raster.color, color: color})^

pub fn crop(raster, rect) element^ =>
    from_array(read_region(raster, rect)^, {color_space: raster.color})^
pub fn resize(raster, width, height) element^ =>
    from_array(lambda.sys.resize(raster.pixels, height, width)^, {color_space: raster.color})^
pub fn adjust(raster, options) element^ =>
    from_array(image_process(raster.pixels, options)^, {color_space: raster.color})^

pub fn render_frame(image, options = {}) element^ => lambda.sys.image_render(image, options)^
pub fn flatten(image, options = {}) element^ => of_raster(render_frame(image, options)^)
pub fn encode(image, options = {}) binary^ {
    let doc = if (name(image) == 'raster') of_raster(image) else image
    if (options.format in ['gif', "gif"]) {
        let timeline = frames(doc)
        let count = if (options.frame != null or len(timeline) == 0) 1 else len(timeline)
        let profile = [for (entry in lambda.sys.image_capabilities()^ where entry.format == "gif") entry][0]
        let checked = if (not (doc.width is int) or not (doc.height is int) or
            doc.width < 1 or doc.height < 1 or doc.width > 16000000 or doc.height > 16000000 or
            count > profile.max_frames or doc.width > profile.decoded_byte_limit / (doc.height * 4 * count))
                raise error("image.encode: animation exceeds the decoded frame budget") else true
        let rendered = if (options.frame != null or len(timeline) == 0)
                [render_frame(doc, {frame: options.frame})^]
            else [for (i in 0 to (len(timeline) - 1)) render_frame(doc, {frame: i})^]
        lambda.sys.image_encode(doc, rendered, options)^
    } else {
        let rendered = render_frame(doc, {frame: options.frame})^
        lambda.sys.image_encode(doc, rendered, options)^
    }
}
pub pn write(image, target, options = {}) bool^ {
    let bytes = encode(image, options)^
    output(bytes, target)^
    true
}

// Pure replacements share unchanged children and invalidate the derived preview.
pub fn with_children(node, children) element^ => lambda.sys.image_patch(node, {},
    if (name(node) == 'image') [for (child in children where name(child) != 'preview') child] else children)^
pub fn with_properties(node, properties) element^ => lambda.sys.image_patch(node, properties,
    if (name(node) == 'image') [for (child in content(node) where name(child) != 'preview') child] else null)^
pub fn adopt(node) element^ {
    if (name(node) == 'raster') {
        let owned = from_array(node.pixels, {color_space: node.color})^
        let channels = [for (channel_name in node.channels) string(channel_name)]
        if (owned.width != node.width or owned.height != node.height or
            owned.channels != channels or string(node.sample_type) != owned.sample_type or
            string(node.alpha_mode) != owned.alpha_mode)
                raise error("image.adopt: raster descriptor disagrees with its samples or profile")
        else owned
    }
    else if (name(node) in ['image', 'frame', 'stack', 'layer', 'mask', 'preview'])
        with_children(node, [for (child in content(node)) adopt(child)^])^
    else node
}

pub fn node_at(node, path) element^ {
    if (len(path) == 0) node
    else if (not (path[0] is int) or path[0] < 0 or path[0] >= len(content(node)))
        raise error("image.node_at: invalid content path")
    else node_at(node[path[0]], sequence.list_drop(path, 1))^
}
pub fn replace_at(node, path, replacement) element^ {
    if (len(path) == 0) replacement
    else {
        let child = node_at(node, [path[0]])^
        let changed = replace_at(child, sequence.list_drop(path, 1), replacement)^
        with_children(node, sequence.list_set(content(node), path[0], changed))^
    }
}
pub fn splice_children(node, start, count, additions) element^ {
    if (not (start is int) or not (count is int) or start < 0 or count < 0 or
        start > len(content(node)) or count > len(content(node)) - start)
        raise error("image.splice_children: invalid range")
    else with_children(node, sequence.list_splice(content(node), start, count, additions))^
}
pub fn insert(node, index, child) element^ => splice_children(node, index, 0, [child])^
pub fn remove(node, index) element^ => splice_children(node, index, 1, [])^
pub fn duplicate(node, index) element^ => insert(node, index + 1, node_at(node, [index])^)^
pub fn reorder(node, from_index, to_index) element^ {
    let child = node_at(node, [from_index])^
    insert(remove(node, from_index)^, to_index, child)^
}
pub fn mask(raster, options = {}) element => <mask role: 'alpha', *: options, raster>
pub fn to_animation(image, duration) element^ {
    if (len(frames(image)) > 0) image
    else with_children(image, [for (child in content(image))
        if (name(child) == 'stack') frame(child, duration) else child])^
}

pub fn with_region(raster, rect, samples, options = {}) element^ {
    let checked = checked_options(options, ["channel"])^
    lambda.sys.image_write_region(raster, samples, {rect: rect, channel: checked.channel})^
}
pub fn with_channel(raster, channel_name, samples) element^ =>
    lambda.sys.image_write_region(raster, samples, {channel: channel_name})^
pub fn clear(raster) element^ => blank({width: raster.width, height: raster.height,
    channels: len(raster.channels), color_space: raster.color})^
pub fn stamp(raster, brush, position, options = {}) element^ {
    let checked = checked_options(options, ["opacity"])^
    render_frame(document(stack([layer(brush, {position: position, opacity: checked.opacity}),
        layer(raster)]), {width: raster.width, height: raster.height}))^
}

// Each checked edit publishes once; failure leaves the borrowed binding untouched (S9.1.3).
pub pn write_region(var raster, rect, samples, options = {}) bool^ {
    let replacement = with_region(raster, rect, samples, options)^
    raster = replacement
    true
}
pub pn replace_channel(var raster, channel_name, samples) bool^ {
    let replacement = with_channel(raster, channel_name, samples)^
    raster = replacement
    true
}
pub fn info(image) map => {width: image.width, height: image.height,
    frames: len(frames(image)), plays: image.plays, assets: assets(image)}
pub fn validate(image) bool^ {
    let timeline = frames(image)
    if (len(timeline) == 0) { let checked = render_frame(image)^; true }
    else { let checked = [for (i in 0 to (len(timeline) - 1)) render_frame(image, {frame: i})^]; true }
}

pub fn capabilities() array^ => lambda.sys.image_capabilities()^
pub fn encode_report(image, options = {}) map^ {
    let bytes = encode(image, options)^
    let format = if (options.format == null) 'png' else options.format
    {bytes: bytes, report: {format: format,
        regenerated: if (format in ['psd', "psd"]) ['samples', 'composite', 'layer_ids'] else ['samples'],
        preserved: if (format in ['psd', "psd"]) ['hierarchy', 'names', 'geometry', 'opacity', 'masks']
            else if (format in ['gif', "gif"]) ['timing', 'plays'] else [],
        rasterized: if (format in ['psd', "psd"]) [] else ['composition'],
        omitted: [*if (assets(image) == null) [] else [for (asset in content(assets(image))) asset.id],
            *if (len(frames(image)) > 0 and not (format in ['gif', "gif"])) ['timing'] else [],
            *if (len([for (child in content(image) where name(child) == 'metadata') child]) > 0) ['metadata'] else []],
        notes: if (format in ['psd', "psd"]) ["opacity and mask default round to 8 bits",
            "source blocks are rewritten; opaque source metadata is not copied"] else []}}
}
pub fn render_region(image, rect, options = {}) element^ =>
    render_frame(image,{*:checked_options(options,["frame"])^,rect:rect})^
pub fn sample(raster, x, y) array^ => read_region(raster, [x, y, x + 1, y + 1])^[0, 0]

pub fn transform(raster, options) element^ => adjust(raster, options)^
pub fn rotate(raster, quarter_turns) element^ => transform(raster, {turns: quarter_turns})^
pub fn flip(raster, horizontal = true, vertical = false) element^ =>
    transform(raster, {flip_x: horizontal, flip_y: vertical})^

// S9.2.2: coverage and paint return immutable rasters with shared untouched tiles.
fn edit_samples(raster,operation,options={}) any^ {
    if (not (options is map) or any([for (key,value in options) string(key) == "op"]))
        raise error("image: edit options must be a map without an op override")
    else lambda.sys.image_edit(raster,{op:operation,*:options})^
}
pub fn selection(width,height,options) element^ =>
    edit_samples(null,"shape",{*:checked_options(options,["shape","start","end","points"])^,width:width,height:height})^
pub fn combine_coverage(coverage,other,mode="replace") element^ => edit_samples(coverage,"combine",{other:other,mode:mode})^
pub fn invert_coverage(coverage) element^ => edit_samples(coverage,"invert")^
pub fn feather_coverage(coverage,radius) element^ => edit_samples(coverage,"feather",{radius:radius})^
pub fn coverage_bounds(coverage) any^ => edit_samples(coverage,"bounds")^
pub fn coverage_overlay(coverage) element^ => edit_samples(coverage,"overlay")^
pub fn paint_stroke(raster,options) element^ => edit_samples(raster,"stroke",options)^
pub fn paint_fill(raster,options) element^ => edit_samples(raster,"fill",options)^
pub fn gradient(raster,options) element^ => edit_samples(raster,"gradient",options)^
pub fn wand(raster,point,options={}) element^ =>
    edit_samples(raster,"wand",{*:checked_options(options,["tolerance","contiguous"])^,point:point})^
pub pn storage_info(raster) map^ { return lambda.sys.image_storage(raster)^ }
pub fn reframe(raster,rect,options={}) element^ =>
    edit_samples(raster,"reframe",{*:checked_options(options,["fill"])^,rect:rect})^
pub fn mix_samples(raster,other,options={}) element^ =>
    edit_samples(raster,"mix",{*:checked_options(options,["selection","origin"])^,other:other})^
pub fn extract_samples(raster,options={}) element^ => edit_samples(raster,"extract",options)^
