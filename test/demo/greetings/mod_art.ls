// What a card is drawn from: clip art, papers, patterns, borders, fonts and
// the colour palette. Art is drawn in its own box in one ink, its tints and
// a few fixed accents, so recolouring a piece keeps it readable.

pub fn n(x) => string(round(x * 10) / 10)

// ---- colour arithmetic on #rrggbb --------------------------------------

let HEX = "0123456789abcdef"
fn digit(c, i) => index_of(HEX, lower(slice(c, i, i + 1)))
fn rgb(c) => [digit(c, 1) * 16 + digit(c, 2), digit(c, 3) * 16 + digit(c, 4), digit(c, 5) * 16 + digit(c, 6)]
fn hex2(v) => (let k = int(max(0, min(255, round(v)))), let q = int(floor(k / 16)),
    slice(HEX, q, q + 1) ++ slice(HEX, k % 16, k % 16 + 1))
pub fn mix(c, d, t) => (let a = rgb(c), let b = rgb(d), "#" ++ join([for (i in 0 to 2) hex2(a[i] + (b[i] - a[i]) * t)], ""))
pub fn tint(c, t) => mix(c, "#ffffff", t)
pub fn shade(c, t) => mix(c, "#000000", t)
// relative luminance decides whether light or dark ink reads on a colour
pub fn is_dark(c) => (let v = rgb(c), v[0] * 0.299 + v[1] * 0.587 + v[2] * 0.114 < 140)

pub let PALETTE = ["#1f2937", "#475569", "#ffffff", "#fdf6e3", "#9d174d", "#e11d48", "#f472b6", "#ef4444",
    "#f97316", "#f5b301", "#d4a017", "#84cc16", "#16a34a", "#2f6b3a", "#14b8a6", "#0ea5e9",
    "#2563eb", "#1e3a8a", "#7c3aed", "#a16207"]

// ---- shared shapes -----------------------------------------------------

let PI = 3.141592653589793
let HEART = "M50 88 C20 66 6 48 6 30 C6 16 17 6 30 6 C39 6 46 11 50 18 C54 11 61 6 70 6 C83 6 94 16 94 30 C94 48 80 66 50 88 Z"
let SPARK = "M50 0 C54 36 64 46 100 50 C64 54 54 64 50 100 C46 64 36 54 0 50 C36 46 46 36 50 0 Z"

fn at(x, y, s) => "translate(" ++ n(x) ++ " " ++ n(y) ++ ") scale(" ++ n(s) ++ ")"
pub fn star_points(cx, cy, outer, inner, k) => join([for (i in 0 to 2 * k - 1)
    (let r = if (i % 2 == 0) outer else inner, let a = PI * i / k - PI / 2,
     n(cx + r * math.cos(a)) ++ "," ++ n(cy + r * math.sin(a)))], " ")
fn heart_at(x, y, s, ink) => <path d:HEART, fill:ink, transform:at(x, y, s)>
fn spark_at(x, y, s, ink) => <path d:SPARK, fill:ink, transform:at(x, y, s)>
fn balloon(x, y, s, c) => <g transform:at(x, y, s),
    <path d:"M50 100 C38 114 62 126 50 148", fill:"none", stroke:shade(c, .3), 'stroke-width':1.6>
    <ellipse cx:50, cy:50, rx:36, ry:44, fill:c>
    <ellipse cx:36, cy:32, rx:8, ry:13, fill:"#ffffff", opacity:.4, transform:"rotate(-25 36 32)">
    <path d:"M44 93 L56 93 L50 100 Z", fill:shade(c, .2)>>
fn flame(cx) => <path d:"M" ++ n(cx) ++ " 6 C" ++ n(cx + 3.5) ++ " 11 " ++ n(cx + 3.5) ++ " 15 " ++ n(cx) ++ " 16 C"
    ++ n(cx - 3.5) ++ " 15 " ++ n(cx - 3.5) ++ " 11 " ++ n(cx) ++ " 6 Z", fill:"#f59e0b">
fn flute(x, rot, c) => <g transform:"translate(" ++ n(x) ++ " 22) rotate(" ++ n(rot) ++ ")",
    <path d:"M-10 0 L10 0 L7 34 C6 42 -6 42 -7 34 Z", fill:"#fff7e0", stroke:c, 'stroke-width':2>
    <path d:"M-8.6 12 L8.6 12 L6.6 34 C5.6 40 -5.6 40 -6.6 34 Z", fill:c, opacity:.8>
    <path d:"M0 41 V70", stroke:c, 'stroke-width':2.5>
    <ellipse cx:0, cy:71, rx:10, ry:3, fill:c>>
