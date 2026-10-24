// Animated tile art for the demo index: one 320×180 SMIL scene per demo.
// Every animation repeats forever, and every gradient/clip id carries the
// demo name, since all the scenes share one document. Each scene draws in
// slate inks plus one muted tone of its own (TONES): colour enough to tell
// the demos apart, quiet enough that the grid reads as one product.

fn n(x: any) => string(round(x * 10) / 10)
fn pt(x: any, y: any) => n(x) ++ " " ++ n(y)

// slate inks, darkest first, and the one accent in four strengths
let INK = "#1e293b"
let MID = "#64748b"
let SOFT = "#94a3b8"
let PALE = "#cbd5e1"
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
    sky: {deep: "#20322f", base: "#56b6dd", soft: "#56b6dd", pale: "#56b6dd", top: "#56b6dd", bottom: "#56b6dd"}
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
// a path that draws itself over [0, at] of the loop and stays drawn
fn draw_in(length: any, dur: any, at: any, begin: any = "0s") => anim("stroke-dashoffset",
    string(length) ++ ";0;0;" ++ string(length), dur, {keyTimes:"0;" ++ n(at) ++ ";.9;1", begin:begin})

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

// ---- games ---------------------------------------------------------------

fn cell(x: any, y: any, color: any) => <rect x:x, y:y, width:15, height:15, rx:2, fill:color>
let TETRIS_STACK = [[0,0,TONES.rose.soft],[1,0,TONES.rose.soft],[2,0,TONES.blue.soft],[3,0,TONES.blue.soft],
    [4,0,TONES.gold.soft],[5,0,TONES.gold.soft],[0,1,TONES.teal.soft],[1,1,TONES.teal.soft],[4,1,TONES.clay.soft],
    [5,1,TONES.clay.soft],[5,2,TONES.clay.soft],[0,2,TONES.teal.soft]]
fn tetris(t) => canvas("tetris", t, <g
    <rect x:112, y:0, width:96, height:180, fill:PAPER>;
    <path d:"M112 0 V180 M208 0 V180", stroke:FAINT, 'stroke-width':"1">
    for (c in TETRIS_STACK) cell(113 + c[0] * 16, 165 - c[1] * 16, c[2]);
    // the bottom row flashes as it clears
    <rect x:112, y:164, width:96, height:16, fill:t.pale, opacity:"0", (blink("2.4s", .82, .9))>;
    // a T piece falls one row per tick
    <g (shift("0 -28;0 -12;0 4;0 20;0 36;0 52;0 68;0 84;0 100", "2.4s", {calcMode:"discrete"}));
        (cell(145, 0, t.base)); (cell(161, 0, t.base)); (cell(177, 0, t.base)); (cell(161, 16, t.base))>;
    (text(28, 40, 9, MID, "SCORE", {'letter-spacing':2}));
    (text(28, 62, 18, INK, "12,480", {'font-weight':700}));
    (text(28, 100, 9, MID, "LINES", {'letter-spacing':2}));
    (text(28, 120, 18, INK, "37", {'font-weight':700}));
    (text(232, 40, 9, MID, "NEXT", {'letter-spacing':2}));
    <g (shift("0 0;0 -4;0 0", "1.2s"));
        (cell(236, 56, t.soft)); (cell(252, 56, t.soft)); (cell(268, 56, t.soft)); (cell(268, 72, t.soft))>>)

fn word_card(t: any, x: any, w: any, caption: any, from: any, begin: any) => <g (shift(from ++ ";0 0;0 0;" ++ from, "4.5s",
        {keyTimes:"0;.3;.85;1", calcMode:"spline", keySplines:".3 .9 .4 1;0 0 1 1;.6 0 .8 .4", begin:begin}));
    <rect x:x, y:96, width:w, height:36, rx:8, fill:PAPER, stroke:t.soft, 'stroke-width':"1.5">;
    (text(x + w / 2, 119, 16, t.deep, caption, {'text-anchor':"middle", 'font-weight':600}))>
fn sentence_game(t) => canvas("sentence-game", t, <g
    <path d:"M40 146 H280", stroke:SOFT, 'stroke-width':1.5, 'stroke-dasharray':"5 6">;
    (word_card(t, 46, 62, "The", "-30 -70", "0s"));
    (word_card(t, 116, 62, "fox", "60 -80", "0s"));
    (word_card(t, 186, 92, "jumps.", "40 -60", "0s"));
    // the finished sentence earns a check
    <g transform:"translate(278 52)", opacity:"0", (blink("4.5s", .32, .82));
        <circle r:13, fill:t.base>;
        <path d:"M-6 0 l4 4.5 l8 -9", fill:"none", stroke:PAPER, 'stroke-width':2.4, 'stroke-linecap':"round", 'stroke-linejoin':"round">>;
    (text(40, 44, 11, MID, "Build the sentence", {'letter-spacing':1}))>)

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

