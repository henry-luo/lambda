// Memories film (vibe/Lambda_Demo_Photo_Album.md §3.4): story generation as pure values, lowered
// to a lambda.slide deck with entry cues only. Shots last whole beats at a fixed tempo, so every
// shot starts on a beat and the soundtrack (mod_score.ls) can follow the film's chapters.
import L: .mod_library
import D: .mod_deck

pub let BEAT_MS = 500.0
// autoplay holds each shot's last pose this long; it is part of the shot's beats
pub let DWELL_MS = 50.0
pub let MAX_PHOTOS = 60
let W = 1280.0
let H = 720.0
let BEATS = {title:6, chapter:4, single:4, split:6, quick:2}

fn photo(lib, id) => L.photo(lib, id)
fn portrait(p) => p.aspect < 0.9
fn year(lib, id) => photo(lib, id).year
fn month(lib, id) => slice(photo(lib, id).taken, 0, 7)

// chapter starts: a new chapter at a key change once the current one holds `size` photos,
// unless fewer than three photos would be left for the last one
fn cuts(lib, ids, key, size, i, s) {
    if (i >= len(ids)) []
    else if (key(lib, ids[i]) != key(lib, ids[i - 1]) and i - s >= size and len(ids) - i >= 3)
        [i, *cuts(lib, ids, key, size, i + 1, i)]
    else cuts(lib, ids, key, size, i + 1, s)
}

fn runs(ids, starts) => [for (j, s in starts) slice(ids, s, if (j + 1 < len(starts)) starts[j + 1] else len(ids))]

// chronological chapters by year, or by month when the photos span a single year
fn chapter_runs(lib, ids) {
    let size = max(6, len(ids) / 5)
    let by_year = runs(ids, [0, *cuts(lib, ids, year, size, 1, 0)]);
    if (len(by_year) > 1 or len(ids) < 16) by_year else runs(ids, [0, *cuts(lib, ids, month, size, 1, 0)])
}

fn chapter_title(lib, ids) {
    let first = photo(lib, ids[0])
    let final_photo = photo(lib, ids[len(ids) - 1]);
    if (first.year != final_photo.year) D.years(lib, ids)
    else if (slice(first.taken, 0, 7) == slice(final_photo.taken, 0, 7)) L.month_label(first)
    // "June–August 2019"
    else split(L.month_label(first), " ")[0] ++ "–" ++ L.month_label(final_photo)
}

// the albums a chapter draws on, most photos first
fn chapter_albums(lib, ids) {
    let names = [for (id in ids) L.album_name(lib, photo(lib, id))]
    let distinct = [for (i, n in names where index_of(names, n) == i) n];
    slice([for (n in distinct order by len([for (m in names where m == n) m]) desc, n) n], 0, 2)
}

// shots of the photos before a chapter's quick-cut run: consecutive portraits pair up
fn plain(lib, ids, i, k) {
    if (i >= len(ids)) []
    else if (portrait(photo(lib, ids[i])) and i + 1 < len(ids) and portrait(photo(lib, ids[i + 1])))
        [{kind:"split", ids:[ids[i], ids[i + 1]], beats:BEATS.split, motion:"drift"}, *plain(lib, ids, i + 2, k)]
    else (
        let motion = if (portrait(photo(lib, ids[i]))) "drift" else ["pan-right", "zoom", "pan-left"][(i + k) % 3],
        [{kind:"single", ids:[ids[i]], beats:BEATS.single, motion:motion}, *plain(lib, ids, i + 1, k)])
}

// every third chapter ends on a quick-cut run of 4 to 6 photos, two beats each
fn chapter_shots(lib, ids, k) {
    let quick = if (k % 3 == 2 and len(ids) >= 8) min(6, max(4, len(ids) / 3)) else 0
    let body = plain(lib, slice(ids, 0, len(ids) - quick), 0, k);
    [*body, *[for (id in slice(ids, len(ids) - quick, len(ids))) {kind:"quick", ids:[id], beats:BEATS.quick, motion:"zoom"}]]
}

fn transition(shot, n) => if (shot.kind == "quick" or shot.kind == "title") "cut"
    else if (shot.first) "morph" else if (shot.kind == "split") "slide" else if (n % 5 == 4) "push" else "fade"

fn credits_beats(n) => 6 + int(ceil(n / 3))

