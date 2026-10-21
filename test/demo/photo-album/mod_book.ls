// Classic album book (vibe/Lambda_Demo_Photo_Album.md §3.5): pagination and leaves as pure values.
// Page coordinates are fractions of a page; a page is PAGE_W x PAGE_H logical units.
import L: .mod_library

pub let PAGE_W = 1000.0
pub let PAGE_H = 1300.0
pub let MAX_PHOTOS = 240
pub let COVERS = [{key:"navy", title:"Navy cloth", css:"#1f3557"}, {key:"burgundy", title:"Burgundy cloth", css:"#6b2232"},
    {key:"forest", title:"Forest cloth", css:"#27493a"}, {key:"leather", title:"Brown leather", css:"#5a3a22"}]
pub let PAPERS = [{key:"cream", title:"Cream paper", css:"#f3ead7", ink:"#4a4036"}, {key:"black", title:"Black paper", css:"#1d1c1a", ink:"#e8e2d6"}]

fn box(x, y, w, h) => {x:x, y:y, w:w, h:h}
// a template is a list of print slots; `fits` names the orientation each slot wants ("L", "P" or "*")
pub let TEMPLATES = [
    {key:"one-landscape", fits:["L"], slots:[box(0.1, 0.2, 0.8, 0.52)]},
    {key:"one-portrait", fits:["P"], slots:[box(0.16, 0.08, 0.68, 0.76)]},
    {key:"two-portraits", fits:["P", "P"], slots:[box(0.05, 0.16, 0.43, 0.6), box(0.52, 0.16, 0.43, 0.6)]},
    {key:"two-landscapes", fits:["L", "L"], slots:[box(0.1, 0.05, 0.8, 0.42), box(0.1, 0.51, 0.8, 0.42)]},
    {key:"landscape-portrait", fits:["L", "P"], slots:[box(0.05, 0.1, 0.56, 0.44), box(0.6, 0.42, 0.35, 0.5)]},
    {key:"landscape-two-small", fits:["L", "*", "*"], slots:[box(0.08, 0.05, 0.84, 0.47), box(0.05, 0.56, 0.43, 0.37), box(0.52, 0.56, 0.43, 0.37)]},
    {key:"portrait-two-small", fits:["P", "*", "*"], slots:[box(0.05, 0.07, 0.5, 0.8), box(0.6, 0.08, 0.35, 0.38), box(0.6, 0.5, 0.35, 0.38)]},
    {key:"four", fits:["*", "*", "*", "*"], slots:[box(0.05, 0.05, 0.43, 0.43), box(0.52, 0.05, 0.43, 0.43), box(0.05, 0.52, 0.43, 0.43), box(0.52, 0.52, 0.43, 0.43)]}]
// preference order; it rotates page by page so the book varies
let PREFERENCE = ["landscape-two-small", "two-portraits", "one-landscape", "four", "two-landscapes", "portrait-two-small",
    "landscape-portrait", "one-portrait"]

fn template(key) => [for (t in TEMPLATES where t.key == key) t][0]
fn orientation(aspect) => if (aspect >= 1.05) "L" else if (aspect <= 0.95) "P" else "S"
fn fits_slot(want, o) => want == "*" or o == "S" or want == o

pub fn fits(t, aspects) => len(aspects) >= len(t.fits) and
    all([for (i, want in t.fits) fits_slot(want, orientation(aspects[i]))])

fn rank(key, n) {
    let at = index_of(PREFERENCE, key) or 0;
    (at - n * 3 + 8 * 100) % 8
}

// the template for the next page: fits the coming photos, differs from the previous page's, and
// follows the rotated preference; a lone photo may use either single-print template
pub fn choose(aspects, prev, n) {
    let fitting = [for (t in TEMPLATES where t.key != prev and fits(t, aspects)) t]
    let singles = [for (t in TEMPLATES where t.key != prev and len(t.fits) == 1) t]
    let pool = if (len(fitting) > 0) fitting else singles;
    [for (t in pool order by rank(t.key, n)) t][0]
}

// deterministic tilt in degrees, within ±1.2, so prints look placed by hand
fn tilt(id, k) => ((id * 7919 + k * 104729) % 241) / 100.0 - 1.2

// a print sits inside its slot at its own aspect, with room for a caption beneath
pub let CAPTION = 0.05
fn place(slot, aspect, id, k) {
    let room_h = slot.h - CAPTION
    let w = min(slot.w, room_h * aspect * PAGE_H / PAGE_W)
    let h = w / aspect * PAGE_W / PAGE_H;
    {id:id, x:slot.x + (slot.w - w) / 2.0, y:slot.y + (room_h - h) / 2.0, w:w, h:h, rot:tilt(id, k)}
}