// sampled positions around a tilted ellipse, for an orbit SMIL can follow,
// z runs from -1 on the far side of the orbit to 1 on the near side
fn orbit(cx: any, cy: any, rx: any, ry: any, tilt: any, steps: any) => [for (i in 0 to steps)
    (let a = i * 2 * math.pi / steps, let x = rx * math.cos(a), let y = ry * math.sin(a),
     {x: cx + x * math.cos(tilt) - y * math.sin(tilt), y: cy + x * math.sin(tilt) + y * math.cos(tilt), z: math.sin(a)})]
let MOON = orbit(160, 92, 118, 30, -0.24, 32)
fn scene3d(t) => canvas("scene3d", t, <g
    <defs <radialGradient id:"scene3d-planet", cx:.35, cy:.3, r:.8,
        <stop offset:0, 'stop-color':t.pale>;
        <stop offset:.5, 'stop-color':t.base>;
        <stop offset:1, 'stop-color':INK>>>
    for (s in [[24,22,2.4],[70,140,1.6],[120,30,1.2],[250,24,1.8],[290,120,1.4],[210,160,1.2],[40,90,1]])
        <circle cx:s[0], cy:s[1], r:s[2], fill:SOFT, (anim("opacity", ".2;1;.2", string(1.5 + s[2]) ++ "s"))>;
    <g transform:"rotate(-14 160 92)",
        <path d:"M88 92 A72 16 0 0 1 232 92", fill:"none", stroke:TONES.violet.soft, 'stroke-width':4, opacity:".6">;
        <path d:"M76 92 A84 20 0 0 1 244 92", fill:"none", stroke:SOFT, 'stroke-width':1.2, opacity:".4">>;
    <circle cx:160, cy:92, r:38, fill:"url(#scene3d-planet)">;
    <g transform:"rotate(-14 160 92)",
        <path d:"M88 92 A72 16 0 0 0 232 92", fill:"none", stroke:TONES.violet.base, 'stroke-width':4, opacity:".8">;
        <path d:"M76 92 A84 20 0 0 0 244 92", fill:"none", stroke:SOFT, 'stroke-width':1.2, opacity:".7">>;
    <circle r:7, fill:MID,
        (anim("cx", join([for (p in MOON) n(p.x)], ";"), "9s"));
        (anim("cy", join([for (p in MOON) n(p.y)], ";"), "9s"))>>)

// blocks laid out on a sunflower spiral, so the city fills in ring by ring
let CITY = [for (i in 1 to 64) (let r = 3 + 10 * math.sqrt(i), let a = i * 2.39996,
    {x: 160 + r * math.cos(a), y: 90 + r * math.sin(a) * .62, s: 7 - i * .06, t: i / 64 * .7})]
fn cityscape(t) => canvas("cityscape", t, <g
    for (r in [26, 52, 78]) <ellipse cx:160, cy:90, rx:r, ry:r * .62, fill:"none", stroke:SOFT,
        'stroke-width':1, 'stroke-dasharray':"3 4", opacity:".6">;
    <ellipse cx:160, cy:90, fill:"none", stroke:t.base, 'stroke-width':1.5,
        (anim("rx", "4;96", "6s")); (anim("ry", "2.5;60", "6s")); (anim("opacity", ".7;0", "6s"))>
    for (b in CITY) <rect x:n(b.x - b.s / 2), y:n(b.y - b.s), width:n(b.s), height:n(b.s * 1.4), fill:INK,
        opacity:"0", (anim("opacity", "0;0;.85;.85;0", "6s", {keyTimes:"0;" ++ n(b.t) ++ ";" ++ n(b.t + .05) ++ ";.9;1"}))>;
    (text(300, 166, 10, MID, "1250 BC → 2022", {'text-anchor':"end", 'letter-spacing':1}))>)

// ---- pictures ------------------------------------------------------------

fn photo_tile(x: any, y: any, w: any, h: any, grad: any) => <g
    <rect x:x, y:y, width:w, height:h, rx:4, fill:"url(#" ++ grad ++ ")">;
    <path d:"M" ++ pt(x, y + h) ++ " L" ++ pt(x + w * .35, y + h * .45) ++ " L" ++ pt(x + w * .6, y + h * .75) ++
        " L" ++ pt(x + w * .8, y + h * .5) ++ " L" ++ pt(x + w, y + h) ++ " Z", fill:INK, opacity:".2">>
