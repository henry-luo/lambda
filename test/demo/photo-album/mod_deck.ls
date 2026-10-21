// Slideshow decks for lambda.slide (vibe/Lambda_Demo_Photo_Album.md §3.6), built as pure values.
// A deck is Mark data with a <presentation> root (SLD1); photo paths are `prefix` + "photos/<file>",
// so a deck saved in <corpus>/slideshows/ uses prefix "../".
import L: .mod_library

pub let W = 1280
pub let H = 720
pub let MAX_PHOTOS = 120
pub let THEMES = [{key:"dark", title:"Gallery, dark"}, {key:"light", title:"Gallery, light"}, {key:"postcard", title:"Postcard"}]
pub let TRANSITIONS = ["fade", "push", "slide"]

fn portrait(p) => p.aspect < 0.9

// Slide plan: every sixth slide shows four photos and then zooms into the first with morph;
// consecutive portraits pair up; a single portrait gets a caption panel; landscapes go full-bleed.
fn step(lib, ids, i, n) {
    let rest = len(ids) - i;
    if (rest <= 0) []
    else if (n % 6 == 5 and rest >= 4) [{kind:"grid", ids:slice(ids, i, i + 4)}, {kind:"zoom", ids:[ids[i]]}, *step(lib, ids, i + 4, n + 2)]
    else if (portrait(L.photo(lib, ids[i])) and rest >= 2 and portrait(L.photo(lib, ids[i + 1])))
        [{kind:"two-up", ids:[ids[i], ids[i + 1]]}, *step(lib, ids, i + 2, n + 1)]
    else if (portrait(L.photo(lib, ids[i]))) [{kind:"captioned", ids:[ids[i]]}, *step(lib, ids, i + 1, n + 1)]
    else [{kind:"full", ids:[ids[i]]}, *step(lib, ids, i + 1, n + 1)]
}

pub fn plan(lib, ids) => [{kind:"title", ids:slice(ids, 0, 1)}, *step(lib, slice(ids, 0, MAX_PHOTOS), 0, 1), {kind:"credits", ids:slice(ids, 0, MAX_PHOTOS)}]

fn years(lib, ids) {
    let ys = [for (id in ids) L.photo(lib, id).year]
    let lo = min(ys)
    let hi = max(ys);
    if (lo == hi) string(lo) else string(lo) ++ "–" ++ string(hi)
}

fn place(lib, p) => join([for (part in [L.album_name(lib, p), L.location_label(p)] where part != "") part], " · ")

// a photo, or its average colour when the file is not downloaded
fn picture(p, id, box, prefix, fit, morph = null) => if (p.has_photo)
    <image id:id, src:prefix ++ "photos/" ++ p.file, x:box[0], y:box[1], width:box[2], height:box[3], fit:symbol(fit),
        *:(if (morph != null) {morph_id:morph} else {})>
    else <shape id:id, kind:'rect', x:box[0], y:box[1], width:box[2], height:box[3], fill:p.color or "#444444",
        *:(if (morph != null) {morph_id:morph} else {})>

// speaker notes: what, when and where, the camera, and the credit its license asks for
fn note_lines(lib, p) => [
    <p L.date_label(p) ++ " at " ++ L.time_label(p) ++ (if (place(lib, p) != "") " · " ++ place(lib, p) else "")>,
    <p join([for (part in [L.camera_label(p).body, L.camera_label(p).exposure] where part != "") part], " · ")>,
    <p "Photo: " ++ p.author ++ " · " ++ p.license ++ (if (p.source_url != "") " · " ++ p.source_url else "")>]
fn notes(lib, p) => <notes *note_lines(lib, p)>

fn caption_cue(targets) => <cue start:'entry', <parallel
    *[for (t in targets) <effect target:t, kind:'fly-in', from:'bottom', distance:40, duration:450, easing:'ease-out-cubic'>]>>

