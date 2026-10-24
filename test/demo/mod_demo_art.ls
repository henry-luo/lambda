// Animated tile art for the demo index: one 320×180 SMIL scene per demo.
// Every animation repeats forever, and every gradient/clip id carries the
// demo name, since all nineteen scenes share one document.

fn n(x: any) => string(round(x * 10) / 10)
fn pt(x: any, y: any) => n(x) ++ " " ++ n(y)

fn anim(attr: any, values: any, dur: any, more: any = {}) => <animate attributeName:attr, values:values, dur:dur,
    repeatCount:"indefinite", *:more>
fn shift(values: any, dur: any, more: any = {}) => <animateTransform attributeName:"transform", type:"translate",
    values:values, dur:dur, repeatCount:"indefinite", *:more>
fn turn(values: any, dur: any, more: any = {}) => <animateTransform attributeName:"transform", type:"rotate",
    values:values, dur:dur, repeatCount:"indefinite", *:more>
fn grow(values: any, dur: any, more: any = {}) => <animateTransform attributeName:"transform", type:"scale",
    values:values, dur:dur, repeatCount:"indefinite", *:more>
// a 0→1→0 blink whose visible window is [from, to] of the loop
fn blink(dur: any, from: any, to: any, begin: any = "0s") => anim("opacity", "0;0;1;1;0",  dur,
    {keyTimes:"0;" ++ n(from) ++ ";" ++ n(from + .04) ++ ";" ++ n(to) ++ ";1", begin:begin})
// a path that draws itself over [0, at] of the loop and stays drawn
fn draw_in(length: any, dur: any, at: any, begin: any = "0s") => anim("stroke-dashoffset",
    string(length) ++ ";0;0;" ++ string(length), dur, {keyTimes:"0;" ++ n(at) ++ ";.9;1", begin:begin})

fn vgrad(id: any, top: any, bottom: any) => <linearGradient id:id, x1:0, y1:0, x2:0, y2:1,
    <stop offset:0, 'stop-color':top>;
    <stop offset:1, 'stop-color':bottom>>

fn canvas(id: any, top: any, bottom: any, body: any) => <svg class:"art", viewBox:"0 0 320 180",
    preserveAspectRatio:"xMidYMid slice", 'aria-hidden':"true",
    <defs (vgrad("bg-" ++ id, top, bottom))>;
    <rect width:320, height:180, fill:"url(#bg-" ++ id ++ ")">
    body>

let FONT = "-apple-system, 'Helvetica Neue', sans-serif"
fn text(x: any, y: any, size: any, color: any, caption: any, more: any = {}) => <text x:x, y:y, 'font-size':size, fill:color,
    'font-family':FONT, *:more, caption>

// ---- games ---------------------------------------------------------------

fn cell(x: any, y: any, color: any) => <rect x:x, y:y, width:15, height:15, rx:2, fill:color>
let TETRIS_STACK = [[0,0,"#f472b6"],[1,0,"#f472b6"],[2,0,"#60a5fa"],[3,0,"#60a5fa"],[4,0,"#facc15"],
    [5,0,"#facc15"],[0,1,"#34d399"],[1,1,"#34d399"],[4,1,"#fb923c"],[5,1,"#fb923c"],[5,2,"#fb923c"],
    [0,2,"#34d399"]]
fn tetris() => canvas("tetris", "#251b57", "#100b28", <g
    <rect x:112, y:0, width:96, height:180, fill:"#0a0720", opacity:".75">
    for (c in TETRIS_STACK) cell(113 + c[0] * 16, 165 - c[1] * 16, c[2]);
    // the bottom row flashes as it clears
    <rect x:112, y:164, width:96, height:16, fill:"#fff", opacity:0, (blink("2.4s", .82, .9))>;
    // a T piece falls one row per tick
    <g (shift("0 -28;0 -12;0 4;0 20;0 36;0 52;0 68;0 84;0 100", "2.4s", {calcMode:"discrete"}));
        (cell(145, 0, "#a78bfa")); (cell(161, 0, "#a78bfa")); (cell(177, 0, "#a78bfa")); (cell(161, 16, "#a78bfa"))>;
    (text(28, 40, 10, "#a5b4fc", "SCORE", {'letter-spacing':2}));
    (text(28, 62, 18, "#fff", "12,480", {'font-weight':700}));
    (text(28, 100, 10, "#a5b4fc", "LINES", {'letter-spacing':2}));
    (text(28, 120, 18, "#fff", "37", {'font-weight':700}));
    (text(232, 40, 10, "#a5b4fc", "NEXT", {'letter-spacing':2}));
    <g (shift("0 0;0 -4;0 0", "1.2s"));
        (cell(236, 56, "#22d3ee")); (cell(252, 56, "#22d3ee")); (cell(268, 56, "#22d3ee")); (cell(268, 72, "#22d3ee"))>>)

