// An archive job and a refresh (AR3, AR9, AR12v2): the procedures behind the
// facade's `job`, `add` and `refresh`, in a module of their own so the job
// service (service.ls) can start them without importing the facade.
import config: .config
import planning: .plan
import transfer: .transfer
import page: .page
import media: .media
import gallery: .gallery
import files: .file
import workers: .workers
import store: .store

// the outputs a snapshot can have; `text` and `thumb` are not formats a caller picks
let OUTPUTS = ["html", "readable", "markdown", "pdf", "text", "thumb"]
let MB = 1024 * 1024

// the library with one record added or replaced (callers fold a job's result in)
pub fn with_record(lib, record) => {*: lib, records: [record, *[for (r in lib.records where r.id != record.id) r]]}

pn notify_through(forward, tag, message) {
    if (forward != null) forward({*: message, tag: tag})
}
// the outputs a job derives: html and text always, the requested formats, the thumbnail when enabled
fn wanted(formats, thumbnail) => [for (o in OUTPUTS where o == "html" or o == "text" or o in formats or (o == "thumb" and thumbnail)) o]
fn not_requested(outputs) => reduce([{}, *[for (o in OUTPUTS where not (o in outputs)) o]],
    (acc, o) => {*: acc, [o]: {status: "skipped", message: "not requested"}})
fn described(record, m) => if (m == null) record else {*: record, title: m.title or record.title, byline: m.byline, site: m.site,
    published: m.published, excerpt: m.excerpt}

// One archive job (the facade's `job`, AR12v2); only acquisition failure
// raises (AR9). Whatever stops it — a failed fetch, a cancellation (319) —
// leaves no staging folder behind: `build` runs under one handler that
// discards it.
pub pn run(lib, url, options = null) map^ {
    let o = options or {}
    let p = planning.plan(url, lib.config, o)^
    // the index as it is on disk, not as `lib` last saw it: a snapshot removed
    // since (by another handler, say) is no longer a duplicate
    let existing = store.duplicate(store.current(lib.root)^, p.normalized)
    let policy = string(p.settings.on_duplicate)
    if (existing != null and policy == "skip") { return existing }
    let stamp = store.stamp()
    let staging = store.stage(lib.root, stamp.prefix ++ "-" ++ store.slug(p.parts.host or ""))^
    var record = null
    var failure = null
    build(lib, url, o, p, stamp, staging) ^ { failure = ^ } ~ { record = ~ }
    if (failure is error) {
        store.discard(staging)^
        raise failure
    }
    if (existing != null and policy == "replace") io.delete(lib.root ++ "/snapshots/" ++ existing.id) ^ { null }
    store.update_index(lib.root, record, if (existing != null and policy == "replace") [existing.id] else [])^
    record
}

pn build(lib, url, o, p, stamp, staging) map^ {
    let settings = p.settings
    let notify = if (o.progress == null) null else pn (m) => { notify_through(o.progress, o.tag, m) }
    // the first fetch goes to disk whatever it turns out to be: the page, the
    // medium, its playlist or the file, so no engine downloads the URL twice
    let fetched = staging ++ "/fetched"
    let first = transfer.fetch(url, settings, {to: fetched, progress: notify, stage: "fetch", tag: o.tag,
        max_bytes: int(p.media.max_mb * MB)})^
    if (not first.ok) raise error("archive: fetch failed: " ++ first.error.message)
    let r = first.response
    if (not r.ok) raise error("archive: HTTP " ++ string(r.status) ++ " for " ++ url)
    // the response's content type decides a kind the URL could not (AR2v2)
    let refined = planning.refine(p, r.headers.'content-type')
    // a page needs no engine of its own here: `fallback` sends it to the page engine below
    let engine = planning.ENGINE_OF[refined.kind]
    let got = if (engine == "media") media.acquire(refined, staging, settings, notify, r, fetched)^
        else if (engine == "gallery") gallery.acquire(refined, staging, settings, notify, r, fetched)^
        else if (engine == "file") files.acquire(refined, staging, settings, notify, r, fetched)^
        else {fallback: true, source: null, remote: [], reason: null}
    // a page, or a page that names nothing the media and gallery engines can take (AR-O14)
    let as_page = got.fallback == true
    let source = if (not as_page) got.source else if (got.source != null) got.source else input(fetched, 'text')^
    if (exists(fetched)) io.delete(fetched)^
    let capture = if (as_page) page.acquire(refined, staging, settings, notify, r, source)^ else null
    if (as_page) {
        output(capture.source, staging ++ "/source.html")^
    } else {
        output(format(got.info, 'json'), staging ++ "/media/info.json")^
        if (source != null) output(source, staging ++ "/source.html")^
    }
    let resources = if (as_page) capture.manifest else []
    output(format({format: 1, url: url, final_url: r.url, base: if (as_page) capture.base else r.url, resources: resources,
        skipped: if (as_page) capture.skipped else (got.info.skipped or [])}, 'json'), staging ++ "/manifest.json")^
    let kind = if (as_page) "page" else got.info.kind
    let outputs = wanted(p.formats, settings.thumbnail)
    let title_hint = if (as_page) (page.title_of(capture.html) or p.parts.host or url) else got.info.title
    let derived = workers.derive(staging, {url: url, final_url: r.url, archived: stamp.iso, title_hint: title_hint,
        kind: kind, meta: null, outputs: outputs, pdf: settings.pdf}, settings, notify)^
    notify_through(o.progress, o.tag, {type: "progress", stage: "pack", received: 0, total: null, rate: null})
    let zip_bytes = store.pack(staging, store.staged_members(staging, resources)^, "snapshot.zip")^
    store.tidy(staging)^
    let m = derived.meta or {title: title_hint}
    let id = store.unique_id(lib.root, stamp.prefix ++ "-" ++ store.slug(m.title))
    let record = described({format: 1, id: id, url: url, normalized: p.normalized, final_url: r.url, kind: kind,
        title: title_hint, archived: stamp.iso, status: r.status, content_type: r.headers.'content-type', tags: o.tags or [],
        engine: {name: if (as_page) "page" else planning.ENGINE_OF[kind], version: "lambda " ++ string(sys.lambda.version)},
        settings: settings, outputs: config.overlay(not_requested(outputs), derived.outputs),
        assets: if (as_page) {count: capture.assets_count, bytes: capture.assets_bytes, skipped: capture.skipped}
            else {count: 0, bytes: 0, skipped: []},
        media: if (as_page) null else media_summary(got.info),
        // what a page named but could not be taken, when it was archived as a page instead
        remote: if (as_page) (got.remote or []) else (got.info.remote or []),
        note: if (as_page and refined.kind != "page" and refined.kind != null) got.reason else null,
        zip_bytes: zip_bytes, readable: derived.readable == true}, derived.meta)
    store.finish(lib.root, staging, id, record)^
}