let ALBUM = [[16,16,78,48,"pa-a"],[100,16,120,48,"pa-b"],[226,16,78,48,"pa-c"],
    [16,70,124,48,"pa-d"],[146,70,64,48,"pa-a"],[216,70,88,48,"pa-b"],
    [16,124,70,44,"pa-c"],[92,124,96,44,"pa-d"],[194,124,110,44,"pa-a"]]
fn photo_album(t) => canvas("photo-album", t, <g
    <defs (vgrad("pa-a", TONES.rose.soft, TONES.amber.soft)); (vgrad("pa-b", TONES.blue.soft, TONES.blue.base));
        (vgrad("pa-c", TONES.sage.soft, TONES.sage.base)); (vgrad("pa-d", TONES.violet.soft, TONES.rose.base))>
    for (t in ALBUM) photo_tile(t[0], t[1], t[2], t[3], t[4]);
    // the selection frame steps through the timeline
    <rect width:0, height:0, fill:"none", stroke:t.deep, 'stroke-width':2.5, rx:5,
        (anim("x", join([for (t in ALBUM) n(t[0] - 2)], ";"), "6.3s", {calcMode:"discrete"}));
        (anim("y", join([for (t in ALBUM) n(t[1] - 2)], ";"), "6.3s", {calcMode:"discrete"}));
        (anim("width", join([for (t in ALBUM) n(t[2] + 4)], ";"), "6.3s", {calcMode:"discrete"}));
        (anim("height", join([for (t in ALBUM) n(t[3] + 4)], ";"), "6.3s", {calcMode:"discrete"}))>;
    <g transform:"translate(288 150)",
        <g (grow("1;1.3;1", "1.2s"));
            <path d:"M0 8 C-12 0 -10 -10 -4 -10 C-2 -10 0 -8 0 -6 C0 -8 2 -10 4 -10 C10 -10 12 0 0 8 Z", fill:PAPER>>>>)

fn photo(t) => canvas("photo", t, <g
    <defs (vgrad("photo-sky", t.soft, TONES.rose.pale)); (vgrad("photo-sea", TONES.cyan.soft, TONES.cyan.deep))>;
    <rect x:30, y:16, width:260, height:134, rx:6, fill:"url(#photo-sky)">;
    <circle cx:200, cy:66, r:20, fill:PAPER>;
    <path d:"M30 110 L90 62 L130 92 L170 56 L230 104 L290 80 L290 116 L30 116 Z", fill:t.deep, opacity:".7">;
    <rect x:30, y:112, width:260, height:38, fill:"url(#photo-sea)">;
    // the graded half follows the comparison handle
    <rect x:30, y:16, height:134, fill:TONES.blue.deep, opacity:".35", (anim("width", "40;220;40", "5s"))>;
    <g (shift("0 0;180 0;0 0", "5s"));
        <path d:"M70 16 V150", stroke:PAPER, 'stroke-width':"2">;
        <circle cx:70, cy:83, r:9, fill:PAPER>;
        <path d:"M66 83 h8 M70 79 v8", stroke:INK, 'stroke-width':"1.5">>;
    <path d:"M60 166 H260", stroke:PALE, 'stroke-width':3, 'stroke-linecap':"round">;
    <circle cy:166, r:6, fill:t.base, (anim("cx", "90;230;90", "5s"))>>)

fn layer(t: any, color: any, opacity: any, offset: any) => <g (shift("0 0;0 " ++ string(offset) ++ ";0 0", "3.6s",
        {calcMode:"spline", keyTimes:"0;.5;1", keySplines:".4 0 .2 1;.4 0 .2 1"}));
    <polygon points:"0,-22 80,0 0,22 -80,0", fill:color, opacity:opacity, stroke:t.deep, 'stroke-width':"1">>
fn photos(t) => canvas("photos", t, <g
    <g transform:"translate(116 112)",
        (layer(t, SOFT, "1", 18));
        <g transform:"translate(0 -26)", (layer(t, t.soft, ".8", 0))>;
        <g transform:"translate(0 -52)", (layer(t, t.pale, ".7", -18))>>;
    <rect x:222, y:22, width:84, height:136, rx:6, fill:PAPER, stroke:FAINT, 'stroke-width':"1">;
    <rect x:226, width:76, height:26, rx:4, fill:t.pale,
        (anim("y", "30;62;94;30", "3.6s", {calcMode:"discrete"}))>
    for (i in 0 to 2) <g
        <circle cx:240, cy:43 + i * 32, r:4, fill:"none", stroke:MID, 'stroke-width':"1.5">;
        <rect x:252, y:40 + i * 32, width:40 - i * 6, height:6, rx:3, fill:SOFT>>>)