fn word_card(x: any, w: any, caption: any, from: any, begin: any) => <g (shift(from ++ ";0 0;0 0;" ++ from, "4.5s",
        {keyTimes:"0;.3;.85;1", calcMode:"spline", keySplines:".3 .9 .4 1;0 0 1 1;.6 0 .8 .4", begin:begin}));
    <rect x:x, y:96, width:w, height:36, rx:9, fill:"#fffdf5", stroke:"#0f766e", 'stroke-width':"2">;
    (text(x + w / 2, 120, 17, "#134e4a", caption, {'text-anchor':"middle", 'font-weight':700}))>
fn sentence_game() => canvas("sentence-game", "#11463f", "#082522", <g
    <path d:"M40 146 H280", stroke:"#5eead4", 'stroke-width':2, 'stroke-dasharray':"6 6", opacity:".6">;
    (word_card(46, 62, "The", "-30 -70", "0s"));
    (word_card(116, 62, "fox", "60 -80", "0s"));
    (word_card(186, 92, "jumps.", "40 -60", "0s"));
    // the finished sentence earns a star
    <g transform:"translate(278 52)",
        <path d:"M0 -16 L4.7 -5 L16 -4.9 L7.4 2.8 L9.9 14 L0 8 L-9.9 14 L-7.4 2.8 L-16 -4.9 L-4.7 -5 Z",
            fill:"#fde047", opacity:0, (blink("4.5s", .32, .82))>>;
    (text(40, 44, 12, "#99f6e4", "Build the sentence", {'letter-spacing':1}))>)

fn superlambda() => canvas("superlambda", "#38bdf8", "#bae6fd", <g
    <g (shift("0 0;-320 0", "14s"));
        for (x in [30, 170, 350, 490]) <g transform:"translate(" ++ string(x) ++ " 30)",
            <ellipse cx:0, cy:8, rx:26, ry:10, fill:"#fff", opacity:".9">;
            <ellipse cx:16, cy:0, rx:18, ry:12, fill:"#fff", opacity:".9">>>;
    <ellipse cx:60, cy:160, rx:90, ry:50, fill:"#4ade80">;
    <ellipse cx:250, cy:165, rx:110, ry:44, fill:"#22c55e">;
    // the question block bumps when the hero's head hits it
    <g (shift("0 0;0 0;0 -6;0 0;0 0", "1.4s", {keyTimes:"0;.42;.5;.58;1"}));
        <rect x:146, y:52, width:24, height:24, rx:3, fill:"#facc15", stroke:"#a16207", 'stroke-width':"2">;
        (text(158, 70, 16, "#a16207", "?", {'text-anchor':"middle", 'font-weight':800}))>;
    <ellipse cx:220, cy:96, rx:7, ry:9, fill:"#fde047", stroke:"#ca8a04", 'stroke-width':1.5,
        (anim("rx", "7;1;7", ".9s"))>;
    <rect x:0, y:148, width:320, height:32, fill:"#a16207">;
    <g (shift("0 0;-32 0", ".5s"));
        for (i in 0 to 11) <rect x:i * 32, y:148, width:30, height:6, fill:"#65a30d">>;
    // Lambda in a red λ cap
    <g (shift("0 0;0 -58;0 0", "1.4s", {keyTimes:"0;.5;1", calcMode:"spline", keySplines:".2 .8 .4 1;.6 0 .8 .2"}));
        <rect x:150, y:128, width:16, height:16, rx:3, fill:"#2563eb">;
        <circle cx:158, cy:120, r:9, fill:"#fcd9b6">;
        <path d:"M148 118 A10 10 0 0 1 168 118 L171 119 L148 119 Z", fill:"#dc2626">;
        (text(158, 117, 8, "#fff", "λ", {'text-anchor':"middle", 'font-weight':800}));
        <rect x:149, y:144, width:7, height:4, rx:1, fill:"#7c2d12">;
        <rect x:160, y:144, width:7, height:4, rx:1, fill:"#7c2d12">>>)

