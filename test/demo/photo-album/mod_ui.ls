// Pure markup for the photo album (S12.1.3): the shell, grid rows, album cards and the viewer.
import L: .mod_library
import G: .mod_grid
import D: .mod_deck
import CU: .mod_collage_ui
import BU: .mod_book_ui

let strokes = {
    library:"M3 3h7v7H3z M14 3h7v7h-7z M3 14h7v7H3z M14 14h7v7h-7z",
    albums:"M4 6h16v13H4z M7 3h10 M9 10l3 3 5-5",
    heart:"M12 20s-7-4.4-7-10a4 4 0 0 1 7-2.6A4 4 0 0 1 19 10c0 5.6-7 10-7 10z",
    search:"M10 3a7 7 0 1 0 0 14 7 7 0 0 0 0-14 M15 15l6 6",
    close:"M6 6l12 12 M18 6L6 18",
    prev:"M15 5l-7 7 7 7", next:"M9 5l7 7-7 7",
    info:"M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18 M12 11v6 M12 7.5v.5",
    plus:"M12 5v14 M5 12h14", minus:"M5 12h14",
    strip:"M3 7h18v10H3z M7 7v10 M12 7v10 M17 7v10",
    album:"M4 4h16v16H4z M4 15l5-5 4 4 3-3 4 4",
    collage:"M3 3h8v11H3z M13 3h8v6h-8z M13 11h8v10h-8z M3 16h8v5H3z",
    book:"M12 6c-2-1.5-5-2-8-2v14c3 0 6 .5 8 2 2-1.5 5-2 8-2V4c-3 0-6 .5-8 2z M12 6v14",
    play:"M4 5h16v11H4z M10 8.5v4.5l4-2.25z M9 20h6 M12 16v4"
}
pub fn icon(graphic, size = 20) => <svg width:string(size),height:string(size),viewBox:"0 0 24 24",'aria-hidden':"true",
    <path d:strokes[graphic] or strokes.library,fill:"none",stroke:"currentColor",'stroke-width':1.7,
        'stroke-linecap':"round",'stroke-linejoin':"round">>
fn tool(id, title, graphic, action = id) => <button id:id,type:"button",class:"tool",'data-action':action,title:title,'aria-label':title,(icon(graphic))>
fn px(v) => string(round(v * 100) / 100) ++ "px"

// ---------------------------------------------------------------- grid

pub fn tile(p, t, selected, favourite, focused) => <div class:"tile",id:"tile-" ++ string(p.id),'data-id':string(p.id),role:"option",
    'aria-selected':string(selected),'data-favorite':string(favourite),'data-focused':string(focused),title:p.title,
    style:"left:" ++ px(t.x) ++ ";width:" ++ px(t.w) ++ ";background-color:" ++ (p.color or "#d8dce2"),
    (if (p.thumb_src != "") <img src:p.thumb_src,alt:"",draggable:"false"> else null);
    <span class:"tile-check",'aria-hidden':"true","✓">;
    <span class:"tile-heart",'aria-hidden':"true",(icon("heart", 14))>>

pub fn row(lib, lay, i, st) {
    let r = lay.rows[i];
    <div class:"grid-row",'data-row':string(i),style:"top:" ++ px(r.y) ++ ";height:" ++ px(r.h),
        *[for (t in r.tiles) tile(L.photo(lib, t.id), t, contains(st.selected, t.id), contains(st.favorites, t.id), st.focus == t.id)]>
}

pub fn header(lay, i) {
    let h = lay.headers[i];
    <div class:"grid-header",'data-header':string(i),style:"top:" ++ px(h.y),
        <h2 h.label>;<span class:"grid-count",string(h.count) ++ (if (h.count == 1) " photo" else " photos")>>
}

// years whose label would sit within MIN_GAP of an earlier shown label are skipped
let MIN_GAP = 0.035
fn spaced(fractions, i, last_shown) => if (i >= len(fractions)) []
    else if (i == 0 or fractions[i] - last_shown >= MIN_GAP) [i, *spaced(fractions, i + 1, fractions[i])]
    else spaced(fractions, i + 1, last_shown)

pub fn scrubber(lay) {
    let total = max(1.0, lay.height)
    let years = [for (i, h in lay.headers where i == 0 or slice(lay.headers[i - 1].key, 0, 4) != slice(h.key, 0, 4)) h]
    let shown = spaced([for (h in years) h.y / total], 0, 0.0);
    <div class:"scrub-marks",*[for (i in shown) <span class:"scrub-year",
        style:"top:" ++ string(round(years[i].y / total * 1000) / 10) ++ "%",slice(years[i].key, 0, 4)>]>
}

pub fn albums(lib) => <div class:"album-cards",*[for (a in lib.albums) (
    let cover = if (a.cover > 0) L.photo(lib, a.cover) else null,
    <button type:"button",class:"album-card",'data-album':a.key,
        <span class:"album-cover",style:"background-color:" ++ (cover.color or "#d8dce2"),
            (if (cover != null and cover.thumb_src != "") <img src:cover.thumb_src,alt:""> else null)>;
        <strong a.title>;
        <span class:"album-meta",string(a.count) ++ " photos · " ++
            (if (a.first_year == a.last_year) string(a.first_year) else string(a.first_year) ++ "–" ++ string(a.last_year))>>)]>

