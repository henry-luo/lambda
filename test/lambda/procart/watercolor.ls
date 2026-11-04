// procart.watercolor, pure parts: pigments and their optics tables, the paper height field, sheet
// tuning and layout, fresh sheets on the same paper, and stroke trails. The simulation procedures
// are covered by test/demo/procart/tests/proc/procart_watercolor_sim.ls.
import wc: lambda.procart.watercolor

fn round4(v) => round(v * 10000.0) / 10000.0

"1 palettes";
[for (p in wc.PALETTES) [p.id, p.title, [for (g in p.pigments) g.name]]];
len(wc.optics(wc.CLASSIC)) == 18 and wc.optics(wc.CLASSIC)[0] == 0.22 and wc.optics(wc.CLASSIC)[9] == 0.05;
wc.adsorption(wc.CLASSIC);

"2 colour to K and S: K/S = (1 - R)^2 / 2R, one scattering for every channel";
(let p = wc.pigment("Test", [0.5, 0.25, 1.2], 0.05, 3.0, 0.4), [p.name, [for (k in p.K) round4(k)], [for (v in p.S) round4(v)], p.density, p.staining, p.granulation]);
[for (p in wc.EARTH) [p.name, [for (k in p.K) round4(k)]]];

"3 paper: per-cell keyed noise around 0.5, inside [0.25, 0.75]";
(let h = wc.paper(64, 48, 7)^, [len(h), min(h) >= 0.25, max(h) <= 0.75, round4(sum(h) / float(len(h))), round4(h[0]), round4(h[100])]);
[wc.paper(16, 16, 7)^ == wc.paper(16, 16, 7)^, wc.paper(16, 16, 7)^ != wc.paper(16, 16, 8)^];

"4 sheet tuning at 2.5 display pixels per cell (Washes constants scaled from 2)";
(let s = wc.sheet(40, 30, 7)^, [s.width, s.height, s.scale, round4(s.wet_diffusion), round4(s.pigment_diffusion),
    s.edge_kernel, s.edge_kernel_large, round4(s.velocity_clamp), len(s.cells) == wc.PLANES * 1200, [s.x0, s.x1], s.wet_cells]);
(let s = wc.sheet(40, 30, 7, 2.0)^, [round4(s.wet_diffusion), round4(s.pigment_diffusion), s.edge_kernel, s.edge_kernel_large, s.velocity_clamp]);
(let s = wc.sheet(40, 30, 7, 1.0)^, [round4(s.wet_diffusion), round4(s.pigment_diffusion), s.edge_kernel, s.edge_kernel_large, s.velocity_clamp]);
(let s = wc.sheet(20, 10, 7, 2.5, 0.62)^, [s.wet_cells, min(wc.plane(s, wc.WET)^), max(wc.plane(s, wc.WET)^), max(wc.plane(s, wc.SUSPENDED)^)]);
[(wc.sheet(4, 4, 7)) ^ { "too small" }, (wc.sheet(1000, 1000, 7)) ^ { "too large" }];

"5 a blank sheet keeps its paper";
(let s = wc.sheet(30, 20, 9)^, let b = wc.blank(s, 0.62)^, [wc.plane(b, wc.PAPER)^ == wc.plane(s, wc.PAPER)^, b.wet_cells, max(wc.plane(b, wc.WET)^), len(b.cells) == len(s.cells)]);

"6 stroke trails: stamps every 0.4 of the radius, ending on the target";
wc.trail(0.0, 0.0, 10.0, 0.0, 5.0)^;
(let t = wc.trail(3.0, 4.0, 3.0, 4.0, 5.0)^, [len(t), t[0]]);
len(wc.trail(0.0, 0.0, 100.0, 100.0, 1.0)^);

"7 a fresh sheet measures empty";
wc.measure(wc.sheet(30, 20, 9)^)
