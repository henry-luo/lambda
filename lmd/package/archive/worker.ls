// The archive worker (AR-O12): `lambda run worker.ls`, with the request's path
// in LAMBDA_ARCHIVE_REQUEST, derives a
// capture's renderings in a child process, so Readability and Radiant's export
// never pause the caller's window. It reads the capture from the staging
// folder, writes each requested output beside it, prints `step <i> <n> <name>`
// before each step (the caller's io.exec progress, AR-E5v2), and ends with
// derived.json: the record's fields and a status per output (AR9).
import derive: lambda.archive.derive
import render: lambda.archive.render
import vtt: lambda.archive.vtt

let ORDER = ["html", "readable", "markdown", "text", "pdf", "thumb"]
let MEMBERS = {html: "index.html", readable: "readable.html", markdown: "article.md"}

pn say(i, n, name) {
    print("step " ++ string(i) ++ " " ++ string(n) ++ " " ++ name ++ "\n")
}

// writes one output file and reports its status
pn write(path, data) map {
    var failure = null
    output(data, path) ^ { failure = ^ }
    if (failure is error) {status: "failed", message: failure.message} else {status: "ok"}
}
fn placed(status, name, bytes) => if (status.status != "ok") status
    else if (MEMBERS[name] != null) {status: "ok", file: "snapshot.zip", member: MEMBERS[name], bytes: bytes}
    else {status: "ok", file: if (name == "text") "text.txt" else if (name == "pdf") "page.pdf" else "thumb.png"}

// one output: written beside the capture, or skipped or failed with the reason
pn derive_one(name, dir, c, art, meta, req) map {
    if (name == "html") {
        let html = derive.snapshot_html(c)
        return placed(write(dir ++ "/index.html", html), name, len(html))
    }
    if (name == "text") { return placed(write(dir ++ "/text.txt", derive.search_text(c, art, meta)), name, null) }
    if (name == "readable" or name == "markdown") {
        if (art == null) { return {status: "skipped", message: "no readable article"} }
        if (name == "readable") { return placed(write(dir ++ "/readable.html", derive.readable_html(c, art, meta)), name, null) }
        let md = derive.markdown_text(c, art, meta) ^ { null }
        if (md == null) { return {status: "failed", message: "Markdown conversion failed"} }
        return placed(write(dir ++ "/article.md", md), name, null)
    }
    // pdf and thumb render a staged page: the readable one when the PDF asks for it and it exists
    let from = if (name == "pdf" and req.pdf.source == "readable" and exists(dir ++ "/readable.html")) "readable.html" else "index.html"
    if (not exists(dir ++ "/" ++ from)) { return {status: "failed", message: "no " ++ from ++ " to render"} }
    let html = input(dir ++ "/" ++ from, 'text')^
    let status = if (name == "pdf") render.pdf(html, dir ++ "/page.pdf", dir, req.pdf)
        else render.thumbnail(html, dir ++ "/thumb.png", dir)
    placed(status, name, null)
}

// one output of a media, gallery, image or file record (media/info.json)
pn derive_info(name, dir, info, transcript, art, meta, req) map {
    if (name == "html") {
        let html = derive.info_page(info, transcript)
        return placed(write(dir ++ "/index.html", html), name, len(html))
    }
    if (name == "text") { return placed(write(dir ++ "/text.txt", derive.info_text(info, transcript, meta)), name, null) }
    if (name == "readable" or name == "markdown") {
        if (not (info.kind in ["video", "audio"])) { return {status: "skipped", message: "not available for a " ++ info.kind} }
        if (art == null) { return {status: "skipped", message: "no description or transcript"} }
        if (name == "readable") { return placed(write(dir ++ "/readable.html", derive.info_readable(art, meta)), name, null) }
        let md = derive.info_markdown(art, meta) ^ { null }
        if (md == null) { return {status: "failed", message: "Markdown conversion failed"} }
        return placed(write(dir ++ "/article.md", md), name, null)
    }
    // the thumbnail is the poster or first image when there is one, else the page drawn
    if (name == "thumb" and info.poster != null and exists(dir ++ "/" ++ info.poster)) {
        return placed(render.image_thumbnail(dir ++ "/" ++ info.poster, dir ++ "/thumb.png"), name, null)
    }
    let from = if (name == "pdf" and req.pdf.source == "readable" and exists(dir ++ "/readable.html")) "readable.html" else "index.html"
    if (not exists(dir ++ "/" ++ from)) { return {status: "failed", message: "no " ++ from ++ " to render"} }
    let html = input(dir ++ "/" ++ from, 'text')^
    let status = if (name == "pdf") render.pdf(html, dir ++ "/page.pdf", dir, req.pdf)
        else render.thumbnail(html, dir ++ "/thumb.png", dir)
    placed(status, name, null)
}

// a media, gallery, image or file record's outputs
pn main_info(req, steps) map^ {
    let dir = req.staging
    let info = input(dir ++ "/media/info.json", 'json')^
    // the transcript is the first subtitle track's text
    let track = if (len(info.subtitles or []) == 0) null else info.subtitles[0]
    let transcript = if (track == null) null else vtt.transcript(input(dir ++ "/" ++ track.path, 'text')^)
    let meta = derive.info_meta(info, req.archived)
    let art = derive.info_article(info, transcript)
    var outputs = {}
    for (i, name in steps) {
        say(i + 1, len(steps), name)
        outputs = {*: outputs, [name]: derive_info(name, dir, info, transcript, art, meta, req)}
    }
    {meta: meta, readable: art != null, outputs: outputs}
}

pn main() {
    let req = input(sys.proc.self.env.LAMBDA_ARCHIVE_REQUEST#, 'json')^
    let dir = req.staging
    let steps = [for (o in ORDER where o in req.outputs) o]
    if (req.kind != null and req.kind != "page") {
        output(main_info(req, steps)^, dir ++ "/derived.json", 'json')^
        return null
    }
    let source = input(dir ++ "/source.html", 'text')^
    let manifest = (input(dir ++ "/manifest.json", 'json')^).resources
    let c = derive.capture(source, manifest, req.final_url)^
    // the article decides the readable renderings and the record's fields
    let needs_article = req.meta == null or any([for (o in ["readable", "markdown", "text"]) o in steps])
    let art = if (needs_article) derive.article_of(c) else null
    let meta = if (needs_article) derive.meta_of(c, art, req.url, req.archived, req.title_hint) else req.meta
    var outputs = {}
    for (i, name in steps) {
        say(i + 1, len(steps), name)
        let status = derive_one(name, dir, c, art, meta, req)
        outputs = {*: outputs, [name]: status}
    }
    // `readable` is known only when the article was looked for
    output({meta: meta, readable: if (needs_article) art != null else null, outputs: outputs}, dir ++ "/derived.json", 'json')^
    null
}
