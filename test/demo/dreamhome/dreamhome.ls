// Run: ./lambda.exe demo dreamhome
// A home planner: draw walls and rooms on a floor plan, put in doors, windows
// and furniture from a catalog, and see the home in native 3D beside the plan,
// orbiting over it or walking through it at eye height.
import dom
import radiant
import resources: ~~.mod_resources
import D: ~~.mod_document
import A: ~~.greetings.mod_art
import C: .mod_catalog
import M: .mod_model
import T: .mod_templates
import P: .mod_plan
import S: .mod_scene
import ui: .mod_ui

let BASE = url_resolve(resources.entry_uri()^, "./")
let CSS = input(url_resolve(BASE, "dreamhome.css"), 'text')^

fn framed(st: any) map => (let p = ui.panes(st), let b = M.bounds(st.home), let ph = if (p.plan > 0.0) p.plan else p.view3d,
    let z = M.clamp(min((p.w - 90.0) / max(200.0, b.x1 - b.x0), (ph - 90.0) / max(200.0, b.y1 - b.y0)), .08, 3.0),
    {*:st, view2d: {cx: (b.x0 + b.x1) / 2.0, cy: (b.y0 + b.y1) / 2.0, zoom: z}})
fn walker_for(home) => (let b = M.bounds(home), {x: (b.x0 + b.x1) / 2.0, y: (b.y0 + b.y1) / 2.0, yaw: -90.0, pitch: 0.0})
fn orbit_for(home) => (let f = S.framing(home), {yaw: .65, pitch: .82, dist: f.dist, tx: f.tx, tz: f.tz})
fn with_home(st: any, home: any) map => {*:st, home: home, orbit: orbit_for(home), walker: walker_for(home)}

fn initial() => framed(with_home({hist: D.history(), tool: "select", layout: "split", sel: null, cat: "living", query: "",
    mode3d: "orbit", cutaway: false, evening: false, dims: true, view2d: {cx: 500.0, cy: 400.0, zoom: .6},
    drag: null, dialog: null, note: "", typing: false, vp: {width: 1280.0, height: 820.0},
    status: "Pick a piece from the catalog, or draw walls and rooms with the tools above.",
    path: "temp/dreamhome", open_path: "temp/dreamhome/city-apartment.mark"}, T.TEMPLATES[0].home))

// ---- edits: pure state transitions -------------------------------------------------

fn record_edit(st, home, status = null) => if (home == st.home) st
    else {*:st, home: home, hist: D.record(st.hist, st.home), typing: false, status: if (status == null) st.status else status}
fn sel_thing(st) => M.thing_of(st.home, st.sel)
fn patch_item(st, patch) => if (M.kind_of(st.sel) != "item" or sel_thing(st) == null) st else record_edit(st, M.update_item(st.home, st.sel, patch))
fn adjust(st, field, delta, lo, hi) => (let it = sel_thing(st), if (it == null) st else patch_item(st, {[field]: M.clamp(it[field] + delta, lo, hi)}))
fn turn(st, delta) => (let it = sel_thing(st), if (it == null or it.wall != null) st
    else (let r = it.rot + delta, patch_item(st, {rot: if (r > 180.0) r - 360.0 else if (r <= -180.0) r + 360.0 else r})))

