// Tile art and app icons for the demo index: one 320×180 scene per demo.
// Most demos show a screenshot of the app itself (previews/, refreshed by
// previews/capture.sh) in a window on a tint of the demo's tone; the games,
// the film and the photo album stay live SMIL drawings made from their own
// art. Every gradient/clip id carries the demo name, since all the scenes
// share one document.

fn n(x: any) => string(round(x * 10) / 10)
fn pt(x: any, y: any) => n(x) ++ " " ++ n(y)

// slate inks, darkest first
let INK = "#1e293b"
let MID = "#64748b"
let SOFT = "#94a3b8"
let FAINT = "#e2e8f0"
let PAPER = "#ffffff"
// muted hues in four strengths, with the canvas tint they sit on
let TONES = {
    blue: {deep: "#34589a", base: "#5b82c4", soft: "#a3bbe0", pale: "#dfe7f4", top: "#f5f8fc", bottom: "#e3eaf5"},
    violet: {deep: "#57488f", base: "#7f70bd", soft: "#b9b0dc", pale: "#e8e4f4", top: "#f8f6fc", bottom: "#ebe7f5"},
    amber: {deep: "#7f5620", base: "#bb8a45", soft: "#dcc292", pale: "#f2e9d6", top: "#fcf9f2", bottom: "#f3e9d6"},
    rose: {deep: "#8e4560", base: "#c07890", soft: "#deb2c1", pale: "#f4e3e9", top: "#fcf6f8", bottom: "#f4e4ea"},
    teal: {deep: "#2f6b66", base: "#559a93", soft: "#a2cac4", pale: "#ddefec", top: "#f4faf9", bottom: "#e1efec"},
    clay: {deep: "#83493b", base: "#b8765f", soft: "#dbb1a2", pale: "#f3e3dd", top: "#fbf6f4", bottom: "#f2e3dd"},
    sage: {deep: "#4b6939", base: "#7c9b64", soft: "#b9cca9", pale: "#e6eedf", top: "#f7faf4", bottom: "#e7efe0"},
    cyan: {deep: "#2c6579", base: "#5394aa", soft: "#a1c7d4", pale: "#ddedf2", top: "#f4f9fb", bottom: "#e0edf2"},
    indigo: {deep: "#3b4789", base: "#6573bb", soft: "#abb3dd", pale: "#e3e6f5", top: "#f6f7fc", bottom: "#e5e8f5"},
    gold: {deep: "#7b5f17", base: "#b39339", soft: "#d9c789", pale: "#f1ebd2", top: "#fbf9f0", bottom: "#f1ead0"},
    slate: {deep: "#334155", base: "#5b82c4", soft: "#a3bbe0", pale: "#dfe7f4", top: "#f8fafc", bottom: "#eef2f7"},
    // Superlambda's own sky, behind the game's scenery
    sky: {deep: "#20322f", base: "#56b6dd", soft: "#56b6dd", pale: "#56b6dd", top: "#56b6dd", bottom: "#56b6dd"},
    // the Tetris app's navy, and the city film's night
    arcade: {deep: "#0b1020", base: "#c084fc", soft: "#26304f", pale: "#131a33", top: "#0f1529", bottom: "#1b2347"},
    night: {deep: "#fcd34d", base: "#f59e0b", soft: "#fde68a", pale: "#475569", top: "#0b1229", bottom: "#1e2a4a"}
}

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

fn vgrad(id: any, top: any, bottom: any) => <linearGradient id:id, x1:0, y1:0, x2:0, y2:1,
    <stop offset:0, 'stop-color':top>;
    <stop offset:1, 'stop-color':bottom>>

// every scene sits on a pale tint of its tone
fn canvas(id: any, t: any, body: any) => <svg class:"art", viewBox:"0 0 320 180",
    preserveAspectRatio:"xMidYMid slice", 'aria-hidden':"true",
    <defs (vgrad("bg-" ++ id, t.top, t.bottom))>;
    <rect width:320, height:180, fill:"url(#bg-" ++ id ++ ")">
    body>

let FONT = "-apple-system, 'Helvetica Neue', sans-serif"
fn text(x: any, y: any, size: any, color: any, caption: any, more: any = {}) => <text x:x, y:y, 'font-size':size, fill:color,
    'font-family':FONT, *:more, caption>

// ---- app windows ---------------------------------------------------------

// A window that bleeds off the tile's bottom-right corner, on a diagonal wash
// of the tone with a light glow behind it and a soft shadow under it.
fn window(id: any, t: any, body: any) => <svg class:"art", viewBox:"0 0 320 180",
    preserveAspectRatio:"xMidYMid slice", 'aria-hidden':"true",
    <defs
        <linearGradient id:"bg-" ++ id, x1:0, y1:0, x2:1, y2:1,
            <stop offset:0, 'stop-color':t.pale>;
            <stop offset:1, 'stop-color':t.soft>>;
        <radialGradient id:"glow-" ++ id,
            <stop offset:0, 'stop-color':"#fff", 'stop-opacity':".75">;
            <stop offset:1, 'stop-color':"#fff", 'stop-opacity':"0">>;
        <filter id:"shade-" ++ id, x:"-20%", y:"-20%", width:"140%", height:"140%",
            <feGaussianBlur stdDeviation:6>>;
        <clipPath id:"win-" ++ id, <rect x:26, y:22, width:320, height:172, rx:7>>>;
    <rect width:320, height:180, fill:"url(#bg-" ++ id ++ ")">;
    <circle cx:30, cy:10, r:150, fill:"url(#glow-" ++ id ++ ")">;
    <rect x:28, y:30, width:320, height:172, rx:9, fill:t.deep, opacity:".35", filter:"url(#shade-" ++ id ++ ")">;
    <g 'clip-path':"url(#win-" ++ id ++ ")", <rect x:26, y:22, width:320, height:172, fill:PAPER>; body>;
    <rect x:26.5, y:22.5, width:320, height:172, rx:7, fill:"none", stroke:"#0f172a", 'stroke-opacity':".14">>