fn doom() => canvas("doom", "#1c0707", "#050101", <g
    <polygon points:"0,0 320,0 190,62 130,62", fill:"#1f1612">;
    <polygon points:"0,180 320,180 190,98 130,98", fill:"#2a2420">;
    <polygon points:"0,0 130,62 130,98 0,180", fill:"#4a2716">;
    <polygon points:"320,0 190,62 190,98 320,180", fill:"#3a1d10">;
    <rect x:130, y:62, width:60, height:36, fill:"#7f1d1d", (anim("fill", "#7f1d1d;#dc2626;#7f1d1d", "1.6s"))>;
    // torches flicker on both walls
    <circle cx:52, cy:70, r:6, fill:"#fb923c", (anim("opacity", "1;.5;.9;.4;1", ".45s"))>;
    <circle cx:268, cy:70, r:6, fill:"#fb923c", (anim("opacity", ".6;1;.4;.9;.6", ".5s"))>;
    // an imp walks out of the dark
    <g transform:"translate(160 92)",
        <g (grow(".35;1.05;.35", "3.2s"));
            <path d:"M-12 -34 L-6 -42 L-2 -32 L2 -32 L6 -42 L12 -34 L14 -8 L20 6 L10 6 L8 26 L2 26 L0 8 L-2 26 L-8 26 L-10 6 L-20 6 L-14 -8 Z",
                fill:"#7c4a2d">;
            <circle cx:-4, cy:-26, r:2, fill:"#facc15">;
            <circle cx:4, cy:-26, r:2, fill:"#facc15">>>;
    <path d:"M160 82 V90 M156 86 H164", stroke:"#fca5a5", 'stroke-width':"1.5">;
    // muzzle flash and the shotgun
    <g transform:"translate(160 136)",
        <path d:"M0 -26 L6 -10 L20 -14 L9 -2 L16 10 L0 2 L-16 10 L-9 -2 L-20 -14 L-6 -10 Z",
            fill:"#fde047", opacity:0, (blink("1.6s", .7, .78))>>;
    <path d:"M146 180 L150 140 L170 140 L174 180 Z", fill:"#4b5563">;
    <rect x:154, y:132, width:12, height:12, fill:"#374151">;
    <rect x:0, y:0, width:320, height:180, fill:"#ef4444", opacity:0, (blink("1.6s", .7, .74))>;
    <rect x:0, y:164, width:320, height:16, fill:"#262626">;
    (text(16, 177, 12, "#ef4444", "100%  ·  AMMO 50", {'font-weight':800, 'font-family':"Menlo, monospace"}))>)

// ---- worlds --------------------------------------------------------------

// sampled positions around a tilted ellipse, for an orbit SMIL can follow
fn orbit(cx: any, cy: any, rx: any, ry: any, tilt: any, steps: any) => [for (i in 0 to steps)
    (let a = i * 2 * math.pi / steps, let x = rx * math.cos(a), let y = ry * math.sin(a),
     {x: cx + x * math.cos(tilt) - y * math.sin(tilt), y: cy + x * math.sin(tilt) + y * math.cos(tilt)})]
let MOON = orbit(160, 92, 118, 30, -0.24, 32)
fn scene3d() => canvas("scene3d", "#040817", "#0d1a44", <g
    <defs <radialGradient id:"scene3d-planet", cx:.35, cy:.3, r:.8,
        <stop offset:0, 'stop-color':"#bae6fd">;
        <stop offset:.45, 'stop-color':"#3b82f6">;
        <stop offset:1, 'stop-color':"#1e1b4b">>>
    for (s in [[24,22,2.4],[70,140,1.6],[120,30,1.2],[250,24,1.8],[290,120,1.4],[210,160,1.2],[40,90,1]])
        <circle cx:s[0], cy:s[1], r:s[2], fill:"#e0f2fe", (anim("opacity", ".2;1;.2", string(1.5 + s[2]) ++ "s"))>;
    <g transform:"rotate(-14 160 92)",
        <path d:"M88 92 A72 16 0 0 1 232 92", fill:"none", stroke:"#c4b5fd", 'stroke-width':5, opacity:".55">;
        <path d:"M76 92 A84 20 0 0 1 244 92", fill:"none", stroke:"#a5b4fc", 'stroke-width':1.5, opacity:".4">>;
    <circle cx:160, cy:92, r:38, fill:"url(#scene3d-planet)">;
    <g transform:"rotate(-14 160 92)",
        <path d:"M88 92 A72 16 0 0 0 232 92", fill:"none", stroke:"#c4b5fd", 'stroke-width':5, opacity:".9">;
        <path d:"M76 92 A84 20 0 0 0 244 92", fill:"none", stroke:"#a5b4fc", 'stroke-width':1.5, opacity:".6">>;
    <circle r:7, fill:"#f1f5f9",
        (anim("cx", join([for (p in MOON) n(p.x)], ";"), "9s"));
        (anim("cy", join([for (p in MOON) n(p.y)], ";"), "9s"))>>)

// blocks laid out on a sunflower spiral, so the city fills in ring by ring
let CITY = [for (i in 1 to 64) (let r = 3 + 10 * math.sqrt(i), let a = i * 2.39996,
    {x: 160 + r * math.cos(a), y: 90 + r * math.sin(a) * .62, s: 7 - i * .06, t: i / 64 * .7})]
