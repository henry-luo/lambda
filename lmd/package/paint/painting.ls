// Painting files (Lambda_Pkg_Paint.md PT12): a ZIP container like Krita's .kra. A .painting holds
// its layer pixels, so loading decodes the stored tiles in one pass and never replays strokes.
// Entries: mimetype (first), painting.mark (the <painting> element, tiles referenced by entry
// name), layers/<id>.tiles (radiant.paint_tiles_encode), preview.png and thumbnail.png.
import layers: lambda.paint.layer
import strokes: lambda.paint.stroke
import sessions: lambda.paint.session
import radiant

pub let MIMETYPE = "application/x-lambda-painting"
pub let VERSION = 1
// the longest side of thumbnail.png, in pixels
pub let THUMBNAIL = 256
// ZIP quotas sized for the bridge's 1 GiB tile blob, above the 256 MiB defaults (Lambda_IO_Zip Z1)
let QUOTAS = {max_archive_bytes: 2147483648, max_member_bytes: 1100000000, max_expanded_bytes: 2147483648}

// straight-alpha sRGB u8 pixels as PNG bytes, through the image system functions (lambda.paint
// does not import lambda.image; PT13).
pub fn png(pixels) binary^ {
    let r = lambda.sys.image_raster(pixels, {})^
    lambda.sys.image_encode(r, r, {format: 'png'})^
}

fn background(s) array^ => strokes.in_space([s.paper[0], s.paper[1], s.paper[2], 1.0], s.layer.blend_space)^

// the session's paint flattened over its paper, as (h, w, 4) sRGB u8 pixels.
pub fn flattened(s) array^ => layers.present(s.layer, {background: background(s)^})^

// image rasters hold at most this many pixels; a larger painting gets a scaled preview
let PREVIEW_PIXELS = 16000000

fn preview_size(w: int, h: int) array {
    let k = if (w * h <= PREVIEW_PIXELS) 1.0 else math.sqrt(float(PREVIEW_PIXELS) / (float(w) * float(h)));
    [max(1, int(floor(float(h) * k))), max(1, int(floor(float(w) * k)))]
}

fn thumbnail_size(w: int, h: int) array => if (w >= h) [max(1, int(round(float(h) * THUMBNAIL / w))), min(w, THUMBNAIL)]
    else [min(h, THUMBNAIL), max(1, int(round(float(w) * THUMBNAIL / h)))]

fn layer_doc(id, l) element => <layer id: id, format: string(l.format), sample: string(l.sample),
    blend_space: string(l.blend_space), tile: l.tile, tiles: "layers/" ++ id ++ ".tiles">

// document(session) -> the <painting> element of painting.mark.
pub fn document(s) element^ {
    let l = s.layer
    let t = thumbnail_size(l.width, l.height)
    let p = preview_size(l.width, l.height)
    let parts = [<assets>, layer_doc("paint", l), <preview src: "preview.png", width: p[1], height: p[0]>,
        <thumbnail src: "thumbnail.png", width: t[1], height: t[0]>];
    (<painting version: VERSION, width: l.width, height: l.height, seed: 1, render_key: "", paper: s.paper, *parts>)
}

// tree(session) -> the <fs> tree of a .painting file, for output(..., {format: 'zip'}).
// The ZIP writer compresses every entry with one method, so mimetype comes first but deflated.
pub fn tree(s) element^ {
    let full = flattened(s)^
    let p = preview_size(s.layer.width, s.layer.height)
    let flat = if (p[0] == s.layer.height) full else resize(full, p[0], p[1])^
    let t = thumbnail_size(s.layer.width, s.layer.height)
    let thumb = if (t[0] == s.layer.height and t[1] == s.layer.width) full else resize(full, t[0], t[1])^;
    let doc = document(s)^
    let tiles = radiant.paint_tiles_encode(s.layer)^
    let preview = png(flat)^
    let small = png(thumb)^
    let entries = [<fs name: "mimetype", kind: 'file', MIMETYPE>, <fs name: "painting.mark", kind: 'file', format: 'mark', doc>,
        <fs name: "layers", kind: 'dir', <fs name: "paint.tiles", kind: 'file', tiles>>,
        <fs name: "preview.png", kind: 'file', preview>, <fs name: "thumbnail.png", kind: 'file', small>];
    (<fs kind: 'dir', *entries>)
}

// save(session, path) -> bytes written. The file is replaced atomically.
pub pn save(s, path) int^ {
    return output(tree(s)^, path, {format: 'zip', *: QUOTAS})^
}

// export_png(session, path) -> bytes written: the painting flattened over its paper, at full size
// (up to 16 million pixels, the image raster limit).
pub pn export_png(s, path) int^ {
    return output(png(flattened(s)^)^, path, 'binary')^
}

pn member(dir, entry) {
    for (p in content(dir)^) {
        if (p.name == entry) { return p }
    }
    return null
}

// the member at a slash-separated entry path, or null.
pn at_path(archive, entry) {
    let parts = split(entry, "/")^
    if (len(parts) == 1) { return member(archive, parts[0])^ }
    if (len(parts) != 2) { return null }
    let dir = member(archive, parts[0])^
    if (dir == null) { return null }
    return member(dir, parts[1])^
}

fn whole_number(v, low, high) bool => (v is int and v >= low and v <= high) ^ { false }

// load(path, options) -> a session holding the stored painting, with nothing to undo.
// Options pass to the new session: history_budget.
pub pn load(path, options = {}) map^ {
    let o = layers.checked(options, ["history_budget"])^
    let z = input(path, {type: 'zip', *: QUOTAS})^
    let mime = at_path(z, "mimetype")^
    if (mime == null or trim(input(mime, 'text')^) != MIMETYPE) raise error("paint: not a .painting file (mimetype)")
    let entry = at_path(z, "painting.mark")^
    if (entry == null) raise error("paint: a .painting file needs painting.mark")
    let doc = input(entry, 'mark')^
    if (name(doc) != 'painting' or doc.version != VERSION) raise error("paint: unsupported painting version")
    if (not whole_number(doc.width, 1, 1048576)^ or not whole_number(doc.height, 1, 1048576)^)
        raise error("paint: painting width and height must be whole numbers")
    let found = [for (c in content(doc) ^ { [] } where (c is element and name(c) == 'layer' and c.id == "paint") ^ { false }) c]
    if (len(found) != 1) raise error("paint: a painting needs one paint layer")
    let ld = found[0]
    let tiles = at_path(z, ld.tiles)^
    if (tiles == null) raise error("paint: missing layer tiles " ++ string(ld.tiles))
    let blank = layers.layer(doc.width, doc.height, {format: ld.format, sample: ld.sample,
        blend_space: ld.blend_space, tile: ld.tile})^
    let layer = radiant.paint_tiles_decode(input(tiles, 'binary')^, blank)^
    let base = sessions.session(doc.width, doc.height, {paper: doc.paper, blend_space: ld.blend_space,
        *: if (o.history_budget == null) ({}) else ({history_budget: o.history_budget})})^
    return {*: base, layer: layer}
}
