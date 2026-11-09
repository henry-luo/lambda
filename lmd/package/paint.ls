// lambda.paint facade (Lambda_Pkg_Paint.md): painting over tiled typed-array layers.
// Layers, round and libmypaint-model brushes, .myb import, strokes with continuations and presentation;
// larger scripts import lambda.paint.<module> directly.
import layers: lambda.paint.layer
import brushes: lambda.paint.brush
import strokes: lambda.paint.stroke
import mybs: lambda.paint.myb
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

pub fn stroke(l, b, color, points) map^ => strokes.stroke(l, b, color, points)^
pub fn stroke_step(source, b, color, points, previous = null) map^ => strokes.stroke_step(source, b, color, points, previous)^
pub fn dabs(b, color, points, carry = null, space = 'linear') map^ => strokes.dabs(b, color, points, carry, space)^