fn cityscape() => canvas("cityscape", "#0c0b12", "#1b1726", <g
    for (r in [26, 52, 78]) <ellipse cx:160, cy:90, rx:r, ry:r * .62, fill:"none", stroke:"#fbbf24",
        'stroke-width':1, 'stroke-dasharray':"3 4", opacity:".35">;
    <ellipse cx:160, cy:90, fill:"none", stroke:"#fde68a", 'stroke-width':1.5,
        (anim("rx", "4;96", "6s")); (anim("ry", "2.5;60", "6s")); (anim("opacity", ".7;0", "6s"))>
    for (b in CITY) <rect x:n(b.x - b.s / 2), y:n(b.y - b.s), width:n(b.s), height:n(b.s * 1.4), fill:"#f5f5f4",
        opacity:0, (anim("opacity", "0;0;.95;.95;0", "6s", {keyTimes:"0;" ++ n(b.t) ++ ";" ++ n(b.t + .05) ++ ";.9;1"}))>;
    (text(300, 166, 11, "#fcd34d", "1250 BC → 2022", {'text-anchor':"end", 'letter-spacing':1}))>)

// ---- pictures ------------------------------------------------------------

fn photo_tile(x: any, y: any, w: any, h: any, grad: any) => <g
    <rect x:x, y:y, width:w, height:h, rx:4, fill:"url(#" ++ grad ++ ")">;
    <path d:"M" ++ pt(x, y + h) ++ " L" ++ pt(x + w * .35, y + h * .45) ++ " L" ++ pt(x + w * .6, y + h * .75) ++
        " L" ++ pt(x + w * .8, y + h * .5) ++ " L" ++ pt(x + w, y + h) ++ " Z", fill:"#000", opacity:".22">>
let ALBUM = [[16,16,78,48,"pa-a"],[100,16,120,48,"pa-b"],[226,16,78,48,"pa-c"],
    [16,70,124,48,"pa-d"],[146,70,64,48,"pa-a"],[216,70,88,48,"pa-b"],
    [16,124,70,44,"pa-c"],[92,124,96,44,"pa-d"],[194,124,110,44,"pa-a"]]
fn photo_album() => canvas("photo-album", "#2b0f24", "#150711", <g
    <defs (vgrad("pa-a", "#fb7185", "#fbbf24")); (vgrad("pa-b", "#38bdf8", "#1e3a8a"));
        (vgrad("pa-c", "#86efac", "#166534")); (vgrad("pa-d", "#c084fc", "#f472b6"))>
    for (t in ALBUM) photo_tile(t[0], t[1], t[2], t[3], t[4]);
    // the selection frame steps through the timeline
    <rect width:0, height:0, fill:"none", stroke:"#fff", 'stroke-width':3, rx:5,
        (anim("x", join([for (t in ALBUM) n(t[0] - 2)], ";"), "6.3s", {calcMode:"discrete"}));
        (anim("y", join([for (t in ALBUM) n(t[1] - 2)], ";"), "6.3s", {calcMode:"discrete"}));
        (anim("width", join([for (t in ALBUM) n(t[2] + 4)], ";"), "6.3s", {calcMode:"discrete"}));
        (anim("height", join([for (t in ALBUM) n(t[3] + 4)], ";"), "6.3s", {calcMode:"discrete"}))>;
    <g transform:"translate(288 150)",
        <g (grow("1;1.3;1", "1.2s"));
            <path d:"M0 8 C-12 0 -10 -10 -4 -10 C-2 -10 0 -8 0 -6 C0 -8 2 -10 4 -10 C10 -10 12 0 0 8 Z", fill:"#f43f5e">>>>)

fn photo() => canvas("photo", "#1f1611", "#0e0907", <g
    <defs (vgrad("photo-sky", "#f97316", "#f9a8d4")); (vgrad("photo-sea", "#0e7490", "#164e63"))>;
    <rect x:30, y:16, width:260, height:134, rx:6, fill:"url(#photo-sky)">;
    <circle cx:200, cy:66, r:20, fill:"#fef3c7">;
    <path d:"M30 110 L90 62 L130 92 L170 56 L230 104 L290 80 L290 116 L30 116 Z", fill:"#7c2d12", opacity:".75">;
    <rect x:30, y:112, width:260, height:38, fill:"url(#photo-sea)">;
    // the cool-grade half follows the comparison handle
    <rect x:30, y:16, height:134, fill:"#1e3a8a", opacity:.45, (anim("width", "40;220;40", "5s"))>;
    <g (shift("0 0;180 0;0 0", "5s"));
        <path d:"M70 16 V150", stroke:"#fff", 'stroke-width':"2">;
        <circle cx:70, cy:83, r:9, fill:"#fff">;
        <path d:"M66 83 h8 M70 79 v8", stroke:"#0f172a", 'stroke-width':"1.5">>;
    <path d:"M60 166 H260", stroke:"#57534e", 'stroke-width':3, 'stroke-linecap':"round">;
    <circle cy:166, r:6, fill:"#fb923c", (anim("cx", "90;230;90", "5s"))>>)

fn layer(color: any, opacity: any, offset: any) => <g (shift("0 0;0 " ++ string(offset) ++ ";0 0", "3.6s",
        {calcMode:"spline", keyTimes:"0;.5;1", keySplines:".4 0 .2 1;.4 0 .2 1"}));
    <polygon points:"0,-22 80,0 0,22 -80,0", fill:color, opacity:opacity, stroke:"#e0f2fe", 'stroke-width':"1">>
