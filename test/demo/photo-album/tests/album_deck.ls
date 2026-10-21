// Slideshow deck generation over the fixture: slide plan, layouts, transitions, morph pairs,
// notes and credits, a successful lambda.slide compile, and a Mark round trip of the saved form.
import L: ~~.mod_library
import D: ~~.mod_deck
import slide: lambda.slide

let manifest = input("test/demo/photo-album/fixture/photos.json", 'json')^
let present = [for (p in manifest.photos) {photo:true, thumb:true}]
let lib = L.build(manifest, present, "file:///corpus/")^
let ids = slice(lib.order, 0, 20)
let options = {title:"Fixture trip", ids:ids, theme:"postcard", transition:"push", dwell_ms:3000, prefix:"../"}
let deck = D.deck(lib, options)
let plan = slide.compile(deck)^
let slides = [for (s in deck where s is element and name(s) == 'slide') s]
fn objects(s) => [for (o in s where o is element and name(o) != 'notes' and name(o) != 'cue') string(name(o)) ++ ":" ++ (o.id or "")]
fn note_text(s) => [for (o in s where o is element and name(o) == 'notes') join([for (para in o) string(para)], " | ")][0]
let saved = format(deck, 'mark')
let missing = D.deck({*:lib, photos:[for (p in lib.photos) {*:p, has_photo:false}]}, {*:options, theme:"dark"});
{
    plan: [for (s in D.plan(lib, ids)) s.kind ++ "(" ++ join([for (id in s.ids) string(id)], ",") ++ ")"],
    photos_in_plan: len([for (s in D.plan(lib, ids), id in s.ids where s.kind != "credits" and s.kind != "title" and s.kind != "zoom") id]),
    compiled_slides: len(plan.slides),
    attributes: [deck.theme, deck.autostart, deck.autoplay_dwell_ms, len(slides)],
    postcard: [for (s in slides) len([for (o in content(s) where o is element and starts_with(o.id or "", "postcard-")) o])] == [for (s in slides) 2],
    transitions: [for (s in slides) s.transition or "none"],
    first_photo: objects(slides[1]),
    grid: objects([for (s in slides where s.title == "Four photos") s][0]),
    morph: [for (s in slides, o in s where o is element and o.morph_id != null) string(s.id) ++ "/" ++ o.id ++ "=" ++ o.morph_id],
    image_src: [for (o in slides[1] where o is element and name(o) == 'image') o.src][0],
    notes: note_text(slides[1]),
    credits_slide: slides[last].title,
    missing_photo: [for (o in [for (s in missing where s is element and name(s) == 'slide') s][1] where o is element and o.id == "photo") string(name(o)) ++ ":" ++ o.fill][0],
    names: [D.file_name("Fixture trip"), D.file_name("  Fog, snow & ice — 2023! "), D.file_name("!!!")],
    // the saved form reads back as the same deck
    round_trip: format(parse(saved, 'mark') ^ { null }, 'mark') == saved,
    reads_as_slides: (parse(saved, 'slides') ^ { null }) is element,
    // tests/fixture_slideshow.slides is this deck as saved next to the fixture; saved_deck.json views it
    committed: format(D.deck(lib, {*:options, prefix:"../fixture/"}), 'mark') ==
        format(input("test/demo/photo-album/tests/fixture_slideshow.slides", 'slides') ^ { null }, 'mark'),
    // a loaded deck's relative image paths resolve against the deck's folder; nothing else changes
    resolved: (
        let r = D.resolve_sources(deck, "file:///corpus/slideshows/"),
        [len([for (c in r where c is element) c]) == len([for (c in deck where c is element) c]),
         [for (s in r, o in s where s is element and name(s) == 'slide' and o is element and name(o) == 'image') o.src][0],
         slide.compile(r) ^ { null } != null])
}
