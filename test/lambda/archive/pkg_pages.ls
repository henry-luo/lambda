import archive: lambda.archive

// M1 acceptance: pages archive over file: URLs into snapshot.zip with their
// resources, a readable page, Markdown and search text; the record is the
// truth and the index a cache (AR5v2, AR7, AR10).
fn member(zip, name) {
    let hits = [for (m in content(zip)^ where m.name == name) m];
    if (len(hits) == 0) null else hits[0]
}
fn text_member(zip, name) => (let m = member(zip, name), if (m == null) null else input(m, 'text')^)

pn collect() {
    var stages = []
    var running = true
    while (running) {
        let m = receive()^
        if (m.type == 'stop') {
            send(m.reply, stages)^
            running = false
        } else if (m.type == "progress" and not (m.stage in stages)) {
            stages = [*stages, m.stage]
        }
    }
}

pn main() {
    // a folder per process: the T0 and JIT runs of this test may overlap
    let root = "temp/archive_test/pages-" ++ string(sys.proc.self.pid#)
    if (exists(root)) io.delete(root)^
    let site = "file://" ++ sys.proc.self.cwd# ++ "/test/lambda/archive/site/"
    let articles = "file://" ++ sys.proc.self.cwd# ++ "/test/demo/readability/articles/"
    // renderings (PDF, thumbnail) are pkg_render's; this test covers capture and derivation
    let lib = archive.library(root, {config: {defaults: {per_host_delay_ms: 0, thumbnail: false}}})^
    let c = start(collect)
    let snap = archive.add(lib, site ++ "page.html", {tag: "harbour", tags: ["walks"],
        progress: pn (m) => { send(c, m)^ }})^
    send(c, {type: 'stop', reply: self()})^
    let stages = receive()^
    let zip = input(root ++ "/snapshots/" ++ snap.id ++ "/snapshot.zip")^
    let names = sort([for (m in content(zip)^) m.name])
    let assets = sort([for (m in content(member(zip, "assets"))^) m.name])
    let index_html = text_member(zip, "index.html")
    let readable_html = text_member(zip, "readable.html")
    let article_md = text_member(zip, "article.md")
    let manifest = input(member(zip, "manifest.json"), 'json')^
    let style = text_member(member(zip, "assets"), "style.css")
    let text = input(root ++ "/snapshots/" ++ snap.id ++ "/text.txt", 'text')^
    let loose = sort([for (f in input(root ++ "/snapshots/" ++ snap.id)^) f.name])
    // the record is the truth: a fresh open reads it back, and the index is a cache
    let again = archive.library(root)^
    let dup = archive.add(archive.with_record(lib, snap), site ++ "page.html")^
    // a short page that is not readerable still gets its snapshot and text
    let short = archive.add(lib, site ++ "short.html", {formats: ["html", "readable"]})^
    // the Reader View demo's pages archive the same way
    let demo = [for (f in ["meridian-trees.html", "crumb-focaccia.html", "fieldnotes-reader-mode.html", "meridian-home.html"])
        archive.add(lib, articles ++ f)^]
    let listed = archive.snapshots(archive.library(root)^, {tag: "walks"})
    let removed = archive.remove(archive.library(root)^, short.id)^
    let results = [snap.kind == "page" and snap.title == "The quiet harbour: a walk along the old pier" and snap.byline == "R. Mariner" and
        snap.site == "Harbour Notes" and snap.readable and snap.tags == ["walks"],
     names == ["article.md", "assets", "index.html", "manifest.json", "readable.html", "source.html"],
     assets == ["bg.svg", "deep.css", "favicon.svg", "harbour.woff2", "header.svg", "pier-2x.svg", "pier.svg", "style.css"],
     snap.assets.count == 8 and snap.assets.skipped == [] and snap.outputs.html.status == "ok" and
        snap.outputs.readable.status == "ok" and snap.outputs.markdown.status == "ok",
     stages == ["fetch", "assets", "render", "pack"],
     loose == ["snapshot.json", "snapshot.zip", "text.txt"] and snap.outputs.pdf.message == "not requested" and
        snap.outputs.thumb.message == "not requested" and snap.outputs.text.status == "ok",
     contains(index_html, "href=\"assets/style.css\"") and contains(index_html, "src=\"assets/pier.svg\"") and
        contains(index_html, "srcset=\"assets/pier.svg 1x, assets/pier-2x.svg 2x\"") and
        contains(index_html, "url(&quot;assets/bg.svg&quot;)") and not contains(index_html, "<script") and
        not contains(index_html, "onload") and contains(index_html, "href=\"#\"") and
        contains(index_html, "href=\"" ++ site ++ "other.html\"") and not contains(index_html, "<base"),
     contains(style, "@import \"deep.css\"") and contains(style, "url(\"bg.svg\")") and contains(style, "url(\"header.svg\")") and
        contains(text_member(member(zip, "assets"), "deep.css"), "url(\"harbour.woff2\")"),
     contains(readable_html, "<h1>The quiet harbour: a walk along the old pier</h1>") and contains(readable_html, "src=\"assets/pier.svg\"") and
        contains(readable_html, "The harbour wakes slowly"),
     starts_with(article_md, "---\ntitle: \"The quiet harbour: a walk along the old pier\"\nbyline: R. Mariner\nsite: Harbour Notes\n") and
        contains(article_md, "![The old pier at dawn](assets/pier.svg)") and contains(article_md, "The harbour wakes slowly"),
     len(manifest.resources) == 8 and all([for (e in manifest.resources) e.status == "ok" and e.bytes > 0]) and
        [for (e in manifest.resources where e.kind == "stylesheet") e.path] == ["style.css", "deep.css"],
     starts_with(text, "The quiet harbour: a walk along the old pier\nR. Mariner\nHarbour Notes\n") and contains(text, "lamp was lit until 1962"),
     len(again.records) == 1 and again.records[0].id == snap.id and exists(root ++ "/index.json") and
        not exists(root ++ "/.staging/" ++ snap.id),
     dup.id == snap.id,
     short.title == "Short notice" and not short.readable and short.outputs.readable.status == "skipped" and
        short.outputs.markdown.message == "not requested" and short.assets.count == 1 and
        contains(short.assets.skipped[0].reason, "missing.css"),
     [for (d in demo) d.title] == ["Shade by design: how street trees cool a city block", "Overnight focaccia with rosemary and sea salt",
        "How reader mode decides what to keep", "The Meridian Ledger — Harbor City news, science and culture"] and
        [for (d in demo) d.readable] == [true, true, true, false] and all([for (d in demo) d.assets.count >= 0]),
     [for (r in listed) r.id] == [snap.id],
     removed and not exists(root ++ "/snapshots/" ++ short.id) and len(archive.library(root)^.records) == 5]
    io.delete(root)^
    results
}
