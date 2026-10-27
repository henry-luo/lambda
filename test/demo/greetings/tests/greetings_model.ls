// The Greetings studio's pure parts: the catalog, assembling a card, card
// edits, reshaping, undo history, colour arithmetic and file names.
import A: ~~.mod_art
import T: ~~.mod_templates
import M: ~~.mod_model
import R: ~~.mod_render
import D: ~~.~~.mod_document
import E: ~~.mod_effects
import P: ~~.mod_popup

let ART_KEYS = [for (a in A.ART) a.key]
fn art_keys(items) => [for (it in items where it.kind == "art") it.art]
fn ids(card, key) => [for (it in card.pages[key].items) it.id]

"catalog:";
{templates: len(T.TEMPLATES), occasions: len(T.OCCASIONS), art: len(A.ART), fonts: len(A.FONTS),
 papers: len(A.PAPERS), patterns: len(A.PATTERNS), borders: len(A.BORDERS)};
"every template names a known occasion, format, paper and art:";
[for (t in T.TEMPLATES where len([for (o in T.OCCASIONS where o.key == t.occasion) o]) == 0
    or len([for (f in T.FORMATS where f.key == t.format) f]) == 0
    or A.paper_of(t.front.paper).key != t.front.paper
    or len([for (k in art_keys(t.front.items) where not contains(ART_KEYS, k)) k]) > 0) t.key];
"every occasion has four verses and a known motif:";
[for (o in T.OCCASIONS) {o: o.key, verses: len(T.VERSES[o.key]), motif: contains(ART_KEYS, o.motif)}];

let card = T.assemble(T.template_of("evergreen"), {})
"an assembled card:";
{title: card.title, format: card.format, front: ids(card, "front"), left: ids(card, "left"),
 right: ids(card, "right"), back: ids(card, "back"), verse: card.pages.right.items[0].text};
"photos resolve against the sample map:";
let postcard = T.assemble(T.template_of("wish-you-were-here"), {coast: "file:///coast.jpg"});
[for (it in postcard.pages.front.items where it.kind == "photo") {src: it.src, frame: it.frame}];

"edits:";
let moved = M.update(card, "front", "f2", {x: 10.0, color: "#ffffff"});
{x: M.item_of(moved, "front", "f2").x, color: M.item_of(moved, "front", "f2").color, untouched: M.item_of(moved, "front", "f1") == M.item_of(card, "front", "f1")};
let added = M.add(card, "back", {*:T.text("Hi", 0, 0, 100, 20, "avenir", "#000000"), id: M.fresh_id(card)});
{back: ids(added, "back"), removed: ids(M.remove(added, "back", "b1"), "back"), next: M.fresh_id(M.add(added, "front", {*:M.item_of(card, "front", "f1"), id: "n7"}))};
let copy = M.duplicate(card, "front", "f1", "n2");
{front: ids(copy, "front"), offset: M.item_of(copy, "front", "n2").x - M.item_of(copy, "front", "f1").x};
{front: ids(M.reorder(card, "front", "f1", "front"), "front"), back: ids(M.reorder(card, "front", "f5", "back"), "front"),
 forward: ids(M.reorder(card, "front", "f2", "forward"), "front"), backward: ids(M.reorder(card, "front", "f2", "backward"), "front")};
"backdrop on every page:";
let papered = M.set_all_pages(card, {paper: "kraft"});
[for (k in ["front", "left", "right", "back"]) papered.pages[k].paper];

"reshaping keeps centres and proportions:";
let wide = M.reformat(card, "landscape")
let tree = M.item_of(card, "front", "f1")
let wide_tree = M.item_of(wide, "front", "f1");
{format: wide.format, centre_x: round(wide_tree.x + wide_tree.w / 2.0), page_centre: 350, w: round(wide_tree.w),
 ratio: round(wide_tree.h / wide_tree.w * 1000) == round(tree.h / tree.w * 1000)};
"placing a new item centres it:";
let placed = M.place(card, T.art("heart", 0, 0, 200));
{x: placed.x, y: placed.y, w: placed.w};

"history:";
let h1 = D.record(D.history(), card);
let back_step = D.undo_step(h1, moved);
let fore_step = D.redo_step(back_step.history, back_step.value);
{undo_is_card: back_step.value == card, redo_is_moved: fore_step.value == moved, empty_undo: D.undo_step(D.history(), card)};