// Where a new piece goes: near the middle of the selected room, else of the room
// in the middle of the view, at the first spot clear of what stands on the floor there.
fn drop_point(st, key) {
    let r = if (M.kind_of(st.sel) == "room") sel_thing(st) else M.room_at(st.home, st.view2d.cx, st.view2d.cy)
    let at = if (r != null) M.centroid(r.pts) else [st.view2d.cx, st.view2d.cy]
    let c = C.item_of(key)
    let reach = (c.w + c.d) / 4.0
    let standing = [for (it in M.furniture(st.home) where not P.overhead(it)) it]
    let spots = [for (ring in 0 to 8, k in 0 to (if (ring == 0) 0 else ring * 6 - 1),
        let a = 2.0 * math.pi * k / max(1, ring * 6), let x = M.snap(at[0] + ring * 30.0 * math.cos(a)), let y = M.snap(at[1] + ring * 30.0 * math.sin(a))
        where (r == null or M.inside(r.pts, x, y)) and all([for (it in standing) M.dist(x, y, it.x, it.y) > reach + (it.w + it.d) / 4.0]))
        [x, y]];
    if (len(spots) > 0) spots[0] else [M.snap(at[0]), M.snap(at[1])]
}
fn add_piece(st, key) {
    let c = C.item_of(key)
    if (c.opening == null) {
        let at = drop_point(st, key)
        let added = M.add_item(st.home, key, at[0], at[1]);
        {*:record_edit(st, added.home, "Added a " ++ lower(c.name) ++ ". Drag it into place."), sel: added.id, tool: "select"}
    }
    else {
        let w = if (M.kind_of(st.sel) == "wall") sel_thing(st) else (let hit = M.nearest_wall(st.home, st.view2d.cx, st.view2d.cy), if (hit == null) null else hit.wall)
        if (w == null) {*:st, status: "Draw a wall first: doors and windows go into walls."}
        else (let added = M.add_opening(st.home, key, w.id, M.wall_len(w) / 2.0),
            {*:record_edit(st, added.home, "Added a " ++ lower(c.name) ++ ". Drag it along the wall, or onto another wall."), sel: added.id, tool: "select"})
    }
}
fn set_wall_len(st, delta) => (let w = sel_thing(st), if (w == null) st
    else (let l = M.clamp(M.wall_len(w) + delta, 30.0, 3000.0), let u = M.wall_dir(w),
        record_edit(st, M.move_points(st.home, [[w.x2, w.y2, w.x1 + u.x * l, w.y1 + u.y * l]]))))
fn paint_all(st) => (let w = sel_thing(st), if (w == null) st
    else record_edit(st, {*:st.home, walls: [for (x in st.home.walls) {*:x, color: w.color}]}, "Every wall now has this paint."))
fn zoom_by(st, k) => {*:st, view2d: {*:st.view2d, zoom: M.clamp(st.view2d.zoom * k, .08, 3.0)}}
fn history_step(st, forward) => (let step = if (forward) D.redo_step(st.hist, st.home) else D.undo_step(st.hist, st.home),
    if (step == null) st else {*:st, home: step.value, hist: step.history, sel: if (M.thing_of(step.value, st.sel) == null) null else st.sel,
        typing: false, status: if (forward) "Redone." else "Undone."})
fn use_template(st, key) => (let t = T.template_of(key),
    {*:framed(with_home(record_edit(st, t.home, "Started from " ++ t.title ++ ". Undo brings back your previous home."), t.home)),
        sel: null, dialog: null, tool: if (key == "blank") "room" else "select"})
fn delete_sel(st) => (let k = M.kind_of(st.sel), if (sel_thing(st) == null) st
    else {*:record_edit(st, M.remove(st.home, st.sel), (if (k == "wall") "Wall deleted" else if (k == "room") "Floor deleted" else "Deleted") ++ ". Undo brings it back."), sel: null})

