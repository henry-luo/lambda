// A collage exported through Radiant matches its layout: gutters show the background and every
// cell shows its own photo (G3 export path, vibe/Lambda_Demo_Photo_Album.md §3.3).
import L: ~~.~~.mod_library
import C: ~~.~~.mod_collage
import CA: ~~.~~.mod_collage_app

// pixels are ubyte: subtract as ints, since ubyte arithmetic wraps (226 - 227 is 255)
fn near(a, b, tolerance) => all([for (k in 0 to 2) abs(int(a[k]) - int(b[k])) <= tolerance])

pn main() {
    let dir = "test/demo/photo-album/fixture/"
    let manifest = input(dir ++ "photos.json", 'json')^
    let base = url_resolve("file://" ++ sys.proc.self.cwd# ++ "/", dir)
    let lib = L.build(manifest, [for (p in manifest.photos) {photo:true, thumb:true}], base)^
    let ctx = {lib:lib, dir:dir, dir_url:base}
    // four landscape-ish fixture photos in a 2 x 2 grid on charcoal, no bands
    let ids = [5, 10, 15, 20]
    let model = {*:C.create(ids, ""), template:"grid", gutter:40, radius:0, background:"charcoal", credits:false}
    if (not exists("temp/photo-album")) { io.mkdir("temp/photo-album")^ }
    let done = CA.render_file(ctx, model, 600.0, "png", "temp/photo-album/collage-export")^
    let image = load(done.path)^
    let lay = C.layout(model, C.slot_aspects(lib, model))
    let s = done.scale
    let charcoal = [31, 33, 36]
    // a gutter point between the first two cells, and the outer margin
    let gap = image[int(lay.cells[0].y * s + 30.0)][int((lay.cells[0].x + lay.cells[0].w + 20.0) * s)]
    let margin = image[4][4]
    // each cell centre against the same photo decoded and sampled at its own centre (object-fit: cover, centred)
    let centres = [for (c in lay.cells) (
        let photo = load(dir ++ "photos/" ++ L.photo(lib, model.slots[c.slot].id).file)^,
        let dims = shape(photo),
        near(image[int((c.y + c.h / 2.0) * s)][int((c.x + c.w / 2.0) * s)], photo[int(dims[0] / 2)][int(dims[1] / 2)], 24))]
    let credits = input("temp/photo-album/collage-export-credits.txt", 'text')^
    let jpeg = CA.render_file(ctx, {*:model, aspect:"landscape"}, 300.0, "jpeg", "temp/photo-album/collage-export-small")^
    return [shape(image), near(gap, charcoal, 2), near(margin, charcoal, 2), centres,
        len(split(trim(credits), "\n")) == 6, ends_with(jpeg.path, ".jpg"), [jpeg.width, jpeg.height]]
}
