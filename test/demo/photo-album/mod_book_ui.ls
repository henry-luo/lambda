// Album book markup (S12.1.3). Pages are drawn at scale `s` (CSS px per logical unit) with inline
// styles; a leaf is a two-faced element turned about the spine in a preserve-3d context.
import L: .mod_library
import B: .mod_book

fn px(v) => string(round(v * 100) / 100) ++ "px"
fn find(items, key) => [for (it in items where it.key == key) it][0] or items[0]
let HAND = "'Bradley Hand','Segoe Print','Comic Sans MS',cursive"

fn caption(lib, p) {
    let place = L.album_name(lib, p);
    L.date_label(p) ++ (if (place != "") " · " ++ place else "")
}

// a mounted print: white border, four photo corners and a handwritten caption beneath
fn print_card(lib, pr, s, paper) {
    let p = L.photo(lib, pr.id)
    let W = B.PAGE_W * s
    let H = B.PAGE_H * s
    let border = max(2.0, 14.0 * s)
    let corner = max(4.0, 34.0 * s)
    fn mount(side) => <span class:"bk-corner bk-" ++ side, style:"width:" ++ px(corner) ++ ";height:" ++ px(corner) ++ ";">;
    <div class:"bk-print", 'data-photo':string(pr.id), title:p.title,
        style:"left:" ++ px(pr.x * W) ++ ";top:" ++ px(pr.y * H) ++ ";width:" ++ px(pr.w * W) ++ ";height:" ++ px(pr.h * H) ++
            ";transform:rotate(" ++ string(pr.rot) ++ "deg);",
        <div class:"bk-photo", style:"border:" ++ px(border) ++ " solid #fdfbf6;background:" ++ (p.color or "#ccc") ++ ";",
            (if (p.thumb_src != "") <img src:if (p.has_photo) p.photo_src else p.thumb_src, alt:"", draggable:"false"> else null)>;
        (mount("tl")); (mount("tr")); (mount("bl")); (mount("br"));
        <div class:"bk-caption", style:"top:100%;font-family:" ++ HAND ++ ";color:" ++ paper.ink ++ ";font-size:" ++ px(max(8.0, 30.0 * s)) ++ ";",
            caption(lib, p)>>
}

fn page_body(lib, book, ref, s, options) {
    let paper = find(B.PAPERS, options.paper)
    let cover = find(B.COVERS, options.cover);
    if (ref == "cover" or ref == "back-cover") <div class:"bk-cover", style:"background-color:" ++ cover.css ++ ";",
        (if (ref == "cover") <div class:"bk-emboss", style:"font-size:" ++ px(max(14.0, 78.0 * s)) ++ ";", book.title> else null);
        (if (ref == "cover") <div class:"bk-emboss bk-small", style:"font-size:" ++ px(max(9.0, 32.0 * s)) ++ ";",
            [for (p in book.pages where p.kind == "title") p.years][0]> else null)>
    else if (ref == "endpaper") <div class:"bk-endpaper">
    else (
        let page = book.pages[ref],
        let ink = "color:" ++ paper.ink ++ ";",
        <div class:"bk-paper", 'data-page':string(ref), style:"background-color:" ++ paper.css ++ ";",
            *(if (page.kind == "title") [<div class:"bk-title", style:ink ++ "font-size:" ++ px(max(14.0, 72.0 * s)) ++ ";", page.title>,
                <div class:"bk-subtitle", style:ink ++ "font-family:" ++ HAND ++ ";font-size:" ++ px(max(10.0, 40.0 * s)) ++ ";",
                    page.years ++ " · " ++ string(page.count) ++ " photos">]
            else if (page.kind == "chapter") [<div class:"bk-chapter", style:ink ++ "font-size:" ++ px(max(18.0, 160.0 * s)) ++ ";", string(page.year)>,
                <div class:"bk-subtitle", style:ink ++ "font-family:" ++ HAND ++ ";font-size:" ++ px(max(10.0, 36.0 * s)) ++ ";",
                    join(page.places, " · ") ++ " — " ++ string(page.count) ++ " photos">]
            else if (page.kind == "photos") [for (pr in page.prints) print_card(lib, pr, s, paper)]
            else if (page.kind == "end") [<div class:"bk-subtitle", style:ink ++ "font-family:" ++ HAND ++ ";font-size:" ++ px(max(10.0, 40.0 * s)) ++ ";", "The end">]
            else []);
            (if (page.kind != "blank") <div class:"bk-folio", style:ink ++ "font-size:" ++ px(max(8.0, 22.0 * s)) ++ ";", string(ref + 1)> else null)>)
}

// Leaf i: the front face shows its right-hand page, the back face (pre-rotated 180°) its next
// left-hand page. Each face carries a shade the turn darkens toward the vertical.
pub fn leaf(lib, book, i, s, options) => <div class:"bk-leaf", 'data-leaf':string(i), id:"bk-leaf-" ++ string(i),
    // a leaf always has a transform, so turning it replaces one and stays a paint-only write
    style:"width:" ++ px(B.PAGE_W * s) ++ ";height:" ++ px(B.PAGE_H * s) ++ ";transform:rotateY(0deg);",
    // each shade is its own plane over its face: its per-frame opacity then leaves the
    // face's content, and so the face's retained raster, unchanged
    <div class:"bk-face bk-front", (page_body(lib, book, book.leaves[i].front, s, options))>;
    <div class:"bk-shade bk-shade-front">;
    <div class:"bk-face bk-back", (page_body(lib, book, book.leaves[i].back, s, options))>;
    <div class:"bk-shade bk-shade-back">>

// spread bar entries: one per turned-leaf count, with the spread's first photo
pub fn spread_bar(lib, book) => <div class:"bk-spreads",
    *[for (t in 0 to len(book.leaves)) (
        let id = B.spread_photo(book, t),
        let p = if (id > 0) L.photo(lib, id) else null,
        <button type:"button", class:"bk-spread", 'data-spread':string(t), 'aria-current':"false", title:B.spread_label(book, t),
            <span class:"bk-thumb", style:"background-color:" ++ (if (p == null) "#8a7f6c" else p.color or "#999") ++ ";",
                (if (p != null and p.thumb_src != "") <img src:p.thumb_src, alt:""> else null)>;
            <span class:"bk-label", B.spread_label(book, t)>>)]>

fn option(key, title, chosen) => <option value:key, *:(if (key == chosen) {selected:""} else {}), title>

// the book layer shell; #bk-stage and #bk-bar are filled when a book opens
pub fn shell() => <div id:"book", class:"book-layer", style:"display:none", role:"dialog", 'aria-modal':"true", 'aria-labelledby':"bk-title", tabindex:"-1",
    <header class:"bk-bar",
        <button type:"button", class:"tool", 'data-action':"book-close", title:"Close book (Esc)", 'aria-label':"Close book", "✕">;
        <h2 id:"bk-title", "Album book">;
        <span id:"bk-position", class:"bk-position">;
        <span class:"cl-spacer">;
        <button type:"button", 'data-action':"book-prev", id:"bk-prev", title:"Previous page (←)", "‹ Previous">;
        <button type:"button", 'data-action':"book-next", id:"bk-next", title:"Next page (→)", "Next ›">;
        <select id:"bk-cover", 'aria-label':"Cover", *[for (c in B.COVERS) option(c.key, c.title, "navy")]>;
        <select id:"bk-paper", 'aria-label':"Paper", *[for (p in B.PAPERS) option(p.key, p.title, "cream")]>>;
    <div id:"bk-stage", class:"bk-stage">;
    <div id:"bk-bar", class:"bk-bar-spreads">>