// the app's own screen, captured at 1280×800 and cropped to the window's shape
let SHOTS = "previews/"
fn shot(key: any, t: any) => window(key, t, <image href:SHOTS ++ key ++ ".jpg", x:26, y:22, width:320, height:172,
    preserveAspectRatio:"xMinYMin slice">)

// ---- games ---------------------------------------------------------------

// the Tetris app's own look: a navy well, bright bevelled cells, side panels
fn cell(x: any, y: any, color: any) => <g
    <rect x:x, y:y, width:15, height:15, rx:2.5, fill:color>;
    <rect x:x + 1.5, y:y + 1.5, width:12, height:4, rx:1.5, fill:PAPER, opacity:".3">>
let BRICK = {rose: "#fb7185", blue: "#60a5fa", amber: "#fbbf24", teal: "#2dd4bf", orange: "#fb923c", violet: "#a78bfa",
    cyan: "#22d3ee"}
let TETRIS_STACK = [[0,0,BRICK.rose],[1,0,BRICK.rose],[2,0,BRICK.blue],[3,0,BRICK.blue],
    [4,0,BRICK.amber],[5,0,BRICK.amber],[0,1,BRICK.teal],[1,1,BRICK.teal],[4,1,BRICK.orange],
    [5,1,BRICK.orange],[5,2,BRICK.orange],[0,2,BRICK.teal]]
fn panel(x: any, y: any, w: any, h: any, t: any, caption: any) => <g
    <rect x:x, y:y, width:w, height:h, rx:8, fill:t.pale, stroke:t.soft, 'stroke-width':"1">;
    (text(x + 10, y + 15, 6.5, SOFT, caption, {'letter-spacing':1.5, 'font-weight':700}))>
fn tetris(t) => canvas("tetris", t, <g
    <rect x:110, y:0, width:100, height:180, fill:t.deep>
    for (i in 1 to 5) <path d:"M" ++ string(112 + i * 16) ++ " 0 V180", stroke:t.pale, 'stroke-width':"1">
    for (i in 0 to 10) <path d:"M112 " ++ string(164 - i * 16) ++ " H208", stroke:t.pale, 'stroke-width':"1">;
    <path d:"M110.5 0 V180 M209.5 0 V180", stroke:t.soft, 'stroke-width':"1">
    for (c in TETRIS_STACK) cell(113 + c[0] * 16, 165 - c[1] * 16, c[2]);
    // the bottom row flashes as it clears
    <rect x:112, y:164, width:96, height:16, fill:PAPER, opacity:"0", (blink("2.4s", .82, .9))>;
    // a T piece falls one row per tick
    <g (shift("0 -28;0 -12;0 4;0 20;0 36;0 52;0 68;0 84;0 100", "2.4s", {calcMode:"discrete"}));
        (cell(145, 0, t.base)); (cell(161, 0, t.base)); (cell(177, 0, t.base)); (cell(161, 16, t.base))>;
    (panel(16, 18, 80, 144, t, "SCORE"));
    (text(26, 56, 20, PAPER, "12,480", {'font-weight':700}));
    (text(26, 84, 6.5, SOFT, "LEVEL", {'letter-spacing':1.5, 'font-weight':700}));
    (text(26, 102, 15, PAPER, "7", {'font-weight':700}));
    (text(60, 84, 6.5, SOFT, "LINES", {'letter-spacing':1.5, 'font-weight':700}));
    (text(60, 102, 15, PAPER, "37", {'font-weight':700}));
    <rect x:26, y:124, width:60, height:22, rx:6, fill:"#86efac">;
    (text(56, 138.5, 7.5, t.deep, "AUTO PLAY", {'text-anchor':"middle", 'font-weight':700, 'letter-spacing':.5}));
    (panel(224, 18, 80, 70, t, "NEXT"));
    <g (shift("0 0;0 -3;0 0", "1.2s"));
        (cell(240, 46, BRICK.cyan)); (cell(256, 46, BRICK.cyan)); (cell(272, 46, BRICK.cyan)); (cell(272, 62, BRICK.cyan))>;
    (panel(224, 96, 80, 66, t, "HOLD"));
    <g opacity:".45", (cell(249, 122, BRICK.violet)); (cell(265, 122, BRICK.violet)); (cell(249, 138, BRICK.violet));
        (cell(265, 138, BRICK.violet))>>)

