// lambda.paint facade (Lambda_Pkg_Paint.md): painting over tiled typed-array layers.
// Layers, round and libmypaint-model brushes, .myb import, strokes with continuations, presentation
// and interactive sessions with undo; larger scripts import lambda.paint.<module> directly.
import layers: lambda.paint.layer
import brushes: lambda.paint.brush
import strokes: lambda.paint.stroke
import mybs: lambda.paint.myb
import sessions: lambda.paint.session
import paintings: lambda.paint.painting
import surfaces: lambda.paint.surface
import radiant

pub fn capabilities() map^ => radiant.paint_capabilities()

pub fn layer(width: int, height: int, options = {}) map^ => layers.layer(width, height, options)^
pub fn from_pixels(pixels, options = {}) map^ => layers.from_pixels(pixels, options)^
pub fn import_pixels(l, pixels, options = {}) map^ => layers.import_pixels(l, pixels, options)^
pub fn present(l, options = {}) array^ => layers.present(l, options)^
pub fn pixel(l, x: int, y: int) array^ => layers.pixel(l, x, y)^
pub fn painted_tiles(l) int^ => layers.painted_tiles(l)^

pub fn brush(options = {}) map^ => brushes.brush(options)^
// a MyPaint .myb brush (parsed JSON): {brush, ignored}.
pub fn brush_from_myb(data, options = {}) map^ => mybs.from_myb(data, options)^

// a whole stroke written into the layer in place: {rect, work} (PT7); stroke_step is the pure form.
pub pn stroke(var l, b, color, points, options = {}) map^ {
    return strokes.stroke(l, b, color, points, options)^
}
pub fn stroke_step(source, b, color, points, previous = null, options = {}) map^ =>
    strokes.stroke_step(source, b, color, points, previous, options)^

// procedural surfaces: a tileable paper height field for brush grain, and bitmap dab tips.
pub fn paper(kind, options = {}) array^ => surfaces.paper(kind, options)^
pub fn tip(kind, options = {}) array^ => surfaces.tip(kind, options)^
pub fn dabs(b, color, points, carry = null, space = 'linear') map^ => strokes.dabs(b, color, points, carry, space)^

pub fn adjust_brush(b, changes) map^ => brushes.adjust(b, changes)^

// interactive sessions (PT10): begin, feed and end a stroke per pointer; frame presents the dirty rect.
pub fn session(width: int, height: int, options = {}) map^ => sessions.session(width, height, options)^
pub fn begin(s, pointer, b, color) map^ => sessions.begin(s, pointer, b, color)^
// feed continues the stroke in place: {rect, work} (PT7).
pub pn feed(var s, pointer, samples) map^ {
    return sessions.feed(s, pointer, samples)^
}
pub fn end(s, pointer) map^ => sessions.end(s, pointer)^
pub fn cancel(s, pointer) map^ => sessions.cancel(s, pointer)^
pub fn undo(s) map^ => sessions.undo(s)^
pub fn redo(s) map^ => sessions.redo(s)^
pub fn with_texture(s, texture) map^ => sessions.with_texture(s, texture)^
pub fn can_undo(s) bool => sessions.can_undo(s)
pub fn can_redo(s) bool => sessions.can_redo(s)
pub fn clear(s) map^ => sessions.clear(s)^
pub fn mark(s, rect = null) map => sessions.mark(s, rect)
pub fn frame(s) map^ => sessions.frame(s)^
pub fn color_at(s, x: int, y: int) array^ => sessions.color_at(s, x, y)^
pub fn in_layer(samples, box, s, pressure = null) array => sessions.in_layer(samples, box, s, pressure)

// painting files (PT12): a ZIP container storing layer pixels, a preview and a thumbnail.
pub pn save_painting(s, path) int^ {
    return paintings.save(s, path)^
}
pub pn open_painting(path, options = {}) map^ {
    return paintings.load(path, options)^
}
pub fn painting_tree(s) element^ => paintings.tree(s)^
pub fn painting_document(s) element^ => paintings.document(s)^
pub pn export_png(s, path) int^ {
    return paintings.export_png(s, path)^
}
pub fn flattened(s) array^ => paintings.flattened(s)^
pub fn png(pixels) binary^ => paintings.png(pixels)^
