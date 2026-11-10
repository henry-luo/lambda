// lambda.paint painting files (Lambda_Pkg_Paint.md PT12): a .painting ZIP stores the layer's tiles,
// a preview and a thumbnail; opening it restores the exact pixels without replaying strokes.
import paint: lambda.paint

pn painted() map^ {
    let pen = paint.brush({size: 14.0, hardness: 0.9})^
    let s0 = paint.session(300, 140, {paper: [0.96, 0.94, 0.88]})^
    let a = paint.end(paint.feed(paint.begin(s0, 1, pen, [0.8, 0.1, 0.1])^, 1, [[20.0, 30.0, 1.0], [280.0, 40.0, 0.6]])^, 1)^
    return paint.end(paint.feed(paint.begin(a, 1, pen, [0.1, 0.2, 0.7])^, 1, [[150.0, 10.0, 0.4], [160.0, 130.0, 1.0]])^, 1)^
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
        paint.frame(back)^.rect == [0, 0, 300, 140],
        png_bytes > 0 and shape(decoded) == [140, 300, 4] and decoded[33][100] == paint.flattened(s)^[33][100],
        rejected, missing]
}