// Superlambda draws with the game's own sprites (superlambda/assets), at 0.6 of
// the game's scale over its full scenery: Lambda runs, clears the pipe and
// lands on a grub while the world scrolls one 320-unit strip every 5 s.
let SL = "superlambda/assets/"
fn sprite(name: any, x: any, y: any, w: any, h: any) => <image href:SL ++ name ++ ".svg", x:x, y:y,
    width:w, height:h, preserveAspectRatio:"none">
fn sl_strip(dx: any) => <g transform:"translate(" ++ string(dx) ++ " 0)",
    (sprite("brick", 116, 66, 19.2, 19.2)); (sprite("question", 135.2, 66, 19.2, 19.2));
    (sprite("brick", 154.4, 66, 19.2, 19.2));
    // the coin spins about its centre above the ? block
    <g transform:"translate(144.8 49.8)",
        <g (grow("1 1;.15 1;1 1", "0.9s")); (sprite("coin", -6, -7.8, 12, 15.6))>>;
    (sprite("pipe", 230, 92.4, 38.4, 57.6));
    // the grub Lambda lands on squashes flat and fades
    <g transform:"translate(280 150)",
        <g (grow("1 1;1 1;1.2 .3;1.2 .3;1 1", "5s", {keyTimes:"0;.63;.66;.97;1"}));
            (anim("opacity", "1;1;1;0;0;1", "5s", {keyTimes:"0;.63;.7;.76;.97;1"}));
            (sprite("grub", 0, -18, 19.2, 18))>>>
fn superlambda(t) => canvas("superlambda", t, <g
    <defs <clipPath id:"superlambda-frame", <rect width:40, height:54>>>;
    <image href:SL ++ "scenery.svg", width:330, height:180, x:-5>;
    <g (shift("0 0;-320 0", "5s")); (sl_strip(0)); (sl_strip(320))>;
    <g (shift("0 0;-38.4 0", "0.6s"));
        for (i in 0 to 9) sprite("ground", i * 38.4, 150, 38.4, 48)>;
    // the running frames of the hero sheet, one 40-unit frame at a time
    <g (shift("0 0;0 0;0 -70;0 0;0 0", "5s", {keyTimes:"0;.4;.52;.64;1", calcMode:"spline",
            keySplines:"0 0 1 1;.2 .8 .4 1;.6 0 .8 .2;0 0 1 1"}));
        <g transform:"translate(70 117.6) scale(.6)", 'clip-path':"url(#superlambda-frame)",
            <image href:SL ++ "hero.svg", y:0, width:120, height:54,
                (anim("x", "0;-40;-80;-40", "0.48s", {calcMode:"discrete"}))>>>;
    <g transform:"translate(10 10)",
        <rect width:58, height:22, rx:5, fill:"#fff9e8", stroke:"#20322f", 'stroke-width':"1.5">;
        (sprite("coin", 7, 4.5, 10, 13));
        (text(22, 15.5, 10, "#bb801c", "× 07", {'font-weight':800, 'font-family':"Arial, sans-serif"}))>>)

// DOOM draws with the game's own art (doom/assets) as a tiny raycaster: the
// corridor walls are vertical strips of STARTAN2, each a slice of the texture
// stretched to its height at that depth and darkened with distance, so the
// edges step like the original renderer's. An imp walks up the corridor and
// is shot down; the shotgun fires and the status bar counts the shell.
let DM = "doom/assets/"
fn doom_image(path: any, x: any, y: any, w: any, h: any, more: any = {}) => <image href:DM ++ path ++ ".png", x:x, y:y,
    width:w, height:h, preserveAspectRatio:"none", 'image-rendering':"pixelated", *:more>

// the view: focal length 160 around (160, 74); walls 1 unit either side, 1 unit
// above and below the eye; the corridor ends at depth 4
let EYE_X = 160
let EYE_Y = 74
let FAR = 4
fn wall_top(z: any) => EYE_Y - 160 / z
fn wall_bottom(z: any) => EYE_Y + 160 / z
// one 6-pixel strip of wall between screen columns xa and xb at depths za and zb
fn wall_strip(xa: any, za: any, zb: any) => (let zm = (za + zb) / 2, let ua = za * 64, let ub = zb * 64,
    let top = wall_top(zm), let h = wall_bottom(zm) - top, <g
    <svg x:n(xa), y:n(top), width:6, height:n(h), viewBox:n(min(ua, ub)) ++ " 0 " ++ n(abs(ub - ua)) ++ " 128",
        preserveAspectRatio:"none",
        for (k in 0 to 2) doom_image("textures/STARTAN2", k * 128, 0, 128, 128)>
    <rect x:n(xa), y:n(top), width:6, height:n(h), fill:"#000", opacity:n(min(.72, (zm - 1) * .2))>>)
let STRIPS = 20
fn corridor() => <g
    for (k in 0 to STRIPS - 1) (let xa = k * 6, wall_strip(xa, 160 / (160 - xa), 160 / (154 - xa)))
    for (k in 0 to STRIPS - 1) (let xa = 200 + k * 6, wall_strip(xa, 160 / (xa - 160), 160 / (xa + 6 - 160)))>

// the imp's walk: its depth over the first 70% of the loop, then it holds
let IMP_Z = [for (i in 0 to 7) 3.8 - i * .3]
let IMP_TIMES = join([for (i in 0 to 7) n(i * .1)], ";") ++ ";1"
fn sprite_at(name: any, w: any, h: any, visible: any, dur: any, times: any) => <g opacity:"0",
    (anim("opacity", visible, dur, {keyTimes:times, calcMode:"discrete"}));
    (doom_image("sprites/" ++ name, -w / 2, -h, w, h))>