// ---- productivity -------------------------------------------------------

fn draw(t) => canvas("draw", t, <g
    for (x in 0 to 15) for (y in 0 to 8) <circle cx:10 + x * 20, cy:10 + y * 20, r:1, fill:PALE>;
    <path d:"M100 50 H136", fill:"none", stroke:MID, 'stroke-width':1.5, 'stroke-dasharray':"40", (draw_in(40, "5s", .2))>;
    <path d:"M184 50 H222", fill:"none", stroke:MID, 'stroke-width':1.5, 'stroke-dasharray':"40", (draw_in(40, "5s", .35))>;
    <path d:"M160 74 V118 H222", fill:"none", stroke:MID, 'stroke-width':1.5, 'stroke-dasharray':"110", (draw_in(110, "5s", .5))>;
    <rect x:28, y:34, width:72, height:32, rx:8, fill:PAPER, stroke:SOFT, 'stroke-width':"1.5">;
    (text(64, 54, 11, INK, "Start", {'text-anchor':"middle"}));
    <polygon points:"160,26 184,50 160,74 136,50", fill:t.pale, stroke:t.base, 'stroke-width':"1.5">;
    (text(160, 54, 11, t.deep, "?", {'text-anchor':"middle"}));
    <rect x:222, y:34, width:72, height:32, rx:4, fill:PAPER, stroke:SOFT, 'stroke-width':"1.5">;
    (text(258, 54, 11, INK, "Render", {'text-anchor':"middle"}));
    // the last box is being dragged into place
    <g (shift("-60 30;0 0;0 0;-60 30", "5s", {keyTimes:"0;.6;.9;1"}));
        <rect x:222, y:102, width:72, height:32, rx:4, fill:PAPER, stroke:t.base, 'stroke-width':"1.5">;
        (text(258, 122, 11, INK, "Export", {'text-anchor':"middle"}));
        <path d:"M270 124 l0 16 l4 -4 l3 7 l3 -1 l-3 -7 l6 0 Z", fill:PAPER, stroke:INK, 'stroke-width':"1">>>)

let WORD_LINES = [[46, 120, 6], [62, 132, 4], [74, 118, 4], [86, 128, 4], [104, 96, 4], [116, 124, 4], [128, 70, 4]]
fn word(t) => canvas("word", t, <g
    <rect x:0, y:0, width:320, height:22, fill:t.deep>
    for (i in 0 to 4) <rect x:12 + i * 22, y:7, width:14, height:8, rx:2, fill:t.soft, opacity:".7">;
    <rect x:84, y:30, width:152, height:170, fill:PAPER, stroke:FAINT, 'stroke-width':"1">
    // lines type themselves in turn; the caret rides the line being written
    for (i in 0 to 6) (let l = WORD_LINES[i], let from = .08 + i * .11,
        <rect x:100, y:l[0] - l[2], height:l[2], rx:1, fill:if (i == 0) INK else PALE,
            (anim("width", "0;0;" ++ string(l[1]) ++ ";" ++ string(l[1]) ++ ";0", "6s",
                {keyTimes:"0;" ++ n(from) ++ ";" ++ n(from + .1) ++ ";.92;1"}))>);
    <rect width:1.5, height:9, fill:t.base,
        (anim("x", join([for (l in WORD_LINES) "100;" ++ string(100 + l[1])], ";") ++ ";100", "6s",
            {keyTimes: "0;" ++ join([for (i in 0 to 6) n(.08 + i * .11) ++ ";" ++ n(.18 + i * .11)], ";") ++ ";1"}));
        (anim("y", join([for (l in WORD_LINES) string(l[0] - 7) ++ ";" ++ string(l[0] - 7)], ";") ++ ";39", "6s",
            {keyTimes: "0;" ++ join([for (i in 0 to 6) n(.08 + i * .11) ++ ";" ++ n(.18 + i * .11)], ";") ++ ";1"}));
        (anim("opacity", "1;0", "0.8s", {calcMode:"discrete"}))>;
    <g transform:"translate(28 60)",
        <rect width:40, height:70, rx:6, fill:PAPER, stroke:FAINT, 'stroke-width':"1">;
        (text(20, 20, 13, INK, "B", {'text-anchor':"middle", 'font-weight':800}));
        (text(20, 40, 13, INK, "I", {'text-anchor':"middle", 'font-style':"italic", 'font-family':"Georgia, serif"}));
        (text(20, 60, 13, INK, "U", {'text-anchor':"middle", 'text-decoration':"underline"}))>>)

