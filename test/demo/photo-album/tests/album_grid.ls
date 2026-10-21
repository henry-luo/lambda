// Justified layout, windowing, hit testing, zoom anchoring and arrow navigation.
import G: ~~.mod_grid

// two sections: three photos in March, five in February
let items = [
    {id:1, aspect:1.5, key:"2023-03", label:"March 2023"}, {id:2, aspect:0.75, key:"2023-03", label:"March 2023"},
    {id:3, aspect:1.0, key:"2023-03", label:"March 2023"}, {id:4, aspect:1.5, key:"2023-02", label:"February 2023"},
    {id:5, aspect:1.5, key:"2023-02", label:"February 2023"}, {id:6, aspect:3.0, key:"2023-02", label:"February 2023"},
    {id:7, aspect:0.75, key:"2023-02", label:"February 2023"}, {id:8, aspect:1.0, key:"2023-02", label:"February 2023"}]
let lay = G.layout(items, 600.0, 200.0, 4.0)
fn r1(v) => round(v * 10) / 10
fn summary(l) => [for (r in l.rows) {y:r1(r.y), h:r1(r.h), tiles:[for (t in r.tiles) [t.id, r1(t.x), r1(t.w)]]}]
// every full row spans the width exactly
fn widths(l) => [for (r in l.rows) r1(r.tiles[last].x + r.tiles[last].w)]
let wide = G.layout(items, 900.0, 200.0, 4.0)
let small = G.layout(items, 600.0, 80.0, 4.0)
let p5 = G.locate(lay, 5)
let p5_small = G.locate(small, 5);
{
    sections: G.sections(items),
    height: r1(lay.height),
    headers: [for (h in lay.headers) [r1(h.y), h.label, h.count]],
    rows: summary(lay),
    row_widths: widths(lay),
    wide_rows: [for (r in wide.rows) [for (t in r.tiles) t.id]],
    small_rows: [for (r in small.rows) [for (t in r.tiles) t.id]],
    window_top: G.window(lay, 0.0, 250.0, 0.0),
    window_mid: G.window(lay, 500.0, 200.0, 0.0),
    window_margin: G.window(lay, 500.0, 200.0, 400.0),
    headers_mid: G.headers_in(lay, 300.0, 300.0, 0.0),
    current: [G.current_header(lay, 0.0).label, G.current_header(lay, 400.0).label],
    tile_at: [G.tile_at(lay, 10.0, 60.0), G.tile_at(lay, 599.0, 60.0), G.tile_at(lay, 10.0, 10.0)],
    band: G.tiles_in(lay, 0.0, 0.0, 600.0, 200.0),
    located: {x:r1(p5.x), y:r1(p5.y), w:r1(p5.w), h:r1(p5.h)},
    // pointer 30 px into tile 5 when zooming from 200 to 80 px rows
    anchored: r1(G.anchored_top(p5, p5_small, 120.0, p5.y + 30.0 - 120.0)),
    anchor_check: r1(p5_small.y + (30.0 / p5.h) * p5_small.h - 120.0),
    neighbours: [G.neighbour(lay, [1,2,3,4,5,6,7,8], 1, "ArrowRight"), G.neighbour(lay, [1,2,3,4,5,6,7,8], 1, "ArrowLeft"),
        G.neighbour(lay, [1,2,3,4,5,6,7,8], 2, "ArrowDown"), G.neighbour(lay, [1,2,3,4,5,6,7,8], 4, "ArrowUp"),
        G.neighbour(lay, [1,2,3,4,5,6,7,8], 1, "ArrowUp")],
    zoom: [G.zoom_step(160, 1), G.zoom_step(160, -1), G.zoom_step(300, 1), G.zoom_step(80, -1), G.zoom_step(130, 0)],
    empty: G.layout([], 600.0, 200.0, 4.0)
}