fn photos() => canvas("photos", "#062a32", "#03161b", <g
    <g transform:"translate(116 112)",
        (layer("#0e7490", 1, 18));
        <g transform:"translate(0 -26)", (layer("#22d3ee", .7, 0))>;
        <g transform:"translate(0 -52)", (layer("#a5f3fc", .55, -18))>>;
    <rect x:222, y:22, width:84, height:136, rx:6, fill:"#0b3b44">;
    <rect x:226, width:76, height:26, rx:4, fill:"#155e75",
        (anim("y", "30;62;94;30", "3.6s", {calcMode:"discrete"}))>
    for (i in 0 to 2) <g
        <circle cx:240, cy:43 + i * 32, r:4, fill:"none", stroke:"#a5f3fc", 'stroke-width':"1.5">;
        <rect x:252, y:40 + i * 32, width:40 - i * 6, height:6, rx:3, fill:"#a5f3fc", opacity:".7">>>)

// ---- productivity -------------------------------------------------------

fn draw() => canvas("draw", "#141c0b", "#0a0f05", <g
    for (x in 0 to 15) for (y in 0 to 8) <circle cx:10 + x * 20, cy:10 + y * 20, r:1, fill:"#3f6212">;
    <path d:"M100 50 H136", fill:"none", stroke:"#bef264", 'stroke-width':2, 'stroke-dasharray':"40", (draw_in(40, "5s", .2))>;
    <path d:"M184 50 H222", fill:"none", stroke:"#bef264", 'stroke-width':2, 'stroke-dasharray':"40", (draw_in(40, "5s", .35))>;
    <path d:"M160 74 V118 H222", fill:"none", stroke:"#bef264", 'stroke-width':2, 'stroke-dasharray':"110", (draw_in(110, "5s", .5))>;
    <rect x:28, y:34, width:72, height:32, rx:8, fill:"#365314", stroke:"#a3e635", 'stroke-width':"2">;
    (text(64, 54, 11, "#ecfccb", "Start", {'text-anchor':"middle"}));
    <polygon points:"160,26 184,50 160,74 136,50", fill:"#3f6212", stroke:"#a3e635", 'stroke-width':"2">;
    (text(160, 54, 11, "#ecfccb", "?", {'text-anchor':"middle"}));
    <rect x:222, y:34, width:72, height:32, rx:4, fill:"#365314", stroke:"#a3e635", 'stroke-width':"2">;
    (text(258, 54, 11, "#ecfccb", "Render", {'text-anchor':"middle"}));
    // the last box is being dragged into place
    <g (shift("-60 30;0 0;0 0;-60 30", "5s", {keyTimes:"0;.6;.9;1"}));
        <rect x:222, y:102, width:72, height:32, rx:4, fill:"#365314", stroke:"#a3e635", 'stroke-width':"2">;
        (text(258, 122, 11, "#ecfccb", "Export", {'text-anchor':"middle"}));
        <path d:"M270 124 l0 16 l4 -4 l3 7 l3 -1 l-3 -7 l6 0 Z", fill:"#fff", stroke:"#1a2e05", 'stroke-width':"1">>>)

let WORD_LINES = [[46, 120, 6], [62, 132, 4], [74, 118, 4], [86, 128, 4], [104, 96, 4], [116, 124, 4], [128, 70, 4]]
fn word() => canvas("word", "#0d1d42", "#070f26", <g
    <rect x:0, y:0, width:320, height:22, fill:"#1d4ed8">
    for (i in 0 to 4) <rect x:12 + i * 22, y:7, width:14, height:8, rx:2, fill:"#bfdbfe", opacity:".7">;
    <rect x:84, y:30, width:152, height:170, fill:"#fff">
    // lines type themselves in turn; the caret rides the line being written
    for (i in 0 to 6) (let l = WORD_LINES[i], let from = .08 + i * .11,
        <rect x:100, y:l[0] - l[2], height:l[2], rx:1, fill:if (i == 0) "#1e3a8a" else "#94a3b8",
            (anim("width", "0;0;" ++ string(l[1]) ++ ";" ++ string(l[1]) ++ ";0", "6s",
                {keyTimes:"0;" ++ n(from) ++ ";" ++ n(from + .1) ++ ";.92;1"}))>);
    <rect width:1.5, height:9, fill:"#2563eb",
        (anim("x", join([for (l in WORD_LINES) "100;" ++ string(100 + l[1])], ";") ++ ";100", "6s",
            {keyTimes: "0;" ++ join([for (i in 0 to 6) n(.08 + i * .11) ++ ";" ++ n(.18 + i * .11)], ";") ++ ";1"}));
        (anim("y", join([for (l in WORD_LINES) string(l[0] - 7) ++ ";" ++ string(l[0] - 7)], ";") ++ ";39", "6s",
            {keyTimes: "0;" ++ join([for (i in 0 to 6) n(.08 + i * .11) ++ ";" ++ n(.18 + i * .11)], ";") ++ ";1"}));
        (anim("opacity", "1;0", ".8s", {calcMode:"discrete"}))>;
    <g transform:"translate(28 60)",
        <rect width:40, height:70, rx:6, fill:"#1e3a8a">;
        (text(20, 20, 13, "#fff", "B", {'text-anchor':"middle", 'font-weight':800}));
        (text(20, 40, 13, "#fff", "I", {'text-anchor':"middle", 'font-style':"italic", 'font-family':"Georgia, serif"}));
        (text(20, 60, 13, "#fff", "U", {'text-anchor':"middle", 'text-decoration':"underline"}))>>)