pub fn empty(query) => <div class:"empty",(icon(if (trim(query) != "") "search" else "heart", 40));
    <h2 if (trim(query) != "") "No matching photos" else "No favorites yet">
    <p if (trim(query) != "") "Try another word, a place, a camera or a year." else "Select photos and press F, or use the heart in the viewer.">>

pub fn banner(lib, command) => if (lib.downloaded == len(lib.photos)) null
    else <div class:"banner",role:"status",
        <strong if (lib.downloaded == 0) "The photos are not downloaded yet." else
            string(lib.downloaded) ++ " of " ++ string(len(lib.photos)) ++ " photos are downloaded.">;
        <span "Tiles show each photo's colour until it is fetched. From the repository root, run">;
        <code command>>

// ---------------------------------------------------------------- viewer

pub fn info(lib, p) {
    let cam = L.camera_label(p)
    let album = L.album_name(lib, p)
    fn item(label, value) => if (value == null or value == "") null else <div class:"info-item",<dt label><dd value>>;
    <div class:"info-body",<h2 p.title>;
        (if (p.description != "") <p class:"info-description",p.description> else null);
        <dl
            (item("Taken", L.date_label(p) ++ " at " ++ L.time_label(p)));
            (item("Album", album));
            (item("Camera", cam.body));
            (item("Lens", cam.lens));
            (item("Exposure", cam.exposure));
            (item("Size", string(p.width) ++ " × " ++ string(p.height)));
            (item("Location", L.location_label(p)));
            (item("Author", p.author));
            (item("License", p.license));
            (item("Source", p.source_url))>;
        (if (len(p.keywords or []) > 0) <div class:"info-keywords",*[for (k in p.keywords) <span class:"chip",k>]> else null)>
}

pub fn filmstrip(lib, ids, id) {
    let at = index_of(ids, id) or 0
    let first = max(0, min(at - 7, len(ids) - 15));
    <div class:"strip-tiles",*[for (j in first to min(len(ids) - 1, first + 14)) (
        let p = L.photo(lib, ids[j]),
        <button type:"button",class:"strip-tile",'data-strip':string(p.id),'aria-current':string(p.id == id),title:p.title,
            style:"background-color:" ++ (p.color or "#333"),
            (if (p.thumb_src != "") <img src:p.thumb_src,alt:""> else null)>)]>
}

// ---------------------------------------------------------------- shell

