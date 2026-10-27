// Reader policy and mutation live in Lambda views/handlers (D7.2.4, S12.1.3).
import dom
import c: .common
import chapters: .chapter
import locations: .location
import store: .state
import page: .page
import flip: .flip

// one palette for the bar's swatches, the sidebar list and the chapter's marks
let swatches = {yellow:"#ffd84d",green:"#7fd16b",blue:"#6fb3ff",pink:"#ff8fbd",purple:"#b98cff"}
let marks = {yellow:"rgba(255,208,0,.4)",green:"rgba(96,196,72,.36)",blue:"rgba(64,150,255,.32)",
    pink:"rgba(255,92,150,.32)",purple:"rgba(160,96,255,.32)"}
// filled buttons sit under .ebook-reader so the generic button hover cannot replace their fill
let highlight_chrome = join([for (color in store.colors) ".ebook-reader .ebook-swatch[data-ebook-color=" ++ color ++ "]{background:" ++
    swatches[color] ++ "}.ebook-highlights button[data-ebook-color=" ++ color ++ "]{border-left-color:" ++ swatches[color] ++ "}"],"")
let highlight_marks = "mark.ebook-highlight{color:inherit;border-radius:2px}" ++ join([for (color in store.colors)
    "mark.ebook-highlight[data-ebook-color=" ++ color ++ "]{background:" ++ marks[color] ++ "}"],"")

