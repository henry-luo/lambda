// Library build, ordering, scopes and search over the committed fixture manifest.
import L: ~~.mod_library

let manifest = input("test/demo/photo-album/fixture/photos.json", 'json')^
let present = [for (i, p in manifest.photos) {photo:i % 6 != 5, thumb:i % 3 != 2}]
let lib = L.build(manifest, present, "file:///corpus/")^
let first = L.photo(lib, lib.order[0]);
{
    count: len(lib.photos),
    downloaded: lib.downloaded,
    newest: [for (id in slice(lib.order, 0, 5)) L.photo(lib, id).taken],
    order_is_desc: all([for (i in 1 to (len(lib.order) - 1)) L.photo(lib, lib.order[i - 1]).taken >= L.photo(lib, lib.order[i]).taken]),
    albums: [for (a in lib.albums) [a.key, a.count, a.cover, a.first_year, a.last_year]],
    thumb_sources: [for (id in [1, 3, 6]) L.photo(lib, id).thumb_src],
    album_scope: L.scope(lib, "album", "cities", [], ""),
    favorites: L.scope(lib, "favorites", "", [3, 10, 17], ""),
    snow: L.scope(lib, "library", "", [], "snow"),
    frozen: L.scope(lib, "library", "", [], "  Frozen "),
    two_words: L.scope(lib, "library", "", [], "fox 2021"),
    none: L.scope(lib, "library", "", [], "zebra"),
    titles: [L.scope_title(lib, "library", "", ""), L.scope_title(lib, "album", "weather", ""),
        L.scope_title(lib, "favorites", "", ""), L.scope_title(lib, "library", "", " snow ")],
    labels: [L.month_label(first), L.date_label(first), L.time_label(first)],
    camera: L.camera_label(first),
    items: slice(L.items(lib, slice(lib.order, 0, 3), false), 0, 3),
    year_items: [for (it in L.items(lib, slice(lib.order, 0, 3), true)) it.label],
    status: [L.status(lib, lib.order, []), L.status(lib, [1], [1, 2])],
    bad_ids: (L.build({*:manifest, photos:[manifest.photos[1]]}, present, "x/") ^ { "rejected" })
}