let WALK = [["TROOA1", 41, 57], ["TROOB1", 39, 56], ["TROOC1", 39, 60], ["TROOD1", 37, 57]]
let DEATH = [["TROOH1", 41, 55], ["TROOI0", 42, 62], ["TROOJ0", 41, 59], ["TROOK0", 40, 54], ["TROOL0", 48, 46], ["TROOM0", 58, 22]]
fn imp() => <g (shift(join([for (z in IMP_Z) n(EYE_X) ++ " " ++ n(wall_bottom(z) - 2)], ";") ++ ";" ++
            n(EYE_X) ++ " " ++ n(wall_bottom(IMP_Z[7]) - 2), "4s", {keyTimes:IMP_TIMES}));
    <g (grow(join([for (z in IMP_Z) n(140 / z / 57)], ";") ++ ";" ++ n(140 / IMP_Z[7] / 57), "4s", {keyTimes:IMP_TIMES}));
        // walking: the four frames in turn until the shot
        <g (anim("opacity", "1;0;1", "4s", {keyTimes:"0;.7;1", calcMode:"discrete"}));
            for (i, f in WALK) sprite_at(f[0], f[1], f[2], join([for (j in 0 to 3) if (j == i) "1" else "0"], ";") ++ ";0", "0.6s",
                "0;.25;.5;.75;1")>;
        // hit: pain, then the death frames, the body lying until the loop restarts
        for (i, f in DEATH) (let from = .7 + i * .03, let to = if (i == 5) .97 else from + .03,
            sprite_at(f[0], f[1], f[2], "0;1;0", "4s", "0;" ++ n(from) ++ ";" ++ n(to)))>>

// one red status-bar digit from DIGITS_SHEET ("0123456789%"), 14 wide
fn digit(glyph: any, x: any, y: any) => <svg x:x, y:y, width:14, height:16, viewBox:string(glyph * 14) ++ " 0 14 16",
    (doom_image("hud/DIGITS_SHEET", 0, 0, 168, 16))>
fn counter(caption: any, right: any, y: any) => <g
    for (i, ch in split(caption, "")) digit(if (ch == "%") 10 else int(ch), right - (len(caption) - i) * 14, y)>
fn status_bar() => <g transform:"translate(0 148)",
    (doom_image("hud/STBAR", 0, 0, 320, 32));
    (doom_image("hud/STARMS", 104, 0, 40, 32));
    // the shell count drops when the shotgun fires
    <g (anim("opacity", "1;0", "4s", {keyTimes:"0;.7", calcMode:"discrete"})); (counter("50", 44, 3))>;
    <g opacity:"0", (anim("opacity", "0;1", "4s", {keyTimes:"0;.7", calcMode:"discrete"})); (counter("49", 44, 3))>;
    (counter("100%", 104, 3));
    (counter("0%", 235, 3));
    // the face glances around, and grins at the kill
    (sprite_face("STFST01", "1;0;0;1;0;1", "0;.2;.35;.5;.7;.85"));
    (sprite_face("STFST00", "0;1;0;0;0;0", "0;.2;.35;.5;.7;.85"));
    (sprite_face("STFST02", "0;0;1;0;0;0", "0;.2;.35;.5;.7;.85"));
    (sprite_face("STFEVL0", "0;0;0;0;1;0", "0;.2;.35;.5;.7;.85"))>
fn sprite_face(name: any, visible: any, times: any) => <g opacity:"0",
    (anim("opacity", visible, "4s", {keyTimes:times, calcMode:"discrete"}));
    (doom_image("hud/" ++ name, 143, 1, 24, if (name == "STFEVL0") 30 else 29))>

fn doom(t) => canvas("doom", t, <g
    <defs <linearGradient id:"doom-dim-up", x1:0, y1:0, x2:0, y2:1, <stop offset:0, 'stop-color':"#000", 'stop-opacity':".2">;
            <stop offset:1, 'stop-color':"#000", 'stop-opacity':".75">>;
        <linearGradient id:"doom-dim-down", x1:0, y1:0, x2:0, y2:1, <stop offset:0, 'stop-color':"#000", 'stop-opacity':".75">;
            <stop offset:1, 'stop-color':"#000", 'stop-opacity':".2">>;
        <clipPath id:"doom-gun", <rect width:119, height:151>>>;
    // the ceiling and floor flats, tiled, dimmer toward the far end
    for (x in 0 to 6) for (y in 0 to 1) doom_image("flats/CEIL3_5", x * 48, EYE_Y - (y + 1) * 48, 48, 48);
    <rect width:320, height:EYE_Y, fill:"url(#doom-dim-up)">
    for (x in 0 to 6) for (y in 0 to 1) doom_image("flats/FLOOR4_8", x * 48, EYE_Y + y * 48, 48, 48);
    <rect y:EYE_Y, width:320, height:180 - EYE_Y, fill:"url(#doom-dim-down)">;
    // the door at the end of the corridor
    (doom_image("textures/BIGDOOR2", 120, wall_top(FAR), 80, wall_bottom(FAR) - wall_top(FAR)));
    <rect x:120, y:wall_top(FAR), width:80, height:wall_bottom(FAR) - wall_top(FAR), fill:"#000", opacity:".55">;
    (corridor());
    (imp());
    // the muzzle flash lights the corridor for a moment
    <rect width:320, height:148, fill:"#fff3c4", opacity:"0", (blink("4s", .7, .73))>;
    // the shotgun: idle, two firing frames, then the reload
    <g transform:"translate(124.3 57.4) scale(.6)", 'clip-path':"url(#doom-gun)",
        <image href:DM ++ "weapons/SHOTGUN.png", y:0, width:714, height:151, preserveAspectRatio:"none", 'image-rendering':"pixelated",
            (anim("x", "0;-119;-238;-357;-476;-595;0", "4s", {keyTimes:"0;.7;.73;.77;.82;.87;.92", calcMode:"discrete"}))>>;
    (status_bar())>)