fn note(t) => canvas("note", t, <g
    <rect x:24, y:16, width:70, height:150, rx:6, fill:t.pale>
    for (i in 0 to 3) <rect x:32, y:28 + i * 18, width:50 - i * 6, height:6, rx:3, fill:t.soft>;
    <rect x:104, y:16, width:196, height:150, rx:6, fill:PAPER, stroke:FAINT, 'stroke-width':"1">;
    (text(118, 42, 14, INK, "Weekend", {'font-weight':700}))
    for (i in 0 to 3) (let y = 60 + i * 24, let at = .12 + i * .16, <g
        <rect x:118, y:y, width:14, height:14, rx:3, fill:"none", stroke:SOFT, 'stroke-width':"1.5">;
        <path d:"M121 " ++ string(y + 7) ++ " l3 4 l6 -8", fill:"none", stroke:t.base, 'stroke-width':2.2,
            'stroke-linecap':"round", 'stroke-dasharray':"16", (draw_in(16, "5s", at))>;
        <rect x:140, y:y + 4, width:120 - i * 14, height:6, rx:3, fill:PALE>;
        <rect x:138, y:y + 6, height:1.6, fill:MID,
            (anim("width", "0;0;" ++ string(124 - i * 14) ++ ";" ++ string(124 - i * 14) ++ ";0", "5s",
                {keyTimes:"0;" ++ n(at) ++ ";" ++ n(at + .06) ++ ";.9;1"}))>>)>)

fn slide(t: any, x: any, kind: any) => <g transform:"translate(" ++ string(x) ++ " 0)",
    <rect x:40, y:22, width:240, height:135, fill:PAPER>;
    <rect x:40, y:22, width:240, height:22, fill:INK>;
    <rect x:52, y:30, width:if (kind == 0) 80 else 64, height:6, rx:3, fill:SOFT>
    if (kind == 0) <g
        for (i in 0 to 5) <rect x:64 + i * 32, y:140 - (20 + i * 14), width:20, height:20 + i * 14, rx:2,
            fill:if (i == 5) t.base else PALE>>
    else <g
        <circle cx:110, cy:98, r:34, fill:"none", stroke:FAINT, 'stroke-width':"14">;
        <circle cx:110, cy:98, r:34, fill:"none", stroke:t.base, 'stroke-width':14,
            'stroke-dasharray':"150 214", transform:"rotate(-90 110 98)">
        for (i in 0 to 2) <rect x:168, y:74 + i * 18, width:84 - i * 18, height:7, rx:3, fill:SOFT>>>
fn slides(t) => canvas("slides", t, <g
    <defs <clipPath id:"slides-frame", <rect x:40, y:22, width:240, height:"135">>>;
    <rect x:40, y:22, width:240, height:135, fill:"none", stroke:FAINT, 'stroke-width':"2">;
    <g 'clip-path':"url(#slides-frame)",
        <g (shift("0 0;0 0;-240 0;-240 0;-480 0", "6s",
                {keyTimes:"0;.4;.5;.9;1", calcMode:"spline", keySplines:"0 0 1 1;.5 0 .2 1;0 0 1 1;.5 0 .2 1"}));
            (slide(t, 0, 0)); (slide(t, 240, 1)); (slide(t, 480, 0))>>
    for (i in 0 to 2) <circle cx:148 + i * 12, cy:168, r:3, fill:PALE>;
    <circle cy:168, r:3, fill:MID, (anim("cx", "148;148;160;160;148", "6s", {keyTimes:"0;.45;.5;.95;1", calcMode:"discrete"}))>>)

let BARS = [40, 72, 56, 94, 80, 112]
fn chart(t) => canvas("chart", t, <g
    <defs (vgrad("chart-bar", t.soft, t.base))>
    for (y in [40, 70, 100, 130]) <path d:"M40 " ++ string(y) ++ " H292", stroke:FAINT, 'stroke-width':"1">;
    <path d:"M40 156 H292", stroke:SOFT, 'stroke-width':"1.5">
    for (i in 0 to 5) (let h = BARS[i], let b = string(i * .12) ++ "s",
        <rect x:54 + i * 40, width:24, rx:3, fill:"url(#chart-bar)",
            (anim("height", "0;" ++ string(h) ++ ";" ++ string(h) ++ ";0", "5s", {keyTimes:"0;.3;.85;1", begin:b}));
            (anim("y", "156;" ++ string(156 - h) ++ ";" ++ string(156 - h) ++ ";156", "5s", {keyTimes:"0;.3;.85;1", begin:b}))>);
    <path d:"M66 120 L106 96 L146 108 L186 70 L226 80 L266 44", fill:"none", stroke:TONES.rose.base, 'stroke-width':2,
        'stroke-linejoin':"round", 'stroke-dasharray':"260", (draw_in(260, "5s", .45))>;
    <circle cx:266, cy:44, r:4, fill:TONES.rose.base, (blink("5s", .45, .88))>>)