pub fn tree(lib, command) => <div id:"album",class:"album",'data-mode':"library",'data-viewer':"false",'data-selected':"0",
    <aside class:"sidebar",
        <div class:"brand",(icon("album", 22));<strong "Photos"><small "LAMBDA">>;
        <nav 'aria-label':"Library",
            <button type:"button",class:"nav",id:"nav-library",'data-nav':"library",(icon("library"));<span "Library">;<small string(len(lib.photos))>>;
            <button type:"button",class:"nav",id:"nav-favorites",'data-nav':"favorites",(icon("heart"));<span "Favorites">;<small id:"favorite-count","0">>;
            <button type:"button",class:"nav",id:"nav-albums",'data-nav':"albums",(icon("albums"));<span "Albums">;<small string(len(lib.albums))>>>;
        <h3 class:"sidebar-heading","Albums">;
        <nav class:"album-nav",'aria-label':"Albums",*[for (a in lib.albums) <button type:"button",class:"nav",id:"nav-album-" ++ a.key,
            'data-album':a.key,<span a.title>;<small string(a.count)>>]>;
        <p class:"sidebar-credit","Photos: Wikimedia Commons featured pictures, credited in each photo's info.">>;
    <section class:"workspace",
        <header class:"toolbar",
            <div class:"heading",<h1 id:"title","Library">;<p id:"status",role:"status","">>;
            <label class:"search",(icon("search", 16));<input id:"search",type:"search",placeholder:"Search titles, places, cameras, years",'aria-label':"Search photos">>;
            <button id:"book-button",type:"button",class:"pill secondary",'data-action':"book-scope",title:"Album book of the photos shown",
                (icon("book", 18));<span "Album book">>;
            <button id:"collage-button",type:"button",class:"pill secondary",'data-action':"collage-scope",title:"Collage of the photos shown (first 30)",
                (icon("collage", 18));<span "Collage">>;
            <button id:"slideshow",type:"button",class:"pill",'data-action':"slideshow-scope",title:"Slideshow of the photos shown",
                (icon("play", 18));<span "Slideshow">>;
            <div class:"zoom",(tool("zoom-out","Smaller tiles","minus"));
                <input id:"zoom",type:"range",min:"80",max:"300",step:"20",value:"160",'aria-label':"Tile size">;
                (tool("zoom-in","Larger tiles","plus"))>>;
        (banner(lib, command));
        <div class:"stage",
            apply(<album_scrollport rendered:<div id:"port",class:"port",tabindex:"0",role:"listbox",'aria-label':"Photos",'aria-multiselectable':"true",
                <div id:"grid",class:"grid">;
                <div id:"cards",class:"cards",style:"display:none",(albums(lib))>;
                <div id:"empty",style:"display:none">>>);
            <div id:"sticky",class:"sticky",'aria-hidden':"true">;
            <div id:"scrubber",class:"scrubber",'aria-label':"Jump to a year",<div id:"scrub-marks">;<div id:"scrub-thumb",class:"scrub-thumb">>;
            // floats over the grid: showing it must not move the photos under the pointer
            <div id:"selection-bar",class:"selection-bar",style:"display:none",
                <span id:"selection-count","">;
                <button type:"button",'data-action':"book-selection",id:"selection-book","Album book">;
                <button type:"button",'data-action':"collage-selection",id:"selection-collage","Collage">;
                <button type:"button",'data-action':"slideshow-selection",id:"selection-slideshow","Slideshow">;
                <button type:"button",'data-action':"favorite-selected",id:"favorite-selected","Add to Favorites">;
                <button type:"button",'data-action':"select-all","Select All">;
                <button type:"button",'data-action':"clear-selection","Clear">>>
    >;
    <div id:"viewer",class:"viewer",style:"display:none",role:"dialog",'aria-modal':"true",'aria-labelledby':"viewer-title",tabindex:"-1",
        <header class:"viewer-bar",
            (tool("viewer-close","Close (Esc)","close","close-viewer"));
            <div class:"viewer-heading",<h2 id:"viewer-title">;<p id:"viewer-subtitle">>;
            <div class:"viewer-tools",
                (tool("viewer-slideshow","Slideshow from this photo","play","slideshow-viewer"));
                (tool("viewer-favorite","Favorite (F)","heart","toggle-favorite"));
                (tool("viewer-zoom-out","Zoom out (−)","minus","viewer-zoom-out"));
                <span id:"viewer-zoom-label",class:"zoom-label","Fit">;
                (tool("viewer-zoom-in","Zoom in (+)","plus","viewer-zoom-in"));
                (tool("viewer-strip","Filmstrip","strip","toggle-strip"));
                (tool("viewer-info","Info (I)","info","toggle-info"))>>;
        <div class:"viewer-body",
            <div id:"viewer-stage",class:"viewer-stage",
                <img id:"viewer-image",class:"viewer-image",alt:"",draggable:"false">;
                <button type:"button",id:"viewer-prev",class:"viewer-nav prev",'data-action':"prev",'aria-label':"Previous photo",(icon("prev", 28))>;
                <button type:"button",id:"viewer-next",class:"viewer-nav next",'data-action':"next",'aria-label':"Next photo",(icon("next", 28))>;
                <div id:"prefetch",class:"prefetch",'aria-hidden':"true">>;
            <aside id:"viewer-info-panel",class:"info",style:"display:none">>;
        <div id:"filmstrip",class:"filmstrip">>;
    (show_sheet());
    (CU.editor());
    (BU.shell());
    <div id:"cl-ghost",class:"cl-ghost",style:"display:none",'aria-hidden':"true">;
    <div id:"show-layer",class:"show-layer",style:"display:none",role:"dialog",'aria-modal':"true",'aria-labelledby':"show-title",
        <header class:"show-bar",<h2 id:"show-title">;<span id:"show-saved",class:"show-saved">;
            (tool("show-close","Close slideshow (Esc)","close","show-close"))>;
        apply(<album_show>)>
>

// slideshow options: the deck file is saved under the corpus's slideshows/ folder
fn select(id, label, options, chosen) => <label class:"field",<span label>;
    <select id:id,*[for (o in options) <option value:o.key,*:(if (o.key == chosen) {selected:""} else {}),o.title>]>>
pub fn show_sheet() => <div id:"show-sheet",class:"sheet-layer",style:"display:none",role:"dialog",'aria-modal':"true",'aria-labelledby':"sheet-title",
    <div class:"sheet",<h2 id:"sheet-title","Slideshow">;<p id:"show-count",class:"sheet-note">;
        <label class:"field",<span "Name">;<input id:"show-name",type:"text",autocomplete:"off">>;
        (select("show-theme", "Theme", D.THEMES, "dark"));
        (select("show-transition", "Transition", [for (t in D.TRANSITIONS) {key:t, title:t}], "fade"));
        (select("show-dwell", "Seconds per slide", [for (n in [2, 3, 4, 6, 8]) {key:string(n), title:string(n)}], "4"));
        <p id:"show-status",class:"sheet-status",role:"status">;
        <div class:"sheet-actions",<button type:"button",'data-action':"show-cancel",id:"show-cancel","Cancel">;
            <button type:"button",'data-action':"show-save",id:"show-save","Save as .slides">;
            <button type:"button",class:"primary",'data-action':"show-present",id:"show-present","Present">>>>
