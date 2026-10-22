// Model checks for the archive demo: planning under the demo's configuration,
// the pure derivations over a fixture capture, and the model's filters, job
// table, requests and labels (vibe/Lambda_Pkg_Archive.md §9).
import archive: lambda.archive
import config: lambda.archive.config
import model: ~~.mod_model

// the demo's configuration is clean, and its rules classify as the demo expects
let cfg_value = input("test/demo/archive/archive.yaml", 'yaml')^
archive.problems(cfg_value)
let cfg = config.resolve(cfg_value).config
let plans = [for (u in ["https://www.youtube.com/watch?v=x", "https://example.org/report.pdf", "https://www.flickr.com/photos/x",
    "https://example.org/blog/post"]) archive.plan(u, cfg)^];
[for (p in plans) [p.kind, p.engine]];
[plans[3].settings.jobs, plans[3].settings.per_host_delay_ms, plans[3].formats]

// a capture is enough for the derivations: no network, no files written
let site = "file://" ++ sys.proc.self.cwd# ++ "/test/lambda/archive/site/"
let capture = {source: input("test/lambda/archive/site/page.html", 'text')^, final_url: site ++ "page.html",
    manifest: [{url: site ++ "style.css", path: "style.css", status: "ok"}, {url: site ++ "img/pier.svg", path: "pier.svg", status: "ok"}],
    archived: "2026-10-22T00:00:00Z"}
let snapshot = archive.snapshot_html(capture)^;
[contains(snapshot, "href=\"assets/style.css\""), contains(snapshot, "src=\"assets/pier.svg\""), contains(snapshot, "<script")]
let reader = archive.readable(capture)^;
[contains(reader, "<h1>The quiet harbour: a walk along the old pier</h1>"), contains(reader, "assets/pier.svg")]
starts_with(archive.markdown(capture)^, "---\ntitle: \"The quiet harbour: a walk along the old pier\"")

// the listing: newest first, kinds and sites with counts, filters
let r1 = {id: "a", url: "https://ledger.example/trees", title: "Shade", site: "The Meridian Ledger", kind: "page",
    archived: "2026-10-20T10:00:00Z", outputs: {html: {status: "ok"}, readable: {status: "ok"}, thumb: {status: "ok"}}}
let r2 = {id: "b", url: "file:///x/video.html", title: "Harbour at dawn", site: null, kind: "video",
    archived: "2026-10-21T10:00:00Z", outputs: {html: {status: "ok"}, pdf: {status: "failed", message: "render export: …"}}}
let r3 = {id: "c", url: "https://ledger.example/home", title: "Front page", site: "The Meridian Ledger", kind: "page",
    archived: "2026-10-19T10:00:00Z", outputs: {html: {status: "ok"}}}
let st = model.loaded(model.initial(), [r1, r2, r3]);
[[for (r in st.records) r.id], st.selected, st.loaded]
model.kind_counts(st.records)
model.site_counts(st.records);
[for (r in model.visible(model.action(st, "kind", "page"))) r.id];
[for (r in model.visible(model.action(st, "site", "Local files"))) r.id]
model.action(model.action(st, "site", "Local files"), "site", "Local files").site;
[model.badges(r1), model.badges(r2)]
model.output_rows(r2)

// requests: a path becomes a file: URL; a forced kind and the formats travel along
let typed = {*: st, url: "test/lambda/archive/site/short.html", force: "page"}
let add = model.request_add(typed, "/work")
add.request;
[add.state.url, add.state.serial]
model.request_add(st, "/work").state.note
model.request_refresh(st, "pdf").request
model.request_refresh(st, null).request.formats;
[model.url_of("https://a.example/x", "/w"), model.url_of("/abs/p.html", "/w"), model.url_of("  ", "/w")];
[for (f in [model.action(st, "format", "pdf").formats, model.action(st, "format", "readable").formats]) f]

// the job table: queued, progress, done (a user job opens its snapshot), failed
let q = model.apply_job(st, {type: "queued", tag: "job-1", url: "https://x.example/a"})
let p = model.apply_job(q, {type: "progress", tag: "job-1", stage: "fetch", received: 2048, total: 8192, rate: 1024})
let row = p.jobs[0];
[row.status, model.progress_label(row), model.progress_fraction(row)]
let assets = model.apply_job(p, {type: "progress", tag: "job-1", stage: "assets", received: 2, total: 5, rate: null})
model.progress_label(assets.jobs[0])
let new_record = {id: "d", url: "https://x.example/a", title: "A", site: null, kind: "page",
    archived: "2026-10-22T10:00:00Z", outputs: {html: {status: "ok"}}}
let done = model.apply_job(p, {type: "done", tag: "job-1", snapshot: new_record});
[len(done.jobs), done.records[0].id, done.selected]
let seeded = model.apply_job(st, {type: "done", tag: "sample-1", snapshot: new_record})
seeded.selected
let failed = model.apply_job(q, {type: "failed", tag: "job-1", url: "https://x.example/a", message: "archive: HTTP 404"});
[failed.jobs[0].status, model.progress_label(failed.jobs[0]), len(model.action(failed, "dismiss", "job-1").jobs)]

// search hits replace the list, keep the filters, and show snippets
let hits = model.with_hits(st, "harbour", [{snapshot: r2, score: 1.5, snippet: "…the harbour wakes…"}]);
[[for (r in model.visible(hits)) r.id], model.snippet_of(hits, "b"), model.with_hits(hits, " ", []).hits]
// deleting asks first, then picks the next snapshot
let asked = model.action(st, "delete", null);
[asked.confirm, model.action(asked, "keep", null).confirm]
let gone = model.removed(asked, "b");
[[for (r in gone.records) r.id], gone.selected, gone.confirm];
[model.date_label("2026-10-21T14:30:12Z"), model.date_label(null), model.size_label(1536), model.size_label(5242880), model.size_label(12)]