// ---- worlds --------------------------------------------------------------

// blocks laid out on a sunflower spiral, so the city lights up ring by ring at night
let CITY = [for (i in 1 to 64) (let r = 3 + 10 * math.sqrt(i), let a = i * 2.39996,
    {x: 160 + r * math.cos(a), y: 90 + r * math.sin(a) * .62, s: 7 - i * .06, t: i / 64 * .7})]
fn cityscape(t) => canvas("cityscape", t, <g
    <defs <radialGradient id:"cityscape-glow",
        <stop offset:0, 'stop-color':t.base, 'stop-opacity':".45">;
        <stop offset:1, 'stop-color':t.base, 'stop-opacity':"0">>>;
    <ellipse cx:160, cy:92, rx:130, ry:80, fill:"url(#cityscape-glow)", (anim("opacity", ".6;1;.6", "6s"))>
    for (r in [26, 52, 78]) <ellipse cx:160, cy:90, rx:r, ry:r * .62, fill:"none", stroke:t.pale,
        'stroke-width':1, 'stroke-dasharray':"3 4">;
    <ellipse cx:160, cy:90, fill:"none", stroke:t.soft, 'stroke-width':1.5,
        (anim("rx", "4;96", "6s")); (anim("ry", "2.5;60", "6s")); (anim("opacity", ".8;0", "6s"))>
    for (b in CITY) <rect x:n(b.x - b.s / 2), y:n(b.y - b.s), width:n(b.s), height:n(b.s * 1.4), rx:.6, fill:t.deep,
        opacity:"0", (anim("opacity", "0;0;.95;.95;0", "6s", {keyTimes:"0;" ++ n(b.t) ++ ";" ++ n(b.t + .05) ++ ";.9;1"}))>;
    (text(22, 30, 7, SOFT, "CHENGDU", {'letter-spacing':2.5, 'font-weight':700}));
    (text(298, 164, 9, t.soft, "1250 BC → 2022", {'text-anchor':"end", 'letter-spacing':1}))>)

// ---- photo album ---------------------------------------------------------

// The album's library window, its grid cut from the two sample photographs
// in photo/assets (Unsplash License): each cell is a different crop, so the
// grid reads as many photos.
let PHOTOS = {coast: {href: "photo/assets/coast.jpg", h: 1064}, peaks: {href: "photo/assets/mountains.jpg", h: 1067}}
let CROPS = [["coast", "500 120 700 420"], ["peaks", "800 150 700 420"], ["coast", "700 560 800 480"],
    ["peaks", "0 560 700 500"], ["peaks", "300 380 900 500"], ["peaks", "0 0 1600 1067"], ["coast", "0 230 520 300"],
    ["peaks", "1000 250 600 380"], ["coast", "0 0 1600 1064"], ["coast", "900 50 700 380"]]
// rows of [crop, width]; every row starts at x 92 and runs off the window
let ALBUM_ROWS = [[[0, 74], [1, 60], [2, 52], [3, 64], [4, 70]],
    [[5, 58], [6, 66], [7, 50], [8, 72], [9, 60]],
    [[3, 52], [2, 70], [1, 62], [0, 56], [4, 60]],
    [[9, 60], [5, 70], [6, 64], [8, 56], [7, 60]]]
fn row_cells(row: any, y: any) => [for (i, c in row) {crop: CROPS[c[0]], w: c[1], y: y,
    x: 92 + sum([for (j in 0 to i) if (j < i) row[j][1] + 3 else 0])}]
let ALBUM = [for (r, row in ALBUM_ROWS) row_cells(row, 50 + r * 41)]
let PICKS = ALBUM[0] ++ ALBUM[1]
fn album_photo(c: any) => (let p = PHOTOS[c.crop[0]], <svg x:c.x, y:c.y, width:c.w, height:38, viewBox:c.crop[1],
    preserveAspectRatio:"xMidYMid slice", <image href:p.href, width:1600, height:p.h>>)
