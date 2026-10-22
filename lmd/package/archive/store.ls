// The library on disk (AR7): each snapshot.json is the truth and index.json a
// cache; a capture is assembled under .staging/<id> and moves into
// snapshots/<id> only once its record is written, so a crash leaves no
// half-snapshot in the library.

pub fn paths(root) => {root: root, staging: root ++ "/.staging", snapshots: root ++ "/snapshots", index: root ++ "/index.json"}

pub pn ensure(root) null^ {
    io.mkdir(root ++ "/snapshots")^
    io.mkdir(root ++ "/.staging")^
    null
}

fn strip_dashes(s) => if (starts_with(s, "-")) strip_dashes(slice(s, 1))
    else if (ends_with(s, "-")) strip_dashes(slice(s, 0, len(s) - 1)) else s
// a folder-safe slug of a title or host: ascii letters and digits, dashes between
pub fn slug(text) {
    let s = strip_dashes(replace(lower(trim(string(text or ""))), \((!(("a" to "z") | ("0" to "9")))+), "-"))
    let cut = strip_dashes(slice(s, 0, 60));
    if (cut == "") "page" else cut
}

// the capture time as an id prefix (sortable, readable) and as ISO text
pub pn stamp() map {
    let t = now().utc;
    {prefix: t.format("YYYYMMDDThhmmss") ++ "Z", iso: t.format("YYYY-MM-DDThh:mm:ss") ++ "Z"}
}

fn taken(root, id) => exists(root ++ "/snapshots/" ++ id) or exists(root ++ "/.staging/" ++ id)
fn free_id(root, base, i) {
    let candidate = if (i == 1) base else base ++ "-" ++ string(i);
    if (taken(root, candidate)) free_id(root, base, i + 1) else candidate
}
// `base` or the first numbered variant no folder holds yet
pub fn unique_id(root, base) => free_id(root, base, 1)

// a new staging folder named after `name`, numbered when another job holds
// that name: jobs started in the same second must not share one, and a folder
// left by an interrupted job is never reused or deleted here (doctor lists it).
// Choosing and creating it does not park, so tasks cannot interleave between.
pub pn stage(root, name) string^ {
    let dir = root ++ "/.staging/" ++ unique_id(root, name)
    io.mkdir(dir)^
    dir
}
pub pn discard(dir) null^ {
    if (exists(dir)) io.delete(dir)^
    null
}

// the loose files a capture is staged as; the ZIP holds them once packed (AR5v2)
let LOOSE = ["index.html", "source.html", "manifest.json", "readable.html", "article.md"]
let SCRATCH = ["request.json", "derived.json"]

// the ZIP's members from the staged files: the pages, then the stored resources
pub pn staged_members(dir, manifest) array^ {
    let top = [for (name in LOOSE where exists(dir ++ "/" ++ name)) {name: name, data: input(dir ++ "/" ++ name, 'binary')^}]
    let assets = [for (e in manifest where e.status == "ok") {name: "assets/" ++ e.path, data: input(dir ++ "/assets/" ++ e.path, 'binary')^}];
    [*top, *assets]
}
// drops what the ZIP now holds and the worker's scratch files; text.txt, page.pdf and thumb.png stay loose
pub pn tidy(dir) null^ {
    for (name in [*LOOSE, *SCRATCH]) {
        if (exists(dir ++ "/" ++ name)) io.delete(dir ++ "/" ++ name)^
    }
    if (exists(dir ++ "/assets")) io.delete(dir ++ "/assets")^
    null
}

pn unpack_into(node, dir) null^ {
    for (m in content(node)^) {
        if (m.kind == 'dir') {
            io.mkdir(dir ++ "/" ++ m.name)^
            unpack_into(m, dir ++ "/" ++ m.name)^
        } else {
            output(input(m, 'binary')^, dir ++ "/" ++ m.name)^
        }
    }
    null
}
// a folder copied file by file: io.copy takes files only (Lambda_Sys_Func, io.copy)
pub pn copy_tree(src, dst) null^ {
    io.mkdir(dst)^
    for (e in input(src)^) {
        if (e.is_dir) {
            copy_tree(src ++ "/" ++ e.name, dst ++ "/" ++ e.name)^
        } else {
            io.copy(src ++ "/" ++ e.name, dst ++ "/" ++ e.name)^
        }
    }
    null
}

