// Album book pagination over 100 photos: no adjacent template repeats, chapters start on right-hand
// pages, every photo printed once and inside its page; leaves, spreads and page-turn math.
import L: ~~.mod_library
import B: ~~.mod_book

let manifest = input("test/demo/photo-album/fixture/photos.json", 'json')^
let lib = L.build(manifest, [for (p in manifest.photos) {photo:true, thumb:true}], "file:///corpus/")^
// 100 photos cycling the 24 fixture photos (fixture years 2019-2023 make five chapters)
let ids = [for (i in 0 to 99) lib.order[i % 24]]
let book = B.plan(lib, ids, "Fixture years")
let pages = book.pages
let photo_pages = [for (p in pages where p.kind == "photos") p]
let prints = [for (p in photo_pages, pr in p.prints) pr]
fn inside(pr) => pr.x >= 0.0 and pr.y >= 0.0 and pr.x + pr.w <= 1.0 and pr.y + pr.h <= 1.0
fn r2(v) => round(v * 100) / 100
let small = B.plan(lib, slice(lib.order, 0, 5), "Five");
{
    pages: len(pages),
    leaves: [len(book.leaves), book.leaves[0], book.leaves[1], book.leaves[last]],
    kinds: [for (p in slice(pages, 0, 12)) p.kind],
    templates: [for (p in slice(photo_pages, 0, 12)) p.template],
    no_adjacent_repeat: all([for (i in 1 to (len(pages) - 1) where pages[i].kind == "photos" and pages[i - 1].kind == "photos")
        pages[i].template != pages[i - 1].template]),
    no_repeat_across_chapters: all([for (i in 1 to (len(photo_pages) - 1)) photo_pages[i].template != photo_pages[i - 1].template]),
    chapters_on_right: [for (i, p in pages where p.kind == "chapter") i % 2 == 0],
    chapter_years: [for (p in pages where p.kind == "chapter") [p.year, p.count]],
    all_printed: len(prints) == 100 and sort([for (pr in prints) pr.id]) == sort(ids),
    within_pages: all([for (pr in prints) inside(pr)]),
    tilt_range: all([for (pr in prints) abs(pr.rot) <= 1.2]),
    even_pages: len(pages) % 2 == 0,
    first_print: (let pr = prints[0], [pr.id, r2(pr.x), r2(pr.y), r2(pr.w), r2(pr.h)]),
    choose: [B.choose([1.5, 1.5], "", 0).key, B.choose([0.7, 0.7], "two-portraits", 1).key,
        B.choose([1.5], "one-landscape", 0).key, B.choose([0.7, 1.5, 1.5, 1.0], "", 2).key],
    fits: [B.fits(B.TEMPLATES[2], [0.7, 0.66]), B.fits(B.TEMPLATES[2], [1.5, 0.7]), B.fits(B.TEMPLATES[7], [1.0, 1.0, 1.0])],
    small: [len(small.pages), [for (p in small.pages) p.kind], len(small.leaves)],
    spreads: [for (t in [0, 1, 2, 3, len(small.leaves)]) [B.spread_label(small, t), B.spread(small, t)]],
    spread_photo: [B.spread_photo(small, 0), B.spread_photo(small, 2)],
    turned_for_page: [B.turned_for_page(0), B.turned_for_page(1), B.turned_for_page(2), B.turned_for_page(5)],
    turn: [B.turn_angle(0.0, -180.0, 0.0), B.turn_angle(0.0, -180.0, 0.25), B.turn_angle(0.0, -180.0, 0.5), B.turn_angle(0.0, -180.0, 1.0), B.turn_angle(0.0, -180.0, 2.0)],
    drag: [B.drag_angle(true, 300.0, 1200.0), B.drag_angle(false, 300.0, 1200.0), B.drag_angle(true, 5000.0, 1200.0)],
    completes: [B.completes(true, -100.0), B.completes(true, -60.0), B.completes(false, -60.0), B.completes(false, -120.0)]
}
