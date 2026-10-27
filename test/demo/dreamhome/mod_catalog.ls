// What a home is furnished from: the catalog of furniture, doors and windows,
// floor finishes and paints. Every piece is a few parts in its own unit box
// (u across its width, v from back to front, t up its height), so one recipe
// draws its 3D model at any size and, seen from above, its plan symbol.

// ---- parts ----------------------------------------------------------------
// a part's colour is a literal #rrggbb or a role: "main" is the piece's own
// colour, "tint" and "shade" are lighter and darker versions of it

fn box(u0, v0, t0, u1, v1, t1, c) => {k: "box", u0: u0, v0: v0, t0: t0, u1: u1, v1: v1, t1: t1, c: c}
// an upright cylinder or ellipsoid filling the box, or a cone narrowing to `top`
fn cyl(u0, v0, t0, u1, v1, t1, c) => {k: "cyl", u0: u0, v0: v0, t0: t0, u1: u1, v1: v1, t1: t1, c: c}
fn ball(u0, v0, t0, u1, v1, t1, c) => {k: "ball", u0: u0, v0: v0, t0: t0, u1: u1, v1: v1, t1: t1, c: c}
fn cone(u0, v0, t0, u1, v1, t1, c) => {k: "cone", u0: u0, v0: v0, t0: t0, u1: u1, v1: v1, t1: t1, c: c}

let WOOD = "#a87b51"
let DARK = "#34363b"
pub let METAL = "#b8bec6"
let WHITE = "#f5f4f0"
let STONE = "#d9d4cb"
pub let GLASS = "#a9d6ea"
let PILLOW = "#fbfaf6"

// four square legs inset by `i` across and `j` deep, up to `top`
fn legs(i, j, top, c) => [box(i, j, 0.0, i + .05, j + .05, top, c), box(.95 - i, j, 0.0, 1.0 - i, j + .05, top, c),
    box(i, .95 - j, 0.0, i + .05, 1.0 - j, top, c), box(.95 - i, .95 - j, 0.0, 1.0 - i, 1.0 - j, top, c)]
// `n` shelves between the sides of a cabinet
fn shelves(n, c) => [for (i in 1 to n) box(.04, .05, i / (n + 1.0), .96, .98, i / (n + 1.0) + .025, c)]
// a row of books standing on each shelf, in a few cover colours
let COVERS = ["#8f3b2e", "#2f5d73", "#c9a24a", "#4c6b3c", "#6b4c7a", "#d8cfc0", "#a14d5b"]
fn books(n) => [for (i in 1 to n, j in 0 to 6)
    box(.07 + j * .12, .2, i / (n + 1.0) + .025, .07 + j * .12 + .1 - (j % 3) * .015, .9, i / (n + 1.0) + .025 + .13 - (j % 2) * .03, COVERS[(i + j) % 7])]

// ---- catalog ----------------------------------------------------------------

pub let CATEGORIES = [{key: "build", name: "Doors & windows", short: "Doors", icon: "M4 21V3h16v18 M4 21h16 M14 12h1"},
    {key: "living", name: "Living room", short: "Living", icon: "M3 11v7h18v-7 M5 11V7h14v4 M3 18v2 M21 18v2 M7 14h10"},
    {key: "dining", name: "Dining", short: "Dining", icon: "M4 10h16 M6 10v9 M18 10v9 M9 4v4 M12 4v4 M15 4v4"},
    {key: "kitchen", name: "Kitchen", short: "Kitchen", icon: "M4 4h16v16H4z M4 10h16 M8 7h1 M8 14v3 M12 14h4"},
    {key: "bedroom", name: "Bedroom", short: "Bedroom", icon: "M3 18V8 M3 14h18v4 M21 14v-3a3 3 0 0 0-3-3h-7v6 M6 11h2"},
    {key: "bath", name: "Bathroom", short: "Bath", icon: "M3 12h18v3a5 5 0 0 1-5 5H8a5 5 0 0 1-5-5z M6 12V6a2 2 0 0 1 4 0 M7 20l-1 2 M17 20l1 2"},
    {key: "office", name: "Office", short: "Office", icon: "M3 9h18 M5 9v11 M19 9v11 M8 4h8v5H8z"},
    {key: "decor", name: "Lighting & decor", short: "Decor", icon: "M9 18h6 M10 21h4 M12 3a6 6 0 0 0-3.5 10.9V16h7v-2.1A6 6 0 0 0 12 3z"}]

