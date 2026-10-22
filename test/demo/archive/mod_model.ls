// The archive demo's state and actions, all pure (S12.1.1v3): the listing and
// its filters, the job table the service's messages fold into, the request an
// Archive press makes, and the labels the page shows. Handlers in archive.ls do
// the IO and hand results here.

pub let KIND_LABELS = [["all", "All"], ["page", "Pages"], ["video", "Video"], ["audio", "Audio"],
    ["gallery", "Galleries"], ["image", "Images"], ["file", "Files"]]
pub let KIND_CHOICES = [["auto", "Auto"], ["page", "Page"], ["video", "Video"], ["audio", "Audio"],
    ["gallery", "Gallery"], ["file", "File"]]
pub let FORMAT_CHOICES = [["html", "HTML"], ["readable", "Reader"], ["markdown", "MD"], ["pdf", "PDF"]]
pub let TABS = [["snapshot", "Snapshot"], ["reader", "Reader"], ["markdown", "Markdown"], ["info", "Info"]]
let MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]

// the sample library a first run archives: the Reader View demo's pages and the
// archive tests' media and gallery pages, all local (`base` is the checkout's file: URL)
pub fn sample_requests(base) => [
    {url: base ++ "test/demo/readability/articles/meridian-trees.html", tag: "sample-1"},
    {url: base ++ "test/demo/readability/articles/crumb-focaccia.html", tag: "sample-2"},
    {url: base ++ "test/demo/readability/articles/fieldnotes-reader-mode.html", tag: "sample-3"},
    {url: base ++ "test/lambda/archive/site/video.html", tag: "sample-4", options: {kind: "video"}},
    {url: base ++ "test/lambda/archive/site/gallery.html", tag: "sample-5", options: {kind: "gallery"}}]

pub fn initial() => {records: [], loaded: false, kind: "all", site: null, query: "", hits: null, selected: null,
    tab: "snapshot", jobs: [], url: "", force: "auto", formats: ["html", "readable", "markdown"], confirm: null,
    serial: 0, note: null}

fn newest_first(rs) => sort(rs, {dir: 'desc', by: (r) => r.archived})
pub fn loaded(st, records) => {*: st, records: newest_first(records), loaded: true,
    selected: if (len(records) == 0) null else newest_first(records)[0].id}

pub fn host_of(url) => (let at = index_of(url or "", "://"), if (at == null) "" else
    (let rest = slice(url, at + 3), let slash = index_of(rest, "/"), lower(if (slash == null) rest else slice(rest, 0, slash))))
// the site a record lists under: its site name, else its host, else "local files"
pub fn site_of(r) => if (r.site != null and r.site != "") r.site else (let h = host_of(r.url), if (h == "") "Local files" else h)

pub fn kind_counts(records) => [for (kl in KIND_LABELS, let n = if (kl[0] == "all") len(records)
    else len([for (r in records where r.kind == kl[0]) r]) where kl[0] == "all" or n > 0) {kind: kl[0], label: kl[1], count: n}]
pub fn site_counts(records) {
    let names = sort(unique([for (r in records) site_of(r)]));
    [for (s in names) {site: s, count: len([for (r in records where site_of(r) == s) r])}]
}

// the rows the list shows: search hits when there is a query, else the records, filtered
pub fn visible(st) {
    let rows = if (st.hits != null) [for (h in st.hits) h.snapshot] else st.records;
    [for (r in rows where (st.kind == "all" or r.kind == st.kind) and (st.site == null or site_of(r) == st.site)) r]
}
pub fn snippet_of(st, id) => if (st.hits == null) null
    else (let hs = [for (h in st.hits where h.snapshot.id == id) h.snippet], if (len(hs) == 0) null else hs[0])
pub fn selected(st) => (let hit = [for (r in st.records where r.id == st.selected) r], if (len(hit) == 0) null else hit[0])

// a typed location as a URL: web and file URLs as they are, paths as file: URLs (against the cwd)
pub fn url_of(text, cwd) {
    let t = trim(text or "");
    if (t == "") null
    else if (contains(t, "://")) t
    else if (starts_with(t, "/")) "file://" ++ t
    else "file://" ++ cwd ++ "/" ++ t
}

// Archive pressed: the service request and the state that shows it, or a note
pub fn request_add(st, cwd) {
    let url = url_of(st.url, cwd)
    if (url == null) {state: {*: st, note: "Type a URL or a path to archive."}, request: null}
    else (let tag = "job-" ++ string(st.serial + 1),
        let options = {formats: st.formats, *: (if (st.force == "auto") {} else {kind: st.force})},
        {state: {*: st, serial: st.serial + 1, url: "", note: null}, request: {op: 'add', url: url, tag: tag, options: options}})
}
// Re-run pressed: refresh one output (or all of them) of the selected snapshot
pub fn request_refresh(st, output) {
    let r = selected(st)
    if (r == null) {state: st, request: null}
    else (let tag = "job-" ++ string(st.serial + 1),
        {state: {*: st, serial: st.serial + 1}, request: {op: 'refresh', id: r.id, tag: tag,
            formats: if (output == null or output == "") null else [output]}})
}