fn note() => canvas("note", "#2c2408", "#151103", <g
    <rect x:24, y:16, width:70, height:150, rx:6, fill:"#3a300d">
    for (i in 0 to 3) <rect x:32, y:28 + i * 18, width:50 - i * 6, height:6, rx:3, fill:"#fde68a", opacity:".5">;
    <rect x:104, y:16, width:196, height:150, rx:6, fill:"#fffbeb">;
    (text(118, 42, 15, "#78350f", "Weekend", {'font-weight':800}))
    for (i in 0 to 3) (let y = 60 + i * 24, let at = .12 + i * .16, <g
        <rect x:118, y:y, width:14, height:14, rx:3, fill:"none", stroke:"#d97706", 'stroke-width':"1.5">;
        <path d:"M121 " ++ string(y + 7) ++ " l3 4 l6 -8", fill:"none", stroke:"#16a34a", 'stroke-width':2.2,
            'stroke-linecap':"round", 'stroke-dasharray':"16", (draw_in(16, "5s", at))>;
        <rect x:140, y:y + 4, width:120 - i * 14, height:6, rx:3, fill:"#a16207", opacity:".45">;
        <rect x:138, y:y + 6, height:1.6, fill:"#78350f",
            (anim("width", "0;0;" ++ string(124 - i * 14) ++ ";" ++ string(124 - i * 14) ++ ";0", "5s",
                {keyTimes:"0;" ++ n(at) ++ ";" ++ n(at + .06) ++ ";.9;1"}))>>)>)

fn slide(x: any, kind: any) => <g transform:"translate(" ++ string(x) ++ " 0)",
    <rect x:40, y:22, width:240, height:135, fill:"#f8fafc">;
    <rect x:40, y:22, width:240, height:22, fill:"#0f2e4c">;
    <rect x:52, y:30, width:if (kind == 0) 80 else 64, height:6, rx:3, fill:"#5eead4">
    if (kind == 0) <g
        for (i in 0 to 5) <rect x:64 + i * 32, y:140 - (20 + i * 14), width:20, height:20 + i * 14, rx:2,
            fill:if (i == 5) "#14b8a6" else "#94a3b8">>
    else <g
        <circle cx:110, cy:98, r:34, fill:"none", stroke:"#e2e8f0", 'stroke-width':"14">;
        <circle cx:110, cy:98, r:34, fill:"none", stroke:"#14b8a6", 'stroke-width':14,
            'stroke-dasharray':"150 214", transform:"rotate(-90 110 98)">
        for (i in 0 to 2) <rect x:168, y:74 + i * 18, width:84 - i * 18, height:7, rx:3, fill:"#0f2e4c", opacity:".6">>>
fn slides() => canvas("slides", "#06302c", "#031a17", <g
    <defs <clipPath id:"slides-frame", <rect x:40, y:22, width:240, height:"135">>>;
    <g 'clip-path':"url(#slides-frame)",
        <g (shift("0 0;0 0;-240 0;-240 0;-480 0", "6s",
                {keyTimes:"0;.4;.5;.9;1", calcMode:"spline", keySplines:"0 0 1 1;.5 0 .2 1;0 0 1 1;.5 0 .2 1"}));
            (slide(0, 0)); (slide(240, 1)); (slide(480, 0))>>
    for (i in 0 to 2) <circle cx:148 + i * 12, cy:168, r:3, fill:"#5eead4", opacity:".35">;
    <circle cy:168, r:3, fill:"#5eead4", (anim("cx", "148;148;160;160;148", "6s", {keyTimes:"0;.45;.5;.95;1", calcMode:"discrete"}))>>)