// An entry gives its size in centimetres, its height off the floor, its own
// colour and its parts. Doors and windows name the kind of opening they cut,
// and lights the light they give.
fn piece(key, title, cat, w, d, h, color, parts, more = {}) => {key: key, name: title, cat: cat, w: w, d: d, h: h, elev: 0.0,
    color: color, parts: parts, *:more}

pub let ITEMS = [
    // openings are placed in a wall and take its thickness
    piece("door", "Door", "build", 90.0, 12.0, 210.0, WOOD, [], {opening: "door"}),
    piece("double-door", "Double door", "build", 160.0, 12.0, 210.0, WHITE, [], {opening: "double"}),
    piece("slider", "Sliding glass door", "build", 210.0, 12.0, 215.0, DARK, [], {opening: "slider"}),
    piece("archway", "Open archway", "build", 100.0, 12.0, 215.0, WHITE, [], {opening: "arch"}),
    piece("window", "Window", "build", 120.0, 12.0, 140.0, WHITE, [], {opening: "window", elev: 90.0}),
    piece("wide-window", "Picture window", "build", 220.0, 12.0, 160.0, WHITE, [], {opening: "window", elev: 70.0}),
    piece("tall-window", "French window", "build", 80.0, 12.0, 220.0, WHITE, [], {opening: "window", elev: 10.0}),

    piece("sofa", "Sofa", "living", 215.0, 92.0, 85.0, "#7d8ca3", [
        box(0.0, .02, .1, 1.0, 1.0, .45, "shade"), box(0.0, 0.0, .1, 1.0, .26, 1.0, "main"),
        box(0.0, 0.0, .1, .09, 1.0, .68, "main"), box(.91, 0.0, .1, 1.0, 1.0, .68, "main"),
        box(.1, .26, .45, .365, .97, .58, "tint"), box(.37, .26, .45, .63, .97, .58, "tint"), box(.635, .26, .45, .9, .97, .58, "tint"),
        box(.03, .05, 0.0, .07, .12, .1, DARK), box(.93, .05, 0.0, .97, .12, .1, DARK),
        box(.03, .88, 0.0, .07, .95, .1, DARK), box(.93, .88, 0.0, .97, .95, .1, DARK)]),
    piece("corner-sofa", "Corner sofa", "living", 260.0, 180.0, 85.0, "#a39b8e", [
        box(0.0, 0.0, .1, 1.0, .5, .45, "shade"), box(.62, .5, .1, 1.0, 1.0, .45, "shade"),
        box(0.0, 0.0, .1, 1.0, .14, 1.0, "main"), box(.9, .14, .1, 1.0, 1.0, 1.0, "main"),
        box(0.0, 0.0, .1, .07, .5, .68, "main"), box(.62, .93, .1, .9, 1.0, .68, "main"),
        box(.08, .15, .45, .45, .48, .58, "tint"), box(.46, .15, .45, .89, .48, .58, "tint"), box(.63, .5, .45, .89, .92, .58, "tint")]),
    piece("armchair", "Armchair", "living", 88.0, 88.0, 85.0, "#c4704f", [
        box(.05, .05, .1, .95, 1.0, .45, "shade"), box(0.0, 0.0, .1, 1.0, .25, 1.0, "main"),
        box(0.0, 0.0, .1, .17, 1.0, .68, "main"), box(.83, 0.0, .1, 1.0, 1.0, .68, "main"),
        box(.18, .25, .45, .82, .97, .58, "tint"), *legs(.06, .06, .1, WOOD)]),
    piece("coffee-table", "Coffee table", "living", 115.0, 62.0, 42.0, WOOD, [
        box(0.0, 0.0, .88, 1.0, 1.0, 1.0, "main"), box(.04, .06, .25, .96, .94, .31, "shade"), *legs(.03, .05, .88, "shade")]),
    piece("tv-stand", "Media console", "living", 170.0, 45.0, 50.0, "#ece7df", [
        box(0.0, 0.0, .1, 1.0, 1.0, 1.0, "main"), box(.02, .97, .2, .49, 1.0, .92, "shade"), box(.51, .97, .2, .98, 1.0, .92, "shade"),
        *legs(.02, .05, .1, DARK)]),
    piece("tv", "Television", "living", 132.0, 25.0, 80.0, DARK, [
        box(.35, .2, 0.0, .65, .8, .03, "main"), box(.47, .45, .03, .53, .55, .18, "main"), box(0.0, .4, .16, 1.0, .6, 1.0, "main"),
        box(.015, .6, .19, .985, .62, .97, "#1c2b3a")], {elev: 50.0}),
    piece("bookshelf", "Bookcase", "living", 90.0, 34.0, 200.0, "#f1eee8", [
        box(0.0, 0.0, 0.0, .04, 1.0, 1.0, "main"), box(.96, 0.0, 0.0, 1.0, 1.0, 1.0, "main"), box(0.0, 0.0, .97, 1.0, 1.0, 1.0, "main"),
        box(0.0, 0.0, 0.0, 1.0, 1.0, .04, "main"), box(.04, 0.0, .04, .96, .05, .97, "shade"), *shelves(4, "main"), *books(4)]),
    piece("rug", "Rug", "living", 230.0, 160.0, 1.5, "#b9a089", [box(0.0, 0.0, 0.0, 1.0, 1.0, 1.0, "main"),
        box(.06, .08, 0.0, .94, .92, 1.2, "tint")]),
    piece("sideboard", "Sideboard", "living", 160.0, 45.0, 80.0, "#5b4636", [
        box(0.0, 0.0, .15, 1.0, 1.0, 1.0, "main"), box(.02, .97, .22, .325, 1.0, .95, "tint"), box(.34, .97, .22, .66, 1.0, .95, "tint"),
        box(.675, .97, .22, .98, 1.0, .95, "tint"), *legs(.03, .06, .15, DARK)]),

    piece("dining-table", "Dining table", "dining", 180.0, 90.0, 75.0, WOOD, [
        box(0.0, 0.0, .94, 1.0, 1.0, 1.0, "main"), box(.05, .08, .82, .95, .92, .94, "shade"), *legs(.04, .07, .94, "shade")]),
    piece("round-table", "Round table", "dining", 110.0, 110.0, 75.0, WHITE, [
        cyl(0.0, 0.0, .94, 1.0, 1.0, 1.0, "main"), cyl(.44, .44, .05, .56, .56, .94, "shade"), cyl(.25, .25, 0.0, .75, .75, .05, "shade")]),
    piece("chair", "Dining chair", "dining", 46.0, 52.0, 90.0, "#2f3b45", [
        box(0.0, .08, .48, 1.0, 1.0, .54, "main"), box(0.0, 0.0, .54, 1.0, .1, 1.0, "main"), box(.08, .1, .56, .92, .14, .9, "tint"),
        *legs(.02, .1, .48, "shade")]),
    piece("bench", "Bench", "dining", 140.0, 38.0, 46.0, WOOD, [box(0.0, 0.0, .88, 1.0, 1.0, 1.0, "main"),
        box(.05, .1, 0.0, .1, .9, .88, "shade"), box(.9, .1, 0.0, .95, .9, .88, "shade"), box(.1, .45, .3, .9, .55, .38, "shade")]),
    piece("pendant", "Pendant lamp", "dining", 45.0, 45.0, 110.0, "#d8b56a", [
        cyl(.49, .49, .3, .51, .51, 1.0, DARK), cone(0.0, 0.0, 0.0, 1.0, 1.0, .32, "main"), ball(.38, .38, .02, .62, .62, .14, "#fff6d8")],
        {elev: 140.0, light: {color: "#ffe2b0", intensity: .9, at: .1}}),

    piece("base-cabinet", "Base cabinet", "kitchen", 60.0, 60.0, 90.0, "#e9e6df", [
        box(0.0, .05, .1, 1.0, 1.0, .95, "main"), box(0.0, 0.0, .95, 1.0, 1.0, 1.0, STONE), box(.02, .99, .14, .98, 1.0, .92, "tint"),
        box(.42, 1.0, .82, .58, 1.02, .85, METAL), box(0.0, .05, 0.0, 1.0, .95, .1, DARK)]),
    piece("sink", "Sink cabinet", "kitchen", 80.0, 60.0, 90.0, "#e9e6df", [
        box(0.0, .05, .1, 1.0, 1.0, .95, "main"), box(0.0, 0.0, .95, 1.0, 1.0, 1.0, STONE), box(.12, .25, .955, .88, .85, 1.005, METAL),
        cyl(.47, .08, 1.0, .53, .16, 1.38, METAL), box(.48, .1, 1.3, .52, .32, 1.36, METAL),
        box(.02, .99, .14, .49, 1.0, .92, "tint"), box(.51, .99, .14, .98, 1.0, .92, "tint"), box(0.0, .05, 0.0, 1.0, .95, .1, DARK)]),
    piece("stove", "Range", "kitchen", 60.0, 62.0, 90.0, METAL, [
        box(0.0, .03, 0.0, 1.0, 1.0, .95, "main"), box(0.0, 0.0, .95, 1.0, 1.0, 1.0, DARK), box(.08, .985, .12, .92, 1.0, .7, "#2a2c30"),
        cyl(.12, .12, 1.0, .42, .42, 1.02, "#55585e"), cyl(.58, .12, 1.0, .88, .42, 1.02, "#55585e"),
        cyl(.12, .55, 1.0, .42, .85, 1.02, "#55585e"), cyl(.58, .55, 1.0, .88, .85, 1.02, "#55585e"), box(0.0, 0.0, .95, 1.0, .08, 1.18, "shade")]),
    piece("fridge", "Refrigerator", "kitchen", 72.0, 70.0, 185.0, "#e4e6e8", [
        box(0.0, 0.0, .02, 1.0, 1.0, 1.0, "main"), box(.01, .995, .66, .99, 1.0, .995, "tint"), box(.01, .995, .03, .99, 1.0, .65, "tint"),
        box(.85, 1.0, .7, .89, 1.04, .9, METAL), box(.85, 1.0, .42, .89, 1.04, .62, METAL)]),
    piece("wall-cabinet", "Wall cabinet", "kitchen", 60.0, 35.0, 70.0, "#e9e6df", [
        box(0.0, 0.0, 0.0, 1.0, .97, 1.0, "main"), box(.02, .97, .03, .98, 1.0, .97, "tint"), box(.42, 1.0, .06, .58, 1.04, .1, METAL)],
        {elev: 145.0}),
    piece("island", "Kitchen island", "kitchen", 180.0, 95.0, 92.0, "#3f5a63", [
        box(.02, .1, .1, .98, .9, .96, "main"), box(0.0, 0.0, .96, 1.0, 1.0, 1.0, STONE), box(.02, .9, .14, .98, .91, .92, "tint"),
        box(.04, .12, 0.0, .96, .88, .1, DARK)]),
    piece("bar-stool", "Bar stool", "kitchen", 40.0, 40.0, 75.0, "#26292e", [
        cyl(0.0, 0.0, .9, 1.0, 1.0, 1.0, "main"), cyl(.45, .45, .05, .55, .55, .9, METAL), cyl(.1, .1, 0.0, .9, .9, .05, METAL),
        cyl(.2, .2, .33, .8, .8, .36, METAL)]),

    piece("double-bed", "Double bed", "bedroom", 165.0, 210.0, 100.0, "#8fa3b8", [
        box(0.0, 0.0, 0.0, 1.0, .05, 1.0, "#6d5643"), box(0.0, .04, .08, 1.0, 1.0, .3, "#6d5643"),
        box(.02, .05, .3, .98, .99, .5, WHITE), box(.01, .3, .5, .99, 1.0, .56, "main"), box(.01, .96, .2, .99, 1.0, .5, "main"),
        box(.08, .08, .5, .47, .25, .64, PILLOW), box(.53, .08, .5, .92, .25, .64, PILLOW)]),
    piece("single-bed", "Single bed", "bedroom", 95.0, 205.0, 90.0, "#d9a19a", [
        box(0.0, 0.0, 0.0, 1.0, .05, 1.0, WHITE), box(0.0, .04, .08, 1.0, 1.0, .3, WHITE),
        box(.03, .05, .3, .97, .99, .52, WHITE), box(.01, .32, .52, .99, 1.0, .58, "main"), box(.01, .96, .2, .99, 1.0, .52, "main"),
        box(.12, .08, .52, .88, .25, .67, PILLOW)]),
    piece("wardrobe", "Wardrobe", "bedroom", 150.0, 60.0, 215.0, "#ede9e2", [
        box(0.0, 0.0, .02, 1.0, .97, 1.0, "main"), box(.01, .97, .04, .33, 1.0, .985, "tint"), box(.34, .97, .04, .66, 1.0, .985, "tint"),
        box(.67, .97, .04, .99, 1.0, .985, "tint"), box(.29, 1.0, .45, .31, 1.03, .6, METAL), box(.36, 1.0, .45, .38, 1.03, .6, METAL),
        box(.69, 1.0, .45, .71, 1.03, .6, METAL), box(0.0, 0.0, 0.0, 1.0, .95, .02, DARK)]),
    piece("nightstand", "Nightstand", "bedroom", 48.0, 40.0, 55.0, "#6d5643", [
        box(0.0, 0.0, .2, 1.0, 1.0, 1.0, "main"), box(.05, .98, .62, .95, 1.0, .94, "tint"), box(.05, .98, .26, .95, 1.0, .58, "tint"),
        *legs(.03, .04, .2, "shade")]),
    piece("dresser", "Chest of drawers", "bedroom", 120.0, 50.0, 85.0, "#ede9e2", [
        box(0.0, 0.0, .1, 1.0, 1.0, 1.0, "main"), *[for (i in 0 to 2) box(.03, .98, .14 + i * .29, .97, 1.0, .4 + i * .29, "tint")],
        *[for (i in 0 to 2) box(.44, 1.0, .25 + i * .29, .56, 1.03, .28 + i * .29, METAL)], *legs(.02, .04, .1, WOOD)]),
    piece("desk", "Desk", "office", 140.0, 70.0, 75.0, "#f0ede6", [
        box(0.0, 0.0, .95, 1.0, 1.0, 1.0, "main"), box(.02, .04, 0.0, .05, .96, .95, DARK), box(.95, .04, 0.0, .98, .96, .95, DARK),
        box(.62, .02, .55, .94, .96, .95, "shade"), box(.3, .1, 1.0, .7, .2, 1.03, DARK), box(.48, .14, 1.03, .52, .18, 1.25, DARK),
        box(.28, .14, 1.25, .72, .18, 1.62, "#1d1f24"), box(.36, .55, 1.0, .64, .75, 1.02, "#d0d3d8")]),
    piece("office-chair", "Office chair", "office", 62.0, 62.0, 105.0, "#2d3138", [
        box(.1, .15, .44, .9, .9, .52, "main"), box(.12, .02, .55, .88, .14, 1.0, "main"), box(.46, .08, .48, .54, .16, .58, DARK),
        cyl(.46, .46, .06, .54, .54, .44, METAL), box(.05, .47, 0.0, .95, .53, .06, DARK), box(.47, .05, 0.0, .53, .95, .06, DARK),
        box(.02, .35, .52, .1, .75, .7, DARK), box(.9, .35, .52, .98, .75, .7, DARK)]),
    piece("filing", "Filing cabinet", "office", 45.0, 60.0, 70.0, "#9aa3ad", [
        box(0.0, 0.0, 0.0, 1.0, 1.0, 1.0, "main"), box(.04, .99, .52, .96, 1.0, .96, "tint"), box(.04, .99, .05, .96, 1.0, .48, "tint")]),

    piece("bathtub", "Bathtub", "bath", 170.0, 78.0, 58.0, WHITE, [
        box(0.0, 0.0, 0.0, 1.0, 1.0, .9, "main"), box(0.0, 0.0, .9, 1.0, .1, 1.0, "main"), box(0.0, .9, .9, 1.0, 1.0, 1.0, "main"),
        box(0.0, 0.0, .9, .07, 1.0, 1.0, "main"), box(.93, 0.0, .9, 1.0, 1.0, 1.0, "main"), box(.07, .1, .88, .93, .9, .9, "#cfe6ee"),
        cyl(.02, .44, 1.0, .06, .56, 1.25, METAL)]),
    piece("shower", "Shower", "bath", 90.0, 90.0, 200.0, GLASS, [
        box(0.0, 0.0, 0.0, 1.0, 1.0, .04, WHITE), box(0.0, .98, .04, 1.0, 1.0, 1.0, GLASS), box(.98, 0.0, .04, 1.0, 1.0, 1.0, GLASS),
        cyl(.45, .03, .9, .55, .1, .98, METAL), box(.48, .0, .5, .52, .04, .95, METAL)]),
    piece("toilet", "Toilet", "bath", 40.0, 68.0, 80.0, WHITE, [
        box(.05, 0.0, .48, .95, .26, 1.0, "main"), ball(.06, .2, .05, .94, 1.0, .52, "main"), cyl(.2, .3, 0.0, .8, .8, .2, "main"),
        cyl(.05, .25, .5, .95, 1.0, .53, "tint"), box(.4, .06, 1.0, .6, .16, 1.02, METAL)]),
    piece("vanity", "Vanity", "bath", 90.0, 50.0, 85.0, "#6b7f8c", [
        box(0.0, .05, .1, 1.0, 1.0, .92, "main"), box(0.0, 0.0, .92, 1.0, 1.0, 1.0, WHITE), cyl(.25, .25, .94, .75, .85, 1.02, "#e8eef0"),
        cyl(.47, .05, 1.0, .53, .14, 1.22, METAL), box(.1, 0.0, 1.25, .9, .03, 2.0, "#cde3ea"), box(.02, .99, .14, .98, 1.0, .88, "tint")]),
    piece("towel-rail", "Towel rail", "bath", 60.0, 12.0, 90.0, METAL, [
        box(0.0, 0.0, 0.0, .06, 1.0, 1.0, "main"), box(.94, 0.0, 0.0, 1.0, 1.0, 1.0, "main"),
        *[for (i in 0 to 5) box(.06, .3, .08 + i * .17, .94, .7, .12 + i * .17, "main")], box(.1, .7, .3, .9, 1.0, .9, "#f0d9c4")],
        {elev: 40.0}),

    piece("floor-lamp", "Floor lamp", "decor", 42.0, 42.0, 165.0, "#efe6d2", [
        cyl(.2, .2, 0.0, .8, .8, .03, DARK), cyl(.47, .47, .03, .53, .53, .8, DARK), cone(0.0, 0.0, .74, 1.0, 1.0, 1.0, "main")],
        {light: {color: "#ffdcae", intensity: .8, at: .82}}),
    piece("table-lamp", "Table lamp", "decor", 32.0, 32.0, 50.0, "#f3ead8", [
        ball(.15, .15, 0.0, .85, .85, .45, "#5f7d8c"), cyl(.47, .47, .4, .53, .53, .6, METAL), cone(0.0, 0.0, .55, 1.0, 1.0, 1.0, "main")],
        {elev: 55.0, light: {color: "#ffdcae", intensity: .55, at: .7}}),
    piece("ceiling-light", "Ceiling light", "decor", 50.0, 50.0, 14.0, WHITE, [
        cyl(.4, .4, .6, .6, .6, 1.0, "main"), ball(0.0, 0.0, 0.0, 1.0, 1.0, .9, "#fff8e8")],
        {elev: 246.0, light: {color: "#fff1dc", intensity: 1.0, at: 0.0}}),
    piece("plant", "Potted plant", "decor", 55.0, 55.0, 130.0, "#4f8a4b", [
        cyl(.22, .22, 0.0, .78, .78, .28, "#b5643c"), cyl(.47, .47, .25, .53, .53, .55, "#6b4e33"),
        ball(.05, .1, .45, .75, .8, .85, "main"), ball(.3, .2, .55, .95, .85, .95, "shade"), ball(.15, .3, .7, .7, .9, 1.0, "tint")]),
    piece("small-plant", "Houseplant", "decor", 30.0, 30.0, 40.0, "#5d9a54", [
        cyl(.2, .2, 0.0, .8, .8, .4, WHITE), ball(0.0, 0.0, .3, 1.0, 1.0, 1.0, "main")], {elev: 75.0}),
    piece("picture", "Framed print", "decor", 80.0, 4.0, 60.0, "#2d2a26", [
        box(0.0, 0.0, 0.0, 1.0, 1.0, 1.0, "main"), box(.07, .9, .09, .93, 1.05, .91, "#f3efe7"), box(.2, 1.0, .25, .55, 1.08, .7, "#d98c5f"),
        box(.45, 1.0, .35, .8, 1.09, .78, "#5d8aa8")], {elev: 140.0}),
    piece("mirror", "Wall mirror", "decor", 60.0, 4.0, 100.0, "#c9a86a", [
        box(0.0, 0.0, 0.0, 1.0, 1.0, 1.0, "main"), box(.06, .9, .04, .94, 1.05, .96, "#d7e9f0")], {elev: 110.0}),
    piece("curtains", "Curtains", "decor", 220.0, 14.0, 245.0, "#d8cbb4", [
        box(0.0, .2, .96, 1.0, .4, .99, METAL), box(0.0, .4, 0.0, .2, 1.0, .96, "main"), box(.8, .4, 0.0, 1.0, 1.0, .96, "main"),
        box(.04, .5, 0.0, .16, .9, .95, "shade"), box(.84, .5, 0.0, .96, .9, .95, "shade")])]