// Film model: {title, ids, chapters: [{title, albums, count, start_beat}], shots: [{kind, ids, beats,
// motion, transition, chapter, start_beat}], beats, duration_ms}
pub fn plan(lib, ids, title) {
    let ordered = [for (id in slice(ids, 0, MAX_PHOTOS) order by photo(lib, id).taken, id) id]
    let groups = chapter_runs(lib, ordered)
    let landscapes = [for (id in ordered where not portrait(photo(lib, id))) id]
    let hero = landscapes[0] or ordered[0]
    let story = [{kind:"title", ids:[hero], beats:BEATS.title, motion:"zoom", chapter:-1},
        *[for (k, g in groups) for (j, s in [{kind:"chapter", ids:[g[0]], beats:BEATS.chapter, motion:"none"}, *chapter_shots(lib, g, k)])
            {*:s, chapter:k, first:j == 1}],
        {kind:"credits", ids:ordered, beats:credits_beats(len(ordered)), motion:"roll", chapter:len(groups)}]
    let starts = [for (i, s in story) sum([for (t in slice(story, 0, i)) t.beats])]
    let shots = [for (i, s in story) {*:s, start_beat:starts[i], transition:transition(s, i)}]
    let beats = sum([for (s in story) s.beats]);
    {title:title, ids:ordered, beats:beats, duration_ms:beats * BEAT_MS,
        chapters:[for (k, g in groups) {title:chapter_title(lib, g), albums:chapter_albums(lib, g), count:len(g),
            start_beat:[for (s in shots where s.kind == "chapter" and s.chapter == k) s.start_beat][0]}],
        shots:shots}
}

// ---------------------------------------------------------------- lowering to a deck

fn transition_ms(shot) => if (shot.transition == "cut") 0.0 else BEAT_MS
fn r(v) => round(v * 100.0) / 100.0
fn frames(target, ms, channel, a, b) => <effect target:target, kind:'keyframes', channel:symbol(channel), duration:ms,
    easing:'ease-in-out', stops:[[0.0, r(a)], [1.0, r(b)]]>

// Ken Burns moves on one photo wrapper; a scale above 1 keeps a pan inside the frame
fn motion(target, kind, ms, flip = false) {
    let dir = if (flip) -1.0 else 1.0;
    if (kind == "pan-right" or kind == "pan-left") (let d = if (kind == "pan-right") 70.0 else -70.0,
        [frames(target, ms, "sx", 1.16, 1.16), frames(target, ms, "sy", 1.16, 1.16), frames(target, ms, "tx", -d, d)])
    else if (kind == "drift") [frames(target, ms, "sx", 1.08, 1.12), frames(target, ms, "sy", 1.08, 1.12), frames(target, ms, "ty", 26.0 * dir, -26.0 * dir)]
    else [frames(target, ms, "sx", 1.0, 1.14), frames(target, ms, "sy", 1.0, 1.14)]
}

fn caption(lib, p) => <text id:"caption", x:48.0, y:H - 72.0, width:900.0, height:40.0, font_size:22, color:"#ffffff",
    style:"font-weight:600;text-shadow:0 1px 6px rgba(0,0,0,0.7);", L.date_label(p) ++ " · " ++ L.album_name(lib, p)>
fn caption_in(ms) => <effect target:"caption", kind:'fade-in', duration:min(600.0, ms), delay:min(400.0, ms / 3.0)>

// a blurred, darkened copy of the photo fills the frame behind a portrait shown whole
fn backdrop(p, prefix) => [D.picture(p, "backdrop", [-60.0, -60.0, W + 120.0, H + 120.0], prefix, "cover", null, "filter:blur(28px);"),
    <shape id:"shade", kind:'rect', x:0.0, y:0.0, width:W, height:H, fill:"#000000", opacity:0.35>]

fn credit_line(lib, id) {
    let p = photo(lib, id);
    <div style:"margin:0 0 12px;", <strong p.title>; <span style:"color:#a1a1a6;", " — " ++ p.author ++ " · " ++ p.license>>
}