let BARS = [40, 72, 56, 94, 80, 112]
fn chart() => canvas("chart", "#161a40", "#0a0b22", <g
    <defs (vgrad("chart-bar", "#a78bfa", "#4f46e5"))>
    for (y in [40, 70, 100, 130]) <path d:"M40 " ++ string(y) ++ " H292", stroke:"#312e81", 'stroke-width':"1">;
    <path d:"M40 156 H292", stroke:"#6366f1", 'stroke-width':"1.5">
    for (i in 0 to 5) (let h = BARS[i], let b = string(i * .12) ++ "s",
        <rect x:54 + i * 40, width:24, rx:3, fill:"url(#chart-bar)",
            (anim("height", "0;" ++ string(h) ++ ";" ++ string(h) ++ ";0", "5s", {keyTimes:"0;.3;.85;1", begin:b}));
            (anim("y", "156;" ++ string(156 - h) ++ ";" ++ string(156 - h) ++ ";156", "5s", {keyTimes:"0;.3;.85;1", begin:b}))>);
    <path d:"M66 120 L106 96 L146 108 L186 70 L226 80 L266 44", fill:"none", stroke:"#f0abfc", 'stroke-width':2.5,
        'stroke-linejoin':"round", 'stroke-dasharray':"260", (draw_in(260, "5s", .45))>;
    <circle cx:266, cy:44, r:4.5, fill:"#f0abfc", (blink("5s", .45, .88))>>)

let CLOUD = [["Lambda", 160, 98, 38, "#f5d0fe"], ["Pure", 70, 60, 18, "#c4b5fd"], ["Data", 248, 58, 22, "#f9a8d4"],
    ["Charts", 236, 140, 17, "#a5b4fc"], ["SVG", 60, 134, 16, "#f0abfc"], ["Types", 140, 40, 15, "#e9d5ff"],
    ["JIT", 290, 100, 14, "#fbcfe8"], ["Layout", 112, 150, 14, "#ddd6fe"], ["中文", 34, 96, 16, "#fda4af"],
    ["café", 206, 32, 13, "#c7d2fe"], ["Maps", 290, 160, 12, "#f5d0fe"], ["Documents", 168, 128, 15, "#fae8ff"]]
fn wordcloud() => canvas("wordcloud", "#2d0b36", "#14041a", <g
    // each word breathes on its own beat
    for (i in 0 to 11) (let w = CLOUD[i], <text x:w[1], y:w[2], 'font-size':w[3], fill:w[4], 'font-family':FONT,
        'text-anchor':"middle", 'font-weight':if (i == 0) 800 else 600,
        (anim("opacity", ".35;1;.35", string(2 + (i % 4) * .7) ++ "s", {begin:string(i * .3) ++ "s"}));
        (w[0])>)>)

// ---- tools ---------------------------------------------------------------

fn folder(x: any, y: any) => <path d:"M" ++ pt(x, y + 4) ++ " h12 l4 4 h20 v22 h-36 Z", fill:"#60a5fa">
fn file_icon(x: any, y: any) => <g
    <path d:"M" ++ pt(x + 4, y) ++ " h20 l8 8 v26 h-28 Z", fill:"#e5e7eb">;
    <path d:"M" ++ pt(x + 9, y + 16) ++ " h14 M" ++ pt(x + 9, y + 22) ++ " h14", stroke:"#9ca3af", 'stroke-width':"2">>
let FINDER_SPOTS = [[110, 46], [160, 46], [210, 46], [260, 46], [110, 104], [160, 104], [210, 104]]
fn finder() => canvas("finder", "#111827", "#0a0f1c", <g
    <rect x:20, y:14, width:280, height:156, rx:8, fill:"#1f2937">
    for (c in [[34, "#ef4444"], [46, "#f59e0b"], [58, "#22c55e"]]) <circle cx:c[0], cy:24, r:4, fill:c[1]>;
    <rect x:20, y:34, width:68, height:136, fill:"#172033">
    for (i in 0 to 4) <rect x:30, y:46 + i * 20, width:48 - (i % 2) * 12, height:6, rx:3, fill:"#64748b">;
    <rect width:44, height:50, rx:6, fill:"#2563eb", opacity:.45,
        (anim("x", join([for (s in FINDER_SPOTS) string(s[0] - 4)], ";"), "4.9s", {calcMode:"discrete"}));
        (anim("y", join([for (s in FINDER_SPOTS) string(s[1] - 6)], ";"), "4.9s", {calcMode:"discrete"}))>
    for (i in 0 to 6) (let s = FINDER_SPOTS[i], if (i % 3 == 1) file_icon(s[0], s[1]) else folder(s[0], s[1]))
    for (i in 0 to 6) <rect x:FINDER_SPOTS[i][0] + 4, y:FINDER_SPOTS[i][1] + 40, width:28, height:4, rx:2, fill:"#94a3b8">>)

fn mail_row(y: any, unread: any) => <g
    <circle cx:126, cy:y + 6, r:3.5, fill:if (unread) "#38bdf8" else "none">;
    <rect x:136, y:y, width:90, height:6, rx:3, fill:"#e2e8f0", opacity:if (unread) "0.9" else "0.5">;
    <rect x:136, y:y + 10, width:140, height:5, rx:2.5, fill:"#94a3b8", opacity:".5">>