"colours:";
{tint: A.tint("#e11d48", .5), shade: A.shade("#e11d48", .5), dark_navy: A.is_dark("#1e2c56"), dark_cream: A.is_dark("#fbf6ea")};
"files:";
{slug: D.slug("  Mr & Mrs: Golden Rings! ", "card"), empty: D.slug("!!", "card"), valid: M.valid(card), invalid: M.valid({pages: {front: {}}})};
"geometry the editor writes during a drag:";
R.geometry(M.item_of(card, "front", "f2"), 2.0)

"effects are known, optional and independent of the template:";
{animations: len(E.ANIMATIONS), tunes: len(E.MUSIC),
 invalid_templates: [for (t in T.TEMPLATES where t.effects != null and E.of(t) != t.effects) t.key],
 animated: len([for (t in T.TEMPLATES where E.of(t).animation != "none") t]),
 musical: len([for (t in T.TEMPLATES where E.of(t).music != "none") t]),
 legacy: E.of({pages: card.pages}), unknown: E.of({effects: {animation: "missing", music: "missing"}}),
 blank: E.of(T.blank("portrait"))};
let animated = T.assemble(T.template_of("winter-wishes"), {})
let silent = E.configure(animated, "music", "none")
let restored = D.undo_step(D.record(D.history(), animated), silent);
{animation_kept: E.of(silent).animation, music_removed: E.of(silent).music,
 undo: E.of(restored.value), reshape: E.of(M.reformat(animated, "square")),
 roundtrip: E.of(parse(format(animated, 'mark'), 'mark')^)};
"only live pages animate; print pages stay still:";
let live = format(R.page(animated, "front", 1.0, {animate: true}), 'html')
let print_page = format(R.page(animated, "front", 3.0), 'html');
{live_motion: contains(live, "motion-snow"), live_particles: contains(live, "effect-particle"),
 print_still: contains(print_page, "motion-none"), print_no_particles: not contains(print_page, "effect-particle")};

"pop-up catalogs, legacy defaults and validation:";
{designs: len(P.DESIGNS), depths: len(P.DEPTHS), templates: len([for (t in T.TEMPLATES where P.of(t).design != "none") t]),
 invalid_templates: [for (t in T.TEMPLATES where t.popup != null and P.of(t) != t.popup) t.key],
 invalid_art: [for (p in P.DESIGNS) for (layer in p.layers) for (it in layer where not contains(ART_KEYS, it.art)) it.art],
 legacy: P.of({}), blank: P.of(T.blank("portrait")),
 unknown: P.of({popup: {design: "missing", depth: "missing", color: "invalid"}})};
let popup = T.assemble(T.template_of("popup-birthday"), {})
let recolored = P.configure(P.configure(popup, "depth", "high"), "color", "#0ea5e9")
let no_popup = P.configure(recolored, "design", "none")
let popup_undo = M.undo_step(M.record(M.history(), recolored), no_popup);
"pop-ups preserve messages, effects, history and file/format roundtrips:";
{configured: P.of(recolored), removed: P.of(no_popup).design, undo: P.of(popup_undo.card),
 redo: P.of(M.redo_step(popup_undo.history, popup_undo.card).card).design,
 verse_untouched: recolored.pages.right == popup.pages.right, effects_untouched: E.of(recolored) == E.of(popup),
 roundtrip: P.of(parse(format(recolored, 'mark'), 'mark')^), reshape: P.of(M.reformat(recolored, "landscape")),
 invalid_color: P.of(P.configure(popup, "color", "#xx0000")).color};
"one illustration is flat in the editor/print and hinged only in live preview:";
let popup_flat = format(R.page(popup, "left", 1.0), 'html')
let popup_live = format(R.page(popup, "left", 1.0, {popup_live: true}), 'html');
{flat: contains(popup_flat, "popup-flat"), print_has_art: contains(popup_flat, "MAKE A WISH!"),
 print_has_no_live_planes: not contains(popup_flat, "popup-live"),
 live: contains(popup_live, "popup-live"), planes: all([for (i in 0 to 2) contains(popup_live, "popup-plane-" ++ string(i))]),
 no_front_popup: not contains(format(R.page(popup, "front", 1.0), 'html'), "popup-scene"),
 no_plain_popup: not contains(format(R.page(T.blank("portrait"), "left", 1.0), 'html'), "popup-scene")}