fn slide_for(lib, film, shot, i, prefix) {
    let sid = "f" ++ string(i)
    let closing = i == len(film.shots) - 1
    // a shot fills its beats: transition, then its motion, then autoplay's dwell (not after the last)
    let ms = shot.beats * BEAT_MS - transition_ms(shot) - (if (closing) 0.0 else DWELL_MS)
    let morph = if (shot.first) "m" ++ string(shot.chapter) else null
    let base = {id:sid, transition:symbol(shot.transition), transition_duration:transition_ms(shot)};
    if (shot.kind == "title") (
        let p = photo(lib, shot.ids[0]),
        <slide *:base, background:"#000000",
            (D.picture(p, "hero", [-40.0, -40.0, W + 80.0, H + 80.0], prefix, "cover", null, "filter:blur(10px);"));
            <shape id:"scrim", kind:'rect', x:0.0, y:0.0, width:W, height:H, fill:"#000000", opacity:0.4>;
            <text id:"title", x:96.0, y:380.0, width:1088.0, height:100.0, font_size:72, color:"#ffffff", style:"font-weight:700;", film.title>;
            <text id:"subtitle", x:96.0, y:492.0, width:1088.0, height:44.0, font_size:28, color:"#e6e6e6",
                D.years(lib, film.ids) ++ " · " ++ string(len(film.ids)) ++ " photos">;
            <cue start:'entry', <parallel *motion("hero", "zoom", ms);
                <effect target:"title", kind:'fade-in', duration:900.0, delay:300.0>
                <effect target:"subtitle", kind:'fly-in', from:'bottom', distance:30, duration:700.0, delay:900.0>>>>)
    else if (shot.kind == "chapter") (
        let ch = film.chapters[shot.chapter],
        let p = photo(lib, shot.ids[0]),
        <slide *:base, background:"#111214",
            // long month spans step down so the heading keeps to one line
            <text id:"heading", x:96.0, y:250.0, width:740.0, height:110.0, font_size:if (len(ch.title) > 10) 56 else 80, color:"#ffffff",
                style:"font-weight:700;white-space:nowrap;", ch.title>;
            <text id:"albums", x:96.0, y:380.0, width:720.0, height:40.0, font_size:26, color:"#a1a1a6",
                join(ch.albums, " · ") ++ " · " ++ string(ch.count) ++ " photos">;
            <shape id:"rule", kind:'rect', x:96.0, y:446.0, width:240.0, height:4.0, fill:"#0a84ff">;
            (D.picture(p, "tile", [860.0, 200.0, 320.0, 320.0], prefix, "cover", "m" ++ string(shot.chapter)));
            // the accent rule draws across the card's beats, so its cue spans the card
            <cue start:'entry', <parallel <effect target:"heading", kind:'fly-in', from:'bottom', distance:40, duration:600.0, easing:'ease-out-cubic'>
                <effect target:"albums", kind:'fade-in', duration:600.0, delay:300.0>;
                (frames("rule", ms, "sx", 0.02, 1.0))>>>)
    else if (shot.kind == "split") (
        let a = photo(lib, shot.ids[0]),
        let b = photo(lib, shot.ids[1]),
        <slide *:base, background:"#000000",
            (D.picture(a, "left", [0.0, 0.0, W / 2.0 - 4.0, H], prefix, "cover", morph));
            (D.picture(b, "right", [W / 2.0 + 4.0, 0.0, W / 2.0 - 4.0, H], prefix, "cover"));
            (caption(lib, a));
            <cue start:'entry', <parallel *motion("left", "drift", ms); *motion("right", "drift", ms, true); (caption_in(ms))>>>)
    else if (shot.kind == "credits")
        (let lines = len(shot.ids) + 3,
        let roll_h = 34.0 * float(lines),
        <slide *:base, background:"#0b0b0c",
            <content id:"roll", x:240.0, y:H, width:800.0, height:roll_h,
                <div style:"color:#f2f2f7;font-size:18px;line-height:22px;text-align:center;",
                    <h2 style:"font-size:30px;margin:0 0 28px;", "Photo credits">;
                    *[for (id in shot.ids) credit_line(lib, id)]>>;
            <cue start:'entry', (frames("roll", ms, "ty", 0.0, -(roll_h + H * 0.5)))>>)
    else (
        let p = photo(lib, shot.ids[0]),
        let whole = shot.motion == "drift",
        <slide *:base, background:"#000000",
            *(if (whole) backdrop(p, prefix) else []);
            (D.picture(p, "photo", [0.0, 0.0, W, H], prefix, if (whole) "contain" else "cover", morph));
            *(if (shot.kind == "single") [caption(lib, p)] else []);
            <cue start:'entry', <parallel *motion("photo", shot.motion, ms); *(if (shot.kind == "single") [caption_in(ms)] else [])>>>)
}

// the deck the film plays; `prefix` is the corpus URL photo files resolve under
pub fn deck(lib, film, prefix) => <presentation id:"film", title:film.title, width:W, height:H, theme:'midnight',
    autoplay_dwell_ms:DWELL_MS, ui:D.ALBUM_UI, *[for (i, s in film.shots) slide_for(lib, film, s, i, prefix)]>

// the slide.snapshot address of film time `ms`: its shot and the time into the shot's motion
// (a transition shows the shot's first pose); a frame sequence for an external encoder
pub fn address(film, ms) {
    let beat = max(0.0, min(film.duration_ms, ms)) / BEAT_MS
    let found = [for (i, s in film.shots where beat >= s.start_beat and beat < s.start_beat + s.beats) i]
    let i = if (len(found) > 0) found[0] else len(film.shots) - 1
    let shot = film.shots[i];
    {slide:i, cue:0, time_ms:max(0.0, min(ms, film.duration_ms) - shot.start_beat * BEAT_MS - transition_ms(shot))}
}

// "1:05"
pub fn clock_label(ms) {
    let s = int(floor(max(0.0, ms) / 1000.0));
    string(int(floor(s / 60))) ++ ":" ++ (if (s % 60 < 10) "0" else "") ++ string(s % 60)
}