// the record's view of media/info.json: what was stored, for listing without reading the file
fn media_summary(info) => {files: [for (f in info.files) {path: f.path, role: f.role, container: f.container, bytes: f.bytes,
    width: f.width, height: f.height}], subtitles: [for (t in info.subtitles or []) t.lang], skipped: len(info.skipped or [])}

// Re-derives a snapshot's outputs from its stored capture, without the
// network (AR3, AR9): `formats` names the outputs to redo (all of the
// record's, the text and the thumbnail when omitted); the others keep their
// members and files. The library's current configuration applies, with the
// formats chosen at capture, so a PDF that failed under one setting can be
// retried under another. The ZIP is rewritten and the folder replaced whole.
pub pn refresh(lib, id, formats = null) map^ {
    let dir = lib.root ++ "/snapshots/" ++ id
    if (not exists(dir ++ "/snapshot.json")) raise error("archive: no snapshot '" ++ id ++ "'")
    let record = input(dir ++ "/snapshot.json", 'json')^
    let settings = planning.plan(record.url, lib.config, {formats: record.settings.formats})^.settings
    let outputs = if (formats == null) wanted(settings.formats, settings.thumbnail)
        else [for (o in OUTPUTS where o in [for (f in formats) string(f)]) o]
    let staging = store.stage(lib.root, id ++ "-refresh")^
    store.unpack(dir ++ "/snapshot.zip", staging)^
    // the loose files stay: the renderings not redone, and media/ for the other kinds
    for (name in ["text.txt", "page.pdf", "thumb.png"]) {
        if (exists(dir ++ "/" ++ name)) io.copy(dir ++ "/" ++ name, staging ++ "/" ++ name)^
    }
    if (exists(dir ++ "/media")) store.copy_tree(dir ++ "/media", staging ++ "/media")^
    // a rendering being redone leaves no stale copy behind if it now fails
    let files = {html: "index.html", readable: "readable.html", markdown: "article.md", text: "text.txt", pdf: "page.pdf", thumb: "thumb.png"}
    for (o in outputs) {
        if (exists(staging ++ "/" ++ files[o])) io.delete(staging ++ "/" ++ files[o])^
    }
    let manifest = input(staging ++ "/manifest.json", 'json')^
    let derived = workers.derive(staging, {url: record.url, final_url: record.final_url, archived: record.archived,
        title_hint: record.title, meta: {title: record.title, byline: record.byline, site: record.site,
        published: record.published, excerpt: record.excerpt, url: record.url, final_url: record.final_url,
        archived: record.archived}, kind: record.kind, outputs: outputs, pdf: settings.pdf}, settings, null)^
    let zip_bytes = store.pack(staging, store.staged_members(staging, manifest.resources)^, "snapshot.zip")^
    store.tidy(staging)^
    let updated = described({*: record, settings: settings, outputs: config.overlay(record.outputs, derived.outputs), zip_bytes: zip_bytes,
        readable: if (derived.readable == null) record.readable else derived.readable,
        refreshed: store.stamp().iso}, derived.meta)
    store.swap(lib.root, staging, id, updated)^
    store.update_index(lib.root, updated, [])^
    updated
}