// a footprint: sole and five toes, mirrored for the right foot
let TOES = [[-9, -24, 4], [-3, -28.5, 3.6], [3, -29, 3.3], [8.5, -27, 3], [12.5, -22, 2.6]]
fn foot(x, y, rot, side, c) => <g transform:"translate(" ++ n(x) ++ " " ++ n(y) ++ ") rotate(" ++ n(rot) ++ ") scale(" ++ n(side) ++ " 1)",
    <ellipse cx:2, cy:4, rx:11, ry:19, fill:c>
    for (t in TOES) <circle cx:t[0], cy:t[1], r:t[2], fill:c>>
fn cloud(cx, cy) => <g fill:"#ffffff",
    <circle cx:cx - 8, cy:cy, r:7>
    <circle cx:cx, cy:cy - 4, r:9>
    <circle cx:cx + 9, cy:cy, r:7>
    <rect x:cx - 15, y:cy, width:31, height:7, rx:3.5>>

// confetti pieces: x, y, rotation, colour slot, round
let BITS = [[8, 14, 20, 0, false], [28, 6, -30, 1, false], [52, 18, 45, 2, true], [74, 8, 10, 3, false],
    [98, 16, -20, 4, false], [16, 38, 60, 2, false], [40, 34, 0, 0, true], [64, 42, -45, 1, false],
    [88, 36, 30, 0, false], [110, 40, 0, 3, true], [6, 62, -10, 4, true], [30, 58, 35, 3, false],
    [54, 66, -60, 0, false], [78, 62, 15, 2, false], [102, 70, -35, 1, false], [20, 86, 50, 1, false],
    [46, 88, 0, 4, true], [70, 86, -15, 0, false], [94, 92, 40, 2, true]]
fn bits(c) => (let colors = [c, "#f6b73c", "#3fb5c0", "#8b5cf6", "#f472b6"],
    for (b in BITS) if (b[4]) <circle cx:b[0], cy:b[1], r:3.2, fill:colors[b[3]]>
        else <rect x:b[0] - 5, y:b[1] - 2, width:10, height:4.5, rx:1, fill:colors[b[3]],
            transform:"rotate(" ++ string(b[2]) ++ " " ++ string(b[0]) ++ " " ++ string(b[1]) ++ ")">)

// ---- the clip art library --------------------------------------------

pub let GROUPS = [{key: "celebrate", name: "Celebrate"}, {key: "love", name: "Love"}, {key: "nature", name: "Nature"},
    {key: "seasons", name: "Seasons"}, {key: "little", name: "Little ones"}, {key: "decor", name: "Decor"}]

