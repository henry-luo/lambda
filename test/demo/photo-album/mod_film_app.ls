// Film playback procedures: an embedded lambda.slide player without controls, driven by
// slide_command events, under the album's own film bar (scrubber, chapters, time, sound).
// The bar reads the deck clock the player publishes (data-slide-position and its sample time)
// and extrapolates it on frame timestamps while playing, so replays are deterministic.
// State: {film, deck, sound, token, drag, frame}; `ctx` is {lib, dir, dir_url}.
import dom
import updates: ~~.mod_publish
import F: .mod_film
import S: .mod_score

// the film layer's header and bar, as sized in photo-album.css
let CHROME_H = 52.0 + 64.0

fn node(owner, id) => dom.get_element_by_id(owner, id)
fn player(owner) => dom.get_element_by_id(owner, "album-film")
fn pct(v) => string(round(v * 10000.0) / 100.0) ++ "%"
fn attr_num(el, name) => float(dom.get_attribute(el, name)) ^ { 0.0 }

// the deck clock now: {position, running, ended}
pn clock(owner, fs, now_ms) {
    let p = player(owner)
    if (p == null) { return {position:0.0, running:false, ended:false} }
    let running = dom.get_attribute(p, "data-slide-autoplay") == "true" and dom.get_attribute(p, "data-slide-paused") != "true"
    let at = attr_num(p, "data-slide-position")
    let position = min(fs.film.duration_ms, if (running) at + (now_ms - attr_num(p, "data-slide-position-at")) * attr_num(p, "data-slide-rate") else at)
    return {position:position, running:running, ended:not running and position >= fs.film.duration_ms - 1.0}
}

pn send(owner, detail) {
    let p = player(owner)
    if (p != null) dom.dispatch(p, {type:"slide_command", bubbles:false, detail:detail})
}

// the film bar: progress, time, the play button and the chapter playing
pn paint(owner, fs, now_ms) {
    let c = clock(owner, fs, now_ms)
    let f = c.position / max(1.0, fs.film.duration_ms)
    updates.style(node(owner, "film-fill"), "width", pct(f))
    updates.style(node(owner, "film-thumb"), "left", pct(f))
    updates.text(owner, "film-time", F.clock_label(c.position))
    updates.attr(node(owner, "film-track"), "aria-valuenow", string(int(c.position / 1000.0)))
    updates.attr(node(owner, "film"), "data-playing", c.running)
    updates.attr(node(owner, "film"), "data-ended", c.ended)
    let label = if (c.running) "Pause" else if (c.ended) "Play again" else "Play"
    updates.attr(node(owner, "film-play"), "aria-label", label)
    updates.attr(node(owner, "film-play"), "title", label ++ " (Space)")
    let beat = c.position / F.BEAT_MS
    let current = len([for (ch in fs.film.chapters where beat >= ch.start_beat) ch]) - 1
    for (b in dom.query_selector_all(owner, "[data-film-chapter]"))
        updates.attr(b, "aria-current", dom.get_attribute(b, "data-film-chapter") == string(current))
    return c
}

// the soundtrack follows the film: playing from the film's position, or paused at it
pn sync_sound(owner, fs, c) {
    if (fs.token == null) { return fs }
    if (not fs.sound or not c.running) { dom.midi_command(owner, fs.token, "pause", 0.0) }
    dom.midi_command(owner, fs.token, "seek", c.position / 1000.0)
    if (fs.sound and c.running) { dom.midi_command(owner, fs.token, "play", 0.0) }
    updates.attr(node(owner, "film"), "data-sound", if (dom.midi_info(owner, fs.token).state == "playing") "playing" else "paused")
    return fs
}

// opens the score; null when the SoundFont or the audio output is unavailable
pn open_sound(owner, fs) {
    var problem = ""
    let bytes = input(S.SOUNDFONT, 'binary') ^ { problem = ^.message }
    if (problem != "") { return null }
    let token = dom.midi_open(owner, S.performance(fs.film), bytes)
    if (token != null) dom.midi_command(owner, token, "volume", 0.8)
    return token
}

fn chapters_markup(film) {
    let total = max(1.0, film.duration_ms);
    join([for (k, ch in film.chapters) (let f = ch.start_beat * F.BEAT_MS / total,
        format([<button type:"button", class:"film-chapter", 'data-film-chapter':string(k), 'data-film-seek':string(ch.start_beat * F.BEAT_MS),
            style:"left:" ++ pct(f) ++ ";", title:ch.title ++ " · " ++ F.clock_label(ch.start_beat * F.BEAT_MS),
            <span ch.title>>], 'html'))], "")
}