// Scoped chrome keeps publication styles inside the chapter document.
pub let stylesheet = "
.ebook-reader{--ebook-bg:#f5f4f0;--ebook-panel:#ffffff;--ebook-text:#25332f;--ebook-muted:#77817b;--ebook-line:#e4e7e2;--ebook-hover:#eef2ee;--ebook-accent:#26705c;--ebook-accent-hover:#1b5444;height:100vh;display:flex;flex-direction:column;overflow:hidden;font:14px system-ui,sans-serif;background:var(--ebook-bg);color:var(--ebook-text)}
.ebook-reader[data-theme=sepia]{--ebook-bg:#eee5d4;--ebook-panel:#faf4e8;--ebook-text:#493f32;--ebook-muted:#887b67;--ebook-line:#e4d9c5;--ebook-hover:#eee5d4}
.ebook-reader[data-theme=dark]{--ebook-bg:#161e1b;--ebook-panel:#202b26;--ebook-text:#e4eae5;--ebook-muted:#a0afa5;--ebook-line:#35433b;--ebook-hover:#303e36;--ebook-accent:#8ccbb0;--ebook-accent-hover:#b0e0ca}
.ebook-reader *{box-sizing:border-box}.ebook-reader button,.ebook-reader input{font:inherit;color:inherit}
.ebook-reader button{display:inline-flex;align-items:center;justify-content:center;gap:6px;min-height:34px;padding:7px 10px;border:1px solid transparent;border-radius:7px;background:transparent;line-height:1.2;cursor:pointer}
.ebook-reader button:hover{background:var(--ebook-hover)}.ebook-reader button:disabled{opacity:.35;cursor:default}
.ebook-reader button:focus-visible,.ebook-reader input:focus-visible{outline:2px solid var(--ebook-accent);outline-offset:2px}
.ebook-reader button[aria-pressed=true]{background:var(--ebook-hover);color:var(--ebook-accent)}
.ebook-icon{width:18px;height:18px;flex-shrink:0}
.ebook-reader button{position:relative}.ebook-tip{display:none;position:absolute;top:100%;margin-top:6px;left:50%;transform:translateX(-50%);z-index:30;white-space:nowrap;padding:4px 8px;border-radius:6px;background:#25332f;color:#fff;font:500 12px/1.3 system-ui,sans-serif;letter-spacing:0;text-transform:none;pointer-events:none;box-shadow:0 2px 8px #0003}
.ebook-reader button:hover>.ebook-tip{display:block}.ebook-footer .ebook-tip{top:auto;bottom:100%;margin-top:0;margin-bottom:6px}.ebook-settings-toggle .ebook-tip{left:auto;right:0;transform:none}.ebook-toolbar{display:flex;align-items:center;gap:10px;min-height:64px;padding:12px 20px;background:var(--ebook-panel);border-bottom:1px solid var(--ebook-line)}
.ebook-history{display:flex;gap:2px;padding-right:12px;border-right:1px solid var(--ebook-line)}.ebook-title{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:15px;font-weight:600}
.ebook-toolbar .ebook-icon-button{width:36px;padding:8px}.ebook-layout-switch{display:flex;gap:2px;padding:2px;margin-right:6px;border:1px solid var(--ebook-line);border-radius:9px}.ebook-layout-switch .ebook-icon-button{width:34px;min-height:30px;padding:6px;border:1px solid transparent}.ebook-reader[data-fixed=true] .ebook-layout-switch{display:none}.ebook-settings-toggle{font-family:Georgia,serif!important;font-size:19px!important}
.ebook-settings,.ebook-search-panel{display:none;flex-shrink:0;padding:18px 24px;background:var(--ebook-panel);border-bottom:1px solid var(--ebook-line)}
.ebook-settings[data-open=true]{display:flex;flex-wrap:wrap;gap:20px 32px}.ebook-search-panel[data-open=true]{display:flex;align-items:center;gap:10px}
.ebook-setting-group{display:flex;flex-direction:column;gap:8px}.ebook-setting-label{font-size:11px;font-weight:600;letter-spacing:1px;text-transform:uppercase;color:var(--ebook-muted)}.ebook-setting-controls{display:flex;align-items:center;gap:4px}.ebook-settings button{border-color:var(--ebook-line);background:var(--ebook-panel);font-size:13px}
.ebook-query{flex:1;min-width:0;max-width:500px;height:36px;padding:8px 12px;border:1px solid var(--ebook-line);border-radius:7px;background:var(--ebook-bg)}.ebook-reader .ebook-search-panel [data-ebook-command=search]{background:var(--ebook-accent);color:var(--ebook-panel)}.ebook-reader .ebook-search-panel [data-ebook-command=search]:hover{background:var(--ebook-accent-hover);color:var(--ebook-panel)}
.ebook-workspace{display:flex;flex:1;min-height:0}.ebook-sidebar{flex-shrink:0;width:244px;overflow:auto;padding:24px 16px;background:var(--ebook-panel);border-right:1px solid var(--ebook-line)}
.ebook-reader[data-sidebar=closed] .ebook-sidebar{display:none}.ebook-book{display:flex;align-items:center;gap:12px;padding:0 8px 24px}.ebook-cover{width:44px;height:62px;object-fit:contain;background:var(--ebook-bg);border:1px solid var(--ebook-line);border-radius:4px}.ebook-book-title{font-size:14px;font-weight:600;line-height:1.4}.ebook-author{margin:5px 0 0;font-size:12px;color:var(--ebook-muted)}
.ebook-sidebar h3{margin:20px 10px 10px;font-size:11px;font-weight:600;letter-spacing:1px;text-transform:uppercase;color:var(--ebook-muted)}.ebook-sidebar ol{list-style:none;margin:0;padding:0}.ebook-sidebar ol ol{padding-left:12px}.ebook-sidebar li{margin:3px 0}
.ebook-sidebar button{display:flex;width:100%;justify-content:flex-start;text-align:left;padding:10px 12px;font-size:13px;line-height:1.4}.ebook-sidebar button[aria-current=location]{background:var(--ebook-hover);color:var(--ebook-accent);font-weight:600}.ebook-sidebar li>span{display:block;padding:10px 12px}
.ebook-bookmarks:empty::after{content:'Your saved places appear here.';display:block;padding:4px 12px;color:var(--ebook-muted);font-size:12px;line-height:1.5}
.ebook-highlights:empty::after{content:'Select a passage to highlight it.';display:block;padding:4px 12px;color:var(--ebook-muted);font-size:12px;line-height:1.5}
.ebook-highlights button{flex-direction:column;align-items:stretch;gap:4px;border-left:4px solid transparent;border-radius:4px 7px 7px 4px}.ebook-highlight-quote{font-family:Georgia,serif;font-size:13px;white-space:normal;overflow-wrap:anywhere}.ebook-highlight-where{font-size:11px;color:var(--ebook-muted)}
.ebook-highlight-bar{position:absolute;z-index:3;display:none;align-items:center;gap:6px;padding:7px 9px;background:var(--ebook-panel);border:1px solid var(--ebook-line);border-radius:12px;box-shadow:0 6px 22px #0000002e}.ebook-highlight-bar[data-open=true]{display:flex}
.ebook-reader .ebook-swatch{width:24px;height:24px;min-height:24px;padding:0;border:2px solid var(--ebook-panel);border-radius:50%;box-shadow:0 0 0 1px var(--ebook-line)}.ebook-reader .ebook-swatch[aria-pressed=true]{box-shadow:0 0 0 2px var(--ebook-text)}
.ebook-unhighlight{display:none!important;margin-left:4px;font-size:12px;min-height:28px!important;color:var(--ebook-muted)}.ebook-highlight-bar[data-mode=edit] .ebook-unhighlight{display:inline-flex!important}
.ebook-content{position:relative;flex:1;min-width:0;display:flex;flex-direction:column;margin:24px 28px;background:var(--ebook-panel);border:1px solid var(--ebook-line);border-radius:8px;overflow:hidden;box-shadow:0 3px 16px #20352908}
.ebook-frame{display:block;width:100%;flex:1;min-height:120px;border:0;background:var(--ebook-panel)}.ebook-results{max-height:170px;overflow:auto}.ebook-results:empty{display:none}.ebook-results button{display:flex;align-items:baseline;gap:12px;width:100%;justify-content:flex-start;text-align:left;border-radius:0;padding:12px 20px;border-bottom:1px solid var(--ebook-line)}
.ebook-result-chapter{flex-shrink:0;min-width:44px;font-size:11px;font-weight:700;letter-spacing:.3px;color:var(--ebook-accent)}.ebook-result-text{flex:1;min-width:0;line-height:1.45}
.ebook-result-match{background:#ffd84d99;color:inherit;border-radius:2px;padding:0 1px;font-weight:600}
.ebook-search-options{display:flex;gap:2px;padding:2px;border:1px solid var(--ebook-line);border-radius:8px}.ebook-search-options button{width:36px;min-height:32px;padding:4px}.ebook-search-options .ebook-icon{width:22px;height:22px}
.ebook-notices{padding:10px 20px;color:var(--ebook-muted);font-size:12px;border-top:1px solid var(--ebook-line)}.ebook-notices:empty{display:none}
.ebook-footer{display:flex;align-items:center;justify-content:space-between;gap:12px;min-height:52px;padding:8px 20px;background:var(--ebook-panel);border-top:1px solid var(--ebook-line)}.ebook-chapter-controls,.ebook-page-controls{display:flex;align-items:center;gap:4px}.ebook-progress{display:flex;align-items:center;gap:16px;font-size:12px;color:var(--ebook-muted)}.ebook-footer button{font-size:12px;min-height:30px}.ebook-page-controls button{width:32px;padding:5px}
@media(max-width:760px){.ebook-toolbar{padding:10px 12px;gap:6px}.ebook-history{padding-right:6px}.ebook-sidebar{width:200px;padding:16px 10px}.ebook-content{margin:12px}.ebook-footer{padding:8px 12px;gap:6px}.ebook-footer .ebook-chapter-controls button{font-size:0;gap:0;padding:5px}.ebook-progress{gap:8px}.ebook-settings{padding:14px 16px;gap:16px}.ebook-title{font-size:14px}}
@media(max-width:540px){.ebook-sidebar{display:none}.ebook-reader[data-sidebar=open] .ebook-sidebar{display:block;width:180px}.ebook-history{display:none}.ebook-content{margin:8px}.ebook-progress{font-size:11px}}
.ebook-option[aria-pressed=true]{border-color:var(--ebook-accent)!important;color:var(--ebook-accent)}
" ++ highlight_chrome ++ page.stylesheet

let icons = {back:"M14 6l-6 6 6 6",forward:"M10 6l6 6-6 6",contents:"M4 5h16M4 12h16M4 19h16",
    search:"M21 21l-5-5M18 10a8 8 0 1 1-16 0 8 8 0 0 1 16 0",bookmark:"M6 3h12v18l-6-4-6 4z",
    previous:"M5 5v14M18 6l-7 6 7 6",next:"M19 5v14M6 6l7 6-7 6",
    // reading layouts: a page with a scroll arrow, one page framed by the window, an open spread
    scroll:"M4 3h10v18H4z M6.5 7h5M6.5 10h5M6.5 13h5M6.5 16h3 M19 4v14 M16.5 15.5l2.5 2.5 2.5-2.5",
    paged:"M8.5 6h7v12h-7z M3 8V3h5 M16 3h5v5 M21 16v5h-5 M8 21H3v-5",
    spread:"M2 6c3-1 6-1 9.5 1v13C8 18 5 18 2 19z M22 6c-3-1-6-1-9.5 1v13c3.5-2 6.5-2 9.5-1z"}
fn icon_svg(children) => <svg class:"ebook-icon",viewBox:"0 0 24 24",fill:"none",stroke:"currentColor",
    ["stroke-width"]:"1.7",["stroke-linecap"]:"round",["stroke-linejoin"]:"round",["aria-hidden"]:"true",*children>
// Radiant shows no native title tooltip and no ::after on buttons, so an icon
// button carries its own tooltip span, shown while hovered (CSS below).
fn tip(label) => <span class:"ebook-tip",["aria-hidden"]:"true",label>
fn button(command,label,icon = null) => <button type:"button",class:if (icon == null) "" else "ebook-icon-button",
    title:label,["aria-label"]:label,["data-ebook-command"]:command,
    if (icon == null) label else [icon_svg([<path d:icons[icon]>]),tip(label)]>
// a preference choice as a toolbar icon; chrome() keeps every .ebook-option's pressed state
// the three reading layouts, shared by the toolbar and the settings panel
fn layout_switch() => <div class:"ebook-layout-switch",role:"group",["aria-label"]:"Reading layout",
    icon_option("mode","scroll","Scroll"); icon_option("mode","paged","Fit to window");
    icon_option("mode","spread","Two pages")>
fn icon_option(pref,value,label) => <button type:"button",class:"ebook-option ebook-icon-button",title:label,["aria-label"]:label,
    ["data-ebook-command"]:"pref",["data-ebook-pref"]:pref,["data-ebook-value"]:value,["aria-pressed"]:"false",
    icon_svg([<path d:icons[value]>]); tip(label)>
// search toggles: letter case, whole words, and the current chapter instead of the book
let search_icons = {case:[<text x:"2",y:"17",['font-size']:"13",['font-weight']:"700",fill:"currentColor",stroke:"none","Aa">],
    word:[<text x:"4",y:"14",['font-size']:"12",['font-weight']:"700",fill:"currentColor",stroke:"none","ab">,<path d:"M3 15v4h18v-4">],
    chapter:[<path d:"M7 3h7l4 4v14H7z">,<path d:"M14 3v4h4">,<path d:"M10 12h5M10 15h5">]}
fn search_option(key,label) => <button type:"button",title:label,["aria-label"]:label,["aria-pressed"]:"false",
    ["data-ebook-command"]:"search-option",["data-ebook-value"]:key,icon_svg(search_icons[key]); tip(label)>
fn contents(nodes) => <ol *[for (node in nodes) <li
    if (node.target != null) <button type:"button",["data-ebook-command"]:"go",
        ["data-ebook-href"]:node.target.href,["data-ebook-fragment"]:c.value(node.target.fragment,""),node.label>
    else <span node.label> if (len(node.children) > 0) contents(node.children)>]>

fn cycle(current,values) {
    let index = c.first([for (i,value in values where value == current) i]);
    values[(c.value(index,0)+1)%len(values)]
}
fn srcdoc(chapter,prefs) {
    // a paper texture shows through a transparent page and sets its own ink
    let paper_ink = page.ink(prefs.texture)
    let colors = if (paper_ink != null) {bg:"transparent",fg:paper_ink}
        else {light:{bg:"#fff",fg:"#202b37"},sepia:{bg:"#f4ecd8",fg:"#46392a"},dark:{bg:"#18222d",fg:"#e6e5df"}}[prefs.theme]
    let margin = page.margin(prefs)
    let css = if (chapter.layout == "fixed") "html,body{margin:0;padding:0}body{position:relative;width:" ++
        string(chapter.viewport.width) ++ "px;height:" ++ string(chapter.viewport.height) ++ "px;transform-origin:0 0}"
        else "html{background:" ++ colors.bg ++ ";color:" ++ colors.fg ++ "}body{margin:" ++ string(margin) ++
        "px auto;padding:0 " ++ string(margin) ++ "px;max-width:48em;font-size:" ++ string(prefs.font_size) ++
        "px!important;line-height:" ++ string(prefs.line_height) ++ "!important;" ++
        (if (prefs.font_family == "publisher") "" else "font-family:" ++ prefs.font_family ++ "!important;") ++
        "}img,svg{max-width:100%;height:auto}.ebook-end{display:block;break-before:column;height:1px;margin:0}" ++
        (if (prefs.theme == "light" and paper_ink == null) "" else "body,p,li,h1,h2,h3,h4,blockquote{color:" ++ colors.fg ++
            "!important;background:" ++ colors.bg ++ "!important}") ++
        (if (paper_ink == null) "" else "a{color:" ++ paper_ink ++ "!important}") ++
        // a column is one page; two columns per viewport make a spread, each viewport step a page turn
        // pages turn by script; the column overflow scrolls without a scrollbar
        (if (prefs.mode == "scroll") "" else "html{overflow:hidden}body{margin:" ++ string(margin) ++ "px;padding:0;max-width:none;width:calc(100vw - " ++
            string(margin*2) ++ "px);height:calc(100vh - " ++ string(margin*2) ++
            "px);column-count:" ++ (if (prefs.mode == "spread") "2" else "1") ++ ";column-fill:auto;column-gap:" ++ string(margin*2) ++ "px}");
    let styled = replace(chapter.html,"</head>","<style id=\"ebook-preferences\">" ++ css ++ highlight_marks ++ "</style></head>")
    // Scrollable overflow ends at the last column's edge, as in browsers, short of
    // a whole viewport, so the last page could not be scrolled into place. Blank
    // trailing columns (one per page, two per spread to round up an odd count)
    // extend the range; page_geometry counts pages from where they start.
    let blanks = if (chapter.layout == "fixed" or prefs.mode == "scroll") 0 else if (prefs.mode == "spread") 2 else 1;
    if (blanks == 0) styled else replace(styled,"</body>",join([for (i in 1 to blanks) "<div class=\"ebook-end\" aria-hidden=\"true\"></div>"],"") ++ "</body>")
}
fn option(pref,value,label) => <button type:"button",class:"ebook-option",["data-ebook-command"]:"pref",
    ["data-ebook-pref"]:pref,["data-ebook-value"]:value,["aria-pressed"]:"false",label>
fn options(pref,choices) => [for (choice in choices) option(pref,choice[0],choice[1])]
fn stage_layout(chapter,prefs) => if (chapter.layout == "fixed") "fixed" else prefs.mode
fn stage_texture(chapter,prefs) => if (chapter.layout == "fixed") "none" else prefs.texture
// the ghost frame keeps the previous page for the turn animation, only when one can play
fn ghost_doc(chapter,prefs) => if (chapter.layout != "fixed" and page.animated(prefs)) srcdoc(chapter,prefs) else ""
// a spread flip also shows the old facing page held and the new one on the leaf's back
fn flip_doc(chapter,prefs) => if (prefs.mode == "spread" and prefs.turn == "flip") ghost_doc(chapter,prefs) else ""
fn ghost_frame(class,doc) => <iframe class:class,title:"",tabindex:"-1",sandbox:"allow-same-origin",srcdoc:doc>
fn initial(book,saved) => if (saved.location != null) saved.location.spine_occurrence
    else c.value(c.first([for (entry in book.reading_order where entry.linear) entry.occurrence]),0)
fn tree(book,saved,chapter) element^ {
    let index = chapter.occurrence
    let cover = if (book.metadata.cover == null) null else
        chapters.resource(book,book.metadata.cover.href) ^ { null };
    <div class:"ebook-reader",tabindex:"0",role:"region",["aria-label"]:"eBook reader",["data-theme"]:saved.preferences.theme,
        <header class:"ebook-toolbar",
            button("contents","Toggle contents","contents");
            <div class:"ebook-history",button("back","Go back","back"); button("forward","Go forward","forward")>
            <span class:"ebook-title",c.value(book.metadata.titles[0],"Untitled")>
            layout_switch()
            button("find","Search book","search");
            button("bookmark","Bookmark this place","bookmark");
            <button type:"button",class:"ebook-settings-toggle",title:"Reading settings",["aria-label"]:"Reading settings",
                ["aria-expanded"]:"false",["data-ebook-command"]:"settings","Aa"; tip("Reading settings")>>
        <div class:"ebook-search-panel",["data-open"]:"false",
            <input class:"ebook-query",type:"search",placeholder:"Find a passage in this book…",["aria-label"]:"Search book">
            <div class:"ebook-search-options",role:"group",["aria-label"]:"Search options",
                search_option("case","Match case"); search_option("word","Whole word only"); search_option("chapter","Current chapter only")>
            button("search","Search")>
        <section class:"ebook-settings",["aria-label"]:"Reading settings",["data-open"]:"false",
            if (chapter.layout != "fixed") <div class:"ebook-setting-group",<span class:"ebook-setting-label","Text size & font">
                <div class:"ebook-setting-controls",button("smaller","A−"); button("larger","A+"); button("font","Publisher font")>>;
            if (chapter.layout != "fixed") <div class:"ebook-setting-group",<span class:"ebook-setting-label","Spacing & margins">
                <div class:"ebook-setting-controls",button("spacing","Line spacing"); button("margin","Margins")>>;
            <div class:"ebook-setting-group",<span class:"ebook-setting-label","Appearance">
                <div class:"ebook-setting-controls",button("theme","Light theme"); button("publisher","Publisher styles")>>
            <div class:"ebook-setting-group",<span class:"ebook-setting-label","Reading layout">
                <div class:"ebook-setting-controls",if (chapter.layout == "fixed") {
                    [button("zoom-out","Zoom −"),button("zoom-in","Zoom +")]
                } else layout_switch()>>
            if (chapter.layout != "fixed") <div class:"ebook-setting-group",<span class:"ebook-setting-label","Paper">
                <div class:"ebook-setting-controls",options("texture",[["none","Plain"],["antique","Antique"],["parchment","Parchment"],
                    ["linen","Linen"],["newsprint","Newsprint"],["ivory","Ivory"]])>>;
            if (chapter.layout != "fixed") <div class:"ebook-setting-group",<span class:"ebook-setting-label","Page turn">
                <div class:"ebook-setting-controls",options("turn",[["none","None"],["slide-left","Slide left"],["slide-up","Slide up"],
                    ["turn","Turn"],["flip","Flip"],["dissolve","Dissolve"]])>>>
        <div class:"ebook-workspace",
            <nav class:"ebook-sidebar",["aria-label"]:"Table of contents",
                <div class:"ebook-book",
                    if (cover.value != null and cover.value != "") <img class:"ebook-cover",src:cover.value,alt:"Book cover">;
                    <div <div class:"ebook-book-title",c.value(book.metadata.titles[0],"Untitled")>
                        if (len(book.metadata.creators) > 0) <p class:"ebook-author",join(book.metadata.creators,", ")>>>
                <h3 "Contents">
                if (len(book.navigation.toc) > 0) contents(book.navigation.toc) else
                    contents([for (entry in book.reading_order) {label:"Chapter " ++ string(entry.occurrence+1),
                        target:{href:entry.href},children:[]}]);
                if (len(book.navigation.landmarks) > 0) <section <h3 "Landmarks"> contents(book.navigation.landmarks)>;
                if (len(book.navigation.page_list) > 0) <section <h3 "Publisher pages"> contents(book.navigation.page_list)>;
                <h3 "Bookmarks"> <div class:"ebook-bookmarks">
                <h3 "Highlights"> <div class:"ebook-highlights">>
            <main class:"ebook-content",<div class:"ebook-results">
                <div class:"ebook-stage",["data-texture"]:stage_texture(chapter,saved.preferences),
                    ["data-layout"]:stage_layout(chapter,saved.preferences),["data-turn"]:"none",
                    ["data-flip"]:string(saved.preferences.turn == "flip"),
                    <div class:"ebook-page",(page.paper());
                        <iframe class:"ebook-frame",title:"Book content",sandbox:"allow-same-origin",["data-navigation-event"]:"ebook_link",
                            ["data-selection-event"]:"ebook_selection",srcdoc:srcdoc(chapter,saved.preferences)>>
                    <div class:"ebook-turn",["aria-hidden"]:"true",
                        *page.hold(ghost_frame("ebook-copy ebook-ghost-hold",flip_doc(chapter,saved.preferences)));
                        <div class:"ebook-turn-page",<div class:"ebook-turn-content",(page.paper());
                            ghost_frame("ebook-copy ebook-ghost",ghost_doc(chapter,saved.preferences))>; <div class:"ebook-turn-shade">>;
                        (page.back(ghost_frame("ebook-copy ebook-turn-next",flip_doc(chapter,saved.preferences))))>;
                    *page.corners()>
                <div class:"ebook-highlight-bar",role:"toolbar",["aria-label"]:"Highlight",["data-open"]:"false",["data-mode"]:"new",
                    for (color in store.colors) <button type:"button",class:"ebook-swatch",title:"Highlight " ++ color,
                        ["aria-label"]:"Highlight " ++ color,["data-ebook-command"]:"highlight",["data-ebook-color"]:color,
                        tip("Highlight " ++ color)>;
                    <button type:"button",class:"ebook-unhighlight",["data-ebook-command"]:"unhighlight","Remove">>
                <div class:"ebook-notices",saved.message>>>
        <footer class:"ebook-footer",
            <div class:"ebook-chapter-controls",button("previous","Previous chapter","previous"); button("next","Next chapter","next")>
            <div class:"ebook-progress",<span "Chapter " <span class:"ebook-status",string(index+1) ++ " / " ++ string(len(book.reading_order))>>
                <span class:"ebook-page-status">>
            <div class:"ebook-page-controls",button("page-previous","Previous page","back"); button("page-next","Next page","forward")>>>

}
fn owner(target) {
    let local = dom.closest(target,".ebook-reader")
    let frame = if (local == null) dom.embedding_element(target) else null;
    if (local != null) local else if (frame != null) owner(frame) else null
}
fn root(host) => dom.embedded_document_root(dom.query_selector(host,".ebook-frame"))
// query_selector searches below the root, so a passage owned by <html> itself ("n") is the root
fn node_for(host,locator) => if (locator == null or locator.node == null) null else (let doc = root(host),
    if (dom.get_attribute(doc,"data-ebook-node") == locator.node) doc
    else dom.query_selector(doc,"[data-ebook-node='" ++ locator.node ++ "']"))

// A selection boundary maps back to a chapter-text offset: the nearest element
// carrying data-ebook-node fixes where its source text starts, and the DOM text
// before the boundary inside it counts on from there. DOM offsets are UTF-16
// units, equal to the source map's scalar counts for BMP text.
fn text_length(node) => if (dom.node_type(node) == 3) len(dom.node_value(node))
    else if (lower(string(dom.node_name(node))) in ["style","script","head"]) 0
    else sum([0,*[for (child in dom.child_nodes(node)) text_length(child)]])
fn before_in(children,i,target,count) => if (i >= len(children)) count else (let child = children[i],
    if (dom.same_node(child,target)) count
    else if (dom.contains(child,target)) count + before_in(dom.child_nodes(child),0,target,0)
    else before_in(children,i+1,target,count+text_length(child)))
fn text_offset(chapter,node,offset) {
    let is_text = dom.node_type(node) == 3
    let scope = dom.closest(if (is_text) dom.parent_element(node) else node,"[data-ebook-node]")
    let path = dom.get_attribute(scope,"data-ebook-node")
    let first = c.first([for (s in chapter.source_map where s.node == path or starts_with(s.node,path ++ "-")) s.offset])
    let inside = if (dom.same_node(scope,node)) 0 else before_in(dom.child_nodes(scope),0,node,0)
    let within = if (is_text) offset else sum([0,*[for (i,child in dom.child_nodes(node) where i < offset) text_length(child)]]);
    c.value(first,0) + inside + within
}
fn mark_of(node) => dom.closest(if (dom.node_type(node) == 3) dom.parent_element(node) else node,"mark.ebook-highlight")
// The chapter selection as a span of chapter text, or the highlight a click landed on.
fn selection_pending(host,session) {
    let b = dom.selection_boundaries(dom.document_selection(root(host)));
    if (b.anchor_node == null or b.focus_node == null) null
    else if (dom.same_node(b.anchor_node,b.focus_node) and b.anchor_offset == b.focus_offset) (
        let mark = mark_of(b.anchor_node),
        let found = if (mark == null) [] else [for (h in session.highlights where h.id == dom.get_attribute(mark,"data-ebook-highlight")) h],
        if (len(found) == 0) null else {*:found[0],occurrence:found[0].spine_occurrence})
    else (let a = text_offset(session.chapter,b.anchor_node,b.anchor_offset),
        let f = text_offset(session.chapter,b.focus_node,b.focus_offset),
        if (a == f) null else {occurrence:session.index,start:min(a,f),end:max(a,f)})
}
fn capture(host,book,session) map^ {
    let chapter = session.chapter
    let frame = dom.query_selector(host,".ebook-frame")
    let scroll = dom.scroll_state(frame)
    let viewport = dom.scroll_metrics(frame)
    // The embedding scrollport clips chapter-document coordinates.
    let candidates = [for (segment in chapter.source_map,
        let node = dom.query_selector(root(host),"[data-ebook-node='" ++ segment.node ++ "']"),
        let box = if (node == null) null else dom.bounding_box(node) where box != null and box.right > scroll.x and box.left < scroll.x+viewport.client_width and
            box.bottom > scroll.y and box.top < scroll.y+viewport.client_height) segment];
    locations.at(book,chapter,if (len(candidates) > 0) candidates[0].offset else 0)^
}
pn notices(host,text) { dom.set_text_content(dom.query_selector(host,".ebook-notices"),text) }
// The bar floats above the selection (below it near the top edge); the frame's
// client rectangle is offset by the frame's place in the content panel.
pn place_bar(host,session) {
    let bar = dom.query_selector(host,".ebook-highlight-bar")
    let pending = session.pending
    if (pending == null) {
        dom.set_attribute(bar,"data-open","false")
        return null
    }
    let doc = root(host)
    let span = if (pending.id == null) dom.selection_get_range_at(dom.document_selection(doc),0) else dom.document_create_range(doc)
    if (pending.id != null) { dom.range_select_node_contents(span,dom.query_selector(doc,"mark[data-ebook-highlight='" ++ pending.id ++ "']")) }
    let rect = dom.range_get_bounding_client_rect(span)
    let frame = dom.bounding_box(dom.query_selector(host,".ebook-frame"))
    let content = dom.bounding_box(dom.query_selector(host,".ebook-content"))
    let x = frame.left - content.left + rect.left + rect.width/2 - 110
    let above = frame.top - content.top + rect.top - 52
    dom.style_set_property(bar,"left",string(max(8,min(content.right - content.left - 236,x))) ++ "px")
    dom.style_set_property(bar,"top",string(if (above < 4) frame.top - content.top + rect.bottom + 10 else above) ++ "px")
    dom.set_attribute(bar,"data-mode",if (pending.id == null) "new" else "edit")
    for (swatch in dom.query_selector_all(bar,".ebook-swatch"))
        dom.set_attribute(swatch,"aria-pressed",string(dom.get_attribute(swatch,"data-ebook-color") == pending.color))
    dom.set_attribute(bar,"data-open","true")
}
pn reveal(host,node,prefs) {
    if (node == null) { return null }
    let frame = dom.query_selector(host,".ebook-frame")
    let box = dom.bounding_box(node)
    let width = dom.scroll_metrics(frame).client_width
    if (prefs.mode != "scroll") { dom.set_scroll_state(frame,floor(box.left/max(1,width))*width,0) }
    else { dom.set_scroll_state(frame,0,max(0,box.top-prefs.margin)) }
}
pn restore(host,session) {
    // a highlight re-render keeps the exact page in view rather than the passage start
    if (session.keep_scroll != null) { dom.set_scroll_state(dom.query_selector(host,".ebook-frame"),session.keep_scroll.x,session.keep_scroll.y) }
    else { reveal(host,node_for(host,session.location),session.preferences) }
    let styles = dom.stylesheets(root(host))
    for (sheet in styles) {
        let style_node = dom.stylesheet_get_owner_node(sheet)
        if (style_node != null and dom.get_attribute(style_node,"id") != "ebook-preferences")
            dom.stylesheet_set_disabled(sheet,not session.preferences.publisher)
    }
}
pn chrome(host,book,session) {
    dom.set_attribute(host,"data-theme",session.preferences.theme)
    // fixed-layout pages keep their own fit/zoom, so the layout switch steps aside
    dom.set_attribute(host,"data-fixed",string(session.chapter.layout == "fixed"))
    let stage = dom.query_selector(host,".ebook-stage")
    dom.set_attribute(stage,"data-texture",stage_texture(session.chapter,session.preferences))
    dom.set_attribute(stage,"data-layout",stage_layout(session.chapter,session.preferences))
    dom.set_attribute(stage,"data-flip",string(session.preferences.turn == "flip"))
    for (control in dom.query_selector_all(host,"[data-ebook-command=search-option]"))
        dom.set_attribute(control,"aria-pressed",string(session.search[dom.get_attribute(control,"data-ebook-value")] == true))
    for (control in dom.query_selector_all(host,".ebook-option"))
        dom.set_attribute(control,"aria-pressed",string(session.preferences[dom.get_attribute(control,"data-ebook-pref")] ==
            dom.get_attribute(control,"data-ebook-value")))
    let labels = {theme:session.preferences.theme ++ " theme",font:session.preferences.font_family ++ " font",
        spacing:"Spacing " ++ string(session.preferences.line_height),margin:"Margins " ++ string(session.preferences.margin),
        publisher:if (session.preferences.publisher) "Publisher styles on" else "Publisher styles off"}
    for (command,label in labels) {
        let control = dom.query_selector(host,"[data-ebook-command='" ++ string(command) ++ "']")
        if (control != null) dom.set_text_content(control,label)
    }
    for (item in dom.query_selector_all(dom.query_selector(host,".ebook-sidebar"),"[data-ebook-command=go]")) {
        if (dom.get_attribute(item,"data-ebook-href") == session.chapter.href) dom.set_attribute(item,"aria-current","location")
        else if (dom.has_attribute(item,"aria-current")) dom.remove_attribute(item,"aria-current")
    }
    for (command,disabled in {back:session.cursor <= 0,forward:session.cursor >= len(session.history)-1,
        previous:not any([for (entry in book.reading_order) entry.linear and entry.occurrence < session.index]),
        next:not any([for (entry in book.reading_order) entry.linear and entry.occurrence > session.index])}) {
        let control = dom.query_selector(host,"[data-ebook-command='" ++ string(command) ++ "']")
        if (disabled) dom.set_attribute(control,"disabled","") else if (dom.has_attribute(control,"disabled")) dom.remove_attribute(control,"disabled")
    }
    dom.set_attribute(dom.query_selector(host,"[data-ebook-command=bookmark]"),"aria-pressed",
        string(any([for (bookmark in session.bookmarks) bookmark == session.location])))
}
// Paged layout: the chapter's columns and viewport-wide turns, counted from the
// first blank trailing column (srcdoc) rather than the padded scroll extent.
pn page_geometry(host,session) map {
    let frame = dom.query_selector(host,".ebook-frame")
    let width = max(1,dom.scroll_metrics(frame).client_width)
    let per_turn = if (session.preferences.mode == "spread") 2 else 1
    let blank = dom.query_selector(root(host),".ebook-end")
    let box = if (blank == null) null else dom.bounding_box(blank)
    // bounding_box reports chapter-document coordinates, independent of the frame's scroll
    let end = if (box == null) dom.scroll_metrics(root(host)).scroll_width else box.left
    let columns = max(1,int(round((end - page.margin(session.preferences))*per_turn/width)))
    return {width:width,columns:columns,turns:int(ceil(float(columns)/per_turn))}
}
pn page_status(host,book,session) {
    let frame = dom.query_selector(host,".ebook-frame")
    let scroll = dom.scroll_state(frame)
    let geometry = if (session.preferences.mode == "scroll") null else page_geometry(host,session)
    let turn = if (geometry == null) 0 else int(floor(scroll.x/geometry.width))
    let label = if (book.reading_order[session.index].layout == "fixed") "Fixed layout"
        else if (session.preferences.mode == "scroll") "Scrolling"
        else if (session.preferences.mode == "spread")
            "Pages " ++ string(turn*2+1) ++ "–" ++ string(min(turn*2+2,geometry.columns)) ++ " / " ++ string(geometry.columns)
        else "Page " ++ string(turn+1) ++ " / " ++ string(geometry.columns);
    dom.set_text_content(dom.query_selector(host,".ebook-page-status"),label)
    chrome(host,book,session)
}
pn mount(host,book,session) map^ {
    let chapter = if (session.chapter.occurrence == session.index) session.chapter else chapters.load(book,session.index,session.highlights)^
    dom.set_attribute(dom.query_selector(host,".ebook-frame"),"srcdoc",srcdoc(chapter,session.preferences))
    dom.set_attribute(dom.query_selector(host,".ebook-turn-next"),"srcdoc",flip_doc(chapter,session.preferences))
    // while a turn holds the old chapter's page, the ghosts keep it until the animation ends
    let hold = session.turn_after != null
    if (not hold) { ghost_docs(host,chapter,session.preferences) }
    dom.set_text_content(dom.query_selector(host,".ebook-status"),string(session.index+1) ++ " / " ++ string(len(book.reading_order)))
    notices(host,join([for (issue in chapter.diagnostics) issue.message],"; "))
    dom.request_frame(host,"ebook_ready")
    return {*:session,chapter:chapter,ghost_stale:hold}
}
// whether a page turn by step (1 or -1) leaves the chapter, at its end or start
pn leaves_chapter(host,book,session,step) {
    let frame = dom.query_selector(host,".ebook-frame")
    let scroll = dom.scroll_state(frame)
    let fixed = book.reading_order[session.index].layout == "fixed"
    let paged = session.preferences.mode != "scroll"
    let metrics = dom.scroll_metrics(frame)
    let geometry = if (paged and not fixed) page_geometry(host,session) else null
    let extent = if (geometry != null) geometry.turns*geometry.width else dom.scroll_metrics(root(host)).scroll_height
    let size = if (paged) metrics.client_width else metrics.client_height
    let position = if (paged) scroll.x else scroll.y
    return fixed or (step == 1 and position+size >= extent-1) or (step == -1 and position <= 0)
}
// A turn shows the page being left in the ghost frame, scrolled where the
// chapter frame was, and animates it out over the new page.
fn can_turn(book,session) => page.animated(session.preferences) and not session.ghost_stale and
    book.reading_order[session.index].layout != "fixed"
pn hold_page(host) {
    let scroll = dom.scroll_state(dom.query_selector(host,".ebook-frame"))
    dom.set_scroll_state(dom.query_selector(host,".ebook-ghost"),scroll.x,scroll.y)
    dom.set_scroll_state(dom.query_selector(host,".ebook-ghost-hold"),scroll.x,scroll.y)
}
// the copies of the page being left: the turning leaf, and a spread flip's held facing page
pn ghost_docs(host,chapter,prefs) {
    dom.set_attribute(dom.query_selector(host,".ebook-ghost"),"srcdoc",ghost_doc(chapter,prefs))
    dom.set_attribute(dom.query_selector(host,".ebook-ghost-hold"),"srcdoc",flip_doc(chapter,prefs))
}
pn play_turn(host,session,dir) map {
    let parity = if (session.turn_parity == 1) 0 else 1
    let stage = dom.query_selector(host,".ebook-stage")
    dom.set_attribute(stage,"data-turn-kind",session.preferences.turn ++ "-" ++ dir)
    if (session.preferences.turn == "flip") {
        // the back of the leaf shows the new page, where the chapter frame now is
        let scroll = dom.scroll_state(dom.query_selector(host,".ebook-frame"))
        dom.set_scroll_state(dom.query_selector(host,".ebook-turn-next"),scroll.x,scroll.y)
        dom.set_attribute(stage,"data-turn","flip")
        let f = flip_geometry(host,session.preferences.mode)
        return timed_flip(host,{*:session,turn_after:null},{dir:dir,corner:"bottom"},flip.rest(f),flip.landing(f),"done")
    }
    dom.set_attribute(stage,"data-turn",page.turn_name(session.preferences.turn,dir,parity))
    return {*:session,turn_parity:parity,turn_after:null}
}
// Flip turns are painted frame by frame (flip.ls). A timed flip carries the
// corner from one point to another, eased in and out; a whole turn takes
// flip_ms, a shorter way its share. A drag puts the corner under the pointer,
// and a hovered corner curls a little (a peel) before anything turns.
let flip_ms = {spread:900.0,paged:700.0}
pn flip_geometry(host,layout) {
    let metrics = dom.scroll_metrics(dom.query_selector(host,".ebook-turn"))
    return flip.frame({w:float(metrics.client_width),h:float(metrics.client_height)},layout)
}
// then: "done" ends the turn, "cancel" puts the page back where the drag began
pn timed_flip(host,session,way,from,to,then) map {
    let f = flip_geometry(host,session.preferences.mode)
    let share = max(0.25,abs(to.x - from.x)/(2.0*f.w))
    flip.paint(host,f,way,from)
    dom.request_frame(host,"ebook_flip")
    return {*:session,flip:{*:c.value(session.flip,{}),way:way,from:from,to:to,at:from,then:then,
        ms:flip_ms[session.preferences.mode]*share,start:null,last:null,drag:false,peel:false}}
}
// the corner and direction a corner hotspot turns
fn corner_way(spot) => (let key = dom.get_attribute(spot,"data-corner"),
    {dir:if (ends_with(key,"right")) "next" else "prev",corner:if (starts_with(key,"top")) "top" else "bottom"})
// a pointer event's place in fold coordinates, kept on the paper
pn pointer_corner(host,session,way,evt) {
    let f = flip_geometry(host,session.preferences.mode)
    let box = dom.bounding_box(dom.query_selector(host,".ebook-turn"))
    return flip.clamp(f,flip.local(f,way,{x:float(evt.clientX - box.left),y:float(evt.clientY - box.top)}))
}
pn end_flip(host,book,session) map^ {
    flip.reset(host)
    dom.set_attribute(dom.query_selector(host,".ebook-stage"),"data-turn","none")
    let turn = session.flip
    var next = {*:session,flip:null}
    // a cancelled drag returns the chapter frame to the page it left
    if (turn != null and turn.then == "cancel" and turn.scroll != null) {
        dom.set_scroll_state(dom.query_selector(host,".ebook-frame"),turn.scroll.x,turn.scroll.y)
        next = {*:next,location:capture(host,book,next)^}
        page_status(host,book,next)
        chrome(host,book,next)
        store.save(book,next)^ { notices(host,"Reading state could not be saved: " ++ ^.message) }
    }
    return release_ghosts(host,next)
}
// a finished turn releases the ghosts to take the current chapter
pn release_ghosts(host,session) map {
    if (not session.ghost_stale) { return session }
    ghost_docs(host,session.chapter,session.preferences)
    return {*:session,ghost_stale:false}
}
pn navigate(host,book,session,target,record = true) map^ {
    let matches = [for (entry in book.reading_order where target.href in [entry.href,entry.source_href]) entry]
    if (len(matches) == 0) { notices(host,"The destination is outside this book."); return session }
    let index = c.value(target.spine_occurrence,matches[0].occurrence)
    let chapter = if (session.chapter.occurrence == index) session.chapter else chapters.load(book,index,session.highlights)^
    let loc = if (target.source_text_position != null) locations.resolve(book,target)^
        else locations.at(book,chapter,0)^
    let history = if (record) [*take(session.history,session.cursor),session.location,loc] else session.history
    let next = {*:session,index:index,chapter:chapter,location:loc,fragment:target.fragment,edge:null,
        history:history,cursor:if (record) len(history)-1 else session.cursor};
    return mount(host,book,next)^
}
pn chapter_step(host,book,session,step) map^ {
    let indices = [for (entry in book.reading_order where entry.linear and
        (if (step == 1) entry.occurrence > session.index else entry.occurrence < session.index)) entry.occurrence];
    return if (len(indices) == 0) session else
        navigate(host,book,session,book.reading_order[if (step == 1) min(indices) else max(indices)])^
}
fn flatten(nodes) => [for (node in nodes,found in [node,*flatten(node.children)]) found]
// a highlight's chapter by its contents label, or its place in the reading order
fn chapter_name(book,h) => c.value(c.first([for (node in flatten(book.navigation.toc)
    where node.target != null and node.target.href == h.href) node.label]),
    "Chapter " ++ string(h.spine_occurrence+1) ++ " of " ++ string(len(book.reading_order)))
fn snippet(text) => (let flat = trim(replace(replace(text,"\n"," "),"\r"," ")),
    if (len(flat) > 120) slice(flat,0,117) ++ "…" else flat)
pn highlight_list(host,book,session) {
    let nodes = [for (i,h in session.highlights) <button type:"button",["data-ebook-command"]:"highlight-go",
        ["data-ebook-index"]:string(i),["data-ebook-color"]:h.color,title:h.text,
        <span class:"ebook-highlight-quote",snippet(h.text)>
        <span class:"ebook-highlight-where",chapter_name(book,h)>>]
    dom.set_inner_html(dom.query_selector(host,".ebook-highlights"),if (len(nodes) == 0) "" else format(nodes,"html")^)
}
// Coloring a span absorbs any highlight it overlaps, as one highlight over the
// union; null color removes them. The chapter re-renders with its marks.
pn annotate(host,book,session,color) map^ {
    let pending = session.pending
    if (pending == null or pending.occurrence != session.index) { return session }
    let hit = [for (h in session.highlights where h.spine_occurrence == pending.occurrence and
        h.start < pending.end and h.end > pending.start) h]
    let start = min([pending.start,*[for (h in hit) h.start]])
    let end = max([pending.end,*[for (h in hit) h.end]])
    let added = if (color == null) [] else [{id:string(pending.occurrence) ++ ":" ++ string(start),
        spine_occurrence:pending.occurrence,href:session.chapter.href,start:start,end:end,color:color,
        text:slice(session.chapter.text,start,end)}]
    let highlights = sort([for (h in session.highlights where not (h in hit)) h] ++ added,
        (h) => h.spine_occurrence*1000000000 + h.start)
    dom.selection_remove_all_ranges(dom.document_selection(root(host)))
    let next = {*:session,highlights:highlights,pending:null,chapter:chapters.load(book,session.index,highlights)^,
        keep_scroll:dom.scroll_state(dom.query_selector(host,".ebook-frame"))}
    place_bar(host,next)
    highlight_list(host,book,next)
    return mount(host,book,next)^
}
// A result names its chapter when the search covers the whole book, and marks the match.
fn search_result(book,i,result,scoped) => <button type:"button",["data-ebook-command"]:"result",["data-ebook-index"]:string(i),
    title:chapter_name(book,{href:book.reading_order[result.chapter].href,spine_occurrence:result.chapter}),
    if (not scoped) <span class:"ebook-result-chapter","Ch. " ++ string(result.chapter+1)>;
    <span class:"ebook-result-text",result.before; <mark class:"ebook-result-match",result.match>; result.after>>
pn run_search(host,book,session) map^ {
    let query = dom.get_state(dom.query_selector(host,".ebook-query"),"value")
    let scoped = session.search.chapter
    let limit = 100
    let results = locations.search(book,query,{limit:limit,match_case:session.search.case,whole_word:session.search.word,
        chapter:if (scoped) session.index else null})^
    dom.set_inner_html(dom.query_selector(host,".ebook-results"),
        if (len(results) == 0) "" else format([for (i,result in results) search_result(book,i,result,scoped)],"html")^)
    notices(host,(if (len(results) == 0) "No matches" else if (len(results) == 1) "1 match" else string(len(results)) ++ " matches") ++
        (if (len(results) == limit) " (the first " ++ string(limit) ++ ")" else "") ++ (if (scoped) " in this chapter" else " in the book"))
    return {*:session,results:results}
}
pn bookmarks(host,session) {
    let nodes = [for (i,bookmark in session.bookmarks) <button type:"button",["data-ebook-command"]:"saved",
        ["data-ebook-index"]:string(i),"Chapter " ++ string(bookmark.spine_occurrence+1) ++ ": " ++ bookmark.quote>]
    dom.set_inner_html(dom.query_selector(host,".ebook-bookmarks"),if (len(nodes) == 0) "" else format(nodes,"html")^)
}
pn action(host,book,current,command,target = null) map^ {
    // any other command dismisses the highlight bar and its pending span
    let session = if (command in ["highlight","unhighlight"]) current else {*:current,pending:null}
    if (session.pending == null) { place_bar(host,session) }
    if (command == "settings" or command == "find") {
        let panel = dom.query_selector(host,if (command == "settings") ".ebook-settings" else ".ebook-search-panel")
        let opened = dom.get_attribute(panel,"data-open") != "true"
        dom.set_attribute(panel,"data-open",string(opened))
        dom.set_attribute(dom.query_selector(host,"[data-ebook-command='" ++ command ++ "']"),"aria-expanded",string(opened))
        if (command == "find" and opened) dom.set_attribute(host,"data-search-focus","pending")
        dom.request_frame(host,"ebook_ready")
        return session
    }
    if (command == "contents") {
        let current = c.value(dom.get_attribute(host,"data-sidebar"),if (dom.scroll_metrics(host).client_width > 540) "open" else "closed")
        dom.set_attribute(host,"data-sidebar",if (current == "open") "closed" else "open")
        dom.request_frame(host,"ebook_ready")
        return session
    }
    var next = {*:session,location:capture(host,book,session)^}
    if (command == "back" or command == "forward") {
        let cursor = session.cursor + (if (command == "back") -1 else 1)
        if (cursor >= 0 and cursor < len(session.history)) {
            next = navigate(host,book,next,session.history[cursor],false)^
            next = {*:next,cursor:cursor}
        }
    } else if (command == "next" or command == "previous") {
        next = chapter_step(host,book,next,if (command == "next") 1 else -1)^
    } else if (command == "go") { next = navigate(host,book,next,target)^ }
    else if (command == "pref") {
        next.preferences = store.preferences({*:session.preferences,[target.pref]:target.value})
        next = mount(host,book,next)^
    }
    else if (command in ["larger","smaller","font","spacing","margin","theme","publisher","zoom-in","zoom-out"]) {
        let prefs = session.preferences
        next.preferences = {*:prefs,font_size:max(12,min(40,prefs.font_size + (if (command == "larger") 2 else if (command == "smaller") -2 else 0))),
            theme:if (command == "theme") cycle(prefs.theme,["light","sepia","dark"]) else prefs.theme,
            font_family:if (command == "font") cycle(prefs.font_family,["publisher","serif","sans-serif","monospace"]) else prefs.font_family,
            line_height:if (command == "spacing") cycle(prefs.line_height,[1.4,1.6,1.8,2.0]) else prefs.line_height,
            margin:if (command == "margin") cycle(prefs.margin,[16,24,40,64]) else prefs.margin,
            publisher:if (command == "publisher") not prefs.publisher else prefs.publisher,
            zoom:max(0.5,min(3.0,prefs.zoom + (if (command == "zoom-in") 0.25 else if (command == "zoom-out") -0.25 else 0.0)))}
        next = mount(host,book,next)^
    } else if (command in ["page-next","page-previous"]) {
        let frame = dom.query_selector(host,".ebook-frame")
        let step = if (command == "page-next") 1 else -1
        let scroll = dom.scroll_state(frame)
        let paged = session.preferences.mode != "scroll"
        let dir = if (step == 1) "next" else "prev"
        let animate = can_turn(book,session)
        let size = if (paged) dom.scroll_metrics(frame).client_width else dom.scroll_metrics(frame).client_height
        if (leaves_chapter(host,book,session,step)) {
            // the new chapter loads under the held page; ebook_ready plays the turn
            if (animate) {
                hold_page(host)
                dom.set_attribute(dom.query_selector(host,".ebook-stage"),"data-turn","hold")
            }
            next = chapter_step(host,book,{*:next,turn_after:if (animate) dir else null},step)^
            if (next.index == session.index) {
                if (animate) { dom.set_attribute(dom.query_selector(host,".ebook-stage"),"data-turn","none") }
                next = {*:next,turn_after:null}
            }
        } else {
            if (paged and animate) { hold_page(host) }
            if (paged) { dom.set_scroll_state(frame,max(0,scroll.x+step*size),0) }
            else { dom.set_scroll_state(frame,scroll.x,max(0,scroll.y+step*size*0.9)) }
            if (paged and animate) { next = play_turn(host,next,dir) }
        }
        if (next.index == session.index) { next = {*:next,location:capture(host,book,next)^} }
        else if (step == -1) { next = {*:next,edge:"end"} }
        page_status(host,book,next)
    } else if (command == "bookmark") {
        let already = any([for (bookmark in session.bookmarks) bookmark == next.location])
        next.bookmarks = if (already) [for (bookmark in session.bookmarks where bookmark != next.location) bookmark]
            else [*session.bookmarks,next.location]
        bookmarks(host,next)
    } else if (command == "highlight" or command == "unhighlight") {
        next = annotate(host,book,next,if (command == "highlight") target.color else null)^
    } else if (command == "highlight-go") {
        let h = session.highlights[int(target.index)^]
        next = navigate(host,book,next,locations.at(book,chapters.load(book,h.spine_occurrence,session.highlights)^,h.start)^)^
    } else if (command == "saved" or command == "result") {
        let index = int(target.index)^
        let loc = if (command == "saved") session.bookmarks[index] else session.results[index].locator
        next = navigate(host,book,next,loc)^
    } else if (command == "search") {
        next = run_search(host,book,next)^
    } else if (command == "search-option") {
        let toggled = {*:session.search,[target.value]:not session.search[target.value]}
        next = {*:next,search:toggled}
        // a changed option re-runs a search already shown
        if (len(session.results) > 0) { next = run_search(host,book,next)^ }
    }
    chrome(host,book,next)
    store.save(book,next)^ { notices(host,"Reading state could not be saved: " ++ ^.message) }
    return next
}
pn initialize(host,book,session) {
    if (host == null or dom.get_attribute(host,"data-ebook-initialized") != null) { return null }
    dom.set_attribute(host,"data-ebook-initialized","true")
    bookmarks(host,session)
    highlight_list(host,book,session)
    dom.watch_window(host,"ebook_resize",true)
    dom.request_frame(host,"ebook_ready")
}
view <ebook_reader> state session: {*:~.saved,highlights:c.value(~.saved.highlights,[]),pending:null,
        turn_parity:0,turn_after:null,flip:null,ghost_stale:false,search:{case:false,word:false,chapter:false},index:~.chapter.occurrence,chapter:~.chapter,results:[],fragment:null,edge:null,history:[~.start],cursor:0,location:~.start} {
    ~.rendered
}
on load(evt) {
    initialize(owner(evt.target),~.book,session)
    return 'handled'
}
on render(evt) {
    initialize(owner(evt.target),~.book,session)
    return 'handled'
}
on ebook_ready(evt) {
    let host = owner(evt.target)
    if (host == null or root(host) == null) { return 'pass' }
    initialize(host,~.book,session)
    restore(host,session)
    if (session.keep_scroll != null) { session = {*:session,keep_scroll:null} }
    let chapter = session.chapter
    if (chapter.layout == "fixed") {
        let frame = dom.query_selector(host,".ebook-frame")
        let metrics = dom.scroll_metrics(frame)
        let scale = min(metrics.client_width/chapter.viewport.width,
            metrics.client_height/chapter.viewport.height)*session.preferences.zoom
        dom.style_set_property(dom.query_selector(root(host),"body"),"transform","scale(" ++ string(scale) ++ ")")
    }
    if (session.edge == "end") {
        let frame = dom.query_selector(host,".ebook-frame")
        let content = dom.scroll_metrics(root(host))
        let metrics = dom.scroll_metrics(frame)
        if (session.preferences.mode != "scroll") {
            let geometry = page_geometry(host,session)
            dom.set_scroll_state(frame,(geometry.turns-1)*geometry.width,0)
        } else { dom.set_scroll_state(frame,0,max(0,content.scroll_height-metrics.client_height)) }
        session = {*:session,edge:null,location:capture(host,~.book,session)^}
    }
    if (session.fragment != null and session.fragment != "") {
        let target = dom.get_element_by_id(root(host),session.fragment)
        reveal(host,target,session.preferences)
        // Later readiness/resize events must restore the resolved passage, not the chapter start.
        let location = capture(host,~.book,session)^
        session = {*:session,fragment:null,location:location,
            history:[for (i,entry in session.history) if (i == session.cursor) location else entry]}
        store.save(~.book,session)^ { notices(host,"Reading state could not be saved: " ++ ^.message) }
    }
    if (session.turn_after != null) { session = play_turn(host,session,session.turn_after) }
    if (dom.get_attribute(host,"data-search-focus") == "pending") {
        dom.remove_attribute(host,"data-search-focus")
        if (dom.get_attribute(dom.query_selector(host,".ebook-search-panel"),"data-open") == "true")
            dom.focus_set(dom.query_selector(host,".ebook-query"),false)
    }
    page_status(host,~.book,session)
    return 'handled'
}
on scrollend(evt) {
    let host = owner(evt.target)
    if (host == null or evt.target != dom.query_selector(host,".ebook-frame")) { return 'pass' }
    // the bar is placed against the old scroll offset
    session = {*:session,pending:null,location:capture(host,~.book,session)^}
    place_bar(host,session)
    page_status(host,~.book,session)
    store.save(~.book,session)^ { notices(host,"Reading state could not be saved: " ++ ^.message) }
    return 'handled'
}
on animationend(evt) {
    let host = owner(evt.target)
    if (host == null or not dom.matches(evt.target,".ebook-turn")) { return 'pass' }
    session = release_ghosts(host,session)
    return 'handled'
}
on ebook_flip(evt) {
    let host = owner(evt.target)
    let turn = session.flip
    // a turn started over a running one leaves two frame chains; the second of a frame stops
    if (host == null or turn == null or turn.drag or turn.peel or turn.last == evt.time_stamp) { return 'pass' }
    let start = if (turn.start == null) evt.time_stamp else turn.start
    let u = min(1.0,float(evt.time_stamp - start)/turn.ms)
    if (u < 1.0) {
        let f = flip_geometry(host,session.preferences.mode)
        let at = flip.path(f,turn.from,turn.to,(1.0 - math.cos(math.pi*u))/2.0)
        flip.paint(host,f,turn.way,at)
        dom.request_frame(host,"ebook_flip")
        session = {*:session,flip:{*:turn,at:at,start:start,last:evt.time_stamp}}
        return 'handled'
    }
    session = end_flip(host,~.book,session)^
    return 'handled'
}
// A hovered corner curls toward the pointer; pressing it turns the page there
// and then, and the leaf follows the pointer until it is let go.
on pointermove(evt) {
    let host = owner(evt.target)
    if (host == null) { return 'pass' }
    let turn = session.flip
    if (turn != null and turn.drag) {
        if (evt.pointerId != turn.pointer) { return 'pass' }
        let at = pointer_corner(host,session,turn.way,evt)
        flip.paint(host,flip_geometry(host,session.preferences.mode),turn.way,at)
        session = {*:session,flip:{*:turn,at:at,moved:true}}
        return 'handled'
    }
    let spot = dom.closest(evt.target,".ebook-flip-corner")
    if (spot == null or (turn != null and not turn.peel)) { return 'pass' }
    let way = corner_way(spot)
    let stage = dom.query_selector(host,".ebook-stage")
    if (turn == null or turn.way != way) {
        hold_page(host)
        dom.set_attribute(stage,"data-turn-kind","flip-" ++ way.dir)
        dom.set_attribute(stage,"data-turn","peel")
    }
    let f = flip_geometry(host,session.preferences.mode)
    let p = pointer_corner(host,session,way,evt)
    // the curl reaches a little past the pointer, so the corner shows under it
    let rest = flip.rest(f)
    let at = flip.clamp(f,{x:rest.x + (p.x - rest.x)*1.4,y:rest.y + (p.y - rest.y)*1.4})
    flip.paint(host,f,way,at)
    session = {*:session,flip:{way:way,peel:true,drag:false,at:at}}
    return 'handled'
}
on mouseout(evt) {
    let host = owner(evt.target)
    let turn = session.flip
    if (host == null or turn == null or not turn.peel or dom.closest(evt.target,".ebook-flip-corner") == null) { return 'pass' }
    flip.reset(host)
    dom.set_attribute(dom.query_selector(host,".ebook-stage"),"data-turn","none")
    session = {*:session,flip:null}
    return 'handled'
}
on pointerdown(evt) {
    let host = owner(evt.target)
    let spot = dom.closest(evt.target,".ebook-flip-corner")
    if (host == null or spot == null or evt.button != 0) { return 'pass' }
    let way = corner_way(spot)
    let step = if (way.dir == "next") 1 else -1
    // at a chapter's edge the press turns into the chapter on release, as a click does
    if (leaves_chapter(host,~.book,session,step)) {
        session = {*:session,flip:{way:way,press:true,drag:false,peel:false}}
        return 'prevent-default'
    }
    let scroll = dom.scroll_state(dom.query_selector(host,".ebook-frame"))
    let turned = action(host,~.book,{*:session,flip:null},if (step == 1) "page-next" else "page-previous")^
    let at = pointer_corner(host,turned,way,evt)
    flip.paint(host,flip_geometry(host,turned.preferences.mode),way,at)
    // the pressed corner spreads over the stage, so the drag's moves reach it and not the chapter frame
    dom.set_attribute(spot,"data-drag","true")
    spot.set_pointer_capture(evt.pointerId)
    session = {*:turned,flip:{way:way,drag:true,peel:false,at:at,pointer:evt.pointerId,scroll:scroll,moved:false,then:"done"}}
    return 'prevent-default'
}
on pointerup(evt) {
    let host = owner(evt.target)
    let turn = session.flip
    if (host == null or turn == null) { return 'pass' }
    if (turn.press == true) {
        session = action(host,~.book,{*:session,flip:null},if (turn.way.dir == "next") "page-next" else "page-previous")^
        return 'handled'
    }
    if (not turn.drag or evt.pointerId != turn.pointer) { return 'pass' }
    let f = flip_geometry(host,session.preferences.mode)
    let spot = dom.query_selector(host,".ebook-flip-corner[data-drag=true]")
    if (spot != null) dom.remove_attribute(spot,"data-drag")
    // past the spine, or a press that never moved, finishes the turn; otherwise it falls back
    let finish = not turn.moved or turn.at.x < f.spine
    session = timed_flip(host,{*:session,flip:{*:turn,drag:false}},turn.way,turn.at,
        if (finish) flip.landing(f) else flip.rest(f),if (finish) "done" else "cancel")
    return 'handled'
}
// the chapter frame reports a settled pointer selection (data-selection-event)
on ebook_selection(evt) {
    let host = owner(evt.target)
    if (host == null or root(host) == null) { return 'pass' }
    session = {*:session,pending:selection_pending(host,session)}
    place_bar(host,session)
    return 'handled'
}
on ebook_link(evt) {
    let host = owner(evt.target)
    let frame = dom.query_selector(host,".ebook-frame")
    let href = dom.get_attribute(frame,"data-navigation-href")
    if (host == null or href == null) { return 'pass' }
    session = action(host,~.book,session,"go",c.target("",href)^)^
    return 'handled'
}
on ebook_resize(evt) {
    let host = owner(evt.target)
    if (host != null) { dom.request_frame(host,"ebook_ready") }
    return 'handled'
}
on click(evt) {
    let host = owner(evt.target)
    let button = dom.closest(evt.target,"[data-ebook-command]")
    if (host == null or button == null) { return 'pass' }
    session = action(host,~.book,session,dom.get_attribute(button,"data-ebook-command"),
        {href:dom.get_attribute(button,"data-ebook-href"),fragment:dom.get_attribute(button,"data-ebook-fragment"),
            index:dom.get_attribute(button,"data-ebook-index"),color:dom.get_attribute(button,"data-ebook-color"),
            pref:dom.get_attribute(button,"data-ebook-pref"),value:dom.get_attribute(button,"data-ebook-value")})^
    return 'prevent-default'
}
on keydown(evt) {
    let host = owner(evt.target)
    if (host != null and evt.key == "Enter" and dom.closest(evt.target,".ebook-query") != null) {
        session = action(host,~.book,session,"search")^
        return 'prevent-default'
    }
    if (host == null or evt.ctrlKey or evt.altKey or evt.metaKey or
        dom.closest(evt.target,"input,textarea,select,button,[contenteditable]") != null) { return 'pass' }
    let command = if (evt.key == "ArrowRight" or evt.key == "PageDown") "page-next" else if (evt.key == "ArrowLeft" or evt.key == "PageUp") "page-previous" else null
    if (command == null) { return 'pass' }
    session = action(host,~.book,session,command)^
    return 'prevent-default'
}
pub fn reader(book,options = null) element^ {
    let opts = c.checked_options(options,["saved"])^
    let saved = c.get(opts,"saved",{path:null,location:null,preferences:store.defaults,bookmarks:[],message:""});
    let chapter = chapters.load(book,initial(book,saved),c.value(saved.highlights,[]))^;
    let start = c.value(saved.location,locations.at(book,chapter,0)^);
    apply(<ebook_reader book:book,saved:saved,chapter:chapter,start:start,rendered:tree(book,saved,chapter)^>)
}
view <ebook_document> {
    ~.rendered
}
on load(evt) {
    let host = dom.query_selector(evt.target,".ebook-reader")
    if (host != null) dom.request_frame(host,"ebook_ready")
    return 'handled'
}
pub fn document(book,saved) element^ {
    let body = reader(book,{saved:saved})^
    let rendered = <html lang:if (len(book.metadata.languages) > 0) book.metadata.languages[0] else "en",
        <head <meta charset:"UTF-8"> <title book.metadata.titles[0]> <style ("html,body{margin:0;height:100%}" ++ stylesheet)>>
        <body body>>;
    apply(<ebook_document rendered:rendered>)
}
