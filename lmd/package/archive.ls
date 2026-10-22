// lambda.archive — download what a link points to, preserve it in several
// formats, keep a searchable library (vibe/Lambda_Pkg_Archive.md). The facade:
// library · plan · job · add · service · refresh · search · snapshots ·
// remove · doctor, and the pure derivations.
import config: .archive.config
import planning: .archive.plan
import derive: .archive.derive
import store: .archive.store
import jobs: .archive.jobs
import service_impl: .archive.service

pub let FORMATS = config.FORMATS
pub let KINDS = config.KINDS
// the configuration problems of a value, as "path: message" lines; empty when usable
pub fn problems(given) => config.resolve(given).problems

// a Library value (S1.4): the root, the merged configuration and the records
pub pn library(root, options = null) map^ {
    let o = options or {}
    let resolved = config.resolve(o.config)
    if (len(resolved.problems) > 0) raise error("archive: invalid configuration: " ++ join(resolved.problems, "; "))
    store.ensure(root)^
    let records = store.load(root)^;
    {root: root, config: resolved.config, records: records}
}

// classification and engine arguments, no IO (AR3)
pub fn plan(url, cfg, options = null) map^ => planning.plan(url, cfg, options)^

// the pure derivations over a capture {source, manifest, final_url, url?, archived?} (AR3)
fn parsed(capture) map^ => derive.capture(capture.source, capture.manifest, capture.final_url)^
fn meta(c, art, capture) => derive.meta_of(c, art, capture.url or capture.final_url, capture.archived, null)
pub fn snapshot_html(capture) string^ => derive.snapshot_html(parsed(capture)^)
pub fn readable(capture) string?^ => (let c = parsed(capture)^, let art = derive.article_of(c),
    if (art == null) null else derive.readable_html(c, art, meta(c, art, capture)))
pub fn markdown(capture) string?^ => (let c = parsed(capture)^, let art = derive.article_of(c),
    if (art == null) null else derive.markdown_text(c, art, meta(c, art, capture))^)

// the library with one record added or replaced (callers fold a job's result in)
pub fn with_record(lib, record) => jobs.with_record(lib, record)

// One archive job (AR12v2): one first fetch to disk, the engine its kind
// names, the worker's renderings, pack, write; meant to run as a task.
// `options.progress` is a callback receiving {type: 'progress', tag, stage,
// received, total, rate}; only acquisition failure raises (AR9).
pub pn job(lib, url, options = null) map^ { jobs.run(lib, url, options)^ }

// `job` in the caller's task, for scripts that do not need concurrency
pub pn add(lib, url, options = null) map^ { jobs.run(lib, url, options)^ }

// Re-derives a snapshot's outputs from its stored capture, without the
// network (AR3, AR9): `formats` names the outputs to redo (all of the
// record's, the text and the thumbnail when omitted); the others keep their
// members and files. The library's current configuration applies, with the
// formats chosen at capture, so a PDF that failed under one setting can be
// retried under another. The ZIP is rewritten and the folder replaced whole.
pub pn refresh(lib, id, formats = null) map^ { jobs.refresh(lib, id, formats)^ }

// The job service (§8): a long-running task archiving the URLs sent to it,
// up to `defaults.jobs` at a time, reporting to `notify` (service.ls).
pub pn service(lib, notify = null) null^ { service_impl.serve(lib, notify)^ }

// Full-text search over the snapshots' text.txt (AR8): `options` filters as
// `snapshots` does and takes `limit` and `snippet` (words, default 24). A
// snapshot whose title, byline, site or excerpt matches ranks above one that
// matches in its body only.
pub pn search(lib, query, options = null) array^ {
    let o = options or {}
    // the index as it is now, not as `lib` last saw it: results are always current
    let rows = snapshots({*: lib, records: store.current(lib.root)^}, o)
    let text_of = (r) => lib.root ++ "/snapshots/" ++ r.id ++ "/text.txt"
    let files = [for (r in rows where exists(text_of(r))) text_of(r)]
    if (len(files) == 0) { return [] }
    let by_file = reduce([{}, *rows], (acc, r) => {*: acc, [text_of(r)]: r})
    let hits = io.text_search(files, query, {snippet: o.snippet or 24})^
    // the head paragraph of text.txt holds the record's fields
    let heads = [for (h in io.text_search(files, query, {unit: "paragraph", rank: false})^ where h.index == 0) h.file]
    let ranked = [*[for (h in hits where h.file in heads) h], *[for (h in hits where not (h.file in heads)) h]]
    let limited = if (o.limit == null) ranked else take(ranked, int(o.limit));
    [for (h in limited) {snapshot: by_file[h.file], score: h.score, snippet: h.snippet}]
}

// the records matching a filter: kind, host, tag, since/until (ISO text), sort 'newest' | 'oldest'
pub fn snapshots(lib, filter = null) array {
    let f = filter or {}
    let rows = [for (r in lib.records where (f.kind == null or r.kind == string(f.kind)) and
        (f.host == null or host_of(r.url) == f.host) and (f.tag == null or (f.tag in (r.tags or []))) and
        (f.since == null or r.archived >= f.since) and (f.until == null or r.archived <= f.until)) r];
    sort(rows, {dir: if (f.sort == "oldest") 'asc' else 'desc', by: (r) => r.archived})
}
fn host_of(url) => (let u = planning.parse(url), if (u == null) null else u.host)

// the files a record says its folder holds
fn expected_files(r) => [*["snapshot.json", "snapshot.zip"],
    *[for (kv in [["text", "text.txt"], ["pdf", "page.pdf"], ["thumb", "thumb.png"]]
        where r.outputs[kv[0]] != null and r.outputs[kv[0]].status == "ok") kv[1]],
    *(if (r.media == null) [] else [for (f in r.media.files) f.path])]
fn bytes_of(entries) => sum([for (e in entries where not e.is_dir) e.size or 0])

// The library's health (§8): the configuration's problems, snapshots and
// bytes per kind, staging folders a job left behind, and snapshots whose
// record names files their folder no longer holds.
pub pn doctor(lib) map^ {
    let records = store.current(lib.root)^
    let staging_dir = lib.root ++ "/.staging"
    let stale = if (exists(staging_dir)) [for (f in input(staging_dir)^ where f.is_dir) f.name] else []
    let rows = [for (r in records, let dir = lib.root ++ "/snapshots/" ++ r.id where exists(dir))
        {record: r, bytes: bytes_of(input(dir)^) + (if (exists(dir ++ "/media")) bytes_of(input(dir ++ "/media")^) else 0),
         missing: [for (name in expected_files(r) where not exists(dir ++ "/" ++ name)) name]}]
    let gone = [for (r in records where not exists(lib.root ++ "/snapshots/" ++ r.id)) r.id]
    let kinds = reduce([{}, *rows], (acc, row) => (let k = row.record.kind, let was = acc[k] or {count: 0, bytes: 0},
        {*: acc, [k]: {count: was.count + 1, bytes: was.bytes + row.bytes}}));
    {problems: config.resolve(lib.config).problems, snapshots: len(rows), bytes: sum([for (row in rows) row.bytes]),
     kinds: kinds, staging: stale, missing: [*[for (row in rows where len(row.missing) > 0) {id: row.record.id, files: row.missing}],
        *[for (id in gone) {id: id, files: ["(folder)"]}]]}
}

// deletes the snapshot folder and updates the index; false when there was none
pub pn remove(lib, id) bool^ {
    let dir = lib.root ++ "/snapshots/" ++ id
    if (not exists(dir)) { return false }
    io.delete(dir)^
    store.update_index(lib.root, null, [id])^
    true
}
