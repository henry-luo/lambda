// SVG drawings of a generated world: the map, the ruin's floor plan and the emblem.
import terrain: lambda.procgen.terrain

let CELL = 6.0
let COLORS = terrain.biome_colors()

fn rgb(c) string => "rgb(" ++ string(c[0]) ++ "," ++ string(c[1]) ++ "," ++ string(c[2]) ++ ")"

// one rect per run of equal cells in a row, so wide oceans and plains stay a few elements.
fn runs(row) array => (let starts = [for (x, v in row where x == 0 or v != row[x - 1]) x],
    [for (j, s in starts) [s, (if (j + 1 < len(starts)) starts[j + 1] else len(row)) - s, row[s]]])

fn river_width(flow) float => 0.6 + math.sqrt(float(flow)) * 0.22

// a town's name, left of its dot near the right edge so it stays on the map.
fn label(t, width: float) element {
    let left = t.x * CELL > width * 0.8;
    <text x:t.x * CELL + (if (left) -9.0 else 9.0), y:t.y * CELL + 4.5, 'text-anchor':(if (left) "end" else "start"),
        class:(if (t.capital) "label capital" else "label"), t.name>
}

pub fn world_map(world, show_rivers) element {
    let w = float(len(world.land.biomes[0])) * CELL
    let h = float(len(world.land.biomes)) * CELL
    let columns = len(world.land.biomes[0])
    // joined paths use their largest drainage, retaining flow-dependent widths.
    let flows = map([for (s in world.land.rivers) for (v in [string(int(s[1]) * columns + int(s[0])), s[4]]) v]);
    // element content takes one spread, so the layers join in one array
    <svg class:"map", viewBox:"0 0 " ++ string(w) ++ " " ++ string(h), width:w, height:h, preserveAspectRatio:"xMidYMid meet",
        *[*[for (y, row in world.land.biomes) for (r in runs(row))
            <rect x:float(r[0]) * CELL, y:float(y) * CELL, width:float(r[1]) * CELL + 0.5, height:CELL + 0.5, fill:rgb(COLORS[r[2]])>],
          *[for (cell in world.districts) <polygon class:"district", fill:"none", stroke:"#64593f", opacity:0.45,
            'stroke-width':0.8, 'stroke-dasharray':"4 3", points:join([for (p in cell) string(p[0] * CELL) ++ "," ++ string(p[1] * CELL)], " ")>],
          *(if (show_rivers) [for (line in world.land.river_polylines)
            <polyline points:join([for (p in line) string(p[0] * CELL) ++ "," ++ string(p[1] * CELL)], " "), fill:"none", stroke:rgb(COLORS.river),
                'stroke-width':river_width(max([for (p in line) either(flows[string(int(p[1]) * columns + int(p[0]))], 0)])),
                'stroke-linecap':"round", 'stroke-linejoin':"round">] else []),
          *[for (t in world.towns) <g class:"town",
            <circle cx:t.x * CELL, cy:t.y * CELL, r:(if (t.capital) 6.5 else 4.5), fill:(if (t.capital) "#b91c1c" else "#fff7ed"),
                stroke:"#1f1a17", 'stroke-width':1.6>
            label(t, w)>]]>
}

// the ruin: floor tiles as runs, rooms outlined.
pub fn floor_plan(dungeon) element {
    let s = 5.0
    let w = float(len(dungeon.tiles[0])) * s
    let h = float(len(dungeon.tiles)) * s;
    <svg class:"plan", viewBox:"0 0 " ++ string(w) ++ " " ++ string(h), width:w, height:h,
        *[<rect x:0, y:0, width:w, height:h, fill:"#2a2420">,
          *[for (y, row in dungeon.tiles) for (r in runs(row) where r[2] == 0)
            <rect x:float(r[0]) * s, y:float(y) * s, width:float(r[1]) * s, height:s, fill:"#d6c7a8">],
          *[for (room in dungeon.rooms) <rect x:float(room[0]) * s, y:float(room[1]) * s, width:float(room[2]) * s, height:float(room[3]) * s,
            fill:"none", stroke:"#8a6d3b", 'stroke-width':1>]]>
}

// the emblem's turtle segments, fitted into a square.
pub fn emblem(segments) element {
    let xs = [for (g in segments) for (v in [g[0], g[2]]) v]
    let ys = [for (g in segments) for (v in [g[1], g[3]]) v]
    let x0 = min(xs)
    let y1 = max(ys)
    let span = max(max(xs) - x0, y1 - min(ys))
    let size = 150.0
    let scale = (size - 16.0) / span
    let pad = (size - (max(xs) - x0) * scale) / 2.0;
    <svg class:"emblem", viewBox:"0 0 150 150", width:size, height:size,
        *[<circle cx:75, cy:75, r:72, fill:"#f5ecd7", stroke:"#8a6d3b", 'stroke-width':2>,
          *[for (g in segments) <line x1:pad + (g[0] - x0) * scale, y1:size - 8.0 - (g[1] - min(ys)) * scale,
            x2:pad + (g[2] - x0) * scale, y2:size - 8.0 - (g[3] - min(ys)) * scale,
            stroke:(if (g[4] < 2) "#5b3a1e" else "#3f7d3a"), 'stroke-width':max(0.7, 2.4 - float(g[4]) * 0.45), 'stroke-linecap':"round">]]>
}

// the biomes present on the map, most common first.
pub fn legend(world) array =>
    [for (b in [for (row in world.land.biomes) for (v in row) v] group by b as biome into g order by len(g) desc)
        <li *[<span class:"swatch", style:("background:" ++ rgb(COLORS[biome]))>, biome]>]