// ---- tools ---------------------------------------------------------------

fn folder(t: any, x: any, y: any) => <path d:"M" ++ pt(x, y + 4) ++ " h12 l4 4 h20 v22 h-36 Z", fill:t.soft>
fn file_icon(x: any, y: any) => <g
    <path d:"M" ++ pt(x + 4, y) ++ " h20 l8 8 v26 h-28 Z", fill:FAINT, stroke:PALE, 'stroke-width':"1">;
    <path d:"M" ++ pt(x + 9, y + 16) ++ " h14 M" ++ pt(x + 9, y + 22) ++ " h14", stroke:SOFT, 'stroke-width':"2">>
let FINDER_SPOTS = [[110, 46], [160, 46], [210, 46], [260, 46], [110, 104], [160, 104], [210, 104]]
fn finder(t) => canvas("finder", t, <g
    <rect x:20, y:14, width:280, height:156, rx:8, fill:PAPER, stroke:FAINT, 'stroke-width':"1">
    for (c in [[34, "#e39a90"], [46, "#e6c47d"], [58, "#94c28e"]]) <circle cx:c[0], cy:24, r:4, fill:c[1]>;
    <rect x:20, y:34, width:68, height:136, fill:"#f1f5f9">
    for (i in 0 to 4) <rect x:30, y:46 + i * 20, width:48 - (i % 2) * 12, height:6, rx:3, fill:PALE>;
    <rect width:44, height:50, rx:6, fill:t.base, opacity:".14",
        (anim("x", join([for (s in FINDER_SPOTS) string(s[0] - 4)], ";"), "4.9s", {calcMode:"discrete"}));
        (anim("y", join([for (s in FINDER_SPOTS) string(s[1] - 6)], ";"), "4.9s", {calcMode:"discrete"}))>
    for (i in 0 to 6) (let s = FINDER_SPOTS[i], if (i % 3 == 1) file_icon(s[0], s[1]) else folder(t, s[0], s[1]))
    for (i in 0 to 6) <rect x:FINDER_SPOTS[i][0] + 4, y:FINDER_SPOTS[i][1] + 40, width:28, height:4, rx:2, fill:PALE>>)

// a short melody on one staff; the playhead lights each note and its syllable in turn
let TUNE = [[86, 106, "Twin"], [110, 106, "kle"], [134, 86, "twin"], [158, 86, "kle"], [182, 78, "lit"], [206, 78, "tle"],
    [238, 86, "star"]]
fn music_note(x: any, y: any, color: any) => <g
    <ellipse cx:x, cy:y, rx:6, ry:4.4, transform:"rotate(-20 " ++ string(x) ++ " " ++ string(y) ++ ")", fill:color>;
    <path d:"M" ++ pt(x + 5.4, y - 1) ++ " V" ++ n(y - 30), stroke:color, 'stroke-width':"1.6">>
fn music(t) => canvas("music", t, <g
    <rect x:36, y:22, width:248, height:136, rx:6, fill:PAPER, stroke:FAINT, 'stroke-width':"1">
    for (i in 0 to 4) <path d:"M52 " ++ string(70 + i * 8) ++ " H268", stroke:SOFT, 'stroke-width':"1">;
    <path d:"M52 70 V102 M268 70 V102", stroke:MID, 'stroke-width':"1.5">;
    (text(60, 86, 14, INK, "4", {'font-weight':800})); (text(60, 101, 14, INK, "4", {'font-weight':800}))
    for (k in TUNE) music_note(k[0], k[1], INK)
    for (k in TUNE) text(k[0] + 3, 134, 9, MID, k[2], {'text-anchor':"middle"})
    // the sounding note and syllable, one per beat
    for (i, k in TUNE) <g opacity:"0", (anim("opacity", join([for (j in 0 to len(TUNE) - 1) if (j == i) "1" else "0"], ";") ++ ";0",
            "4.2s", {keyTimes:"0;" ++ join([for (j in 1 to len(TUNE)) n(j / (len(TUNE) + 1))], ";"), calcMode:"discrete"}));
        (music_note(k[0], k[1], t.base));
        (text(k[0] + 3, 134, 9, t.deep, k[2], {'text-anchor':"middle", 'font-weight':700}))>;
    <rect x:52, y:146, width:216, height:3, rx:1.5, fill:FAINT>;
    <rect x:52, y:146, height:3, rx:1.5, fill:t.base, (anim("width", "0;216", "4.2s"))>>)

