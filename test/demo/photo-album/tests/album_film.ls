// Memories film from 40 photos of one album: chapters, shot plan, timing on beats, deck timeline.
import L: ~~.mod_library
import F: ~~.mod_film
import S: ~~.mod_score
import slide: lambda.slide

// the committed manifest of the full corpus; no photo files are needed
let manifest = input("test/demo/photo-album/photos.json", 'json')^
let lib = L.build(manifest, [for (p in manifest.photos) {photo:false, thumb:false}], "file:///corpus/")^
let ids = slice([for (id in lib.order where L.photo(lib, id).album == "cities") id], 0, 40)
let film = F.plan(lib, ids, "Cities")
let deck = F.deck(lib, film, "file:///corpus/")
let line = slide.timeline(deck, {autoplay_dwell_ms: F.DWELL_MS})^
let used = [for (s in film.shots, id in s.ids where s.kind != "title" and s.kind != "chapter" and s.kind != "credits") id]
let music = S.performance(film)
let notes = [for (e in music.events where e.status >= 144 and e.status < 160 and e.b > 0) e]
fn frame_at(ms) string^ => format(slide.snapshot(deck, F.address(film, ms))^, 'html')^
let frames = [frame_at(20250.0)^, frame_at(20250.0)^, frame_at(21250.0)^]
fn kinds(k) => len([for (s in film.shots where s.kind == k) s]);
{
    seconds: film.duration_ms / 1000.0,
    about_ninety: film.duration_ms >= 75000.0 and film.duration_ms <= 105000.0,
    chapters: [for (c in film.chapters) [c.title, c.count, c.start_beat, c.albums]],
    shots: {single: kinds("single"), split: kinds("split"), quick: kinds("quick"), chapter: kinds("chapter")},
    motions: [for (s in slice(film.shots, 0, 14)) [s.kind, s.motion, s.transition, s.beats]],
    every_photo_once: len(used) == 40 and sort(used) == sort(film.ids),
    chronological: film.ids == [for (id in film.ids order by L.photo(lib, id).taken, id) id],
    quick_runs_cut: all([for (s in film.shots where s.kind == "quick") s.transition == "cut"]),
    chapter_openers_morph: all([for (i, s in film.shots where i > 0 and film.shots[i - 1].kind == "chapter") s.transition == "morph"]),
    // the deck's clock puts every shot on its beat, and the film's length is the deck's
    deck_on_beats: all([for (i, s in film.shots) line.slides[i].start_ms == s.start_beat * F.BEAT_MS]),
    deck_duration: line.duration_ms == film.duration_ms,
    credits: [film.shots[len(film.shots) - 1].kind, len(film.shots[len(film.shots) - 1].ids)],
    small: (let few = F.plan(lib, slice(ids, 0, 3), "Three"), [few.beats, [for (s in few.shots) s.kind]]),
    // the soundtrack lasts as long as the film, plays in time order, and lands chords on chapter cards
    score: {seconds: music.seconds, same_length: music.seconds * 1000.0 == film.duration_ms,
        sorted: all([for (i, e in music.events where i > 0) e.seconds >= music.events[i - 1].seconds]),
        notes: len(notes), channels: sort(unique([for (e in notes) e.status - 144])),
        within: all([for (e in music.events) e.seconds >= 0.0 and e.seconds <= music.seconds]),
        chapter_downbeats: all([for (c in film.chapters) len([for (e in notes where e.status == 145 and e.seconds == c.start_beat * F.BEAT_MS / 1000.0) e]) == 3])},
    // a frame at a fixed film time renders the same every time, and frames differ over time
    snapshots: [frames[0] == frames[1], frames[0] != frames[2], F.address(film, 20250.0), F.address(film, 97000.0)],
    clock: [F.clock_label(0.0), F.clock_label(65400.0), F.clock_label(600000.0)]
}
