// Places (vibe/Lambda_Demo_Photo_Album.md §3.1.1): photos with GPS, clustered for each integer
// zoom as pure values. lambda.map has no clustering, so every band's clusters go into one GeoJSON
// source; a layer filter on the map's zoom shows one band at a time, and zooming splits them.
import L: .mod_library

// bands 0..BANDS-1; the last band holds every zoom from it up
pub let BANDS = 12
// photos within one cell of this many CSS pixels share a pin (lambda.map: a 512 px world at zoom 0)
let CELL = 56.0

fn photo(lib, id) => L.photo(lib, id)

// Web Mercator world pixels at zoom z
fn world_x(lon, z) => (lon + 180.0) / 360.0 * 512.0 * math.pow(2.0, z)
fn world_y(lat, z) {
    let s = math.sin(max(-85.0, min(85.0, lat)) * math.pi / 180.0);
    (0.5 - math.log((1.0 + s) / (1.0 - s)) / (4.0 * math.pi)) * 512.0 * math.pow(2.0, z)
}
fn cell(p, z) => floor(world_x(p.gps.lon, z) / CELL) * 100000.0 + floor(world_y(p.gps.lat, z) / CELL)

fn mean(xs) => sum(xs) / len(xs)

// one band's clusters: photos sorted by cell (then library order), split where the cell changes
fn band(lib, ids, z) {
    let keyed = [for (i, id in ids order by cell(photo(lib, id), z), i) {id:id, key:cell(photo(lib, id), z)}]
    let starts = [for (i, k in keyed where i == 0 or k.key != keyed[i - 1].key) i];
    [for (j, s in starts) (
        let members = [for (k in slice(keyed, s, if (j + 1 < len(starts)) starts[j + 1] else len(keyed))) k.id],
        {id:z * 100000 + j + 1, band:z, ids:members,
            lon:mean([for (id in members) photo(lib, id).gps.lon]), lat:mean([for (id in members) photo(lib, id).gps.lat])})]
}

// {ids, bounds: [west, south, east, north], clusters, features}: `ids` keeps the library's order
pub fn plan(lib, ids) {
    let located = [for (id in ids where photo(lib, id).gps != null) id]
    let lons = [for (id in located) photo(lib, id).gps.lon]
    let lats = [for (id in located) photo(lib, id).gps.lat]
    let clusters = if (len(located) == 0) [] else [for (z in 0 to BANDS - 1, c in band(lib, located, z)) c];
    {ids:located, bounds:if (len(located) == 0) [-180.0, -60.0, 180.0, 75.0] else [min(lons), min(lats), max(lons), max(lats)],
        clusters:clusters,
        features:{type:"FeatureCollection", features:[for (c in clusters) {type:"Feature", id:c.id,
            properties:{count:len(c.ids), zmin:c.band, zmax:if (c.band == BANDS - 1) 99 else c.band + 1},
            geometry:{type:"Point", coordinates:[c.lon, c.lat]}}]}}
}

pub fn cluster(places, id) => [for (c in places.clusters where c.id == id) c][0]
pub fn band_of(zoom) => max(0, min(BANDS - 1, int(floor(zoom))))
pub fn shown(places, zoom) => [for (c in places.clusters where c.band == band_of(zoom)) c]

// a pin's heading: its photo's title, or how many photos and where
pub fn label(lib, c) => if (len(c.ids) == 1) photo(lib, c.ids[0]).title
    else string(len(c.ids)) ++ " photos · " ++ L.location_label({gps:{lat:round(c.lat * 10.0) / 10.0, lon:round(c.lon * 10.0) / 10.0}})

// the circle layer: the pins of the band the map shows, sized by how many photos they hold
pub let PIN_FILTER = ["all", [">=", ["zoom"], ["get", "zmin"]], ["<", ["zoom"], ["get", "zmax"]]]
pub let PIN_PAINT = {'circle-color':"#0a84ff", 'circle-opacity':0.88, 'circle-stroke-color':"#ffffff", 'circle-stroke-width':2,
    'circle-radius':["interpolate", ["linear"], ["get", "count"], 1, 6, 10, 12, 100, 22, 400, 30]}
