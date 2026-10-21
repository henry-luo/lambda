// S1.4 / S12.1.3: the library is one immutable value; scopes and search are pure views of it.

pub let MONTHS = ["January","February","March","April","May","June","July","August","September","October","November","December"]

// manifest dates are validated ISO strings; a malformed field reads as 0 rather than failing the page
fn field(p, from, to) => int(slice(p.taken, from, to)) ^ { 0 }
fn year_of(p) => field(p, 0, 4)
fn month_of(p) => max(1, field(p, 5, 7))
fn day_of(p) => field(p, 8, 10)
pub fn month_label(p) => MONTHS[month_of(p) - 1] ++ " " ++ slice(p.taken, 0, 4)
pub fn date_label(p) => string(day_of(p)) ++ " " ++ month_label(p)
pub fn time_label(p) => slice(p.taken, 11, 16)

fn album_title(albums, key) => [for (a in albums where a.key == key) a.title][0] or key

// lower-case text every search token is matched against
fn haystack(p, albums) => lower(join([p.title, p.description, join(p.keywords or [], " "), album_title(albums, p.album),
    p.camera.make or "", p.camera.model or "", slice(p.taken, 0, 4), MONTHS[month_of(p) - 1]], " "))

// manifest: photos.json; present: per manifest entry {photo:bool, thumb:bool}; base: corpus URL ending in "/"
pub fn build(manifest, present, base) map^ {
    let entries = manifest.photos
    let bad = [for (i, p in entries where p.id != i + 1) p.id]
    if (len(bad) > 0) raise error("Photo album: manifest ids must run 1..N in order")
    else (
        let albums = manifest.albums,
        let photos = [for (i, p in entries) {*:p, year:year_of(p), aspect:p.width / p.height,
            has_photo:present[i].photo, has_thumb:present[i].thumb,
            photo_src:base ++ "photos/" ++ p.file,
            thumb_src:if (present[i].thumb) base ++ "thumbs/" ++ p.file else if (present[i].photo) base ++ "photos/" ++ p.file else "",
            text:haystack(p, albums)}],
        // newest first, ties by id so the order is total
        let order = [for (p in photos order by p.taken desc, p.id) p.id],
        let downloaded = len([for (p in photos where p.has_photo) p]),
        {photos:photos, order:order, downloaded:downloaded,
            albums:[for (a in albums) (
                let ids = [for (id in order where photos[id - 1].album == a.key) id],
                {key:a.key, title:a.title, count:len(ids), cover:ids[0] or 0,
                    first_year:if (len(ids) > 0) photos[ids[last] - 1].year else 0,
                    last_year:if (len(ids) > 0) photos[ids[0] - 1].year else 0})]}
    )
}

pub fn photo(lib, id) => lib.photos[id - 1]

pub fn tokens(query) => [for (t in split(lower(trim(query or "")), " ") where t != "") t]

pub fn matches(p, words) => all([for (w in words) contains(p.text, w)])

// mode: "library" | "album" | "favorites"; a non-empty query searches the whole library
pub fn scope(lib, mode, album, favorites, query) {
    let words = tokens(query);
    if (len(words) > 0) [for (id in lib.order where matches(photo(lib, id), words)) id]
    else if (mode == "album") [for (id in lib.order where photo(lib, id).album == album) id]
    else if (mode == "favorites") [for (id in lib.order where contains(favorites, id)) id]
    else lib.order
}

pub fn scope_title(lib, mode, album, query) {
    let words = tokens(query);
    if (len(words) > 0) "Search: “" ++ trim(query) ++ "”"
    else if (mode == "album") [for (a in lib.albums where a.key == album) a.title][0] or "Album"
    else if (mode == "favorites") "Favorites" else if (mode == "albums") "Albums" else "Library"
}

// month sections only when months hold enough photos to fill rows; sparse scopes group by year
pub let DENSE_MONTH = 8
pub fn by_year(lib, ids) {
    let months = len([for (i, id in ids where i == 0 or slice(photo(lib, ids[i - 1]).taken, 0, 7) != slice(photo(lib, id).taken, 0, 7)) id]);
    months == 0 or len(ids) / months < DENSE_MONTH
}

// grid items carry the date section they belong to
pub fn items(lib, ids, by_year) => [for (id in ids) (
    let p = photo(lib, id),
    {id:id, aspect:p.aspect, key:if (by_year) slice(p.taken, 0, 4) else slice(p.taken, 0, 7),
        label:if (by_year) slice(p.taken, 0, 4) else month_label(p)})]

// "45.98° N, 7.76° E", or "" without coordinates
pub fn location_label(p) => if (p.gps == null) "" else
    string(abs(p.gps.lat)) ++ "° " ++ (if (p.gps.lat >= 0) "N" else "S") ++ ", " ++
    string(abs(p.gps.lon)) ++ "° " ++ (if (p.gps.lon >= 0) "E" else "W")

pub fn album_name(lib, p) => [for (a in lib.albums where a.key == p.album) a.title][0] or p.album

pub fn camera_label(p) {
    let c = p.camera
    let parts = [for (part in [if (c.focal_mm != null) string(c.focal_mm) ++ " mm" else null,
        if (c.aperture != null) "ƒ/" ++ string(c.aperture) else null,
        if (c.exposure != "") c.exposure ++ " s" else null,
        if (c.iso != null) "ISO " ++ string(c.iso) else null] where part != null) part];
    {body:trim((if (starts_with(c.model or "", c.make or "")) c.model else (c.make or "") ++ " " ++ (c.model or ""))),
        lens:c.lens or "", exposure:join(parts, " · ")}
}

pub fn status(lib, ids, selected) {
    let total = len(ids);
    string(total) ++ (if (total == 1) " photo" else " photos") ++
        (if (len(selected) > 0) " · " ++ string(len(selected)) ++ " selected" else "")
}

// file-name stem for a title: its lower-case letter and digit runs joined by "-", at most 48 characters
type SlugWord = \(("a" to "z" | "0" to "9")+)
pub fn slug(title, fallback) {
    let stem = slice(join([for (m in find(lower(title or ""), SlugWord)) m.value], "-"), 0, 48);
    if (stem == "") fallback else stem
}