fn job_row(m) => {tag: m.tag, url: m.url, id: m.id, op: m.op, status: "queued", stage: null, received: 0, total: null,
    rate: null, message: null}
fn update_job(jobs, tag, f) => [for (j in jobs) if (j.tag == tag) f(j) else j]
fn replace_record(records, r) => newest_first([r, *[for (x in records where x.id != r.id) x]])

// one message from the service (§8): queued adds a row, progress moves it, done
// files the snapshot (and opens it when the user asked for it), failed keeps
// the row with its message
pub fn apply_job(st, m) {
    if (m.type == "queued") {*: st, jobs: [*st.jobs, job_row(m)]}
    else if (m.type == "progress") {*: st, jobs: update_job(st.jobs, m.tag, (j) => {*: j, status: "running", stage: m.stage,
        received: m.received or 0, total: m.total, rate: m.rate})}
    else if (m.type == "done") (let user = not starts_with(m.tag or "", "sample-"),
        {*: st, jobs: [for (j in st.jobs where j.tag != m.tag) j], records: replace_record(st.records, m.snapshot),
         selected: if (user or st.selected == null) m.snapshot.id else st.selected})
    else if (m.type == "failed") {*: st, jobs: update_job(st.jobs, m.tag, (j) => {*: j, status: "failed", message: m.message})}
    else st
}

pub fn with_hits(st, query, hits) => {*: st, query: query, hits: if (trim(query) == "") null else hits}
pub fn removed(st, id) {
    let rest = [for (r in st.records where r.id != id) r];
    {*: st, records: rest, confirm: null, hits: if (st.hits == null) null else [for (h in st.hits where h.snapshot.id != id) h],
     selected: if (st.selected != id) st.selected else if (len(rest) == 0) null else rest[0].id}
}

// a pure action from a button: its name and value
pub fn action(st, name, value) {
    if (name == "kind") {*: st, kind: value, site: null}
    else if (name == "site") {*: st, site: if (st.site == value) null else value}
    else if (name == "select") {*: st, selected: value, confirm: null}
    else if (name == "tab") {*: st, tab: value}
    else if (name == "force") {*: st, force: value}
    else if (name == "format") {*: st, formats: if (value in st.formats) [for (f in st.formats where f != value) f]
        else [for (fc in FORMAT_CHOICES where fc[0] == value or fc[0] in st.formats) fc[0]]}
    else if (name == "delete") {*: st, confirm: st.selected}
    else if (name == "keep") {*: st, confirm: null}
    else if (name == "dismiss") {*: st, jobs: [for (j in st.jobs where j.tag != value) j]}
    else if (name == "clear-search") {*: st, query: "", hits: null}
    else st
}

// ---- labels ----
pub fn date_label(iso) => if (not (iso is string) or len(iso) < 10) ""
    else (let m = int(slice(iso, 5, 7)), let d = int(slice(iso, 8, 10)),
        if (m is error or d is error or m < 1 or m > 12) "" else string(d) ++ " " ++ MONTHS[m - 1] ++ " " ++ slice(iso, 0, 4))
pub fn size_label(bytes) => if (bytes == null) ""
    else if (bytes >= 1048576) string(round(bytes / 104857.6) / 10) ++ " MB"
    else if (bytes >= 1024) string(round(bytes / 102.4) / 10) ++ " KB" else string(bytes) ++ " B"
// "2 of 5 assets", "412 KB of 580 KB · 9.6 MB/s", "render 3/6"
pub fn progress_label(j) => if (j.status == "queued") "Queued"
    else if (j.status == "failed") j.message or "Failed"
    else if (j.stage == "fetch" or (j.stage == "media" and j.total != null and j.total > 1000))
        size_label(j.received) ++ (if (j.total != null) " of " ++ size_label(j.total) else "") ++
        (if (j.rate != null and j.rate > 0) " · " ++ size_label(j.rate) ++ "/s" else "")
    else if (j.total != null) string(j.stage) ++ " " ++ string(j.received) ++ "/" ++ string(j.total)
    else string(j.stage or "")
pub fn progress_fraction(j) => if (j.total == null or j.total <= 0) null else min(1.0, float(j.received) / float(j.total))
// the formats a record holds, as the list's badges
pub fn badges(r) => [for (fc in [["html", "HTML"], ["readable", "R"], ["markdown", "MD"], ["pdf", "PDF"]]
    where r.outputs[fc[0]] != null and r.outputs[fc[0]].status == "ok") fc[1]]
pub fn kind_label(kind) => (let k = [for (kl in KIND_LABELS where kl[0] == kind) kl[1]], if (len(k) == 0) kind else k[0])
// the outputs the Info tab lists, each with a re-run when it is not ok
pub fn output_rows(r) => [for (o in ["html", "readable", "markdown", "pdf", "text", "thumb"] where r.outputs[o] != null)
    {output: o, status: r.outputs[o].status, message: r.outputs[o].message}]
