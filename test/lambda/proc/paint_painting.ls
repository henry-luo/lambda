// lambda.paint painting files (Lambda_Pkg_Paint.md PT12): a .painting ZIP stores the layer's tiles,
// a preview and a thumbnail; opening it restores the exact pixels without replaying strokes.
import paint: lambda.paint

// one stroke fed in place in one batch (PT7)
pn stroked(s, b, color, samples) map^ {
    var t = paint.begin(s, 1, b, color)^
    paint.feed(t, 1, samples)^
    return paint.end(t, 1)^
}

pn painted() map^ {
    let pen = paint.brush({size: 14.0, hardness: 0.9})^
    let s0 = paint.session(300, 140, {paper: [0.96, 0.94, 0.88]})^
    let a = stroked(s0, pen, [0.8, 0.1, 0.1], [[20.0, 30.0, 1.0], [280.0, 40.0, 0.6]])^
    return stroked(a, pen, [0.1, 0.2, 0.7], [[150.0, 10.0, 0.4], [160.0, 130.0, 1.0]])^
}

pn main() {
    let root = "temp/paint_painting-" ++ string(sys.proc.self.pid#)
    io.mkdir(root)^
    let s = painted()^
    let path = root ++ "/study.painting"
    let written = paint.save_painting(s, path)^
    let z = input(path, 'zip')^
    let names = [for (p in content(z)^) p.name]
    let layer_names = [for (p in content(content(z)^[2])^) p.name]
    let doc = input(content(z)^[1], 'mark')^
    let thumb = input(content(z)^[4], 'binary')^
    let back = paint.open_painting(path)^
    let png_path = root ++ "/study.png"
    let png_bytes = paint.export_png(back, png_path)^
    let decoded = load(png_path)^
    // an opened painting presents its whole canvas on the first frame
    var shown = back
    let first_frame = paint.frame(shown)^
    // a paper texture built from a spec is stored as that spec and rebuilt on open
    let textured = paint.with_texture(s, {kind: 'cold_press', seed: 5, size: 32})^
    let tpath = root ++ "/textured.painting"
    paint.save_painting(textured, tpath)^
    let reopened = paint.open_painting(tpath)^
    let bad = root ++ "/not.painting"
    output("hello", bad, 'text')^
    let rejected = (paint.open_painting(bad) ^ { "rejected" }) == "rejected"
    let missing = (paint.open_painting(root ++ "/none.painting") ^ { "missing" }) == "missing"
    io.delete(root)^
    return [written > 0,
        names == ["mimetype", "painting.mark", "layers", "preview.png", "thumbnail.png"],
        layer_names == ["paint.tiles"],
        name(doc) == 'painting' and doc.width == 300 and doc.height == 140 and doc.version == 1,
        len(thumb) > 8 and slice(thumb, 1, 4) == binary("PNG"),
        back.layer == s.layer and back.paper == s.paper and not paint.can_undo(back),
        paint.painted_tiles(back.layer) == paint.painted_tiles(s.layer) and paint.painted_tiles(s.layer) < 15,
        paint.color_at(back, 100, 33)^ == paint.color_at(s, 100, 33)^,
        first_frame.rect == [0, 0, 300, 140],
        png_bytes > 0 and shape(decoded) == [140, 300, 4] and decoded[33][100] == paint.flattened(s)^[33][100],
        rejected, missing, doc[3].width == 256 and doc[3].height == 119,
        reopened.texture_spec == textured.texture_spec and reopened.texture == textured.texture and back.texture == null]
}