pub let ART = [
    {key: "balloons", name: "Balloons", group: "celebrate", box: [130, 160], ink: "#e11d48", draw: (c) => <g
        (balloon(0, 18, .72, "#f6b73c")); (balloon(56, 24, .72, "#3fb5c0")); (balloon(24, 0, .8, c))>},
    {key: "balloon", name: "Balloon", group: "celebrate", box: [100, 150], ink: "#ef4444", draw: (c) => balloon(0, 0, 1, c)},
    {key: "cake", name: "Cake", group: "celebrate", box: [100, 100], ink: "#f472b6", draw: (c) => <g
        <ellipse cx:50, cy:92, rx:46, ry:6, fill:"#e2e8f0">
        <rect x:12, y:58, width:76, height:32, rx:6, fill:c>
        <path d:"M12 64 Q12 58 18 58 H82 Q88 58 88 64 V68 Q84 74 80 68 Q76 62 72 70 Q68 76 64 68 Q60 62 56 70 Q52 78 48 68 Q44 62 40 70 Q36 76 32 68 Q28 62 24 70 Q20 76 16 68 Q12 72 12 66 Z", fill:"#ffffff", opacity:.92>
        for (i in 0 to 4) <circle cx:22 + i * 14, cy:81, r:2.5, fill:tint(c, .6)>;
        <rect x:24, y:36, width:52, height:24, rx:5, fill:tint(c, .35)>
        <rect x:24, y:36, width:52, height:7, rx:3, fill:"#ffffff">
        for (x in [34, 48, 62]) <rect x:x, y:18, width:5, height:18, rx:1.5, fill:"#fde68a", stroke:"#f59e0b", 'stroke-width':.8>;
        for (x in [36.5, 50.5, 64.5]) flame(x)>},
    {key: "gift", name: "Gift", group: "celebrate", box: [100, 100], ink: "#7c3aed", draw: (c) => <g
        <rect x:14, y:44, width:72, height:52, rx:3, fill:c>
        <rect x:8, y:32, width:84, height:16, rx:3, fill:shade(c, .15)>
        <rect x:45, y:32, width:10, height:64, fill:"#fde68a">
        <path d:"M50 32 C40 14 22 18 30 30 C34 34 44 33 50 32 Z M50 32 C60 14 78 18 70 30 C66 34 56 33 50 32 Z", fill:"#f6b73c">
        <circle cx:50, cy:31, r:4, fill:"#d97706">>},
    {key: "confetti", name: "Confetti", group: "celebrate", box: [120, 100], ink: "#e11d48", draw: (c) => <g bits(c)>},
    {key: "cheers", name: "Cheers", group: "celebrate", box: [100, 100], ink: "#d4a017", draw: (c) => <g
        (flute(38, -14, c)); (flute(62, 14, c));
        <path d:"M50 2 V11 M42 6 L46 11 M58 6 L54 11", stroke:c, 'stroke-width':2, 'stroke-linecap':"round">
        <circle cx:30, cy:24, r:1.8, fill:c>
        <circle cx:72, cy:20, r:1.5, fill:c>>},
    {key: "star", name: "Star", group: "celebrate", box: [100, 96], ink: "#f5b301", draw: (c) => <g
        <polygon points:star_points(50, 52, 48, 20, 5), fill:c, stroke:shade(c, .2), 'stroke-width':2, 'stroke-linejoin':"round">
        <polygon points:star_points(50, 52, 30, 12.5, 5), fill:tint(c, .35)>>},
    {key: "sparkle", name: "Sparkle", group: "celebrate", box: [100, 100], ink: "#f5b301", draw: (c) => <g
        (spark_at(14, 14, .72, c)); (spark_at(74, 2, .22, tint(c, .3))); (spark_at(4, 72, .18, tint(c, .3)))>},
    {key: "crown", name: "Crown", group: "celebrate", box: [110, 80], ink: "#d4a017", draw: (c) => <g
        <path d:"M8 30 L30 50 L55 12 L80 50 L102 30 L92 72 L18 72 Z", fill:c, stroke:shade(c, .2), 'stroke-width':2, 'stroke-linejoin':"round">
        <rect x:18, y:62, width:74, height:10, fill:shade(c, .15)>
        for (p in [[8, 28], [55, 10], [102, 28]]) <circle cx:p[0], cy:p[1], r:5, fill:"#e11d48">;
        for (x in [35, 55, 75]) <circle cx:x, cy:67, r:3, fill:"#7dd3fc">>},
    {key: "banner", name: "Ribbon banner", group: "celebrate", box: [240, 72], ink: "#be123c", draw: (c) => <g
        <polygon points:"4,22 44,22 44,62 4,62 18,42", fill:shade(c, .22)>
        <polygon points:"236,22 196,22 196,62 236,62 222,42", fill:shade(c, .22)>
        <polygon points:"28,54 44,62 44,54", fill:shade(c, .45)>
        <polygon points:"212,54 196,62 196,54", fill:shade(c, .45)>
        <rect x:28, y:10, width:184, height:44, fill:c>>},
    {key: "heart", name: "Heart", group: "love", box: [100, 92], ink: "#e11d48", draw: (c) => <g
        <path d:HEART, fill:c>
        <ellipse cx:30, cy:28, rx:10, ry:6, fill:"#ffffff", opacity:.35, transform:"rotate(-35 30 28)">>},
    {key: "hearts", name: "Hearts", group: "love", box: [100, 90], ink: "#e11d48", draw: (c) => <g
        (heart_at(2, 30, .5, tint(c, .45))); (heart_at(42, 2, .56, c)); (heart_at(56, 52, .38, shade(c, .2)))>},
    {key: "rings", name: "Rings", group: "love", box: [110, 80], ink: "#d4a017", draw: (c) => <g
        <circle cx:40, cy:48, r:26, fill:"none", stroke:c, 'stroke-width':6>
        <circle cx:70, cy:48, r:26, fill:"none", stroke:tint(c, .3), 'stroke-width':6>
        <polygon points:"70,6 78,14 70,22 62,14", fill:"#e0f2fe", stroke:"#7dd3fc", 'stroke-width':1.5>>},
    {key: "envelope", name: "Love letter", group: "love", box: [110, 80], ink: "#e11d48", draw: (c) => <g
        <rect x:6, y:10, width:98, height:64, rx:4, fill:tint(c, .82), stroke:c, 'stroke-width':2>
        <path d:"M6 14 L55 48 L104 14", fill:"none", stroke:c, 'stroke-width':2, 'stroke-linejoin':"round">;
        (heart_at(45, 38, .2, c))>},
    {key: "bow", name: "Bow", group: "love", box: [110, 84], ink: "#e11d48", draw: (c) => <g
        <path d:"M51 38 L32 78 L42 73 L47 82 L57 40 Z M59 38 L78 78 L68 73 L63 82 L53 40 Z", fill:shade(c, .15)>
        <path d:"M55 34 C40 6 6 4 10 26 C12 40 40 40 55 34 Z M55 34 C70 6 104 4 100 26 C98 40 70 40 55 34 Z", fill:c>
        <rect x:47, y:27, width:16, height:14, rx:5, fill:shade(c, .25)>>},
    {key: "flower", name: "Flower", group: "nature", box: [100, 100], ink: "#f472b6", draw: (c) => <g
        for (i in 0 to 5) <ellipse cx:50, cy:26, rx:14, ry:22, fill:tint(c, .15), stroke:shade(c, .1), 'stroke-width':1,
            transform:"rotate(" ++ string(i * 60) ++ " 50 50)">;
        for (i in 0 to 5) <ellipse cx:50, cy:34, rx:8, ry:14, fill:tint(c, .5),
            transform:"rotate(" ++ string(i * 60 + 30) ++ " 50 50)">;
        <circle cx:50, cy:50, r:11, fill:"#f6b73c">
        <circle cx:50, cy:50, r:5, fill:"#d97706">>},
    {key: "tulip", name: "Tulip", group: "nature", box: [70, 120], ink: "#ef4444", draw: (c) => <g
        <path d:"M35 60 C34 80 36 100 35 118", fill:"none", stroke:"#4d7c4f", 'stroke-width':4>
        <path d:"M35 104 C18 96 12 80 14 66 C26 74 33 88 35 104 Z", fill:"#6b9e6e">
        <path d:"M35 96 C52 90 58 76 56 62 C44 70 37 82 35 96 Z", fill:"#5a8c5c">
        <path d:"M14 22 C14 50 22 62 35 62 C48 62 56 50 56 22 L46 34 L35 18 L24 34 Z", fill:c>
        <path d:"M24 34 L35 18 L46 34 C44 50 40 60 35 62 C30 60 26 50 24 34 Z", fill:tint(c, .25)>>},
    {key: "sprig", name: "Sprig", group: "nature", box: [80, 130], ink: "#4d7c4f", draw: (c) => <g
        <path d:"M40 128 C38 96 42 60 40 8", fill:"none", stroke:c, 'stroke-width':2.5, 'stroke-linecap':"round">
        for (i in 0 to 5) <g
            <ellipse cx:28, cy:24 + i * 18, rx:12, ry:5, fill:tint(c, .1 * (i % 3)), transform:"rotate(-35 28 " ++ string(24 + i * 18) ++ ")">
            <ellipse cx:52, cy:32 + i * 16, rx:12, ry:5, fill:tint(c, .1 * ((i + 1) % 3)), transform:"rotate(35 52 " ++ string(32 + i * 16) ++ ")">>;
        <ellipse cx:40, cy:8, rx:5, ry:10, fill:c>>},
    {key: "leaf", name: "Leaf", group: "nature", box: [80, 120], ink: "#5a8c5c", draw: (c) => <g
        <path d:"M40 4 C70 30 74 74 40 116 C6 74 10 30 40 4 Z", fill:c>
        <path d:"M40 14 V112 M40 40 L56 28 M40 40 L24 28 M40 62 L60 48 M40 62 L20 48 M40 84 L58 72 M40 84 L22 72",
            fill:"none", stroke:shade(c, .25), 'stroke-width':1.6, 'stroke-linecap':"round">>},
    {key: "butterfly", name: "Butterfly", group: "nature", box: [100, 90], ink: "#8b5cf6", draw: (c) => <g
        <ellipse cx:30, cy:32, rx:24, ry:19, fill:c, transform:"rotate(-30 30 32)">
        <ellipse cx:70, cy:32, rx:24, ry:19, fill:c, transform:"rotate(30 70 32)">
        <ellipse cx:34, cy:62, rx:15, ry:12, fill:tint(c, .4), transform:"rotate(20 34 62)">
        <ellipse cx:66, cy:62, rx:15, ry:12, fill:tint(c, .4), transform:"rotate(-20 66 62)">
        for (p in [[24, 30], [76, 30], [34, 62], [66, 62]]) <circle cx:p[0], cy:p[1], r:4, fill:"#ffffff", opacity:.55>;
        <rect x:47, y:24, width:6, height:50, rx:3, fill:"#334155">
        <path d:"M49 26 C46 14 40 10 36 8 M51 26 C54 14 60 10 64 8", fill:"none", stroke:"#334155", 'stroke-width':1.8, 'stroke-linecap':"round">>},
    {key: "bird", name: "Bird", group: "nature", box: [100, 80], ink: "#3b82f6", draw: (c) => <g
        <path d:"M16 48 L2 38 L8 54 Z", fill:shade(c, .2)>
        <path d:"M14 50 C14 28 36 18 54 26 C62 14 78 12 86 22 L86 30 C86 52 64 70 40 66 C28 64 18 58 14 50 Z", fill:c>
        <path d:"M86 21 L97 24 L86 28 Z", fill:"#f6b73c">
        <path d:"M36 38 C46 30 62 34 66 46 C56 52 42 50 36 38 Z", fill:shade(c, .2)>
        <circle cx:79, cy:24, r:2.6, fill:"#1e293b">>},
    {key: "sun", name: "Sun", group: "nature", box: [100, 100], ink: "#f5b301", draw: (c) => <g
        for (i in 0 to 11) <rect x:48, y:2, width:4, height:15, rx:2, fill:c, transform:"rotate(" ++ string(i * 30) ++ " 50 50)">;
        <circle cx:50, cy:50, r:26, fill:c>
        <circle cx:50, cy:50, r:19, fill:tint(c, .35)>>},
    {key: "rainbow", name: "Rainbow", group: "nature", box: [120, 74], ink: "#ef4444", draw: (c) => <g
        for (i, col in [c, "#f59e0b", "#facc15", "#22c55e", "#3b82f6", "#8b5cf6"])
            <path d:"M" ++ string(14 + i * 7) ++ " 64 A" ++ string(46 - i * 7) ++ " " ++ string(46 - i * 7) ++ " 0 0 1 "
                ++ string(106 - i * 7) ++ " 64", fill:"none", stroke:col, 'stroke-width':7>;
        (cloud(18, 62)); (cloud(102, 62))>},
    {key: "tree", name: "Evergreen", group: "seasons", box: [90, 120], ink: "#2f6b3a", draw: (c) => <g
        <rect x:40, y:100, width:10, height:16, fill:"#8b5e3c">
        <polygon points:"45,56 90,106 0,106", fill:shade(c, .15)>
        <polygon points:"45,30 82,80 8,80", fill:c>
        <polygon points:"45,10 74,54 16,54", fill:tint(c, .15)>
        for (b in [[30, 70, "#e11d48"], [58, 62, "#f6b73c"], [40, 94, "#f6b73c"], [66, 92, "#e11d48"], [20, 98, "#7dd3fc"], [48, 44, "#7dd3fc"]])
            <circle cx:b[0], cy:b[1], r:3.6, fill:b[2]>;
        <polygon points:star_points(45, 10, 9, 4, 5), fill:"#f6b73c">>},
    {key: "ornament", name: "Ornament", group: "seasons", box: [80, 100], ink: "#be123c", draw: (c) => <g
        <path d:"M40 4 C33 4 33 13 40 13", fill:"none", stroke:"#94a3b8", 'stroke-width':2>
        <rect x:32, y:12, width:16, height:10, rx:2, fill:"#d4a017">
        <circle cx:40, cy:60, r:36, fill:c>
        <path d:"M5 54 Q40 68 75 54 L76 64 Q40 78 4 64 Z", fill:"#f6d36b", opacity:.9>
        <ellipse cx:26, cy:44, rx:8, ry:12, fill:"#ffffff", opacity:.35, transform:"rotate(30 26 44)">>},
    {key: "snowflake", name: "Snowflake", group: "seasons", box: [100, 100], ink: "#7dd3fc", draw: (c) => <g
        for (i in 0 to 5) <path d:"M50 50 V6 M50 20 L40 12 M50 20 L60 12 M50 34 L42 27 M50 34 L58 27", fill:"none", stroke:c,
            'stroke-width':4.5, 'stroke-linecap':"round", transform:"rotate(" ++ string(i * 60) ++ " 50 50)">;
        <circle cx:50, cy:50, r:5, fill:c>>},
    {key: "moon", name: "Moon & stars", group: "seasons", box: [100, 100], ink: "#facc15", draw: (c) => <g
        <path d:"M62 8 A42 42 0 1 0 92 70 A34 34 0 1 1 62 8 Z", fill:c>
        <polygon points:star_points(22, 20, 8, 3.2, 5), fill:tint(c, .3)>
        <polygon points:star_points(86, 28, 5.5, 2.2, 5), fill:tint(c, .3)>>},
    {key: "candle", name: "Candle", group: "seasons", box: [60, 120], ink: "#f5ecd7", draw: (c) => <g
        <circle cx:30, cy:20, r:18, fill:"#fde68a", opacity:.35>
        <path d:"M30 6 C38 18 38 28 30 32 C22 28 22 18 30 6 Z", fill:"#f59e0b">
        <path d:"M30 16 C34 22 34 27 30 29 C26 27 26 22 30 16 Z", fill:"#fef3c7">
        <path d:"M30 32 V38", stroke:"#334155", 'stroke-width':1.5>
        <ellipse cx:30, cy:114, rx:24, ry:5, fill:"#94a3b8">
        <rect x:16, y:38, width:28, height:76, rx:4, fill:c>
        <path d:"M16 44 Q16 38 22 38 H38 Q44 38 44 44 V50 Q41 56 38 50 Q35 46 32 54 Q29 60 27 50 Q24 46 21 52 Q18 56 16 50 Z", fill:tint(c, .55)>>},
    {key: "bear", name: "Teddy", group: "little", box: [100, 100], ink: "#b07a4f", draw: (c) => <g
        for (x in [24, 76]) <g <circle cx:x, cy:24, r:14, fill:c> <circle cx:x, cy:24, r:7, fill:tint(c, .4)>>;
        <circle cx:50, cy:56, r:38, fill:c>
        <ellipse cx:50, cy:68, rx:16, ry:12, fill:tint(c, .55)>
        <ellipse cx:50, cy:62, rx:6, ry:4.5, fill:"#3f2a1d">
        <circle cx:36, cy:48, r:3.6, fill:"#3f2a1d">
        <circle cx:64, cy:48, r:3.6, fill:"#3f2a1d">
        <path d:"M44 72 Q50 77 56 72", fill:"none", stroke:"#3f2a1d", 'stroke-width':2, 'stroke-linecap':"round">
        <circle cx:27, cy:64, r:5, fill:"#f9a8d4", opacity:.6>
        <circle cx:73, cy:64, r:5, fill:"#f9a8d4", opacity:.6>>},
    {key: "footprints", name: "Footprints", group: "little", box: [100, 100], ink: "#93c5fd", draw: (c) => <g
        (foot(32, 60, -10, 1, c)); (foot(68, 42, 10, -1, tint(c, .25)))>},
    {key: "flourish", name: "Flourish", group: "decor", box: [220, 40], ink: "#a16207", draw: (c) => <g
        <path d:"M110 20 C96 4 78 4 70 14 C64 22 72 30 80 26 C86 23 84 15 78 16 M110 20 C124 4 142 4 150 14 C156 22 148 30 140 26 C134 23 136 15 142 16 M68 20 H10 M152 20 H210",
            fill:"none", stroke:c, 'stroke-width':1.8, 'stroke-linecap':"round">
        <path d:"M110 13 L116 20 L110 27 L104 20 Z", fill:c>
        <circle cx:6, cy:20, r:2.5, fill:c>
        <circle cx:214, cy:20, r:2.5, fill:c>>},
    {key: "lambda", name: "Lambda", group: "decor", box: [100, 100], ink: "#334155", draw: (c) => <g
        <circle cx:50, cy:50, r:46, fill:c>
        <path d:"M30 80 L48 44 L38 22 H47 L73 80 H64 L52.5 54 L39.5 80 Z", fill:"#ffffff">>}
]