fn change(st, verb, arg) => if (verb == "tool") {*:st, tool: arg, sel: if (arg == "select") st.sel else null,
        status: if (arg == "wall") "Drag in the plan to draw a wall." else if (arg == "room") "Drag in the plan to draw a room." else st.status}
    else if (verb == "layout") framed({*:st, layout: arg})
    else if (verb == "cat") {*:st, cat: arg}
    else if (verb == "add") add_piece(st, arg)
    else if (verb == "select") {*:st, sel: arg, tool: "select"}
    else if (verb == "deselect") {*:st, sel: null}
    else if (verb == "delete") delete_sel(st)
    else if (verb == "duplicate" and M.kind_of(st.sel) == "item")
        (let d = M.duplicate(st.home, st.sel), if (d == null) st else {*:record_edit(st, d.home, "Duplicated."), sel: d.id})
    else if (verb == "undo" or verb == "redo") history_step(st, verb == "redo")
    else if (verb == "rot") turn(st, float(arg))
    else if (verb == "size-w") adjust(st, "w", float(arg), 10.0, 1200.0)
    else if (verb == "size-d") adjust(st, "d", float(arg), 2.0, 1200.0)
    else if (verb == "size-h") adjust(st, "h", float(arg), 1.0, st.home.wall_height)
    else if (verb == "elev") adjust(st, "elev", float(arg), 0.0, st.home.wall_height - 10.0)
    else if (verb == "color") patch_item(st, {color: arg})
    else if (verb == "flip" or verb == "mirror") (let it = sel_thing(st), if (it == null) st else patch_item(st, {[verb]: not (it[verb] == true)}))
    else if (verb == "wall-len") set_wall_len(st, float(arg))
    else if (verb == "wall-thick") (let w = sel_thing(st), if (w == null) st else record_edit(st, M.update_wall(st.home, st.sel, {thick: M.clamp(w.thick + float(arg), 5.0, 60.0)})))
    else if (verb == "paint") record_edit(st, M.update_wall(st.home, st.sel, {color: arg}))
    else if (verb == "paint-all") paint_all(st)
    else if (verb == "floor") record_edit(st, M.update_room(st.home, st.sel, {floor: arg}))
    else if (verb == "wall-height") record_edit(st, {*:st.home, wall_height: M.clamp(st.home.wall_height + float(arg), 200.0, 400.0)})
    else if (verb == "zoom") zoom_by(st, float(arg))
    else if (verb == "fit") framed(st)
    else if (verb == "dims") {*:st, dims: not st.dims}
    else if (verb == "mode3d") {*:st, mode3d: arg, status: if (arg == "walk") "Walking: drag to look around, W A S D to walk." else "Orbiting the home."}
    else if (verb == "cutaway") {*:st, cutaway: not st.cutaway}
    else if (verb == "evening") {*:st, evening: not st.evening}
    else if (verb == "reset3d") {*:st, orbit: orbit_for(st.home), walker: if (st.mode3d == "walk") walker_for(st.home) else st.walker}
    else if (verb == "template") use_template(st, arg)
    else if (verb == "dialog") {*:st, dialog: arg, note: ""}
    else if (verb == "dialog-close") {*:st, dialog: null}
    else st

// ---- files ---------------------------------------------------------------------------

pn ensure_dir(path) {
    var at = ""
    for (part in split(path, "/")) {
        at = if (at == "") part else at ++ "/" ++ part
        if (part != "" and part != "." and not exists(at)) io.mkdir(at) ^ { null }
    }
}
pn render_file(html, path, opts) any^ {
    let done = radiant.export_document(html, path, opts)
    if (done is error) raise done
    if (done != true) raise error(string(done))
    return path
}
let SHEET_CSS = "html, body { margin: 0; background: #ffffff; } svg { display: block; font-family: Helvetica, Arial, sans-serif; }"
pn export_files(st, kind) any^ {
    let dir = trim(st.path)
    if (dir == "") raise error("Choose a folder first.")
    ensure_dir(dir)
    let stem = dir ++ "/" ++ D.slug(st.home.title, "home")
    let b = M.bounds(st.home)
    let aspect = (b.y1 - b.y0 + 240.0) / (b.x1 - b.x0 + 240.0)
    if (kind == "mark") {
        output(st.home, stem ++ ".mark", {format: 'mark', atomic: true})^
        return "Saved " ++ stem ++ ".mark"
    }
    if (kind == "glb") {
        let model = format(S.scene(st.home, {*:st, cutaway: false, mode3d: "orbit"}, 1280, 800), 'glb')^
        output(model, stem ++ ".glb")^
        return "Wrote the 3D model to " ++ stem ++ ".glb"
    }
    if (kind == "svg") {
        output(format(P.sheet(st.home, 1600.0, 1600.0 * aspect, 1000.0), 'xml'), stem ++ "-plan.svg", {format: 'text'})^
        return "Wrote " ++ stem ++ "-plan.svg"
    }
    // the PDF is one page the size of the plan sheet (a landscape A4 proportion); the PNG is 2400 px across
    let w = if (kind == "pdf") 1123.0 else 2400.0
    let h = if (kind == "pdf") 794.0 else 2400.0 * aspect
    let html = format(<html <head <meta charset:"UTF-8"><title st.home.title><style SHEET_CSS>> <body (P.sheet(st.home, w, h, 1000.0))>>, 'html')
    if (kind == "pdf") {
        render_file(html, stem ++ "-plan.pdf", {format: "pdf"})^
        return "Wrote " ++ stem ++ "-plan.pdf"
    }
    render_file(html, stem ++ "-plan.png", {format: "png", width: int(w), height: int(h)})^
    return "Wrote " ++ stem ++ "-plan.png"
}
pn open_home(st) any^ {
    let path = trim(st.open_path)
    let home = input(path, 'mark')^
    if (not M.valid(home)) raise error(path ++ " is not a Dreamhome home.")
    return {*:framed(with_home(record_edit(st, home, "Opened " ++ path ++ "."), home)), dialog: null, sel: null}
}

