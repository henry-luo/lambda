// Reader policy and mutation live in Lambda views/handlers (D7.2.4, S12.1.3).
import dom
import c: .common
import chapters: .chapter
import locations: .location
import store: .state

pub let stylesheet = ".ebook-reader{height:100vh;display:flex;flex-direction:column;font:15px system-ui,sans-serif;background:#f4f1eb;color:#202b37}.ebook-toolbar{display:flex;gap:8px;align-items:center;flex-wrap:wrap;padding:12px;background:#fff;border-bottom:1px solid #ccc}.ebook-toolbar button{padding:7px 12px}.ebook-workspace{display:flex;flex:1;min-height:0}.ebook-sidebar{width:220px;overflow:auto;padding:16px;border-right:1px solid #ccc}.ebook-sidebar button{display:block;text-align:left;border:0;background:transparent;padding:7px;color:#31526d;cursor:pointer}.ebook-sidebar ol{padding-left:18px}.ebook-content{flex:1;min-width:0;display:flex;flex-direction:column}.ebook-frame{width:100%;flex:1;min-height:200px;border:0;background:white}.ebook-status{padding:8px 16px;border-top:1px solid #ccc}.ebook-title{font-weight:600;margin-right:auto}.ebook-results{max-height:140px;overflow:auto}.ebook-results button{display:block;background:white;border:0;padding:8px;text-align:left}.ebook-notices{padding:6px 16px;color:#774522;font-size:13px}.ebook-query{width:160px}"

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
    <div class:"ebook-reader",tabindex:"0",role:"region",["aria-label"]:"eBook reader",
        <header class:"ebook-toolbar",
            <span class:"ebook-title",c.value(book.metadata.titles[0],"Untitled")>
            <button type:"button",["data-ebook-command"]:"back","Back">
            <button type:"button",["data-ebook-command"]:"forward","Forward">
            <button type:"button",["data-ebook-command"]:"previous","Previous chapter">
            <button type:"button",["data-ebook-command"]:"next","Next chapter">
            <button type:"button",["data-ebook-command"]:"page-previous","Previous page">
            <button type:"button",["data-ebook-command"]:"page-next","Next page">
            <button type:"button",["data-ebook-command"]:"mode","Reading mode">
            <button type:"button",["data-ebook-command"]:"smaller",["aria-label"]:"Smaller text","A−">
            <button type:"button",["data-ebook-command"]:"larger",["aria-label"]:"Larger text","A+">
            <button type:"button",["data-ebook-command"]:"font","Font">
            <button type:"button",["data-ebook-command"]:"spacing","Line spacing">
            <button type:"button",["data-ebook-command"]:"margin","Margins">
            <button type:"button",["data-ebook-command"]:"theme","Theme">
            <button type:"button",["data-ebook-command"]:"publisher","Publisher styles">
            <button type:"button",["data-ebook-command"]:"zoom-out","Zoom−">
            <button type:"button",["data-ebook-command"]:"zoom-in","Zoom+">
            <button type:"button",["data-ebook-command"]:"bookmark","Bookmark">
            <input class:"ebook-query",type:"search",placeholder:"Search book",["aria-label"]:"Search book">
            <button type:"button",["data-ebook-command"]:"search","Search">>
        <div class:"ebook-workspace",
            <nav class:"ebook-sidebar",["aria-label"]:"Table of contents",<h3 "Contents">
                if (cover.value != null and cover.value != "") <img class:"ebook-cover",src:cover.value,alt:"Book cover",style:"max-width:100%;height:auto">;
                if (len(book.metadata.creators) > 0) <p class:"ebook-author",join(book.metadata.creators,", ")>;
                if (len(book.navigation.toc) > 0) contents(book.navigation.toc) else
                    contents([for (entry in book.reading_order) {label:"Chapter " ++ string(entry.occurrence+1),
                        target:{href:entry.href},children:[]}]);
                if (len(book.navigation.landmarks) > 0) <section <h3 "Landmarks"> contents(book.navigation.landmarks)>;
                if (len(book.navigation.page_list) > 0) <section <h3 "Publisher pages"> contents(book.navigation.page_list)>;
                <h3 "Bookmarks"> <div class:"ebook-bookmarks">>
            <main class:"ebook-content",<div class:"ebook-results">
                <iframe class:"ebook-frame",title:"Book content",sandbox:"allow-same-origin",["data-navigation-event"]:"ebook_link",srcdoc:srcdoc(chapter,saved.preferences)>
                <div class:"ebook-notices",saved.message>
                <footer <span class:"ebook-status",string(index+1) ++ " / " ++ string(len(book.reading_order))>
                    <span class:"ebook-page-status"> >>>>
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
    dom.set_inner_html(dom.query_selector(host,".ebook-bookmarks"),format(nodes,"html")^)
}
pn action(host,book,session,command,target = null) map^ {
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
