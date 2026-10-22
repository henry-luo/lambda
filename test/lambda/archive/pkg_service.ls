import archive: lambda.archive

// M4's job service (§8, AR12v2): requests queue behind `defaults.jobs`
// workers, and every job's progress, done and failed messages reach the
// callback with the job's tag. A queued job is cancelled before it starts; a
// running one is cancelled in its render step — the worker process is
// stopped and the job fails with 319 — and neither leaves a staging folder.
// The harbour page's thumbnail draws a tiled SVG background slowly, which
// keeps that job inside its worker when the cancel arrives.
fn first_index(log, pred) => (let hits = [for (i, m in log where pred(m)) i], if (len(hits) == 0) -1 else hits[0])

pn main() {
    // a folder per process: the T0 and JIT runs of this test may overlap
    let root = "temp/archive_test/service-" ++ string(sys.proc.self.pid#)
    if (exists(root)) io.delete(root)^
    let base = "file://" ++ sys.proc.self.cwd# ++ "/test/"
    let lib = archive.library(root, {config: {defaults: {per_host_delay_ms: 0, jobs: 2, thumbnail: false}}})^
    let me = self()
    let svc = start(archive.service, [lib, pn (m) => { send(me, m)^ }])
    send(svc, {op: 'add', url: base ++ "demo/readability/articles/meridian-trees.html", tag: "trees"})^
    send(svc, {op: 'add', url: base ++ "demo/readability/articles/crumb-focaccia.html", tag: "bread"})^
    send(svc, {op: 'add', url: base ++ "demo/readability/articles/fieldnotes-reader-mode.html", tag: "reader"})^
    send(svc, {op: 'cancel', tag: "reader"})^
    send(svc, {op: 'add', url: base ++ "lambda/archive/site/page.html", tag: "harbour", options: {thumbnail: true}})^
    var log = []
    var ended = 0
    var cancelled = false
    while (ended < 4) {
        let m = receive()^
        if (m.tag == "harbour" and m.stage == "render" and not cancelled) {
            send(svc, {op: 'cancel', tag: "harbour"})^
            cancelled = true
        }
        log = [*log, m]
        if (m.type == "done" or m.type == "failed") { ended = ended + 1 }
    }
    send(svc, {op: 'stop'})^
    let stopped = wait(svc)^
    let ends = [for (m in log where m.type == "done" or m.type == "failed") [m.tag, m.type, m.message]]
    let after = archive.library(root)^
    let staging = [for (f in input(root ++ "/.staging")^) f.name]
    let results = [
     [for (m in log where m.type == "queued") m.tag] == ["trees", "bread", "reader", "harbour"],
     sort(ends) == sort([["bread", "done", null], ["harbour", "failed", "cancelled"], ["reader", "failed", "cancelled"],
        ["trees", "done", null]]),
     // the queued job ends before anything runs to completion; both workers start at once
     first_index(log, (m) => m.tag == "reader" and m.type == "failed") < first_index(log, (m) => m.type == "done") and
        first_index(log, (m) => m.tag == "bread" and m.type == "progress") < first_index(log, (m) => m.tag == "trees" and m.type == "done"),
     all([for (m in log where m.type == "progress") m.tag in ["trees", "bread", "harbour"] and m.stage in ["fetch", "assets", "render", "pack"]]),
     cancelled and len([for (m in log where m.tag == "harbour" and m.stage == "pack") m]) == 0,
     stopped == null and len(after.records) == 2 and sort([for (r in after.records) r.title]) ==
        ["Overnight focaccia with rosemary and sea salt", "Shade by design: how street trees cool a city block"],
     staging == []]
    io.delete(root)^
    results
}