pn command(st, verb, arg) {
    if (verb == "export" or verb == "save") {
        var next = {*:st, note: ""}
        export_files(st, if (verb == "save") "mark" else arg) ^ { next = {*:st, note: ^.message, status: ^.message} }
            ~ { next = {*:st, note: ~, status: ~} }
        return next
    }
    if (verb == "open") {
        var next = st
        open_home(st) ^ { next = {*:st, note: ^.message} } ~ { next = ~ }
        return next
    }
    return change({*:st, typing: false}, verb, arg)
}

// ---- plan gestures -----------------------------------------------------------------------

let WALK_STEP = 25.0
fn plan_xy(d, x, y) => [d.f.x0 + (x - d.box.x) / d.z, d.f.y0 + (y - d.box.y) / d.z]
fn wrap(a) => if (a > 180.0) a - 360.0 else if (a <= -180.0) a + 360.0 else a
fn local(it, x, y) => (let r = it.rot / M.DEG, let dx = x - it.x, let dy = y - it.y,
    {x: dx * math.cos(r) + dy * math.sin(r), y: dy * math.cos(r) - dx * math.sin(r)})

// The result of a plan drag so far, recomputed from the pointer's total travel:
// a patch for the dragged piece, a whole new home, a camera, or a draft shape.
fn dragged(st, x, y, free) {
    let d = st.drag
    let p = plan_xy(d, x, y)
    let q = d.p0
    if (d.mode == "pan") {view2d: {*:st.view2d, cx: d.cx - (x - d.x) / d.z, cy: d.cy - (y - d.y) / d.z}}
    else if (d.mode == "item") (let it = d.start, let nx = it.x + p[0] - q[0], let ny = it.y + p[1] - q[1],
        {patch: {x: if (free) nx else M.snap(nx, 5.0), y: if (free) ny else M.snap(ny, 5.0)}})
    else if (d.mode == "rotate") (let a = math.atan2(p[1] - d.start.y, p[0] - d.start.x) * M.DEG + 90.0,
        {patch: {rot: wrap(if (free) a else round(a / 15.0) * 15.0)}})
    else if (d.mode == "size") (let l = local(d.start, p[0], p[1]),
        {patch: {w: max(10.0, M.snap(abs(l.x) * 2.0 - 10.0 / d.z, if (free) 1.0 else 5.0)), d: max(2.0, M.snap(abs(l.y) * 2.0 - 10.0 / d.z, if (free) 1.0 else 5.0))}})
    else if (d.mode == "a" or d.mode == "b") (let w = d.start, let other = if (d.mode == "a") [w.x2, w.y2] else [w.x1, w.y1],
        let s = M.snap_point(d.before, p[0], p[1], 14.0 / d.z),
        let e = if (s[0] != M.snap(p[0]) or s[1] != M.snap(p[1])) s else M.steer(other[0], other[1], p[0], p[1], free),
        let from = if (d.mode == "a") [w.x1, w.y1] else [w.x2, w.y2],
        {home: M.move_points(d.before, [[from[0], from[1], e[0], e[1]]])})
    else if (d.mode == "wall") (let u = M.wall_dir(d.start), let k = (p[0] - q[0]) * (0.0 - u.y) + (p[1] - q[1]) * u.x,
        let s = if (free) k else M.snap(k, 5.0), {home: M.shift_wall(d.before, d.start.id, 0.0 - u.y * s, u.x * s)})
    else if (d.mode == "opening") (let hit = M.nearest_wall(d.before, p[0], p[1]),
        if (hit == null) {home: d.before} else {home: M.update_item(d.before, d.start.id, {wall: hit.wall.id, at: if (free) hit.at else M.snap(hit.at, 5.0)})})
    else if (d.mode == "walker") {walker: {*:st.walker, x: p[0], y: p[1]}}
    else if (d.mode == "draw-wall") (let s = M.snap_point(d.before, p[0], p[1], 14.0 / d.z),
        let e = if (s[0] != M.snap(p[0]) or s[1] != M.snap(p[1])) s else M.steer(q[0], q[1], p[0], p[1], free), {draft: [q[0], q[1], e[0], e[1]]})
    else if (d.mode == "draw-room") (let e = [M.snap(p[0]), M.snap(p[1])], {draft: [q[0], q[1], e[0], e[1]]})
    else {}
}