let BY_KEY = map([for (it in ITEMS) (it.key, it)])
pub fn item_of(key) => BY_KEY[key] or ITEMS[0]
pub fn is_opening(key) => item_of(key).opening != null

// ---- finishes -------------------------------------------------------------------

pub let FLOORS = [{key: "oak", name: "Oak boards", color: "#c9a376", line: "#b08a5f"},
    {key: "walnut", name: "Walnut", color: "#7f5a3e", line: "#6a4930"},
    {key: "ash", name: "Pale ash", color: "#e2cfad", line: "#cdb791"},
    {key: "herringbone", name: "Herringbone", color: "#b98d5f", line: "#9f7548"},
    {key: "tile", name: "Grey tile", color: "#c7cacc", line: "#aeb2b5"},
    {key: "marble", name: "Marble", color: "#ece9e3", line: "#d6d1c8"},
    {key: "terracotta", name: "Terracotta", color: "#c47a55", line: "#a9623f"},
    {key: "slate", name: "Slate", color: "#545a60", line: "#454a4f"},
    {key: "carpet", name: "Wool carpet", color: "#b7aa98", line: "#a99b88"},
    {key: "blue-carpet", name: "Blue carpet", color: "#7586a6", line: "#66779a"},
    {key: "concrete", name: "Polished concrete", color: "#aeaca6", line: "#9a9892"},
    {key: "cork", name: "Cork", color: "#c49a6a", line: "#b08757"}]
let FLOOR_BY_KEY = map([for (f in FLOORS) (f.key, f)])
pub fn floor_of(key) => FLOOR_BY_KEY[key] or FLOORS[0]

pub let PAINTS = ["#f6f3ec", "#ffffff", "#e9e2d4", "#d9cbb4", "#c9d6cf", "#a9bfb4", "#cbd8e3", "#9fb3c8",
    "#e8c9c1", "#d9a38f", "#f2dfa7", "#4d5b6a", "#2f3a3f", "#7d8471"]
pub let FINISHES = ["#f5f4f0", "#ece7df", "#d9cbb4", "#a87b51", "#7f5a3e", "#5b4636", "#34363b", "#9aa3ad",
    "#7d8ca3", "#3f5a63", "#2f3b45", "#4f8a4b", "#c4704f", "#d9a19a", "#c9a24a", "#8fa3b8", "#a39b8e", "#b9a089"]