pub fn art_of(key) => (let hit = [for (a in ART where a.key == key) a], if (len(hit) == 0) ART[0] else hit[0])
pub fn ratio(key) => (let a = art_of(key), a.box[1] / a.box[0])
pub fn art_svg(key, color) {
    let a = art_of(key);
    <svg class:"art-svg", viewBox:"0 0 " ++ string(a.box[0]) ++ " " ++ string(a.box[1]), width:"100%", height:"100%",
        'aria-hidden':"true", (a.draw)(color)>
}

// ---- papers -------------------------------------------------------------

pub let PAPERS = [
    {key: "white", name: "White", bg: "#ffffff", ink: "#1f2937"},
    {key: "cream", name: "Cream", bg: "#fbf6ea", ink: "#3f3a33"},
    {key: "kraft", name: "Kraft", bg: "linear-gradient(160deg, #dcbf94, #c49b6c)", ink: "#3b2a1a"},
    {key: "blush", name: "Blush", bg: "linear-gradient(165deg, #fff1f2, #fbd0dc)", ink: "#9d174d"},
    {key: "peach", name: "Peach", bg: "linear-gradient(160deg, #fff4e6, #ffd3b0)", ink: "#9a3412"},
    {key: "sunshine", name: "Sunshine", bg: "linear-gradient(170deg, #fffbe8, #ffe48f)", ink: "#92400e"},
    {key: "mint", name: "Mint", bg: "linear-gradient(165deg, #f0fdf7, #c4eedb)", ink: "#166534"},
    {key: "sky", name: "Sky", bg: "linear-gradient(170deg, #eff8ff, #bfdcf8)", ink: "#1e3a8a"},
    {key: "lavender", name: "Lavender", bg: "linear-gradient(165deg, #f8f4ff, #d9cbff)", ink: "#5b21b6"},
    {key: "dove", name: "Dove", bg: "linear-gradient(170deg, #f7f8fa, #dde2e8)", ink: "#334155"},
    {key: "coral", name: "Coral", bg: "linear-gradient(160deg, #ff8f7e, #f05d5e)", ink: "#ffffff"},
    {key: "sunset", name: "Sunset", bg: "linear-gradient(170deg, #ffd08a, #ff8fa3 55%, #a86ab8)", ink: "#ffffff"},
    {key: "berry", name: "Berry", bg: "linear-gradient(160deg, #9d1747, #640d2c)", ink: "#ffe4ec"},
    {key: "forest", name: "Forest", bg: "linear-gradient(170deg, #22543b, #12301f)", ink: "#f6e7b0"},
    {key: "navy", name: "Navy", bg: "linear-gradient(170deg, #1e2c56, #0e1631)", ink: "#f8e7b9"},
    {key: "night", name: "Night", bg: "linear-gradient(165deg, #2c1c60, #110c2c)", ink: "#fde68a"},
    {key: "charcoal", name: "Charcoal", bg: "linear-gradient(170deg, #3b4553, #1f2731)", ink: "#f3f4f6"}
]
pub fn paper_of(key) => (let hit = [for (p in PAPERS where p.key == key) p], if (len(hit) == 0) PAPERS[0] else hit[0])