fn photo_album(t) => window("photo-album", t, <g
    <rect x:26, y:22, width:58, height:172, fill:"#f2f2f7">;
    <path d:"M84.5 22 V180", stroke:FAINT, 'stroke-width':"1">
    for (c in [[34, "#ff5f57"], [41, "#febc2e"], [48, "#28c840"]]) <circle cx:c[0], cy:30, r:2.4, fill:c[1]>;
    <rect x:30, y:43, width:50, height:10, rx:3, fill:"#e3e3e8">
    for (i in 0 to 5) <g
        <rect x:34, y:46 + i * 12, width:4, height:4, rx:1, fill:if (i == 0) "#0a84ff" else SOFT>;
        <rect x:41, y:47 + i * 12, width:30 - (i % 3) * 6, height:2.4, rx:1.2, fill:if (i == 0) INK else SOFT>>;
    (text(92, 38, 9, INK, "Library", {'font-weight':700}));
    (text(126, 38, 6, MID, "2026 · 1,000 photos"));
    for (row in ALBUM) for (c in row) album_photo(c);
    // the selection steps through the first two rows
    <rect width:0, height:0, fill:"none", stroke:"#0a84ff", 'stroke-width':2.2, rx:2,
        (anim("x", join([for (c in PICKS) n(c.x - 1)], ";"), "8s", {calcMode:"discrete"}));
        (anim("y", join([for (c in PICKS) n(c.y - 1)], ";"), "8s", {calcMode:"discrete"}));
        (anim("width", join([for (c in PICKS) n(c.w + 2)], ";"), "8s", {calcMode:"discrete"}));
        (anim("height", join([for (c in PICKS) "40"], ";"), "8s", {calcMode:"discrete"}))>>)

// ---- icons ---------------------------------------------------------------

// Strokes on a 24-unit grid: the app glyphs, then the sidebar's category glyphs.
let GLYPHS = {
    chart: "M4 20v-8 M9.3 20V6 M14.6 20v-9 M20 20V4",
    scene3d: "M12 2.5l8.5 4.8v9.4L12 21.5l-8.5-4.8V7.3z M12 12l8.5-4.7 M12 12v9.5 M12 12L3.5 7.3",
    cityscape: "M3 21h18 M5 21V11h4v10 M9 21V5h6v16 M15 21v-8h4v8 M11.5 9h1 M11.5 13h1 M11.5 17h1",
    'marble-drop': "M7 3h10l-4 5h-2z M12 8v2c0 3-6 3-6 7v3 M12 10c0 3 6 3 6 7v3",
    'sentence-game': "M4 4h16v12h-9l-5 4v-4H4z M8 8.5h8 M8 12h5",
    'photo-album': "M3 7h14v13H3z M7 3h14v13 M3 17l4-4 3 3 3-3 4 4",
    photo: "M4 6h9 M17 6h3 M4 12h3 M11 12h9 M4 18h11 M19 18h1 M15 4v4 M9 10v4 M17 16v4",
    photos: "M12 3l9 5-9 5-9-5z M3 12.5l9 5 9-5 M3 17l9 5 9-5",
    draw: "M4 4h6v6H4z M17 4a3 3 0 1 1 0 6 3 3 0 0 1 0-6 M10 7h4 M14 20h7l-3.5-6z M7 10v6.5h7",
    greetings: "M5 3h14v18H5z M12 16s-4-2.5-4-5.2a2 2 0 0 1 4-1.2 2 2 0 0 1 4 1.2C16 13.5 12 16 12 16z",
    word: "M4 5l3 14 5-11 5 11 3-14",
    note: "M6 3h12v18H6z M6 7h12 M9 11h6 M9 15h6",
    slides: "M3 4h18v12H3z M12 16v4 M8 20h8 M7 12l3-3 2 2 4-4",
    finder: "M3 6h6l2 2h10v11H3z M3 11h18",
    music: "M9 18V5l11-2v13 M9 18a3 3 0 1 1-6 0 3 3 0 0 1 6 0z M20 16a3 3 0 1 1-6 0 3 3 0 0 1 6 0z",
    soundflow: "M6 3v18 M11 5v14 M16 7v10 M20.5 9v6 M2.5 15c4-7 8 3 11-2.5s6-3.5 8-1.5",
    mail: "M3 6h18v12H3z M3 7l9 6 9-6",
    archive: "M3 4h18v5H3z M5 9v11h14V9 M10 13h4",
    book: "M3 5h6a3 3 0 0 1 3 3v12a2 2 0 0 0-2-2H3z M21 5h-6a3 3 0 0 0-3 3v12a2 2 0 0 1 2-2h7z",
    readability: "M5 6h14 M5 10h14 M5 14h14 M5 18h9",
    discover: "M12 3l2.6 5.6 6 .7-4.5 4.1 1.2 6L12 16.4l-5.3 3 1.2-6-4.5-4.1 6-.7z",
    games: "M6 8h12a4 4 0 0 1 4 4v1a4 4 0 0 1-7 2.6L14 14h-4l-1 1.6A4 4 0 0 1 2 13v-1a4 4 0 0 1 4-4z M7 10.5v3 M5.5 12h3 M16 11h.01 M18 13h.01",
    productivity: "M5 3h10l4 4v14H5z M14 3v5h5 M8.5 12.5h7 M8.5 16.5h5",
    photography: "M3 8h4l2-3h6l2 3h4v11H3z M12 10a3.5 3.5 0 1 0 0 7 3.5 3.5 0 0 0 0-7",
    design: "M12 3a9 9 0 1 0 0 18c1.5 0 2-1 2-2s-1-1.6-1-2.6 1-1.4 2-1.4h2a4 4 0 0 0 4-4c0-4.4-4-8-9-8z M7.5 11h.01 M10 7.5h.01 M14.5 7.5h.01",
    science: "M9 3h6 M10 3v6l-5 9a2 2 0 0 0 2 3h10a2 2 0 0 0 2-3l-5-9V3 M7.5 15h9",
    media: "M3 5h18v14H3z M10 9v6l5-3z",
    utilities: "M4 4h6v6H4z M14 4h6v6h-6z M4 14h6v6H4z M14 14h6v6h-6z"
}