// writes every member of a snapshot ZIP back into a staging folder (refresh)
pub pn unpack(zip_path, dir) null^ {
    unpack_into(input(zip_path)^, dir)^
    null
}

// snapshot.zip from the members: top-level files and the assets/ folder
pub pn pack(dir, members, zip_name) int^ {
    let top = [for (m in members where index_of(m.name, "/") == null) <fs name: m.name, kind: 'file', (m.data or "")>]
    let assets = [for (m in members where starts_with(m.name, "assets/")) <fs name: slice(m.name, 7), kind: 'file', (m.data or "")>]
    let folders = if (len(assets) == 0) [] else [<fs name: "assets", kind: 'dir', *assets>]
    let tree = <fs kind: 'dir', *[*top, *folders]>;
    output(tree, dir ++ "/" ++ zip_name, {format: 'zip', deterministic: true})^
}

pub pn finish(root, dir, id, record) map^ {
    output(record, dir ++ "/snapshot.json", 'json')^
    io.move(dir, root ++ "/snapshots/" ++ id)^
    record
}

// replaces a snapshot's folder with a rebuilt one (refresh): the old folder
// moves aside first, so a crash leaves one whole folder in snapshots/
pub pn swap(root, dir, id, record) map^ {
    output(record, dir ++ "/snapshot.json", 'json')^
    let live = root ++ "/snapshots/" ++ id
    let old = root ++ "/.staging/" ++ id ++ "-old"
    if (exists(old)) io.delete(old)^
    io.move(live, old)^
    io.move(dir, live)^
    io.delete(old)^
    record
}

pub pn scan(root) array^ {
    let dir = root ++ "/snapshots"
    let rows = if (not exists(dir)) []
        else [for (p in input(dir)^ where p.is_dir and exists(dir ++ "/" ++ p.name ++ "/snapshot.json"))
            input(dir ++ "/" ++ p.name ++ "/snapshot.json", 'json')^];
    sort(rows, {dir: 'desc', by: (r) => r.archived})
}

pub pn write_index(root, records) null^ {
    output({format: 1, snapshots: records}, root ++ "/index.json", 'json')^
    null
}

// the index as last written, or rebuilt from the folders when there is none
pub pn current(root) array^ {
    let path = root ++ "/index.json"
    if (exists(path)) ((input(path, 'json')^).snapshots or []) else scan(root)^
}
// the index after one change: the file's records with `record` upserted and the
// `drop` ids removed. It reads the file rather than the caller's Library value,
// so jobs started from one Library keep each other's records.
pub pn update_index(root, record, drop) array^ {
    let gone = [*drop, *(if (record == null) [] else [record.id])]
    let existing = current(root)^
    let rows = [*(if (record == null) [] else [record]), *[for (r in existing where not (r.id in gone)) r]]
    write_index(root, rows)^
    rows
}

// the records, from the index cache when it is at least as new as the
// snapshots folder, otherwise rebuilt from the folders (AR7)
pub pn load(root) array^ {
    let entries = input(root)^
    let index = [for (p in entries where p.name == "index.json") p]
    let snaps = [for (p in entries where p.name == "snapshots") p]
    if (len(index) == 1 and len(snaps) == 1 and index[0].modified >= snaps[0].modified) {
        let cached = input(root ++ "/index.json", 'json')^
        cached.snapshots or []
    } else {
        let rows = scan(root)^
        write_index(root, rows)^
        rows
    }
}

// the record already holding this normalized URL, or null
pub fn duplicate(records, normalized) {
    let hits = [for (r in records where r.normalized == normalized) r];
    if (len(hits) == 0) null else hits[0]
}