fn mail_row(t: any, y: any, unread: any) => <g
    <circle cx:126, cy:y + 6, r:3.5, fill:if (unread) t.base else "none">;
    <rect x:136, y:y, width:90, height:6, rx:3, fill:INK, opacity:if (unread) "0.85" else "0.4">;
    <rect x:136, y:y + 10, width:140, height:5, rx:2.5, fill:PALE>>
fn mail(t) => canvas("mail", t, <g
    <rect x:110, y:20, width:190, height:144, rx:8, fill:PAPER, stroke:FAINT, 'stroke-width':"1">;
    <rect x:110, y:20, width:190, height:22, rx:8, fill:"#f1f5f9">;
    // the new message lands on top of the list
    <g (anim("opacity", "0;0;1;1;0", "4s", {keyTimes:"0;.45;.55;.9;1"})); (mail_row(t, 54, true))>;
    (mail_row(t, 84, true)); (mail_row(t, 112, false)); (mail_row(t, 140, false));
    <g (shift("-90 40;40 -10;40 -10;-90 40", "4s", {keyTimes:"0;.5;.95;1"}));
        (anim("opacity", "0;1;1;0;0", "4s", {keyTimes:"0;.15;.45;.55;1"}));
        <rect x:30, y:60, width:56, height:38, rx:4, fill:PAPER, stroke:t.base, 'stroke-width':"1.5">;
        <path d:"M30 62 L58 84 L86 62", fill:"none", stroke:t.base, 'stroke-width':"2">>>)

// a folded card swings open on its hinge (x = 160) and closes again; the cover's
// front shows until it is edge-on, then its inside face takes over
fn card_face(t: any, inside: any) => if (inside) <g
        <rect x:160, y:30, width:92, height:124, rx:2, fill:"#fffaf3">;
        <path d:"M206 86 C200 78 190 80 190 88 C190 96 206 104 206 104 C206 104 222 96 222 88 C222 80 212 78 206 86 Z", fill:t.soft>>
    else <g
        <rect x:160, y:30, width:92, height:124, rx:2, fill:t.pale, stroke:t.soft, 'stroke-width':"1">;
        <path d:"M206 70 C198 58 182 62 182 74 C182 88 206 100 206 100 C206 100 230 88 230 74 C230 62 214 58 206 70 Z", fill:t.base>;
        (text(206, 126, 13, t.deep, "Hello!", {'text-anchor':"middle", 'font-family':"'Snell Roundhand', cursive", 'font-weight':700}))>
fn greetings(t) => canvas("greetings", t, <g
    for (c in [[34, 40, 0], [286, 36, 1.2], [60, 150, 2.1], [270, 146, .6]]) <path d:"M0 -5 L1.4 -1.4 L5 0 L1.4 1.4 L0 5 L-1.4 1.4 L-5 0 L-1.4 -1.4 Z",
        fill:t.soft, transform:"translate(" ++ pt(c[0], c[1]) ++ ")", (anim("opacity", "0;1;0", "2.6s", {begin:string(c[2]) ++ "s"}))>;
    <ellipse cx:160, cy:160, rx:110, ry:6, fill:INK, opacity:".08">
    // the closed card sits centred, and slides right as it opens so the spread is centred
    <g (shift("-46 0;-46 0;0 0;0 0;-46 0", "5s", {keyTimes:"0;.3;.5;.85;1"}));
    // the inside right page: a verse and a signature
    <rect x:160, y:30, width:92, height:124, rx:2, fill:PAPER, stroke:FAINT, 'stroke-width':"1">;
    for (i in 0 to 3) <rect x:172, y:56 + i * 12, width:if (i == 3) 44 else 68, height:4, rx:2, fill:PALE>;
    <path d:"M176 122 C182 112 188 128 194 118 S206 114 212 122", fill:"none", stroke:t.base, 'stroke-width':"1.6", 'stroke-linecap':"round">;
    <g transform:"translate(160 0)",
        <g (grow("1 1;1 1;-1 1;-1 1;1 1", "5s", {keyTimes:"0;.3;.5;.85;1"}));
            <g transform:"translate(-160 0)",
                <g (anim("opacity", "1;0;1", "5s", {keyTimes:"0;.4;.925", calcMode:"discrete"})); (card_face(t, false))>
                <g opacity:"0", (anim("opacity", "0;1;0", "5s", {keyTimes:"0;.4;.925", calcMode:"discrete"})); (card_face(t, true))>>>>>>)

