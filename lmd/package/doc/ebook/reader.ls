// Reader policy and mutation live in Lambda views/handlers (D7.2.4, S12.1.3).
import dom
import c: .common
import chapters: .chapter
import locations: .location
import store: .state

// Scoped chrome keeps publication styles inside the chapter document.
pub let stylesheet = "
.ebook-reader{--ebook-bg:#f5f4f0;--ebook-panel:#ffffff;--ebook-text:#25332f;--ebook-muted:#77817b;--ebook-line:#e4e7e2;--ebook-hover:#eef2ee;--ebook-accent:#26705c;height:100vh;display:flex;flex-direction:column;overflow:hidden;font:14px system-ui,sans-serif;background:var(--ebook-bg);color:var(--ebook-text)}
.ebook-reader[data-theme=sepia]{--ebook-bg:#eee5d4;--ebook-panel:#faf4e8;--ebook-text:#493f32;--ebook-muted:#887b67;--ebook-line:#e4d9c5;--ebook-hover:#eee5d4}
.ebook-reader[data-theme=dark]{--ebook-bg:#161e1b;--ebook-panel:#202b26;--ebook-text:#e4eae5;--ebook-muted:#a0afa5;--ebook-line:#35433b;--ebook-hover:#303e36;--ebook-accent:#8ccbb0}
.ebook-reader *{box-sizing:border-box}.ebook-reader button,.ebook-reader input{font:inherit;color:inherit}
.ebook-reader button{display:inline-flex;align-items:center;justify-content:center;gap:6px;min-height:34px;padding:7px 10px;border:1px solid transparent;border-radius:7px;background:transparent;line-height:1.2;cursor:pointer}
.ebook-reader button:hover{background:var(--ebook-hover)}.ebook-reader button:disabled{opacity:.35;cursor:default}
.ebook-reader button:focus-visible,.ebook-reader input:focus-visible{outline:2px solid var(--ebook-accent);outline-offset:2px}
.ebook-reader button[aria-pressed=true]{background:var(--ebook-hover);color:var(--ebook-accent)}
.ebook-icon{width:18px;height:18px;flex-shrink:0}.ebook-toolbar{display:flex;align-items:center;gap:10px;min-height:64px;padding:12px 20px;background:var(--ebook-panel);border-bottom:1px solid var(--ebook-line)}
.ebook-history{display:flex;gap:2px;padding-right:12px;border-right:1px solid var(--ebook-line)}.ebook-title{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:15px;font-weight:600}
.ebook-toolbar .ebook-icon-button{width:36px;padding:8px}.ebook-settings-toggle{font-family:Georgia,serif!important;font-size:19px!important}
.ebook-settings,.ebook-search-panel{display:none;flex-shrink:0;padding:18px 24px;background:var(--ebook-panel);border-bottom:1px solid var(--ebook-line)}
.ebook-settings[data-open=true]{display:flex;flex-wrap:wrap;gap:20px 32px}.ebook-search-panel[data-open=true]{display:flex;align-items:center;gap:10px}
.ebook-setting-group{display:flex;flex-direction:column;gap:8px}.ebook-setting-label{font-size:11px;font-weight:600;letter-spacing:1px;text-transform:uppercase;color:var(--ebook-muted)}.ebook-setting-controls{display:flex;align-items:center;gap:4px}.ebook-settings button{border-color:var(--ebook-line);background:var(--ebook-panel);font-size:13px}
.ebook-query{flex:1;min-width:0;max-width:500px;height:36px;padding:8px 12px;border:1px solid var(--ebook-line);border-radius:7px;background:var(--ebook-bg)}.ebook-search-panel [data-ebook-command=search]{background:var(--ebook-accent);color:var(--ebook-panel)}
.ebook-workspace{display:flex;flex:1;min-height:0}.ebook-sidebar{flex-shrink:0;width:244px;overflow:auto;padding:24px 16px;background:var(--ebook-panel);border-right:1px solid var(--ebook-line)}
.ebook-reader[data-sidebar=closed] .ebook-sidebar{display:none}.ebook-book{display:flex;align-items:center;gap:12px;padding:0 8px 24px}.ebook-cover{width:44px;height:62px;object-fit:contain;background:var(--ebook-bg);border:1px solid var(--ebook-line);border-radius:4px}.ebook-book-title{font-size:14px;font-weight:600;line-height:1.4}.ebook-author{margin:5px 0 0;font-size:12px;color:var(--ebook-muted)}
.ebook-sidebar h3{margin:20px 10px 10px;font-size:11px;font-weight:600;letter-spacing:1px;text-transform:uppercase;color:var(--ebook-muted)}.ebook-sidebar ol{list-style:none;margin:0;padding:0}.ebook-sidebar ol ol{padding-left:12px}.ebook-sidebar li{margin:3px 0}
.ebook-sidebar button{display:flex;width:100%;justify-content:flex-start;text-align:left;padding:10px 12px;font-size:13px;line-height:1.4}.ebook-sidebar button[aria-current=location]{background:var(--ebook-hover);color:var(--ebook-accent);font-weight:600}.ebook-sidebar li>span{display:block;padding:10px 12px}
.ebook-bookmarks:empty::after{content:'Your saved places appear here.';display:block;padding:4px 12px;color:var(--ebook-muted);font-size:12px;line-height:1.5}
.ebook-content{flex:1;min-width:0;display:flex;flex-direction:column;margin:24px 28px;background:var(--ebook-panel);border:1px solid var(--ebook-line);border-radius:8px;overflow:hidden;box-shadow:0 3px 16px #20352908}
.ebook-frame{display:block;width:100%;flex:1;min-height:120px;border:0;background:var(--ebook-panel)}.ebook-results{max-height:170px;overflow:auto}.ebook-results:empty{display:none}.ebook-results button{display:flex;width:100%;justify-content:flex-start;text-align:left;border-radius:0;padding:12px 20px;border-bottom:1px solid var(--ebook-line)}
.ebook-notices{padding:10px 20px;color:var(--ebook-muted);font-size:12px;border-top:1px solid var(--ebook-line)}.ebook-notices:empty{display:none}
.ebook-footer{display:flex;align-items:center;justify-content:space-between;gap:12px;min-height:52px;padding:8px 20px;background:var(--ebook-panel);border-top:1px solid var(--ebook-line)}.ebook-chapter-controls,.ebook-page-controls{display:flex;align-items:center;gap:4px}.ebook-progress{display:flex;align-items:center;gap:16px;font-size:12px;color:var(--ebook-muted)}.ebook-footer button{font-size:12px;min-height:30px}.ebook-page-controls button{width:32px;padding:5px}
@media(max-width:760px){.ebook-toolbar{padding:10px 12px;gap:6px}.ebook-history{padding-right:6px}.ebook-sidebar{width:200px;padding:16px 10px}.ebook-content{margin:12px}.ebook-footer{padding:8px 12px;gap:6px}.ebook-footer .ebook-chapter-controls button{font-size:0;gap:0;padding:5px}.ebook-progress{gap:8px}.ebook-settings{padding:14px 16px;gap:16px}.ebook-title{font-size:14px}}
@media(max-width:540px){.ebook-sidebar{display:none}.ebook-reader[data-sidebar=open] .ebook-sidebar{display:block;width:180px}.ebook-history{display:none}.ebook-content{margin:8px}.ebook-progress{font-size:11px}}
"

let icons = {back:"M14 6l-6 6 6 6",forward:"M10 6l6 6-6 6",contents:"M4 5h16M4 12h16M4 19h16",
    search:"M21 21l-5-5M18 10a8 8 0 1 1-16 0 8 8 0 0 1 16 0",bookmark:"M6 3h12v18l-6-4-6 4z",
    previous:"M5 5v14M18 6l-7 6 7 6",next:"M19 5v14M6 6l7 6-7 6"}
fn button(command,label,icon = null) => <button type:"button",class:if (icon == null) "" else "ebook-icon-button",
    title:label,["aria-label"]:label,["data-ebook-command"]:command,
    if (icon == null) label else <svg class:"ebook-icon",viewBox:"0 0 24 24",fill:"none",stroke:"currentColor",
        ["stroke-width"]:"1.7",["stroke-linecap"]:"round",["stroke-linejoin"]:"round",["aria-hidden"]:"true",
        <path d:icons[icon]>>>
fn contents(nodes) => <ol *[for (node in nodes) <li
    if (node.target != null) <button type:"button",["data-ebook-command"]:"go",
        ["data-ebook-href"]:node.target.href,["data-ebook-fragment"]:c.value(node.target.fragment,""),node.label>
    else <span node.label> if (len(node.children) > 0) contents(node.children)>]>

fn cycle(current,values) {
    let index = c.first([for (i,value in values where value == current) i]);
    values[(c.value(index,0)+1)%len(values)]
}
fn srcdoc(chapter,prefs) {
    let colors = {light:{bg:"#fff",fg:"#202b37"},sepia:{bg:"#f4ecd8",fg:"#46392a"},dark:{bg:"#18222d",fg:"#e6e5df"}}[prefs.theme]
    let css = if (chapter.layout == "fixed") "html,body{margin:0;padding:0}body{position:relative;width:" ++
        string(chapter.viewport.width) ++ "px;height:" ++ string(chapter.viewport.height) ++ "px;transform-origin:0 0}"
        else "html{background:" ++ colors.bg ++ ";color:" ++ colors.fg ++ "}body{margin:" ++ string(prefs.margin) ++
        "px auto;padding:0 " ++ string(prefs.margin) ++ "px;max-width:48em;font-size:" ++ string(prefs.font_size) ++
        "px!important;line-height:" ++ string(prefs.line_height) ++ "!important;" ++
        (if (prefs.font_family == "publisher") "" else "font-family:" ++ prefs.font_family ++ "!important;") ++
        "}img,svg{max-width:100%;height:auto}" ++
        (if (prefs.theme == "light") "" else "body,p,li,h1,h2,h3,h4,blockquote{color:" ++ colors.fg ++
            "!important;background:" ++ colors.bg ++ "!important}") ++
        (if (prefs.mode != "paged") "" else "body{margin:" ++ string(prefs.margin) ++ "px;padding:0;max-width:none;width:calc(100vw - " ++
            string(prefs.margin*2) ++ "px);height:calc(100vh - " ++ string(prefs.margin*2) ++
            "px);column-count:1;column-fill:auto;column-gap:" ++ string(prefs.margin*2) ++ "px}");
    replace(chapter.html,"</head>","<style id=\"ebook-preferences\">" ++ css ++ "</style></head>")
}
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
            button("find","Search book","search");
            button("bookmark","Bookmark this place","bookmark");
            <button type:"button",class:"ebook-settings-toggle",title:"Reading settings",["aria-label"]:"Reading settings",
                ["aria-expanded"]:"false",["data-ebook-command"]:"settings","Aa">>
        <div class:"ebook-search-panel",["data-open"]:"false",
            <input class:"ebook-query",type:"search",placeholder:"Find a passage in this book…",["aria-label"]:"Search book">
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
                } else button("mode","Scrolling")>>>
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
                <h3 "Bookmarks"> <div class:"ebook-bookmarks">>
            <main class:"ebook-content",<div class:"ebook-results">
                <iframe class:"ebook-frame",title:"Book content",sandbox:"allow-same-origin",["data-navigation-event"]:"ebook_link",srcdoc:srcdoc(chapter,saved.preferences)>
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
fn node_for(host,locator) => if (locator == null or locator.node == null) null else
    dom.query_selector(root(host),"[data-ebook-node='" ++ locator.node ++ "']")
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
pn reveal(host,node,prefs) {
    if (node == null) { return null }
    let frame = dom.query_selector(host,".ebook-frame")
    let box = dom.bounding_box(node)
    let width = dom.scroll_metrics(frame).client_width
    if (prefs.mode == "paged") { dom.set_scroll_state(frame,floor(box.left/max(1,width))*width,0) }
    else { dom.set_scroll_state(frame,0,max(0,box.top-prefs.margin)) }
}
pn restore(host,session) {
    let node = node_for(host,session.location)
    reveal(host,node,session.preferences)
    let styles = dom.stylesheets(root(host))
    for (sheet in styles) {
        let style_node = dom.stylesheet_get_owner_node(sheet)
        if (style_node != null and dom.get_attribute(style_node,"id") != "ebook-preferences")
            dom.stylesheet_set_disabled(sheet,not session.preferences.publisher)
    }
}
pn chrome(host,book,session) {
    dom.set_attribute(host,"data-theme",session.preferences.theme)
    let labels = {mode:if (session.preferences.mode == "paged") "Paged" else "Scrolling",
        theme:session.preferences.theme ++ " theme",font:session.preferences.font_family ++ " font",
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
pn page_status(host,book,session) {
    let frame = dom.query_selector(host,".ebook-frame")
    let metrics = dom.scroll_metrics(frame)
    let width = metrics.client_width
    let total = dom.scroll_metrics(root(host)).scroll_width
    let scroll = dom.scroll_state(frame)
    let label = if (book.reading_order[session.index].layout == "fixed") "Fixed layout"
        else if (session.preferences.mode != "paged") "Scrolling" else
            "Page " ++ string(int(floor(scroll.x/max(1,width)))+1) ++ " / " ++ string(int(ceil(total/max(1,width))));
    dom.set_text_content(dom.query_selector(host,".ebook-page-status"),label)
    chrome(host,book,session)
}
pn mount(host,book,session) map^ {
    let chapter = if (session.chapter.occurrence == session.index) session.chapter else chapters.load(book,session.index)^
    dom.set_attribute(dom.query_selector(host,".ebook-frame"),"srcdoc",srcdoc(chapter,session.preferences))
    dom.set_text_content(dom.query_selector(host,".ebook-status"),string(session.index+1) ++ " / " ++ string(len(book.reading_order)))
    notices(host,join([for (issue in chapter.diagnostics) issue.message],"; "))
    dom.request_frame(host,"ebook_ready")
    return {*:session,chapter:chapter}
}
pn navigate(host,book,session,target,record = true) map^ {
    let matches = [for (entry in book.reading_order where target.href in [entry.href,entry.source_href]) entry]
    if (len(matches) == 0) { notices(host,"The destination is outside this book."); return session }
    let index = c.value(target.spine_occurrence,matches[0].occurrence)
    let chapter = if (session.chapter.occurrence == index) session.chapter else chapters.load(book,index)^
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
pn bookmarks(host,session) {
    let nodes = [for (i,bookmark in session.bookmarks) <button type:"button",["data-ebook-command"]:"saved",
        ["data-ebook-index"]:string(i),"Chapter " ++ string(bookmark.spine_occurrence+1) ++ ": " ++ bookmark.quote>]
    dom.set_inner_html(dom.query_selector(host,".ebook-bookmarks"),if (len(nodes) == 0) "" else format(nodes,"html")^)
}
pn action(host,book,session,command,target = null) map^ {
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
    else if (command in ["larger","smaller","font","spacing","margin","theme","publisher","mode","zoom-in","zoom-out"]) {
        let prefs = session.preferences
        next.preferences = {*:prefs,font_size:max(12,min(40,prefs.font_size + (if (command == "larger") 2 else if (command == "smaller") -2 else 0))),
            theme:if (command == "theme") cycle(prefs.theme,["light","sepia","dark"]) else prefs.theme,
            font_family:if (command == "font") cycle(prefs.font_family,["publisher","serif","sans-serif","monospace"]) else prefs.font_family,
            line_height:if (command == "spacing") cycle(prefs.line_height,[1.4,1.6,1.8,2.0]) else prefs.line_height,
            margin:if (command == "margin") cycle(prefs.margin,[16,24,40,64]) else prefs.margin,
            publisher:if (command == "publisher") not prefs.publisher else prefs.publisher,
            mode:if (command == "mode") (if (prefs.mode == "paged") "scroll" else "paged") else prefs.mode,
            zoom:max(0.5,min(3.0,prefs.zoom + (if (command == "zoom-in") 0.25 else if (command == "zoom-out") -0.25 else 0.0)))}
        next = mount(host,book,next)^
    } else if (command in ["page-next","page-previous"]) {
        let frame = dom.query_selector(host,".ebook-frame")
        let step = if (command == "page-next") 1 else -1
        let scroll = dom.scroll_state(frame)
        let fixed = book.reading_order[session.index].layout == "fixed"
        let paged = session.preferences.mode == "paged"
        let content = dom.scroll_metrics(root(host))
        let metrics = dom.scroll_metrics(frame)
        let extent = if (paged) content.scroll_width else content.scroll_height
        let size = if (paged) metrics.client_width else metrics.client_height
        let position = if (paged) scroll.x else scroll.y
        if (fixed or (step == 1 and position+size >= extent-1) or (step == -1 and position <= 0)) {
            next = chapter_step(host,book,next,step)^
        } else if (paged) { dom.set_scroll_state(frame,max(0,scroll.x+step*size),0) }
        else { dom.set_scroll_state(frame,scroll.x,max(0,scroll.y+step*size*0.9)) }
        if (next.index == session.index) { next = {*:next,location:capture(host,book,next)^} }
        else if (step == -1) { next = {*:next,edge:"end"} }
        page_status(host,book,next)
    } else if (command == "bookmark") {
        let already = any([for (bookmark in session.bookmarks) bookmark == next.location])
        next.bookmarks = if (already) [for (bookmark in session.bookmarks where bookmark != next.location) bookmark]
            else [*session.bookmarks,next.location]
        bookmarks(host,next)
    } else if (command == "saved" or command == "result") {
        let index = int(target.index)^
        let loc = if (command == "saved") session.bookmarks[index] else session.results[index].locator
        next = navigate(host,book,next,loc)^
    } else if (command == "search") {
        let query = dom.get_state(dom.query_selector(host,".ebook-query"),"value")
        let results = locations.search(book,query)^
        next.results = results
        dom.set_inner_html(dom.query_selector(host,".ebook-results"),format([for (i,result in results)
            <button type:"button",["data-ebook-command"]:"result",["data-ebook-index"]:string(i),result.snippet>],"html")^)
        notices(host,string(len(results)) ++ " search results")
    }
    chrome(host,book,next)
    store.save(book,next)^ { notices(host,"Reading state could not be saved: " ++ ^.message) }
    return next
}
pn initialize(host,session) {
    if (host == null or dom.get_attribute(host,"data-ebook-initialized") != null) { return null }
    dom.set_attribute(host,"data-ebook-initialized","true")
    bookmarks(host,session)
    dom.watch_window(host,"ebook_resize",true)
    dom.request_frame(host,"ebook_ready")
}
view <ebook_reader> state session: {*:~.saved,index:~.chapter.occurrence,chapter:~.chapter,results:[],fragment:null,edge:null,history:[~.start],cursor:0,location:~.start} {
    ~.rendered
}
on load(evt) {
    initialize(owner(evt.target),session)
    return 'handled'
}
on render(evt) {
    initialize(owner(evt.target),session)
    return 'handled'
}
on ebook_ready(evt) {
    let host = owner(evt.target)
    if (host == null or root(host) == null) { return 'pass' }
    initialize(host,session)
    restore(host,session)
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
        if (session.preferences.mode == "paged") {
            dom.set_scroll_state(frame,max(0,(ceil(content.scroll_width/max(1,metrics.client_width))-1)*metrics.client_width),0)
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
    session = {*:session,location:capture(host,~.book,session)^}
    page_status(host,~.book,session)
    store.save(~.book,session)^ { notices(host,"Reading state could not be saved: " ++ ^.message) }
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
            index:dom.get_attribute(button,"data-ebook-index")})^
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
    let chapter = chapters.load(book,initial(book,saved))^;
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
