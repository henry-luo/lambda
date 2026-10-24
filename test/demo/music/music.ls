// Run: ./lambda.exe demo music
// A score player: a library of music sheets on the left, and the chosen one
// engraved on A4 pages by lambda.music, played through its SoundFont player
// with the sounding notes and lyric syllables highlighted as they play.
import dom
import music: lambda.music
import resources: ~~.mod_resources

let BASE = url_resolve(resources.entry_uri()^, "./")
let CSS = input(url_resolve(BASE, "music.css"), 'text')^
// A4 portrait in CSS pixels, as the music package's samples lay it out
let LAYOUT = {width: 210.0 * 96.0 / 25.4, page_height: 297.0 * 96.0 / 25.4, mode: 'pages', scale: 0.72}

// the library, in showcase order; scores/ holds the MusicXML (README)
let SHEETS = [
    {id: "ode-to-joy", file: "ode-to-joy.musicxml", about: "Piano · melody over block chords"},
    {id: "twinkle", file: "twinkle.musicxml", about: "Piano · with lyrics"},
    {id: "morning-song", file: "lyrics.musicxml", about: "Piano · two verses and a repeat"},
    {id: "morning-study", file: "a4-study.musicxml", about: "Piano · five systems on one page"},
    {id: "two-passes", file: "playback.musicxml", about: "Two instruments · repeats and endings"}]

fn clock(seconds) => (let s = int(round(seconds)), string(int(floor(s / 60))) ++ ":" ++ (if (s % 60 < 10) "0" else "") ++ string(s % 60))

// A sheet with its score and what the library shows of it; a score that
// cannot be read stays listed with the reason.
fn entry(s) {
    let score = music.load(url_resolve(BASE, "scores/" ++ s.file)) ^ { null }
    let seconds = if (score == null) null else ((music.performance(score) ^ { null }) or {seconds: null}).seconds;
    {*: s, score: score, title: if (score == null) s.file else score.title or s.file,
     composer: if (score == null or score.composer == null or score.composer == "") "" else score.composer,
     bars: if (score == null) 0 else len(score.parts[0].measures), seconds: seconds}
}
let LIBRARY = [for (s in SHEETS) entry(s)]

fn sheet_of(id) => [for (s in LIBRARY where s.id == id) s][0]
fn details(s) => s.about ++ " · " ++ string(s.bars) ++ " bars" ++ (if (s.seconds == null) "" else " · " ++ clock(s.seconds))

// a small staff with a note for each library row
fn staff_icon() => <svg class:"sheet-icon", viewBox:"0 0 28 28", 'aria-hidden':"true",
    <path d:"M3 8 H25 M3 12 H25 M3 16 H25 M3 20 H25", stroke:"currentColor", 'stroke-width':"1", opacity:".45">;
    <ellipse cx:12, cy:18, rx:3.4, ry:2.5, transform:"rotate(-20 12 18)", fill:"currentColor">;
    <path d:"M15.2 17.2 V5.5", stroke:"currentColor", 'stroke-width':"1.4">;
    <path d:"M15.2 5.5 C18 7 19.5 8 19 11", fill:"none", stroke:"currentColor", 'stroke-width':"1.4">>

fn sheet_row(s, chosen) => <button type:"button", class:"sheet" ++ (if (chosen) " selected" else ""), 'data-sheet':s.id,
    *:(if (chosen) {'aria-current':"true"} else {}), *:(if (s.score == null) {disabled:""} else {}),
    (staff_icon());
    <span class:"sheet-text",
        <span class:"sheet-title", s.title>
        <span class:"sheet-composer", if (s.score == null) "Cannot read this score" else s.composer>
        <span class:"sheet-details", details(s)>>>

fn player(s) => if (s.score == null) <p class:"stage-error", "This score cannot be read.">
    else (music.player(s.score, {layout: LAYOUT}) ^ { <p class:"stage-error", "This score cannot be engraved."> })

// The library is a fixed column and the player fills the window's full height
// beside it: the engraved page carries the title and composer itself.
view <music_app> state selected: SHEETS[0].id {
    let s = sheet_of(selected);
    <body id:"music", 'data-selected':selected, 'data-title':s.title,
        <aside class:"library",
            <header class:"library-head", <h1 "Music">; <p (string(len(LIBRARY)) ++ " scores")>>
            <nav class:"sheets", 'aria-label':"Scores", for (x in LIBRARY) sheet_row(x, x.id == selected)>
            <footer class:"library-foot", "MusicXML, engraved and played by lambda.music">>
        <main class:"stage", (player(s))>>
}
on click(evt) {
    let row = dom.closest(evt.target, "[data-sheet]")
    if (row == null) { return 'pass' }
    // the player closes its audio session when its score changes
    selected = dom.get_attribute(row, "data-sheet")
    return 'handled'
}

<html lang:"en",
    <head <meta charset:"UTF-8"><title "Music — Lambda"><style CSS>>
    apply(<music_app>)>