fn mail() => canvas("mail", "#072236", "#03121e", <g
    <rect x:110, y:20, width:190, height:144, rx:8, fill:"#0f2a40">;
    <rect x:110, y:20, width:190, height:22, rx:8, fill:"#123552">;
    // the new message lands on top of the list
    <g (anim("opacity", "0;0;1;1;0", "4s", {keyTimes:"0;.45;.55;.9;1"})); (mail_row(54, true))>;
    (mail_row(84, true)); (mail_row(112, false)); (mail_row(140, false));
    <g (shift("-90 40;40 -10;40 -10;-90 40", "4s", {keyTimes:"0;.5;.95;1"}));
        (anim("opacity", "0;1;1;0;0", "4s", {keyTimes:"0;.15;.45;.55;1"}));
        <rect x:30, y:60, width:56, height:38, rx:4, fill:"#e0f2fe">;
        <path d:"M30 62 L58 84 L86 62", fill:"none", stroke:"#0369a1", 'stroke-width':"2.5">>>)

fn archive() => canvas("archive", "#2c1b04", "#150c01", <g
    <circle cx:58, cy:80, r:30, fill:"none", stroke:"#fbbf24", 'stroke-width':"2">;
    <ellipse cx:58, cy:80, rx:13, ry:30, fill:"none", stroke:"#fbbf24", 'stroke-width':"1.5">;
    <path d:"M28 80 H88 M33 64 H83 M33 96 H83", stroke:"#fbbf24", 'stroke-width':"1.5">
    // pages fly from the web into the box
    for (i in 0 to 2) <g (shift("0 0;90 -40;176 10", "2.7s", {begin:string(i * .9) ++ "s"}));
        (anim("opacity", "0;1;1;0", "2.7s", {keyTimes:"0;.15;.8;1", begin:string(i * .9) ++ "s"}));
        <rect x:58, y:66, width:22, height:28, rx:2, fill:"#fffbeb">;
        <path d:"M62 74 h14 M62 80 h14 M62 86 h9", stroke:"#d97706", 'stroke-width':"1.5">>;
    <path d:"M210 90 L290 90 L290 140 L210 140 Z", fill:"#b45309">;
    <path d:"M210 90 L196 74 L268 74 L290 90 Z", fill:"#d97706">;
    <rect x:236, y:104, width:28, height:8, rx:2, fill:"#fde68a">;
    <rect x:40, y:158, width:240, height:6, rx:3, fill:"#422006">;
    <rect x:40, y:158, height:6, rx:3, fill:"#fbbf24", (anim("width", "0;240;240", "2.7s", {keyTimes:"0;.9;1"}))>>)

fn readability() => canvas("readability", "#0b2416", "#05120b", <g
    <rect x:28, y:14, width:264, height:156, rx:8, fill:"#e5e7eb">;
    <rect x:28, y:14, width:264, height:20, rx:8, fill:"#cbd5e1">;
    <rect x:60, y:20, width:180, height:8, rx:4, fill:"#f8fafc">;
    <path d:"M266 19 h7 l3 3 v8 h-10 Z", fill:"none", stroke:"#16a34a", 'stroke-width':1.5,
        (anim("stroke-width", "1.5;1.5;3;1.5;1.5", "6s", {keyTimes:"0;.38;.45;.52;1"}))>;
    // the cluttered page …
    <g (anim("opacity", "1;1;0;0;1", "6s", {keyTimes:"0;.4;.5;.9;1"}));
        <rect x:36, y:40, width:248, height:22, fill:"#f97316">;
        (text(160, 56, 11, "#fff", "SALE! 50% OFF — CLICK NOW", {'text-anchor':"middle", 'font-weight':800}));
        <rect x:36, y:68, width:60, height:96, fill:"#a855f7", opacity:".8">;
        <rect x:224, y:68, width:60, height:46, fill:"#ef4444", opacity:".8">;
        <rect x:224, y:118, width:60, height:46, fill:"#0ea5e9", opacity:".8">
        for (i in 0 to 6) <rect x:104, y:72 + i * 13, width:112 - (i % 3) * 18, height:5, rx:2, fill:"#64748b">>;
    // … becomes one calm column
    <g (anim("opacity", "0;0;1;1;0", "6s", {keyTimes:"0;.4;.5;.9;1"}));
        <rect x:36, y:40, width:248, height:126, fill:"#fffdf7">;
        (text(92, 62, 14, "#1f2937", "The Article", {'font-weight':700, 'font-family':"Georgia, serif"}))
        for (i in 0 to 6) <rect x:92, y:74 + i * 12, width:136 - (i % 3) * 14, height:5, rx:2, fill:"#9ca3af">>>)

let ARTS = {tetris: tetris, 'sentence-game': sentence_game, superlambda: superlambda, doom: doom,
    scene3d: scene3d, cityscape: cityscape, 'photo-album': photo_album, photo: photo, photos: photos,
    draw: draw, word: word, note: note, slides: slides, chart: chart, wordcloud: wordcloud,
    finder: finder, mail: mail, archive: archive, readability: readability}

pub fn of(key) => ARTS[key]()