// ---- patterns: an SVG layer over the paper, in the page's accent colour ----

pub let PATTERNS = [{key: "none", name: "None"}, {key: "dots", name: "Dots"}, {key: "polka", name: "Polka"},
    {key: "stripes", name: "Stripes"}, {key: "grid", name: "Grid"}, {key: "hearts", name: "Hearts"},
    {key: "stars", name: "Stars"}, {key: "snow", name: "Snow"}, {key: "confetti", name: "Confetti"}]

let SNOW = [[10, 12, 2.5], [44, 6, 1.6], [62, 30, 3], [22, 44, 1.8], [52, 58, 2.2], [8, 64, 1.4], [34, 26, 1.2]]
fn tile(key, c) => if (key == "dots") [24, <circle cx:12, cy:12, r:2.2, fill:c>]
    else if (key == "polka") [48, <g fill:c, <circle cx:12, cy:12, r:7> <circle cx:36, cy:36, r:7>>]
    else if (key == "stripes") [28, <path d:"M-7 7 L7 -7 M0 28 L28 0 M21 35 L35 21", stroke:c, 'stroke-width':6>]
    else if (key == "grid") [26, <path d:"M26 0 H0 V26", fill:"none", stroke:c, 'stroke-width':1>]
    else if (key == "hearts") [56, <g (heart_at(6, 6, .16, c)); (heart_at(34, 34, .16, c))>]
    else if (key == "stars") [60, <g fill:c, <polygon points:star_points(14, 14, 6, 2.5, 5)> <polygon points:star_points(44, 42, 4.5, 1.8, 5)>>]
    else if (key == "snow") [70, <g fill:c, for (s in SNOW) <circle cx:s[0], cy:s[1], r:s[2]>>]
    else if (key == "confetti") [100, <g transform:"scale(.85)", bits(c)>]
    else null