// the player is created by the album_film view; it starts on the next frame (film_start)
pub pn begin(owner, ctx, ids, title) {
    let film = F.plan(ctx.lib, ids, title)
    let deck = F.deck(ctx.lib, film, ctx.dir_url)
    let screen = dom.viewport_size(owner)
    let width = (if (screen == null) 1400.0 else screen.width)
    let height = max(240.0, (if (screen == null) 900.0 else screen.height) - CHROME_H)
    updates.text(owner, "film-title", title)
    updates.text(owner, "film-length", F.clock_label(film.duration_ms))
    updates.attr(node(owner, "film-track"), "aria-valuemax", string(int(film.duration_ms / 1000.0)))
    updates.markup(node(owner, "film-ticks"), chapters_markup(film))
    dom.dispatch(node(owner, "film-stage"), {type:"album_film_open", bubbles:false, detail:{deck:deck, width:width, height:height}})
    return {film:film, sound:true, token:null, drag:null, frame:dom.request_frame(owner, "film_start")}
}

pub pn start(owner, fs, now_ms) {
    send(owner, {command:"play"})
    let opened = {*:fs, token:open_sound(owner, fs)}
    let c = paint(owner, opened, now_ms)
    return {*:sync_sound(owner, opened, c), frame:dom.request_frame(owner, "film_frame")}
}

// one bar frame; frames continue only while the film plays
pub pn frame(owner, fs, now_ms) {
    let c = paint(owner, fs, now_ms)
    if (c.running) { return {*:fs, frame:dom.request_frame(owner, "film_frame")} }
    // the film ended or was paused by the player: the soundtrack stops with it
    return {*:sync_sound(owner, fs, c), frame:0}
}

pn after(owner, fs, now_ms) {
    let c = paint(owner, fs, now_ms)
    let synced = sync_sound(owner, fs, c)
    if (c.running and fs.frame == 0) { return {*:synced, frame:dom.request_frame(owner, "film_frame")} }
    return synced
}

pub pn toggle(owner, fs, now_ms) {
    send(owner, {command:if (clock(owner, fs, now_ms).running) "pause" else "play"})
    return after(owner, fs, now_ms)
}

// a seek keeps playing or paused as it was
pub pn seek(owner, fs, position_ms, now_ms) {
    send(owner, {command:"seek_time", position_ms:max(0.0, min(fs.film.duration_ms, position_ms))})
    return after(owner, fs, now_ms)
}

pub pn skip(owner, fs, delta_ms, now_ms) {
    return seek(owner, fs, clock(owner, fs, now_ms).position + delta_ms, now_ms)
}

pub pn sound(owner, fs, now_ms) {
    let next = {*:fs, sound:not fs.sound}
    updates.attr(node(owner, "film-sound"), "aria-pressed", next.sound)
    return sync_sound(owner, next, clock(owner, next, now_ms))
}

// the scrubber: a press pauses and seeks, a drag follows the pointer, release resumes if it was playing
fn track_time(owner, fs, x) {
    let box = dom.bounding_box(node(owner, "film-track"));
    max(0.0, min(1.0, (x - box.x) / max(1.0, box.width))) * fs.film.duration_ms
}
pub pn press(owner, fs, evt, now_ms) {
    let was = clock(owner, fs, now_ms).running
    if (was) send(owner, {command:"pause"})
    return seek(owner, {*:fs, drag:{resume:was}}, track_time(owner, fs, evt.clientX), now_ms)
}
pub pn drag(owner, fs, evt, now_ms) {
    return seek(owner, fs, track_time(owner, fs, evt.clientX), now_ms)
}
pub pn release(owner, fs, now_ms) {
    if (fs.drag.resume) send(owner, {command:"play"})
    return after(owner, {*:fs, drag:null}, now_ms)
}

pub pn close(owner, fs) {
    if (fs.token != null) dom.audio_close(owner, fs.token)
    if (fs.frame > 0) dom.cancel_frame(owner, fs.frame)
    dom.dispatch(node(owner, "film-stage"), {type:"album_film_close", bubbles:false, detail:{}})
    return null
}