fn transform_of(it) => "translate(" ++ A.n(it.x) ++ " " ++ A.n(it.y) ++ ") rotate(" ++ A.n(it.rot) ++ ")"
pn set(el, key, value) { if (el != null) dom.set_attribute(el, key, value) }
// D4.5.1v4: a drag previews on the plan's own elements and commits once, on release
pn publish_structure(root, st, home) {
    let g = dom.query_selector(root, "#plan-structure")
    if (g == null) { return null }
    let el = P.structure(home, st.sel, st.view2d.zoom, st.dims)
    dom.set_inner_html(g, join([for (c in content(el)) format(c, 'xml')], ""))
}
pn publish_camera(root, st) {
    let cam = dom.query_selector(root, "#cam")
    if (cam == null) { return null }
    let a = S.camera_attrs(st)
    dom.set_attribute(cam, "position", a.position)
    dom.set_attribute(cam, "target", a.target)
}
pn publish_drag(root, st, step) {
    let d = st.drag
    if (step.view2d != null) {
        let p = ui.panes(st)
        let f = P.frame(step.view2d, p.w, p.plan)
        set(dom.query_selector(root, "#plan"), "viewBox", A.n(f.x0) ++ " " ++ A.n(f.y0) ++ " " ++ A.n(f.w) ++ " " ++ A.n(f.h))
    }
    else if (step.patch != null) {
        let it = {*:d.start, *:step.patch}
        let k = if (d.mode == "size") " scale(" ++ string(it.w / d.start.w) ++ " " ++ string(it.d / d.start.d) ++ ")" else ""
        set(dom.query_selector(root, "#plan [data-id=\"" ++ d.start.id ++ "\"]"), "transform", transform_of(it) ++ k)
    }
    else if (step.home != null) publish_structure(root, st, step.home)
    else if (step.walker != null) {
        set(dom.query_selector(root, "#walker"), "transform", "translate(" ++ A.n(step.walker.x) ++ " " ++ A.n(step.walker.y) ++ ") rotate(" ++ A.n(step.walker.yaw) ++ ")")
        publish_camera(root, {*:st, walker: step.walker})
    }
    else if (step.draft != null) {
        let r = step.draft
        let label = dom.query_selector(root, "#draft-label")
        if (d.mode == "draw-wall") {
            let line = dom.query_selector(root, "#draft-wall")
            set(line, "x1", A.n(r[0])); set(line, "y1", A.n(r[1])); set(line, "x2", A.n(r[2])); set(line, "y2", A.n(r[3]))
            set(line, "visibility", "visible")
            if (label != null) dom.set_text_content(label, A.n(M.dist(r[0], r[1], r[2], r[3]) / 100.0) ++ " m")
        }
        else {
            let rect = dom.query_selector(root, "#draft-room")
            set(rect, "x", A.n(min(r[0], r[2]))); set(rect, "y", A.n(min(r[1], r[3])))
            set(rect, "width", A.n(max(1.0, abs(r[2] - r[0])))); set(rect, "height", A.n(max(1.0, abs(r[3] - r[1]))))
            set(rect, "visibility", "visible")
            if (label != null) dom.set_text_content(label, A.n(abs(r[2] - r[0]) / 100.0) ++ " × " ++ A.n(abs(r[3] - r[1]) / 100.0) ++ " m")
        }
        set(label, "x", A.n(r[2] + 12.0 / d.z)); set(label, "y", A.n(r[3] - 12.0 / d.z)); set(label, "visibility", "visible")
    }
}

