// Watercolor page model: study catalog, seeded stroke plans, controls, pointer input and labels.
// The simulation itself is covered by proc/procart_watercolor_sim.ls and the package goldens.
import W: ~~.mod_watercolor

fn summary(id, seed: int) array^ {
    let p = W.plan(id, seed)^;
    [id, len(p), if (len(p) == 0) 0 else p[len(p) - 1].frame,
     all([for (i in 1 to (len(p) - 1)) p[i - 1].frame <= p[i].frame]),
     all([for (s in p where s.x != null) s.x >= 0.0 and s.x <= float(W.SHEET_WIDTH) and s.y >= 0.0 and s.y <= float(W.SHEET_HEIGHT)]),
     [for (k in ["pigment", "water", "lift", "dry"]) len([for (s in p where s.kind == k) s])]]
}

let st = W.initial()^

"1 catalog";
[for (s in W.STUDIES) [s.id, s.title, s.tag, s.wetness]];
[for (t in W.TOOLS) t.id];
[W.WIDTH, W.HEIGHT, W.SHEET_WIDTH, W.SHEET_HEIGHT, W.SCALE, W.SIZES];

"2 plans: frame-ordered stamps on the sheet, counted by kind";
[for (s in W.STUDIES) summary(s.id, 2026)^];
[for (s in W.STUDIES where s.id != "free") W.plan(s.id, 2026)^ == W.plan(s.id, 2026)^];
[for (s in W.STUDIES where s.id != "free") W.plan(s.id, 2026)^ != W.plan(s.id, 2027)^];
[for (s in W.plan("blooms", 2026)^ where s.kind == "dry") s.level];
len([for (s in W.plan("blooms", 2026)^ where s.kind == "water" and s.load == 2.0) s]);

"3 the opening state";
[st.seed, st.id, st.tool, st.size, st.palette, st.drying, st.sheet.width, st.sheet.height, st.sheet.wet_cells,
 len(st.rgba), st.cursor, st.frame, st.full, W.busy(st)^];
[for (m in st.record) m.label];

"4 controls";
(let t = W.command(st, "p0")^, [t.tool, W.TOOLS[t.tool].id]);
[W.command(W.command(st, "larger")^, "larger")^.size, W.command(W.command(W.command(st, "smaller")^, "smaller")^, "smaller")^.size];
(let a = W.command(st, "palette")^, let b = W.command(a, "palette")^, let c = W.command(b, "palette")^, [a.palette, b.palette, c.palette, a.full]);
(let a = W.command(st, "drying")^, let b = W.command(a, "drying")^, [W.DRYING[a.drying].id, W.DRYING[b.drying].id, W.settings(a).evaporation]);
(let t = W.command(W.command(W.command(W.command(st, "pause")^, "edges")^, "tilt")^, "show-water")^, [t.paused, t.edges, t.tilt, t.water, W.settings(t)]);
(let t = W.command(W.command(st, "wet")^, "dry")^, t.pending);

"5 seeds and studies restart on the same paper";
(let n = W.command(st, "next")^, let p = W.command(n, "previous")^, [n.seed, p.seed, n.plan != st.plan, p.plan == st.plan, n.cursor, n.frame]);
(let g = W.command(st, "glazes")^, [g.id, g.sheet.wet_cells, len(g.plan), g.sheet.cells[0] == st.sheet.cells[0]]);
(let c = W.command(st, "clear")^, [c.id, len(c.plan), W.busy({*:c,full:false})^]);
W.command(st, "unknown")^ == st;

"6 the pointer: a press stamps, a drag lays a trail, a release lets go";
(let a = W.press(st, 40.0, 30.0), [a.drag, len(a.pending), a.pending[0]]);
(let a = W.move(W.press(st, 40.0, 30.0), 60.0, 30.0)^, [len(a.pending), a.pending[len(a.pending) - 1].x, a.drag]);
W.release(W.press(st, 40.0, 30.0)).drag == null;
W.move(st, 60.0, 30.0)^ == st;
W.cell({x:100.0,y:50.0,width:930.0,height:630.0}, 565.0, 365.0);

"7 labels";
[for (p in 0 to 2) (let t = {*:st,palette:p}, [for (i in 0 to 4) W.tool_label(t, i)^])];
[for (i in 0 to 4) W.tool_title(st, i)^]
