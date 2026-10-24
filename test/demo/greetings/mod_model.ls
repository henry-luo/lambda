// Card edits as pure functions over the card value, plus the undo history.
// A card is {title, template, occasion, format, pages: {front, left, right, back}};
// each page is {paper, pattern, pattern_color, pattern_alpha, border, border_color, items}.
import A: .mod_art
import T: .mod_templates

pub fn items(card, key) => card.pages[key].items
pub fn item_of(card, key, id) => (let hit = [for (it in items(card, key) where it.id == id) it], if (len(hit) == 0) null else hit[0])

fn with_page(card, key, pg) => {*:card, pages: {*:card.pages, [key]: pg}}
fn with_items(card, key, layers) => with_page(card, key, {*:card.pages[key], items: layers})

pub fn set_page(card, key, patch) => with_page(card, key, {*:card.pages[key], *:patch})
pub fn set_all_pages(card, patch) => {*:card, pages: {front: {*:card.pages.front, *:patch}, left: {*:card.pages.left, *:patch},
    right: {*:card.pages.right, *:patch}, back: {*:card.pages.back, *:patch}}}

pub fn update(card, key, id, patch) => with_items(card, key, [for (it in items(card, key)) if (it.id == id) {*:it, *:patch} else it])
pub fn add(card, key, it) => with_items(card, key, [*items(card, key), it])
// ids the studio adds are "n1", "n2", …; the next is past every such id on the card,
// so a card opened from a file or restored by undo never gets a duplicate
fn added_numbers(pg) => [for (it in pg.items where starts_with(it.id, "n")) (int(slice(it.id, 1, len(it.id))) ^ { 0 })]
pub fn fresh_id(card) => (let used = [*added_numbers(card.pages.front), *added_numbers(card.pages.left),
    *added_numbers(card.pages.right), *added_numbers(card.pages.back)],
    "n" ++ string(if (len(used) == 0) 1 else max(used) + 1))
pub fn remove(card, key, id) => with_items(card, key, [for (it in items(card, key) where it.id != id) it])

// a copy lands a little below and to the right, on top of the layers
pub fn duplicate(card, key, id, new_id) => (let it = item_of(card, key, id),
    if (it == null) card else add(card, key, {*:it, id: new_id, x: it.x + 18.0, y: it.y + 18.0}))

// "front"/"back" move to the end of the layers; "forward"/"backward" swap with a neighbour
pub fn reorder(card, key, id, dir) {
    let layers = items(card, key)
    let at = [for (i, it in layers where it.id == id) i]
    if (len(at) == 0) { card }
    else {
        let i = at[0]
        let it = layers[i]
        let rest = [for (x in layers where x.id != id) x]
        let j = if (dir == "front") len(rest) else if (dir == "back") 0
            else if (dir == "forward") min(len(rest), i + 1) else max(0, i - 1);
        with_items(card, key, [*slice(rest, 0, j), it, *slice(rest, j, len(rest))])
    }
}

// keeps every item's centre in the same relative place and scales it by the
// smaller of the two axis ratios, so nothing is stretched by a new format
fn refit(it, sx, sy) => (let k = min(sx, sy), let w = it.w * k,
    if (it.kind == "text") {*:it, x: (it.x + it.w / 2.0) * sx - w / 2.0, y: it.y * sy, w: w, size: it.size * k, spacing: it.spacing * k}
    else (let h = it.h * k, {*:it, x: (it.x + it.w / 2.0) * sx - w / 2.0, y: (it.y + it.h / 2.0) * sy - h / 2.0, w: w, h: h}))
pub fn reformat(card, fmt) {
    let a = T.format_of(card.format)
    let b = T.format_of(fmt)
    let sx = float(b.w) / a.w
    let sy = float(b.h) / a.h;
    {*:card, format: fmt, pages: {front: {*:card.pages.front, items: [for (it in card.pages.front.items) refit(it, sx, sy)]},
        left: {*:card.pages.left, items: [for (it in card.pages.left.items) refit(it, sx, sy)]},
        right: {*:card.pages.right, items: [for (it in card.pages.right.items) refit(it, sx, sy)]},
        back: {*:card.pages.back, items: [for (it in card.pages.back.items) refit(it, sx, sy)]}}}
}

// a new item is centred on the page; text that is too wide wraps
pub fn place(card, it) => (let f = T.format_of(card.format), let w = min(it.w, f.w * .84),
    {*:it, w: w, h: if (it.kind == "text") it.h else it.h * w / it.w, x: (f.w - w) / 2.0,
        y: if (it.kind == "text") f.h * .42 else (f.h - it.h * w / it.w) / 2.0})

// ---- history ------------------------------------------------------------------

let HISTORY_LIMIT = 60
pub fn history() => {past: [], future: []}
pub fn record(h, before) => (let past = [*h.past, before],
    {past: if (len(past) > HISTORY_LIMIT) slice(past, 1, len(past)) else past, future: []})
pub fn undo_step(h, current) => if (len(h.past) == 0) null
    else {card: h.past[len(h.past) - 1], history: {past: slice(h.past, 0, len(h.past) - 1), future: [current, *h.future]}}
pub fn redo_step(h, current) => if (len(h.future) == 0) null
    else {card: h.future[0], history: {past: [*h.past, current], future: slice(h.future, 1, len(h.future))}}

// ---- files ----------------------------------------------------------------------

// a file is a card when it carries the four pages; anything else is refused
pub fn valid(card) => card is map and card.pages is map and
    all([for (k in ["front", "left", "right", "back"]) card.pages[k] is map and card.pages[k].items is array])
pub fn slug(title) => (let s = join([for (ch in split(lower(trim(title)), "")) if (contains("abcdefghijklmnopqrstuvwxyz0123456789", ch)) ch else "-"], ""),
    let parts = [for (p in split(s, "-") where p != "") p], if (len(parts) == 0) "card" else join(parts, "-"))
