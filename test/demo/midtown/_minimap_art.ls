// Render the city and region together into the static minimap asset. See the README for the command.
import u: .mod_util
import city: .mod_city
import land: .mod_land
import roads: .mod_roads
import minimap: .mod_minimap

let PLAN_HALF = city.SHORE + 30.0
fn kind_fill(kind) => if (kind == "P") "#4d7a45" else if (kind == "X") "#8a7967" else if (kind == "S") "#4a4c52"
    else if (kind == "C") "#8c7e63" else "#c8c3b8"
fn plan() {
    let l = city.LIMIT, w = PLAN_HALF
    let blocks = [for (b in city.BLOCKS) <rect x: b.x0 + city.ROAD, y: b.z0 + city.ROAD, width: city.PITCH - 2.0 * city.ROAD,
        height: city.PITCH - 2.0 * city.ROAD, rx: 2, fill: kind_fill(b.kind)>]
    let towers = [for (b in city.BUILDINGS) <rect x: b.x0, y: b.z0, width: b.x1 - b.x0, height: b.z1 - b.z0,
        fill: if (b.kind == "T") "#6f7f92" else "#9a958b">];
    [
        <rect x: 0.0 - w, y: 0.0 - w, width: 2.0 * w, height: 2.0 * w, fill: "#34502f">,
        <rect x: 0.0 - w, y: city.SHORE, width: 2.0 * w, height: w - city.SHORE + w, fill: "#2e5d78">,
        <rect x: 0.0 - l, y: 0.0 - l, width: 2.0 * l, height: 2.0 * l, fill: "#5a5f66">, *blocks, *towers]
}
// the land beyond the city at the same scale: sea, river, the loop road (darker through the tunnel)
// and the roundabout; it sits under the city plan, as an image of an SVG
fn pt(x, z) any^ => string(round(x)) ++ " " ++ string(round(z))
fn outline(points) any^ => join([for (k, p in points) (if (k == 0) "M " else "L ") ++ pt(p[0], p[1])], " ") ++ " Z"
// a band `half` wide either side of a run of road samples (every third), as one closed outline
fn band(pts, half) any^ => (let picked = [for (k, p in pts where k % 3 == 0 or k == len(pts) - 1) p],
    outline([*[for (p in picked, let r = u.right(p.heading)) [p.x + r[0] * half, p.z + r[1] * half]],
        *[for (p in reverse(picked), let r = u.right(p.heading)) [p.x - r[0] * half, p.z - r[1] * half]]]))
fn river_band() any^ {
    let pts = land.RIVER
    let n = len(pts)
    let side = (k, sgn) any^ => (let a = pts[max(0, k - 1)], let b = pts[min(n - 1, k + 1)], let dx = b[0] - a[0], let dz = b[1] - a[1],
        let l = sqrt(dx * dx + dz * dz), let w = land.river_half(pts[k][1]) * sgn, [pts[k][0] - dz / l * w, pts[k][1] + dx / l * w]);
    outline([*[for (k in 0 to (n - 1)) side(k, 1.0)], *[for (k in 0 to (n - 1)) side(n - 1 - k, -1.0)]])
}
fn map_svg() element^ {
    let r = minimap.REGION
    let samples = int(ceil((r.x1 - r.x0) / 40.0))^
    let coast = [for (k in 0 to samples, let x = min(r.x1, r.x0 + k * 40.0)) [x, land.coast_z(x)]]
    let loop = roads.LOOP.samples^
    let bores = [for (st in roads.STRUCTURES where st.kind == "tunnel") [for (p in loop where p.s >= st.a and p.s <= st.b) p]];
    <svg xmlns: "http://www.w3.org/2000/svg", width: r.x1 - r.x0, height: r.z1 - r.z0,
        viewBox: string(r.x0) ++ " " ++ string(r.z0) ++ " " ++ string(r.x1 - r.x0) ++ " " ++ string(r.z1 - r.z0), *[
        <rect x: r.x0, y: r.z0, width: r.x1 - r.x0, height: r.z1 - r.z0, fill: "#3c5a33">,
        <path d: outline([*coast, [r.x1, r.z1], [r.x0, r.z1]]), fill: "#2e5d78">, <path d: river_band(), fill: "#2e5d78">,
        <path d: band(loop, 6.5), fill: "#2b2f35">, <path d: band(loop, 4.0), fill: "#d6d0c2">,
        *[for (b in bores) <path d: band(b, 4.0), fill: "#5d6168">],
        *plan(),
        // draw the gateway over the city's surrounding ground so it joins the street.
        <rect x: city.LIMIT, y: city.road(city.GATE_J) - city.ROAD, width: roads.R1.x - city.LIMIT, height: 2.0 * city.ROAD, fill: "#d6d0c2">,
        <circle cx: roads.R1.x, cy: roads.R1.z, r: roads.R1.ring + 5.0, fill: "#d6d0c2">,
        <circle cx: roads.R1.x, cy: roads.R1.z, r: roads.R1.island, fill: "#4d7a45">]>
}

<html <head <style "html,body{margin:0}svg{display:block}">> <body map_svg()^>>
