import archive: lambda.archive

// M2 acceptance: the worker process (AR-O12) renders page.pdf and thumb.png
// from the staged files; a failed PDF leaves the snapshot and its other
// outputs ok (AR9) and `refresh` retries it from the stored capture without
// the network (AR3); `search` finds phrases, prefixes and negations over
// text.txt with snippets, title matches first (AR8).
fn size_of(dir, name) => (let hits = [for (f in input(dir)^ where f.name == name) f.size], if (len(hits) == 0) 0 else hits[0])
fn members(path) => sort([for (m in content(input(path)^)^) m.name])
fn titles(hits) => [for (h in hits) h.snapshot.title]

pn main() {
    // a folder per process: the T0 and JIT runs of this test may overlap
    let root = "temp/archive_test/render-" ++ string(sys.proc.self.pid#)
    if (exists(root)) io.delete(root)^
    let articles = "file://" ++ sys.proc.self.cwd# ++ "/test/demo/readability/articles/"
    let lib = archive.library(root, {config: {defaults: {per_host_delay_ms: 0,
        formats: ["html", "readable", "markdown", "pdf"]}}})^
    let trees = archive.add(lib, articles ++ "meridian-trees.html", {tags: ["city"]})^
    let bread = archive.add(lib, articles ++ "crumb-focaccia.html")^
    let reader = archive.add(lib, articles ++ "fieldnotes-reader-mode.html")^
    let home = archive.add(lib, articles ++ "meridian-home.html")^
    let snaps = [trees, bread, reader, home]
    let dir_of = (s) => root ++ "/snapshots/" ++ s.id
    // a page size Radiant cannot lay out: the PDF fails, nothing else does
    let forced = archive.add(lib, articles ++ "crumb-focaccia.html", {on_duplicate: "new", pdf: {width: 3000000000}})^
    let forced_zip = members(dir_of(forced) ++ "/snapshot.zip")
    let forced_pdf = exists(dir_of(forced) ++ "/page.pdf")
    let retried = archive.refresh(lib, forced.id, ["pdf"])^
    let before = members(dir_of(trees) ++ "/snapshot.zip")
    let html_bytes = trees.outputs.html.bytes
    let renewed = archive.refresh(lib, trees.id)^
    var missing = null
    archive.refresh(lib, "no-such-snapshot") ^ { missing = ^ }
    let phrase = archive.search(lib, "\"street trees\"")^
    let prefix = archive.search(lib, "focacc*")^
    let negated = archive.search(lib, "bread or trees -cool")^
    let ranked = archive.search(lib, "trees")^
    let tagged = archive.search(lib, "trees", {tag: "city"})^
    let first = archive.search(lib, "the", {limit: 1})^
    let results = [
     all([for (s in snaps) s.outputs.pdf.status == "ok" and s.outputs.thumb.status == "ok" and
        size_of(dir_of(s), "page.pdf") > 1000 and size_of(dir_of(s), "thumb.png") > 1000]),
     forced.outputs.pdf.status == "failed" and starts_with(forced.outputs.pdf.message, "render export:") and
        forced.outputs.html.status == "ok" and forced.outputs.readable.status == "ok" and forced.outputs.thumb.status == "ok" and
        not forced_pdf and forced.id != bread.id,
     retried.outputs.pdf.status == "ok" and size_of(dir_of(forced), "page.pdf") > 1000 and retried.refreshed != null and
        members(dir_of(forced) ++ "/snapshot.zip") == forced_zip and retried.outputs.html == forced.outputs.html,
     renewed.outputs.html.bytes == html_bytes and members(dir_of(trees) ++ "/snapshot.zip") == before and
        renewed.title == trees.title and renewed.tags == ["city"] and not exists(root ++ "/.staging/" ++ trees.id ++ "-refresh"),
     missing is error,
     titles(phrase) == [trees.title, home.title] and contains(phrase[0].snippet, "street trees"),
     titles(prefix) == [bread.title, bread.title] and len(prefix[0].snippet) > 0,
     titles(negated) == [bread.title, bread.title],
     titles(ranked) == [trees.title, home.title] and ranked[0].score > 0,
     titles(tagged) == [trees.title],
     len(first) == 1]
    io.delete(root)^
    results
}
