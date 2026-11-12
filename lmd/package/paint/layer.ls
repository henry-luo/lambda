// Tiled typed-array layers (Lambda_Pkg_Paint.md PT3, PT4). A layer is a map whose `tiles`
// list holds one 64 x 64 tile per grid cell: an f32 (64, 64, 4) premultiplied RGBA array, a
// (64, 64) coverage array, an f32 (64, 64, 3B + 2) pigment film over its substrate (PE8), or null
// for an untouched tile. Edits return new layers that share every untouched tile with their input
// (S9.1.2), so a history entry is a previous value.
import media: lambda.paint.media
import radiant

pub let TILE = 64
pub let FORMATS = ['rgba', 'coverage', 'pigment']

fn text_in(value, choices) bool => (value is symbol or value is string) and string(value) in [for (c in choices) string(c)]

pub fn checked(options, allowed) map^ {
    if (not (options is map) or len([for (key, value in options where ((not (string(key) in allowed)) ^ { false })) key]) > 0)
        raise error("paint: unknown option or options are not a map")
    else options
}

fn unit(v) bool => ((v is int or v is float) and v >= 0.0 and v <= 1.0) ^ { false }

// a pigment layer's substrate: `substrate`, linear reflectance per band, or else the sRGB `paper`
// colour (white by default).
fn substrate_of(o) array^ {
    let given = if (o.substrate != null) o.substrate else if (o.paper != null) media.substrate(o.paper) else ([1.0, 1.0, 1.0])
    if (not (given is array and len(given) == media.BANDS and all([for (v in given) unit(v)])))
        raise error("paint: a pigment layer's paper is sRGB [r, g, b] and its substrate 3 reflectances, all in [0, 1]")
    else [for (v in given) float(v)]
}

// a blank layer: rgba f32 by default; coverage layers may hold u8 samples; a pigment layer is a
// clean film over `paper` (or `substrate`), always f32 in linear light.
pub fn layer(width: int, height: int, options = {}) map^ {
    let o = checked(options, ["format", "sample", "blend_space", "tile", "paper", "substrate"])^
    let kind = if (o.format == null) 'rgba' else o.format
    let sample = if (o.sample == null) 'f32' else o.sample
    let blend = if (o.blend_space == null) 'linear' else o.blend_space
    let tile = if (o.tile == null) TILE else o.tile
    if (width < 1 or height < 1 or width > 1048576 or height > 1048576) raise error("paint: layer extent must be 1 to 1048576")
    else if (not text_in(kind, FORMATS)) raise error("paint: layer format must be 'rgba' or 'coverage'")
    else if (not text_in(sample, ['f32', 'u8']) or (string(sample) == "u8" and string(kind) != "coverage"))
        raise error("paint: layer sample must be 'f32' (or 'u8' for coverage)")
    else if (not text_in(blend, ['linear', 'srgb'])) raise error("paint: blend_space must be 'linear' or 'srgb'")
    else if (string(kind) == "pigment" and (string(sample) != "f32" or string(blend) != "linear"))
        raise error("paint: a pigment layer is f32 in linear light")
    else if (string(kind) != "pigment" and (o.paper != null or o.substrate != null))
        raise error("paint: only a pigment layer takes paper or substrate")
    else {
        let cells = ((width + tile - 1) div tile) * ((height + tile - 1) div tile)
        let base = {width: width, height: height, tile: tile, format: symbol(string(kind)), sample: symbol(string(sample)),
            blend_space: symbol(string(blend)), tiles: [for (i in 0 to (cells - 1)) null]}
        if (string(kind) == "pigment") ({*: base, bands: media.BANDS, substrate: substrate_of(o)^}) else base
    }
}

pub fn pigmented(l) bool => (string(l.format) == "pigment") ^ { false }

// a blank layer of the same kind and geometry as `l`.
pub fn blank_like(l) map^ => layer(l.width, l.height, {format: l.format, sample: l.sample, blend_space: l.blend_space,
    tile: l.tile, *: if (pigmented(l)) ({substrate: l.substrate}) else ({})})^

// bytes one tile of `l` holds: 4 per sample, 1 for u8 coverage.
pub fn tile_bytes(l) int => l.tile * l.tile * (if (pigmented(l)) 3 * l.bands + 2 else if (l.format == 'rgba') 4 else 1) *
    (if (l.sample == 'u8') 1 else 4)

// a coverage layer on the same grid as `like`.
pub fn coverage_like(like, sample = 'f32') map^ =>
    layer(like.width, like.height, {format: 'coverage', sample: sample, tile: like.tile})^

pub fn tile_count(l) int^ => len(l.tiles)^
pub fn painted_tiles(l) int^ => len([for (t in l.tiles where t != null) t])

// writes straight-alpha (H, W, 3|4) u8 or f32 samples at `origin`; sRGB decoding follows
// the layer's blend space unless `decode` says otherwise.
pub fn import_pixels(l, pixels, options = {}) map^ {
    let o = checked(options, ["origin", "decode"])^
    let decode = if (o.decode != null) o.decode else if (string(l.blend_space) == "linear") 'srgb' else 'none'
    radiant.paint_import(l, pixels, {origin: if (o.origin == null) ([0, 0]) else o.origin, decode: decode})^
}

// a layer holding an image's pixels, e.g. from `load` or `raster.pixels`.
pub fn from_pixels(pixels, options = {}) map^ {
    let o = checked(options, ["blend_space", "decode"])^
    let s = shape(pixels)
    let base = layer(s[1], s[0], {blend_space: if (o.blend_space == null) 'linear' else o.blend_space})^
    import_pixels(base, pixels, if (o.decode == null) ({}) else ({decode: o.decode}))^
}

// straight-alpha (h, w, 4) u8 samples of `rect`, encoded to sRGB when the layer is linear. An rgba
// layer may lie over a `background`; a pigment layer is opaque and may be lit by its relief
// (`lighting`, media.lighting).
pub fn present(l, options = {}) array^ {
    let o = checked(options, ["rect", "background", "encode", "lighting"])^
    let encode = if (o.encode != null) o.encode else if (string(l.blend_space) == "linear") 'srgb' else 'none'
    radiant.paint_present(l, {rect: if (o.rect == null) ([0, 0, l.width, l.height]) else o.rect,
        encode: encode, background: o.background, lighting: o.lighting})^
}

// the straight-alpha samples of one pixel as [r, g, b, a] in [0, 255].
pub fn pixel(l, x: int, y: int) array^ => present(l, {rect: [x, y, x + 1, y + 1]})^[0, 0]