// `uid` keeps pattern ids apart when several pages share one document
pub fn pattern_layer(key, color, opacity, uid, w, h) => (let t = tile(key, color),
    if (t == null) null else <svg class:"page-layer", viewBox:"0 0 " ++ string(w) ++ " " ++ string(h), width:"100%", height:"100%",
        preserveAspectRatio:"none", 'aria-hidden':"true",
        <defs <pattern id:uid, width:t[0], height:t[0], patternUnits:"userSpaceOnUse", t[1]>>
        <rect width:w, height:h, fill:"url(#" ++ uid ++ ")", opacity:opacity>>)

// ---- borders ------------------------------------------------------------

pub let BORDERS = [{key: "none", name: "None"}, {key: "line", name: "Line"}, {key: "double", name: "Double"},
    {key: "stitch", name: "Stitch"}, {key: "dots", name: "Dots"}, {key: "frame", name: "Frame"}, {key: "corners", name: "Corners"}]

fn inset(d, w, h, c, more) => <rect x:d, y:d, width:w - 2 * d, height:h - 2 * d, fill:"none", stroke:c, *:more>
fn corner(x, y, sx, sy, c) => <g transform:"translate(" ++ string(x) ++ " " ++ string(y) ++ ") scale(" ++ string(sx) ++ " " ++ string(sy) ++ ")",
    <path d:"M0 58 V12 Q0 0 12 0 H58 M10 40 V18 Q10 10 18 10 H40", fill:"none", stroke:c, 'stroke-width':2.4, 'stroke-linecap':"round">
    <circle cx:24, cy:24, r:4, fill:c>>