// a line glyph that takes the text colour, for the sidebar and buttons
pub fn glyph(key) => <svg class:"line-glyph", viewBox:"0 0 24 24", 'aria-hidden':"true",
    <path d:GLYPHS[key], fill:"none", stroke:"currentColor", 'stroke-width':1.8, 'stroke-linecap':"round",
        'stroke-linejoin':"round">>

// each icon's hue, top to bottom, and the ink its glyph draws in
let HUES = {chart: ["#a5b4fc", "#4f46e5"], scene3d: ["#64748b", "#0f172a"], cityscape: ["#1e3a8a", "#0b1229", "#fcd34d"],
    tetris: ["#8b5cf6", "#3b0764"], 'marble-drop': ["#e8c27a", "#9a6a24"], superlambda: ["#bae6fd", "#38bdf8"],
    doom: ["#b91c1c", "#1f0707"], 'sentence-game': ["#fef08a", "#f59e0b", "#78350f"], 'photo-album': ["#fda4af", "#e11d48"],
    photo: ["#6ee7b7", "#047857"], photos: ["#67e8f9", "#0e7490"], draw: ["#fdba74", "#ea580c"],
    greetings: ["#f9a8d4", "#db2777"], word: ["#60a5fa", "#1e40af"], note: ["#fef9c3", "#facc15", "#854d0e"],
    slides: ["#fca5a5", "#b91c1c"], finder: ["#93c5fd", "#2563eb"], music: ["#fda4af", "#be123c"],
    soundflow: ["#3f4448", "#0b0b0c", "#5cb8ad"],
    mail: ["#7dd3fc", "#0369a1"], book: ["#fdba74", "#c2410c"], archive: ["#d6d3d1", "#78716c"], readability: ["#5eead4", "#0f766e"]}

// a rounded square in two shades of the app's hue, with a light inner edge
fn icon_frame(key: any, body: any) => <svg class:"icon-art", viewBox:"0 0 64 64", 'aria-hidden':"true",
    <defs (vgrad("icon-" ++ key, HUES[key][0], HUES[key][1]))>;
    <rect width:64, height:64, rx:15, fill:"url(#icon-" ++ key ++ ")">;
    body;
    <rect x:.75, y:.75, width:62.5, height:62.5, rx:14.3, fill:"none", stroke:PAPER, 'stroke-opacity':".25", 'stroke-width':"1.5">>

// three icons draw with the game's own art rather than a glyph
let ICON_ART = {
    tetris: (let at = [[14.5, 14, "#e9d5ff"], [26.5, 14, "#e9d5ff"], [38.5, 14, "#e9d5ff"], [26.5, 26, "#e9d5ff"],
            [14.5, 26, BRICK.amber], [14.5, 38, BRICK.rose], [26.5, 38, BRICK.rose], [38.5, 38, BRICK.cyan], [38.5, 26, BRICK.cyan]],
        <g for (c in at) <rect x:c[0], y:c[1], width:11, height:11, rx:2.2, fill:c[2]>>),
    doom: <image href:"doom/assets/hud/STFST01.png", x:14, y:10, width:36, height:43.5, preserveAspectRatio:"none",
        'image-rendering':"pixelated">,
    // the first running frame of the hero sheet
    superlambda: <svg x:13, y:6, width:38, height:51.3, viewBox:"0 0 40 54", <image href:"superlambda/assets/hero.svg", width:120, height:54>>
}

pub fn icon(key) => icon_frame(key, if (ICON_ART[key] != null) ICON_ART[key]
    else <path d:GLYPHS[key], transform:"translate(12 12) scale(1.6667)", fill:"none",
        stroke:if (len(HUES[key]) > 2) HUES[key][2] else PAPER, 'stroke-width':1.9, 'stroke-linecap':"round",
        'stroke-linejoin':"round">)

// ---- hero ----------------------------------------------------------------

// sampled positions around a tilted ellipse, for an orbit SMIL can follow,
// z runs from -1 on the far side of the orbit to 1 on the near side
fn orbit(cx: any, cy: any, rx: any, ry: any, tilt: any, steps: any) => [for (i in 0 to steps)
    (let a = i * 2 * math.pi / steps, let x = rx * math.cos(a), let y = ry * math.sin(a),
     {x: cx + x * math.cos(tilt) - y * math.sin(tilt), y: cy + x * math.sin(tilt) + y * math.cos(tilt), z: math.sin(a)})]