fn archive(t) => canvas("archive", t, <g
    <circle cx:58, cy:80, r:30, fill:"none", stroke:SOFT, 'stroke-width':"1.5">;
    <ellipse cx:58, cy:80, rx:13, ry:30, fill:"none", stroke:SOFT, 'stroke-width':"1.2">;
    <path d:"M28 80 H88 M33 64 H83 M33 96 H83", stroke:SOFT, 'stroke-width':"1.2">
    // pages fly from the web into the box
    for (i in 0 to 2) <g (shift("0 0;90 -40;176 10", "2.7s", {begin:string(i * .9) ++ "s"}));
        (anim("opacity", "0;1;1;0", "2.7s", {keyTimes:"0;.15;.8;1", begin:string(i * .9) ++ "s"}));
        <rect x:58, y:66, width:22, height:28, rx:2, fill:PAPER, stroke:PALE, 'stroke-width':"1">;
        <path d:"M62 74 h14 M62 80 h14 M62 86 h9", stroke:SOFT, 'stroke-width':"1.5">>;
    // the box: front, open top and right side, seen from above and to the left
    <path d:"M202 96 H274 V146 H202 Z", fill:SOFT>;
    <path d:"M202 96 L218 80 H290 L274 96 Z", fill:PALE>;
    <path d:"M274 96 L290 80 V130 L274 146 Z", fill:MID>;
    <rect x:224, y:110, width:28, height:8, rx:2, fill:FAINT>;
    <rect x:40, y:158, width:240, height:6, rx:3, fill:FAINT>;
    <rect x:40, y:158, height:6, rx:3, fill:t.base, (anim("width", "0;240;240", "2.7s", {keyTimes:"0;.9;1"}))>>)

fn readability(t) => canvas("readability", t, <g
    <rect x:28, y:14, width:264, height:156, rx:8, fill:FAINT>;
    <rect x:28, y:14, width:264, height:20, rx:8, fill:PALE>;
    <rect x:60, y:20, width:180, height:8, rx:4, fill:"#f8fafc">;
    <path d:"M266 19 h7 l3 3 v8 h-10 Z", fill:"none", stroke:t.base, 'stroke-width':1.5,
        (anim("stroke-width", "1.5;1.5;3;1.5;1.5", "6s", {keyTimes:"0;.38;.45;.52;1"}))>;
    // the cluttered page …
    <g (anim("opacity", "1;1;0;0;1", "6s", {keyTimes:"0;.4;.5;.9;1"}));
        <rect x:36, y:40, width:248, height:22, fill:TONES.clay.base>;
        (text(160, 55, 10, PAPER, "SALE! 50% OFF — CLICK NOW", {'text-anchor':"middle", 'font-weight':700}));
        <rect x:36, y:68, width:60, height:96, fill:TONES.violet.soft>;
        <rect x:224, y:68, width:60, height:46, fill:TONES.rose.soft>;
        <rect x:224, y:118, width:60, height:46, fill:TONES.cyan.soft>
        for (i in 0 to 6) <rect x:104, y:72 + i * 13, width:112 - (i % 3) * 18, height:5, rx:2, fill:SOFT>>;
    // … becomes one calm column
    <g (anim("opacity", "0;0;1;1;0", "6s", {keyTimes:"0;.4;.5;.9;1"}));
        <rect x:36, y:40, width:248, height:126, fill:PAPER>;
        (text(92, 62, 14, INK, "The Article", {'font-weight':700, 'font-family':"Georgia, serif"}))
        for (i in 0 to 6) <rect x:92, y:74 + i * 12, width:136 - (i % 3) * 14, height:5, rx:2, fill:PALE>>>)

// ---- hero ----------------------------------------------------------------

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

let ARTS = {tetris: tetris, 'sentence-game': sentence_game, superlambda: superlambda, doom: doom,
    scene3d: scene3d, cityscape: cityscape, 'photo-album': photo_album, photo: photo, photos: photos,
    draw: draw, word: word, note: note, slides: slides, chart: chart,
    finder: finder, mail: mail, music: music, archive: archive, readability: readability, greetings: greetings}

let TONE_OF = {tetris: "violet", 'sentence-game': "teal", superlambda: "sky", doom: "clay",
    scene3d: "blue", cityscape: "gold", 'photo-album': "rose", photo: "amber", photos: "cyan",
    draw: "sage", word: "blue", note: "gold", slides: "teal", chart: "indigo",
    finder: "slate", mail: "cyan", music: "rose", archive: "amber", readability: "sage", greetings: "rose"}

pub fn of(key) => ARTS[key](TONES[TONE_OF[key]])