fn border_marks(key, c, w, h) => if (key == "line") [inset(18, w, h, c, {'stroke-width':2})]
    else if (key == "double") [inset(16, w, h, c, {'stroke-width':3}), inset(25, w, h, c, {'stroke-width':1})]
    else if (key == "stitch") [inset(20, w, h, c, {'stroke-width':2, 'stroke-dasharray':"9 6", rx:10})]
    else if (key == "dots") [inset(20, w, h, c, {'stroke-width':5, 'stroke-dasharray':"0 13", 'stroke-linecap':"round"})]
    else if (key == "frame") [inset(0, w, h, c, {'stroke-width':40}), inset(30, w, h, c, {'stroke-width':1.5})]
    else if (key == "corners") [corner(18, 18, 1, 1, c), corner(w - 18, 18, -1, 1, c), corner(18, h - 18, 1, -1, c), corner(w - 18, h - 18, -1, -1, c)]
    else []
pub fn border_layer(key, color, w, h) => (let marks = border_marks(key, color, w, h),
    if (len(marks) == 0) null else <svg class:"page-layer", viewBox:"0 0 " ++ string(w) ++ " " ++ string(h), width:"100%", height:"100%",
        preserveAspectRatio:"none", 'aria-hidden':"true", for (m in marks) m>)

