// Places: clusters per zoom band over the fixture's located photos and the full manifest's 836.
import L: ~~.mod_library
import P: ~~.mod_places

let fixture = input("test/demo/photo-album/fixture/photos.json", 'json')^
let small = L.build(fixture, [for (p in fixture.photos) {photo:false, thumb:false}], "file:///corpus/")^
let manifest = input("test/demo/photo-album/photos.json", 'json')^
let full = L.build(manifest, [for (p in manifest.photos) {photo:false, thumb:false}], "file:///corpus/")^
let places = P.plan(small, small.order)
let world = P.plan(full, full.order)
fn counts(pl, z) => [for (c in P.shown(pl, z)) len(c.ids)]
// every band partitions the located photos: each photo in exactly one pin
fn partitions(pl) => all([for (z in 0 to P.BANDS - 1) (let ids = [for (c in P.shown(pl, float(z)), id in c.ids) id],
    len(ids) == len(pl.ids) and sort(ids) == sort(pl.ids))])
// photo id -> its pin at a band, and -> its position in the library order
fn pins(pl, z) => map([for (c in P.shown(pl, float(z)), id in c.ids, v in [string(id), c.id]) v])
let position = map([for (i, id in world.ids, v in [string(id), i]) v])
fn nested_at(z) => (let parent = pins(world, z - 1),
    all([for (c in P.shown(world, float(z))) parent[string(c.ids[0])] != null and len(unique([for (id in c.ids) parent[string(id)]])) == 1]))
let outline = input("test/demo/photo-album/world-outline.json", 'json')^;
{
    located: [len(places.ids), len(world.ids)],
    bounds: places.bounds,
    fixture_bands: [for (z in [0, 2, 4, 6, 11]) counts(places, float(z))],
    world_pins: [for (z in [0, 2, 4, 8, 11]) len(P.shown(world, float(z)))],
    partitions: [partitions(places), partitions(world)],
    // zooming in only splits pins: every pin's photos sit inside one pin of the band above it
    nested: all([for (z in 1 to P.BANDS - 1) nested_at(z)]),
    library_order: all([for (c in world.clusters, i in 1 to len(c.ids) - 1 where len(c.ids) > 1) position[string(c.ids[i - 1])] < position[string(c.ids[i])]]),
    features: [len(world.features.features), world.features.features[0].properties],
    biggest: (let c = [for (c in P.shown(world, 0.0) order by len(c.ids) desc) c][0], [len(c.ids), P.label(full, c)]),
    single: P.label(small, [for (c in P.shown(places, 11.0) where len(c.ids) == 1) c][0]),
    band_of: [P.band_of(-1.0), P.band_of(3.7), P.band_of(30.0)],
    outline: [len(outline.features), all([for (f in outline.features) f.geometry.coordinates[0][0] == f.geometry.coordinates[0][len(f.geometry.coordinates[0]) - 1]])]
}
