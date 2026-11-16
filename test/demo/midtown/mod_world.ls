// One world for the car: the city's rules inside the perimeter barrier and the region's beyond it
// (Lambda_Demo_Midtown_Open_Road OR1, OR5), plus the place and road names the HUD shows.
import u: .mod_util
import city: .mod_city
import land: .mod_land
import roads: .mod_roads
import region: .mod_region
import scenery: .mod_scenery

pub fn ground(x, z) any^ => if (city.inside_city(x, z)) city.ground(x, z) else region.ground(x, z)
// the oriented solids near (x, z) beyond the city; the city's own boxes come from city.near_obstacles
pub fn near_solids(x, z, r) any^ => if (abs(x) < city.LIMIT - r and abs(z) < city.LIMIT - r) []
    else [*region.near_solids(x, z, r)^, *scenery.near_solids(x, z, r)^]
// a car below this has gone into the water
pub let SUNK = land.SEA - 0.5
pub fn outside(x, z) => not city.inside_city(x, z)

// ---- names: the loop road's stretches by arc length, then the land around them
fn structure_name(kind) => if (kind == "truss") "Fernbrook Bridge" else if (kind == "cable") "Estuary Bridge" else "Ridge Tunnel"
let TUNNEL_END = [for (st in roads.STRUCTURES where st.kind == "tunnel") st.b][0]
fn region_district(x, z) any^ {
    let r = roads.road_at(x, z)^
    let st = if (r == null or r.r != 0) [] else [for (s in roads.STRUCTURES where r.s >= s.a and r.s <= s.b and abs(r.lat) < 9.0) s];
    if (roads.in_r1(x, z, roads.R1.apron + 25.0)) "Gateway Roundabout"
    else if (len(st) > 0) structure_name(st[0].kind)
    else if (region.terrain(x, z) < land.SEA) "Open Water"
    else if (z > land.coast_z(x) - 140.0 and x > 860.0 and x < 1060.0) "Pelican Beach"
    else if (z > land.coast_z(x) - 160.0 and x > 1540.0) "Lighthouse Point"
    else if (z > 120.0 and x > 560.0) "Cliff Road"
    else if (x < 440.0) "Eastgate"
    else if (x < 1000.0 and z < 160.0) "Hollow Farms"
    else "Ridge Country"
}
pub fn district(x, z) any^ => if (city.inside_city(x, z) or (z > city.LIMIT and abs(x) < city.LIMIT + 1.0)) city.district(x, z) else region_district(x, z)
pub fn street(x, z) any^ {
    if (city.inside_city(x, z)) city.street(x, z)
    else if (roads.in_r1(x, z, roads.R1.apron + 2.0)) "Gateway Roundabout"
    else (let r = roads.road_at(x, z)^,
        if (r == null or abs(r.lat) > 12.0) "Off road"
        else if (r.r == 1) "Union Street"
        else if (r.s <= TUNNEL_END) "Ridge Road" else "Coast Road")
}