// ---- fonts: macOS families first, with portable fallbacks -----------------

pub let FONTS = [
    {key: "roundhand", name: "Roundhand", family: "'Snell Roundhand', 'Segoe Script', 'URW Chancery L', cursive"},
    {key: "savoye", name: "Savoye", family: "'Savoye LET', 'Snell Roundhand', 'Segoe Script', cursive"},
    {key: "chancery", name: "Chancery", family: "'Apple Chancery', 'URW Chancery L', 'Segoe Script', cursive"},
    {key: "brush", name: "Brush", family: "'Brush Script MT', 'Brush Script', 'Segoe Script', cursive"},
    {key: "bradley", name: "Bradley Hand", family: "'Bradley Hand', 'Segoe Print', cursive"},
    {key: "noteworthy", name: "Noteworthy", family: "Noteworthy, 'Segoe Print', 'Comic Sans MS', cursive"},
    {key: "marker", name: "Marker", family: "'Marker Felt', 'Comic Sans MS', fantasy"},
    {key: "chalk", name: "Chalkboard", family: "'Chalkboard SE', 'Comic Sans MS', sans-serif"},
    {key: "luminari", name: "Luminari", family: "Luminari, 'Apple Chancery', fantasy"},
    {key: "didot", name: "Didot", family: "Didot, 'Bodoni 72', 'Times New Roman', serif"},
    {key: "baskerville", name: "Baskerville", family: "Baskerville, 'Libre Baskerville', Georgia, serif"},
    {key: "georgia", name: "Georgia", family: "Georgia, 'Times New Roman', serif"},
    {key: "copperplate", name: "Copperplate", family: "Copperplate, 'Copperplate Gothic Light', serif"},
    {key: "avenir", name: "Avenir", family: "'Avenir Next', Avenir, 'Segoe UI', sans-serif"},
    {key: "futura", name: "Futura", family: "Futura, 'Century Gothic', 'Trebuchet MS', sans-serif"},
    {key: "typewriter", name: "Typewriter", family: "'American Typewriter', 'Courier New', monospace"}
]
pub fn font_of(key) => (let hit = [for (f in FONTS where f.key == key) f], if (len(hit) == 0) FONTS[0] else hit[0])

// ---- frames for photos ------------------------------------------------------

pub let FRAMES = [{key: "plain", name: "Plain"}, {key: "rounded", name: "Rounded"}, {key: "circle", name: "Circle"},
    {key: "arch", name: "Arch"}, {key: "polaroid", name: "Snapshot"}]