// the drag's last step becomes one edit, or a selection when the pointer barely moved
fn finish(st, step, moved) {
    let d = st.drag
    let base = {*:st, drag: null}
    if (d.mode == "pan") (if (moved) {*:base, view2d: step.view2d} else {*:base, sel: null})
    else if (not moved and d.mode != "draw-wall" and d.mode != "draw-room") base
    else if (step.patch != null) record_edit(base, M.update_item(d.before, d.start.id, step.patch),
        if (d.mode == "rotate") "Turned." else if (d.mode == "size") "Resized." else "Moved.")
    else if (step.home != null) record_edit(base, step.home, if (d.mode == "opening") "Moved along the wall." else "Walls moved.")
    else if (step.walker != null) {*:base, walker: step.walker}
    else if (step.draft != null and d.mode == "draw-wall") (let r = step.draft,
        if (M.dist(r[0], r[1], r[2], r[3]) < 20.0) {*:base, status: "Drag further to draw a wall."}
        else (let added = M.add_wall(d.before, r[0], r[1], r[2], r[3]),
            {*:record_edit(base, added.home, "Wall drawn, " ++ A.n(M.dist(r[0], r[1], r[2], r[3]) / 100.0) ++ " m. Keep dragging to add more."), sel: null}))
    else if (step.draft != null) (let r = step.draft,
        if (abs(r[2] - r[0]) < 60.0 or abs(r[3] - r[1]) < 60.0) {*:base, status: "Drag a bigger rectangle to make a room."}
        else (let added = M.add_room(d.before, r[0], r[1], r[2], r[3]),
            {*:record_edit(base, added.home, "Room added. Name it and choose its floor on the right."), sel: added.id, tool: "select"}))
    else base
}

// ---- 3D gestures --------------------------------------------------------------------------

fn orbit_drag(st, x, y) {
    let d = st.drag
    let o = d.start
    let dx = x - d.x
    let dy = y - d.y
    if (d.mode == "orbit") {orbit: {*:o, yaw: o.yaw - dx * .008, pitch: M.clamp(o.pitch + dy * .006, .08, 1.5)}}
    else if (d.mode == "pan3d") (let k = o.dist / 900.0, let rx = math.cos(o.yaw), let rz = 0.0 - math.sin(o.yaw),
        let fx = 0.0 - math.sin(o.yaw), let fz = 0.0 - math.cos(o.yaw),
        {orbit: {*:o, tx: o.tx - dx * k * rx + dy * k * fx, tz: o.tz - dx * k * rz + dy * k * fz}})
    else {walker: {*:o, yaw: wrap(o.yaw + dx * .25), pitch: M.clamp(o.pitch - dy * .004, -.7, .7)}}
}
// a step of the walker: forward and back along its heading, or sideways
fn walked(wk, key) => (let a = wk.yaw / M.DEG,
    let f = if (key == "w" or key == "arrowup") 1.0 else if (key == "s" or key == "arrowdown") -1.0 else 0.0,
    let s = if (key == "d") 1.0 else if (key == "a") -1.0 else 0.0,
    let t = if (key == "arrowleft") -10.0 else if (key == "arrowright") 10.0 else 0.0,
    {*:wk, yaw: wrap(wk.yaw + t), x: wk.x + WALK_STEP * (f * math.cos(a) - s * math.sin(a)), y: wk.y + WALK_STEP * (f * math.sin(a) + s * math.cos(a))})

fn travelled(d, evt) => abs(evt.clientX - d.x) + abs(evt.clientY - d.y) >= 3
fn typing_target(evt) => dom.closest(evt.target, "input, textarea, select") != null