// photo pages of one chapter, as [{kind:"photos", template, prints}], plus the last template used
fn paginate(lib, ids, i, prev, n) {
    if (i >= len(ids)) []
    else (
        let aspects = [for (id in slice(ids, i, min(len(ids), i + 4))) L.photo(lib, id).aspect],
        let t = choose(aspects, prev, n),
        let k = min(len(t.slots), len(ids) - i),
        let prints = [for (j in 0 to (k - 1)) place(t.slots[j], aspects[j], ids[i + j], n)],
        [{kind:"photos", template:t.key, prints:prints}, *paginate(lib, ids, i + k, t.key, n + 1)])
}

fn chapters(lib, ids) {
    let starts = [for (i, id in ids where i == 0 or L.photo(lib, ids[i - 1]).year != L.photo(lib, id).year) i];
    [for (j, s in starts) (
        let end = if (j + 1 < len(starts)) starts[j + 1] else len(ids),
        {year:L.photo(lib, ids[s]).year, ids:slice(ids, s, end)})]
}

fn last_template(pages) => [for (p in pages where p.kind == "photos") p.template][last] or ""

// Pages in reading order. Even indexes are right-hand pages; every chapter's heading page lands
// on one, padded with a blank left page when needed. The page count is even so leaves pair up.
fn assemble(lib, chs, k, pages) {
    if (k >= len(chs)) pages
    else (
        let ch = chs[k],
        let padded = if (len(pages) % 2 == 1) [*pages, {kind:"blank"}] else pages,
        let heading = {kind:"chapter", year:ch.year, count:len(ch.ids),
            places:slice([for (i, id in ch.ids where i == 0 or L.album_name(lib, L.photo(lib, id)) != L.album_name(lib, L.photo(lib, ch.ids[i - 1])))
                L.album_name(lib, L.photo(lib, id))], 0, 4)},
        assemble(lib, chs, k + 1, [*padded, heading, *paginate(lib, ch.ids, 0, last_template(pages), len(padded) + 1)]))
}

fn years(lib, ids) {
    let ys = [for (id in ids) L.photo(lib, id).year];
    if (min(ys) == max(ys)) string(min(ys)) else string(min(ys)) ++ "–" ++ string(max(ys))
}

// Book plan: pages, and leaves pairing them. Leaf 0 is the cover (inside: endpaper); leaf k holds
// pages 2k-2 (front, a right page) and 2k-1 (back, the next left page); the last leaf is the back cover.
pub fn plan(lib, ids, title) {
    let ordered = [for (id in slice(ids, 0, MAX_PHOTOS) order by L.photo(lib, id).taken, id) id]
    let opening = [{kind:"title", title:title, years:years(lib, ordered), count:len(ordered)}]
    let body = assemble(lib, chapters(lib, ordered), 0, opening)
    let pages = if (len(body) % 2 == 1) [*body, {kind:"end"}] else body
    let leaves = [{front:"cover", back:"endpaper"}, *[for (k in 0 to (len(pages) / 2 - 1)) {front:2 * k, back:2 * k + 1}],
        {front:"endpaper", back:"back-cover"}];
    {title:title, ids:ordered, pages:pages, leaves:leaves}
}

// the spread after `turned` leaves: the left shows the back of leaf turned-1, the right the front of leaf turned
pub fn spread(book, turned) => {left:if (turned > 0) book.leaves[turned - 1].back else null,
    right:if (turned < len(book.leaves)) book.leaves[turned].front else null}

pub fn spread_label(book, turned) {
    let s = spread(book, turned);
    if (turned == 0) "Cover" else if (turned == len(book.leaves)) "Back"
    else if (s.left is int and s.right is int) string(s.left + 1) ++ "–" ++ string(s.right + 1)
    else if (s.right is int) string(s.right + 1) else if (s.left is int) string(s.left + 1) else "Endpapers"
}

// the first photo on a spread, for its thumbnail in the spread bar
pub fn spread_photo(book, turned) {
    let s = spread(book, turned)
    let found = [for (side in [s.left, s.right], pr in (if (side is int) book.pages[side].prints or [] else [])) pr.id];
    found[0] or 0
}

// the turned-leaf count that shows a page
pub fn turned_for_page(page) => int(floor((page + 1) / 2)) + 1

// the eased angle of a turn at progress p: 0 is flat on the right, -180 flat on the left
pub fn ease(p) => if (p < 0.5) 2.0 * p * p else 1.0 - 2.0 * (1.0 - p) * (1.0 - p)
pub fn turn_angle(from, to, p) => from + (to - from) * ease(max(0.0, min(1.0, p)))

// a drag: pointer distance from the grabbed edge over the spread width sets the angle
pub fn drag_angle(forward, travelled, spread_w) {
    let f = max(0.0, min(1.0, travelled / spread_w));
    if (forward) -180.0 * f else -180.0 * (1.0 - f)
}
// release completes the turn past the vertical, otherwise it falls back
pub fn completes(forward, angle) => if (forward) angle < -90.0 else angle > -90.0