// A comet on an orbit: a soft halo, the head and five lagging copies for a
// tail, smaller and dimmer on the far side, so the flat orbit reads as depth.
let COMET = [[0, 13, .22], [0, 5.6, 1], [1, 4.8, .8], [2, 4, .62], [3, 3.2, .46], [4, 2.5, .32], [5, 1.8, .2]]
fn comet(path: any, color: any, dur: any) => <g
    for (c in COMET) (let lag = {begin: n(-c[0] * dur * .007) ++ "s"}, let size = c[1], let fade = c[2],
        let period = string(dur) ++ "s",
        <circle fill:color,
            (anim("cx", join([for (p in path) n(p.x)], ";"), period, lag));
            (anim("cy", join([for (p in path) n(p.y)], ";"), period, lag));
            (anim("r", join([for (p in path) n(size * (.8 + .3 * p.z))], ";"), period, lag));
            (anim("opacity", join([for (p in path) n(fade * (.55 + .45 * p.z))], ";"), period, lag))>)>

// four-pointed glints that blink in and out around the nucleus
let SPARKS = [[52, 70, 0], [270, 58, .8], [292, 214, 1.6], [40, 236, 2.4], [228, 290, 1.2], [98, 300, 2]]
fn spark(x: any, y: any, at: any) => <g transform:"translate(" ++ pt(x, y) ++ ")",
    <g transform:"scale(.01)", (grow(".01;1;.01;.01", "3.2s", {keyTimes:"0;.15;.3;1", begin:n(at) ++ "s"}));
        <path d:"M0 -7 L1.6 -1.6 L7 0 L1.6 1.6 L0 7 L-1.6 1.6 L-7 0 L-1.6 -1.6 Z", fill:"#fff">>>

// The hero mark is a λ atom: a glowing λ nucleus, light running along its
// strokes, heartbeat ripples, and three comets on orbits sixty degrees apart.
let LAMBDA = "M76 58 C96 52 108 62 116 82 L160 186 M124 104 L82 186"
let ATOM = [[0, "#fff", 4.8], [math.pi / 3, "#e0f2fe", 6.4], [-math.pi / 3, "#e0e7ff", 8]]
pub fn emblem() => <svg class:"emblem", viewBox:"0 0 320 320", 'aria-hidden':"true",
    <defs
        <linearGradient id:"emblem-ink", x1:0, y1:0, x2:1, y2:1,
            <stop offset:0, 'stop-color':"#fff">;
            <stop offset:.5, 'stop-color':"#fff">;
            <stop offset:1, 'stop-color':"#e0f2fe">>;
        <radialGradient id:"emblem-glow",
            <stop offset:0, 'stop-color':"#fff", 'stop-opacity':".45">;
            <stop offset:.6, 'stop-color':"#bae6fd", 'stop-opacity':".14">;
            <stop offset:1, 'stop-color':"#bae6fd", 'stop-opacity':"0">>>;
    <circle cx:160, cy:160, r:130, fill:"url(#emblem-glow)", (anim("opacity", ".5;1;.5", "4s"))>
    for (i in 0 to 2) <circle cx:160, cy:160, fill:"none", stroke:"#fff", 'stroke-width':1.5,
        (anim("r", "36;156", "3s", {begin:string(i) ++ "s"}));
        (anim("opacity", ".7;0", "3s", {begin:string(i) ++ "s"}))>;
    <circle cx:160, cy:160, r:154, fill:"none", stroke:"#fff", 'stroke-width':1.2, 'stroke-dasharray':"2 10",
        opacity:".45", (turn("0 160 160;360 160 160", "60s"))>;
    // the orbits precess slowly, comets and all
    <g (turn("0 160 160;360 160 160", "40s"));
        for (o in ATOM) <ellipse cx:160, cy:160, rx:138, ry:40, fill:"none", stroke:o[1], 'stroke-width':1,
            opacity:".3", transform:"rotate(" ++ n(o[0] * 180 / math.pi) ++ " 160 160)">>;
    <g transform:"translate(160 164) scale(1.2) translate(-118 -122)",
        <path d:LAMBDA, fill:"none", stroke:"url(#emblem-ink)", 'stroke-width':18,
            'stroke-linecap':"round", 'stroke-linejoin':"round">;
        <path d:LAMBDA, fill:"none", stroke:"#38bdf8", 'stroke-width':5, 'stroke-linecap':"round",
            'stroke-dasharray':"30 330", opacity:".9", (anim("stroke-dashoffset", "360;0", "2.4s"))>>;
    <g (turn("0 160 160;360 160 160", "40s"));
        for (o in ATOM) comet(orbit(160, 160, 138, 40, o[0], 64), o[1], o[2])>
    for (s in SPARKS) spark(s[0], s[1], s[2])>

// ---- catalog -------------------------------------------------------------

let LIVE = {tetris: tetris, superlambda: superlambda, doom: doom, cityscape: cityscape, 'photo-album': photo_album}

let TONE_OF = {tetris: "arcade", 'marble-drop': "amber", 'sentence-game': "amber", superlambda: "sky", doom: "clay",
    scene3d: "slate", cityscape: "night", 'photo-album': "rose", photo: "teal", photos: "cyan",
    draw: "clay", word: "blue", note: "gold", slides: "rose", chart: "indigo",
    finder: "blue", mail: "cyan", book: "amber", music: "violet", soundflow: "teal", archive: "slate", readability: "sage", greetings: "rose"}

pub fn of(key) => (let t = TONES[TONE_OF[key]], if (LIVE[key] != null) LIVE[key](t) else shot(key, t))