view <dreamhome_app> state st: initial() {
    <body ui.planner(st, T.TEMPLATES)>
}
on load(evt) {
    let size = dom.viewport_size(dom.root_node(evt.target))
    if (size != null) { st = framed({*:st, vp: {width: float(size.width), height: float(size.height)}}) }
    return 'pass'
}
on resize(evt) {
    let size = dom.viewport_size(dom.root_node(evt.target))
    if (size != null) { st = {*:st, vp: {width: float(size.width), height: float(size.height)}} }
    return 'pass'
}
on click(evt) {
    let btn = dom.closest(evt.target, "[data-act]")
    if (btn == null or dom.has_attribute(btn, "disabled")) { return 'pass' }
    st = command(st, dom.get_attribute(btn, "data-act"), dom.get_attribute(btn, "data-arg") or "")
    return 'handled'
}
on control_change(req) {
    if (req.action == "title") { st = {*:record_edit(st, {*:st.home, title: req.value}), typing: false} }
    else if (req.action == "room-name" and M.kind_of(st.sel) == "room") {
        // a burst of typing is one undo step
        let base = if (st.typing) st else {*:st, hist: D.record(st.hist, st.home)}
        st = {*:base, home: M.update_room(st.home, st.sel, {name: req.value}), typing: true}
    }
    else if (req.action == "query") { st = {*:st, query: req.value} }
    else if (req.action == "path") { st = {*:st, path: req.value} }
    else if (req.action == "open-path") { st = {*:st, open_path: req.value} }
}
on pointerdown(evt) {
    if (st.dialog != null or evt.button != 0) { return 'pass' }
    let root = dom.root_node(evt.target)
    let scene = dom.closest(evt.target, "scene3d")
    if (scene != null) {
        let mode = if (st.mode3d == "walk") "look" else if (evt.shiftKey) "pan3d" else "orbit"
        st = {*:st, drag: {mode: mode, x: evt.clientX, y: evt.clientY, start: if (mode == "look") st.walker else st.orbit}}
        dom.set_pointer_capture(scene, evt.pointerId)
        return 'prevent-default'
    }
    let svg = dom.closest(evt.target, "#plan")
    if (svg == null) { return 'pass' }
    let p = ui.panes(st)
    let base = {x: evt.clientX, y: evt.clientY, box: dom.bounding_box(svg), f: P.frame(st.view2d, p.w, p.plan), z: st.view2d.zoom,
        cx: st.view2d.cx, cy: st.view2d.cy, before: st.home}
    let start = plan_xy(base, evt.clientX, evt.clientY)
    let handle = dom.closest(evt.target, "[data-handle]")
    let hit = dom.closest(evt.target, "[data-id]")
    let id = if (hit == null) null else dom.get_attribute(hit, "data-id")
    let kind = M.kind_of(id)
    let drag = if (st.tool == "wall") {*:base, mode: "draw-wall", p0: M.snap_point(st.home, start[0], start[1], 14.0 / base.z)}
        else if (st.tool == "room") {*:base, mode: "draw-room", p0: [M.snap(start[0]), M.snap(start[1])]}
        else if (handle != null and dom.get_attribute(handle, "data-handle") == "walker") {*:base, mode: "walker", p0: start}
        else if (handle != null and st.sel != null) {*:base, mode: dom.get_attribute(handle, "data-handle"), p0: start, start: sel_thing(st)}
        else if (kind == "item") (let it = M.item_of(st.home, id), {*:base, mode: if (it.wall != null) "opening" else "item", p0: start, start: it})
        else if (kind == "wall") {*:base, mode: "wall", p0: start, start: M.wall_of(st.home, id)}
        else {*:base, mode: "pan", p0: start}
    st = {*:st, drag: drag, sel: if (st.tool != "select" or drag.mode == "walker") st.sel
        else if (kind == "item" or kind == "wall" or kind == "room") id else if (handle != null) st.sel else null, typing: false}
    dom.set_pointer_capture(svg, evt.pointerId)
    return 'prevent-default'
}
on pointermove(evt) {
    if (st.drag == null) { return 'pass' }
    let root = dom.root_node(evt.target)
    if (st.drag.box == null) {
        let step = orbit_drag(st, evt.clientX, evt.clientY)
        publish_camera(root, {*:st, *:step})
    }
    else if (travelled(st.drag, evt) or st.drag.mode == "draw-wall" or st.drag.mode == "draw-room") {
        publish_drag(root, st, dragged(st, evt.clientX, evt.clientY, evt.shiftKey))
    }
    return 'prevent-default'
}
on pointerup(evt) {
    if (st.drag == null) { return 'pass' }
    let d = st.drag
    let target = if (d.box == null) dom.query_selector(dom.root_node(evt.target), "scene3d") else dom.query_selector(dom.root_node(evt.target), "#plan")
    if (target != null) dom.release_pointer_capture(target, evt.pointerId)
    // a click in the 3D view without a drag picks what is under it
    st = if (d.box == null and not travelled(d, evt) and target != null)
            (let b = dom.bounding_box(target), {*:st, drag: null, sel: S.pick(st.home, st, b.width, b.height, evt.clientX - b.x, evt.clientY - b.y)})
        else if (d.box == null) {*:st, drag: null, *:orbit_drag(st, evt.clientX, evt.clientY)}
        else finish(st, dragged(st, evt.clientX, evt.clientY, evt.shiftKey), travelled(d, evt))
    return 'prevent-default'
}
on pointercancel(evt) {
    if (st.drag == null) { return 'pass' }
    // the drag only touched the plan's elements and the camera: a re-render restores them
    st = {*:st, drag: null, view2d: {*:st.view2d}}
    return 'prevent-default'
}
on wheel(evt) {
    if (dom.closest(evt.target, "scene3d") != null) {
        if (st.mode3d == "walk") { st = {*:st, walker: walked(st.walker, if (evt.deltaY < 0) "w" else "s")} }
        else { st = {*:st, orbit: {*:st.orbit, dist: M.clamp(st.orbit.dist * (if (evt.deltaY < 0) .9 else 1.0 / .9), 2.0, 60.0)}} }
        return 'prevent-default'
    }
    let svg = dom.closest(evt.target, "#plan")
    if (svg == null) { return 'pass' }
    // zoom about the pointer: the plan point under it stays put
    let p = ui.panes(st)
    let base = {box: dom.bounding_box(svg), f: P.frame(st.view2d, p.w, p.plan), z: st.view2d.zoom}
    let at = plan_xy(base, evt.clientX, evt.clientY)
    let z = M.clamp(st.view2d.zoom * (if (evt.deltaY < 0) 1.12 else 1.0 / 1.12), .08, 3.0)
    let k = st.view2d.zoom / z
    st = {*:st, view2d: {zoom: z, cx: at[0] + (st.view2d.cx - at[0]) * k, cy: at[1] + (st.view2d.cy - at[1]) * k}}
    return 'prevent-default'
}
on keydown(evt) {
    if (st.dialog != null) {
        if (evt.key == "Escape") { st = {*:st, dialog: null}; return 'prevent-default' }
        return 'pass'
    }
    if (typing_target(evt)) { return 'pass' }
    let mod = evt.metaKey or evt.ctrlKey
    let key = lower(evt.key)
    if (st.mode3d == "walk" and st.layout != "plan" and not mod and (contains(["w", "a", "s", "d"], key) or
            (st.sel == null and contains(["arrowup", "arrowdown", "arrowleft", "arrowright"], key)))) {
        st = {*:st, walker: walked(st.walker, key)}
        return 'prevent-default'
    }
    if (contains(["arrowleft", "arrowright", "arrowup", "arrowdown"], key) and M.kind_of(st.sel) == "item") {
        let it = sel_thing(st)
        let step = if (evt.shiftKey) 1.0 else 10.0
        st = if (it.wall != null) patch_item(st, {at: it.at + (if (key == "arrowleft" or key == "arrowup") 0.0 - step else step)})
            else patch_item(st, {x: it.x + (if (key == "arrowleft") 0.0 - step else if (key == "arrowright") step else 0.0),
                y: it.y + (if (key == "arrowup") 0.0 - step else if (key == "arrowdown") step else 0.0)})
        return 'prevent-default'
    }
    let action = if (mod and key == "z") (if (evt.shiftKey) "redo" else "undo") else if (mod and key == "y") "redo"
        else if (mod and key == "d") "duplicate" else if (mod and key == "s") "save"
        else if (key == "delete" or key == "backspace") "delete" else if (key == "r" and not mod) "rot"
        else if (key == "escape") "escape" else null
    if (action == null) { return 'pass' }
    st = if (action == "escape") {*:st, sel: null, tool: "select"} else command(st, action, if (action == "rot") "90" else "")
    return 'prevent-default'
}

<html lang:"en",
    <head <meta charset:"UTF-8"><title "Dreamhome — Lambda"><style CSS>>
    apply(<dreamhome_app>)>