fn slide_for(lib, s, i, options) {
    let prefix = options.prefix
    let transition = symbol(options.transition)
    let sid = "s" ++ string(i);
    if (s.kind == "title") (
        let p = L.photo(lib, s.ids[0]),
        <slide id:sid, title:options.title,
            (picture(p, "hero", [0, 0, W, H], prefix, "cover"));
            <shape id:"scrim", kind:'rect', x:0, y:0, width:W, height:H, fill:"#000000", opacity:0.42>;
            <text id:"title", x:80, y:420, width:1120, height:110, font_size:64, color:"#ffffff", style:"font-weight:700;", options.title>;
            <text id:"subtitle", x:80, y:540, width:1120, height:50, font_size:26, color:"#e6e6e6",
                string(len(options.ids)) ++ " photos · " ++ years(lib, options.ids)>;
            <cue start:'entry', <sequence <effect target:"title", kind:'fade-in', duration:500>
                <effect target:"subtitle", kind:'fly-in', from:'bottom', distance:30, duration:400>>>;
            <notes <p "Slideshow of " ++ string(len(options.ids)) ++ " photos from Wikimedia Commons.">>>)
    else if (s.kind == "full" or s.kind == "zoom") (
        let p = L.photo(lib, s.ids[0]),
        <slide id:sid, title:p.title, transition:if (s.kind == "zoom") 'morph' else transition,
            (picture(p, "photo", [0, 0, W, H], prefix, "cover", if (s.kind == "zoom") "m" ++ string(p.id) else null));
            <shape id:"scrim", kind:'rect', x:0, y:600, width:W, height:120, fill:"#000000", opacity:0.45>;
            <text id:"caption", x:60, y:614, width:1160, height:48, font_size:34, color:"#ffffff", style:"font-weight:600;", p.title>;
            <text id:"meta", x:60, y:664, width:1160, height:34, font_size:20, color:"#dddddd",
                L.date_label(p) ++ (if (place(lib, p) != "") " · " ++ place(lib, p) else "")>;
            (caption_cue(["caption", "meta"]));
            (notes(lib, p))>)
    else if (s.kind == "captioned") (
        let p = L.photo(lib, s.ids[0]),
        <slide id:sid, title:p.title, transition:transition,
            (picture(p, "photo", [60, 40, 520, 640], prefix, "contain"));
            <text id:"caption", x:640, y:150, width:580, height:130, font_size:44, style:"font-weight:700;", p.title>;
            <text id:"description", x:640, y:300, width:580, height:250, font_size:22, slice(p.description, 0, 260)>;
            <text id:"meta", x:640, y:590, width:580, height:60, font_size:20,
                L.date_label(p) ++ (if (place(lib, p) != "") " · " ++ place(lib, p) else "")>;
            <cue start:'entry', <parallel <effect target:"caption", kind:'fade-in', duration:400>
                <effect target:"description", kind:'wipe-in', from:'left', duration:500>>>;
            (notes(lib, p))>)
    else if (s.kind == "two-up") (
        let a = L.photo(lib, s.ids[0]),
        let b = L.photo(lib, s.ids[1]),
        <slide id:sid, title:a.title ++ " · " ++ b.title, transition:transition,
            (picture(a, "left", [60, 40, 560, 580], prefix, "contain"));
            (picture(b, "right", [660, 40, 560, 580], prefix, "contain"));
            <text id:"left-caption", x:60, y:636, width:560, height:44, font_size:22, a.title>;
            <text id:"right-caption", x:660, y:636, width:560, height:44, font_size:22, b.title>;
            (caption_cue(["left-caption", "right-caption"]));
            <notes *[*note_lines(lib, a), *note_lines(lib, b)]>>)
    else if (s.kind == "grid") (
        let cells = [[40, 40, 590, 310], [650, 40, 590, 310], [40, 370, 590, 310], [650, 370, 590, 310]],
        <slide id:sid, title:"Four photos", transition:transition,
            *[for (k, id in s.ids) (let p = L.photo(lib, id),
                picture(p, "g" ++ string(k), cells[k], prefix, "cover", if (k == 0) "m" ++ string(p.id) else null))];
            <notes *[for (id in s.ids) <p L.photo(lib, id).title ++ " · " ++ L.photo(lib, id).author ++ " · " ++ L.photo(lib, id).license>]>>)
    else
        <slide id:sid, title:"Photo credits", transition:'fade',
            <text id:"title", role:'title', "Photo credits">;
            <content id:"credits", x:80, y:170, width:1120, height:500,
                <div style:"columns:2;column-gap:40px;font-size:15px;line-height:1.45;",
                    *[for (id in s.ids) (let p = L.photo(lib, id),
                        <div <strong p.title>;" — " ++ p.author ++ " (" ++ p.license ++ ")">)]>>;
            <notes <p "Every photo's author, license and source page are in the notes of its slide.">>>
}

// The postcard theme frames every slide with a paper border and a stamp. A master's objects
// compose beneath the slide's own, under a full-bleed photo, so the frame is appended per slide.
let POSTCARD = [
    <shape id:"postcard-frame", kind:'rect', x:12, y:12, width:1256, height:696, fill:"none", stroke:"#f4efe6", stroke_width:24>,
    <text id:"postcard-stamp", x:1090, y:34, width:150, height:44, font_size:16, color:"#9b2c2c", rotation:6,
        style:"border:2px solid #9b2c2c;border-radius:4px;text-align:center;font-weight:700;background:#fffaf0;", "LAMBDA ✉">]
fn trailing(c) => c is element and (name(c) == 'cue' or name(c) == 'notes')
fn framed(s) => <slide *:map(s), *[for (c in content(s) where trailing(c) == false) c]; *[for (o in POSTCARD) o];
    *[for (c in content(s) where trailing(c)) c]>

// options: {title, ids, theme: "dark"|"light"|"postcard", transition: "fade"|"push"|"slide", dwell_ms, prefix}
pub fn deck(lib, options) {
    let slides = plan(lib, options.ids)
    let postcard = options.theme == "postcard";
    <presentation id:"album", title:options.title, width:W, height:H,
        theme:if (options.theme == "dark") 'midnight' else 'paper',
        autostart:true, autoplay_dwell_ms:options.dwell_ms,
        *[for (i, s in slides) (let built = slide_for(lib, s, i, options), if (postcard) framed(built) else built)]>
}

// An embedded player resolves image sources against the album page, not the deck file, so a
// loaded deck's relative image paths are resolved against the deck's folder first. Iteration
// uses content(): `for` over an element also yields its attribute values.
fn resolve_object(o, base) => if (not (o is element)) o
    else if (name(o) == 'image' and o.src is string) <image *:{*:map(o), src:url_resolve(base, o.src)}>
    else if (name(o) == 'group') <group *:map(o), *[for (k in content(o)) resolve_object(k, base)]>
    else o
fn resolve_child(c, base) => if (not (c is element)) c
    else if (name(c) == 'slide') <slide *:map(c), *[for (o in content(c)) resolve_object(o, base)]>
    else if (name(c) == 'master') <master *:map(c), *[for (o in content(c)) resolve_object(o, base)]>
    else c
pub fn resolve_sources(deck, base) => <presentation *:map(deck), *[for (c in content(deck)) resolve_child(c, base)]>

// file name for a deck title: its lower-case letter and digit runs joined by "-"
type SlugWord = \(("a" to "z" | "0" to "9")+)
pub fn file_name(title) {
    let slug = slice(join([for (m in find(lower(title), SlugWord)) m.value], "-"), 0, 48);
    (if (slug == "") "slideshow" else slug) ++ ".slides"
}
