// The template catalog: occasions, their verses, and twenty designed cards.
// A template lays out its front; `assemble` adds the inside spread and back
// from the template's inside style, so every card opens with a verse.
import A: .mod_art

pub let FORMATS = [
    {key: "portrait", name: "Portrait", size: "5 × 7 in", w: 500, h: 700},
    {key: "landscape", name: "Landscape", size: "7 × 5 in", w: 700, h: 500},
    {key: "square", name: "Square", size: "6 × 6 in", w: 600, h: 600}]
pub fn format_of(key) => (let hit = [for (f in FORMATS where f.key == key) f], if (len(hit) == 0) FORMATS[0] else hit[0])

// the four pages of a folded card, in reading order
pub let PAGES = [{key: "front", name: "Front"}, {key: "left", name: "Inside left"},
    {key: "right", name: "Inside right"}, {key: "back", name: "Back"}]
pub fn page_name(key) => [for (p in PAGES where p.key == key) p.name][0]

pub let OCCASIONS = [
    {key: "birthday", name: "Birthday", motif: "balloon"},
    {key: "thanks", name: "Thank you", motif: "sprig"},
    {key: "holidays", name: "Holidays", motif: "snowflake"},
    {key: "love", name: "Love", motif: "heart"},
    {key: "wedding", name: "Wedding", motif: "rings"},
    {key: "baby", name: "New baby", motif: "footprints"},
    {key: "congrats", name: "Congratulations", motif: "star"},
    {key: "getwell", name: "Get well", motif: "sun"},
    {key: "sympathy", name: "Sympathy", motif: "leaf"},
    {key: "hello", name: "Just because", motif: "bird"}]
pub fn occasion_of(key) => (let hit = [for (o in OCCASIONS where o.key == key) o], if (len(hit) == 0) OCCASIONS[0] else hit[0])

pub let VERSES = {
    birthday: ["Wishing you a day as bright\nas the smile you give everyone else,\nand a year full of good surprises.",
        "Here's to cake for breakfast,\nlaughter till midnight,\nand every wish coming true.",
        "Another trip around the sun,\nand the world is still lucky\nto have you in it.",
        "May this year bring you\nnew adventures, old friends,\nand plenty of reasons to celebrate."],
    thanks: ["Your kindness meant more\nthan words can say.\nThank you, from the bottom of my heart.",
        "Thank you for being\nexactly the kind of person\nthe world needs more of.",
        "A small note\nfor a big thank-you.",
        "Grateful for your help,\nyour time,\nand your generous heart."],
    holidays: ["Wishing you warm lights,\nquiet snow, and the company\nof everyone you love.",
        "May your holidays sparkle\nand the new year shine\neven brighter.",
        "Peace, joy and good cheer\nto you and yours,\nthis season and all year long.",
        "Here's to cozy evenings,\nfull tables\nand happy hearts."],
    love: ["Every day with you\nis my favourite day.",
        "You are my today\nand all of my tomorrows.",
        "Of all the places I could be,\nmy favourite is next to you.",
        "Loving you is the easiest thing\nI have ever done."],
    wedding: ["Wishing you a lifetime\nof love, laughter\nand happily ever after.",
        "Two hearts, one home,\nand a whole life of adventures ahead.",
        "May your love grow deeper\nwith every passing year.",
        "Here's to the beautiful story\nyou are writing together."],
    baby: ["Welcome to the world, little one.\nYou are already so loved.",
        "Tiny fingers, tiny toes,\nand a love that only grows.",
        "Congratulations on your\nsweet new arrival!",
        "Wishing your family\nsleepy nights, gentle days\nand endless cuddles."],
    congrats: ["You worked for it,\nyou earned it,\nnow celebrate it!",
        "So proud of you,\nand so excited for\nwhat comes next.",
        "Here's to your success.\nMay it be the first of many.",
        "Hard work, big dreams\nand a well-deserved win.\nCongratulations!"],
    getwell: ["Sending sunshine,\nwarm soup and get-well wishes\nyour way.",
        "Rest up, take it easy\nand feel better soon.\nWe miss you!",
        "Wishing you a speedy recovery\nand brighter days ahead.",
        "Thinking of you\nand hoping each day\nbrings you closer to well."],
    sympathy: ["With heartfelt sympathy\nand warm memories\nshared with you.",
        "May the love of those around you\nbring comfort and peace\nin the days ahead.",
        "Holding you close\nin my thoughts and in my heart.",
        "Those we love\nnever truly leave us."],
    hello: ["Just a little note\nto say I'm thinking of you.",
        "No reason, no occasion.\nJust wanted you to know\nyou're wonderful.",
        "Wish you were here!\nThe view is lovely,\nbut it would be better with you.",
        "Hello from me,\nwith a hug attached."]
}

// ---- item constructors, in card units (1/100 in) --------------------------

pub fn text(s, x, y, w, size, font, color, more = {}) => {kind: "text", x: float(x), y: float(y), w: float(w), rot: 0.0,
    text: s, font: font, size: float(size), color: color, align: "center", bold: false, italic: false,
    spacing: 0.0, line: 1.2, shadow: false, opacity: 1.0, *:more}
pub fn art(key, x, y, w, color = null, more = {}) => {kind: "art", art: key, x: float(x), y: float(y), w: float(w),
    h: float(w) * A.ratio(key), rot: 0.0, color: if (color == null) A.art_of(key).ink else color,
    flip: false, opacity: 1.0, *:more}
pub fn photo(src, x, y, w, h, frame, more = {}) => {kind: "photo", src: src, x: float(x), y: float(y), w: float(w),
    h: float(h), rot: 0.0, frame: frame, zoom: 1.0, opacity: 1.0, *:more}
pub fn page(paper, items = [], more = {}) => {paper: paper, pattern: "none", pattern_color: "#ffffff", pattern_alpha: .25,
    border: "none", border_color: "#a16207", items: items, *:more}

// ---- the catalog --------------------------------------------------------------
// Photos name a sample by key; `assemble` resolves it against the demo's assets.

pub let TEMPLATES = [
    {key: "balloon-party", name: "Balloon party", occasion: "birthday", format: "portrait",
     front: page("sunshine", [
        art("balloons", 150, 46, 200),
        text("Happy\nBirthday", 20, 318, 460, 84, "roundhand", "#9d174d", {line: 1.0}),
        text("MAKE A WISH", 100, 540, 300, 16, "avenir", "#92400e", {spacing: 6.0, bold: true}),
        art("confetti", 330, 582, 140), art("confetti", 30, 582, 140, null, {flip: true})],
        {pattern: "confetti", pattern_color: "#f472b6", pattern_alpha: .16}),
     inside: {paper: "cream", font: "georgia", color: "#7c2d12", sign: "bradley"}},
    {key: "candlelight-cake", name: "Night of candles", occasion: "birthday", format: "portrait",
     front: page("navy", [
        text("it's your", 50, 84, 400, 40, "chancery", "#f8e7b9"),
        text("BIRTHDAY", 20, 138, 460, 70, "didot", "#f5d27a", {spacing: 6.0}),
        art("cake", 140, 266, 220),
        art("flourish", 120, 520, 260, "#d4a017"),
        text("celebrate every moment", 50, 570, 400, 22, "baskerville", "#f8e7b9", {italic: true})],
        {pattern: "stars", pattern_color: "#f5b301", pattern_alpha: .35, border: "corners", border_color: "#d4a017"}),
     inside: {paper: "cream", font: "baskerville", color: "#1e2c56", sign: "roundhand"}},
    {key: "mountain-birthday", name: "Summit birthday", occasion: "birthday", format: "portrait",
     front: page("peach", [
        photo("mountains", 110, 70, 280, 280, "circle"),
        art("sparkle", 392, 56, 64), art("sparkle", 46, 318, 44),
        text("Happy Birthday", 20, 384, 460, 62, "roundhand", "#9a3412"),
        text("to someone who makes\nevery view better", 70, 486, 360, 20, "avenir", "#7c2d12", {line: 1.4})],
        {border: "stitch", border_color: "#f97316"}),
     inside: {paper: "white", font: "avenir", color: "#7c2d12", sign: "roundhand"}},
    {key: "botanical-thanks", name: "Botanical", occasion: "thanks", format: "portrait",
     front: page("cream", [
        art("sprig", 150, 80, 70, null, {rot: -24.0}), art("sprig", 280, 80, 70, null, {rot: 24.0, flip: true}),
        text("Thank you", 20, 262, 460, 90, "roundhand", "#2f6b3a"),
        art("flourish", 140, 404, 220, "#5a8c5c"),
        text("for everything you do", 50, 452, 400, 24, "baskerville", "#3f3a33", {italic: true})],
        {border: "line", border_color: "#5a8c5c"}),
     inside: {paper: "cream", font: "baskerville", color: "#2f3a2f", sign: "roundhand"}},
    {key: "bold-thanks", name: "Big and bold", occasion: "thanks", format: "portrait",
     front: page("coral", [
        text("THANK\nYOU", 30, 160, 440, 108, "futura", "#ffffff", {bold: true, line: .95, spacing: 4.0}),
        text("so much!", 40, 470, 260, 46, "marker", "#fff7ed", {rot: -6.0}),
        art("hearts", 330, 470, 120, "#fecdd3")],
        {pattern: "dots", pattern_color: "#ffffff", pattern_alpha: .3}),
     inside: {paper: "white", font: "avenir", color: "#be123c", sign: "marker"}},
    {key: "evergreen", name: "Evergreen", occasion: "holidays", format: "portrait",
     front: page("forest", [
        art("tree", 165, 76, 170),
        text("Merry & Bright", 20, 340, 460, 66, "roundhand", "#f6e7b0"),
        text("SEASON'S GREETINGS", 50, 452, 400, 18, "copperplate", "#d4a017", {spacing: 4.0}),
        art("ornament", 70, 540, 60, null, {rot: -10.0}), art("ornament", 370, 540, 60, "#d4a017", {rot: 10.0})],
        {pattern: "snow", pattern_color: "#ffffff", pattern_alpha: .3, border: "double", border_color: "#d4a017"}),
     inside: {paper: "cream", font: "georgia", color: "#22543b", sign: "roundhand"}},
    {key: "snowfall", name: "Snowfall", occasion: "holidays", format: "portrait",
     front: page("sky", [
        art("snowflake", 190, 90, 120, "#3b82f6"),
        art("snowflake", 76, 56, 56, "#93c5fd"), art("snowflake", 372, 196, 46, "#93c5fd"),
        text("Let it snow", 20, 286, 460, 72, "chancery", "#1e3a8a"),
        text("warm wishes for a cozy season", 60, 400, 380, 20, "avenir", "#1e40af"),
        art("tree", 96, 520, 70, "#2f6b3a"), art("tree", 340, 540, 56, "#3f7d4a")],
        {pattern: "snow", pattern_color: "#ffffff", pattern_alpha: .9}),
     inside: {paper: "white", font: "avenir", color: "#1e3a8a", sign: "chancery"}},
    {key: "sweetheart", name: "Sweetheart", occasion: "love", format: "portrait",
     front: page("blush", [
        art("heart", 150, 104, 200),
        text("Be mine", 20, 326, 460, 88, "roundhand", "#9d174d"),
        text("always & forever", 50, 452, 400, 26, "baskerville", "#be185d", {italic: true})],
        {pattern: "hearts", pattern_color: "#f472b6", pattern_alpha: .12}),
     inside: {paper: "blush", font: "baskerville", color: "#9d174d", sign: "roundhand"}},
    {key: "love-letter", name: "Love letter", occasion: "love", format: "square",
     front: page("berry", [
        art("envelope", 210, 84, 180, "#f9a8d4"),
        text("You & Me", 30, 256, 540, 104, "savoye", "#ffe4ec"),
        text("MY FAVOURITE STORY", 100, 424, 400, 18, "avenir", "#fbcfe8", {spacing: 4.0})],
        {border: "dots", border_color: "#fbcfe8"}),
     inside: {paper: "blush", font: "georgia", color: "#831843", sign: "savoye"}},
    {key: "two-hearts", name: "Golden rings", occasion: "wedding", format: "portrait",
     front: page("white", [
        art("rings", 185, 116, 130),
        text("Mr & Mrs", 20, 262, 460, 70, "didot", "#3f3a33"),
        art("flourish", 140, 368, 220, "#c9a227"),
        text("on your wedding day", 40, 404, 420, 40, "roundhand", "#a16207"),
        art("leaf", 64, 556, 40, "#a3b18a", {rot: -30.0}), art("leaf", 396, 556, 40, "#a3b18a", {rot: 30.0})],
        {border: "double", border_color: "#c9a227"}),
     inside: {paper: "white", font: "didot", color: "#3f3a33", sign: "roundhand"}},
    {key: "garden-wedding", name: "Garden party", occasion: "wedding", format: "portrait",
     front: page("cream", [
        art("flower", 84, 84, 116, "#f9a8d4"), art("flower", 304, 100, 96, "#fbcfe8"),
        art("sprig", 214, 54, 72, "#6b8f71"),
        text("With love on\nyour wedding", 40, 290, 420, 50, "chancery", "#7c2d12", {line: 1.15}),
        text("two hearts, one home", 50, 470, 400, 24, "baskerville", "#57534e", {italic: true})],
        {border: "corners", border_color: "#a16207"}),
     inside: {paper: "cream", font: "chancery", color: "#7c2d12", sign: "chancery"}},
    {key: "little-one", name: "Little one", occasion: "baby", format: "portrait",
     front: page("mint", [
        art("footprints", 165, 86, 170, "#60a5fa"),
        text("Welcome,\nlittle one", 30, 296, 440, 64, "noteworthy", "#166534", {line: 1.1}),
        art("star", 70, 520, 50, "#fde68a"), art("star", 380, 540, 40, "#fde68a"), art("star", 230, 584, 34, "#fcd34d")],
        {pattern: "dots", pattern_color: "#16a34a", pattern_alpha: .14}),
     inside: {paper: "white", font: "noteworthy", color: "#166534", sign: "noteworthy"}},
    {key: "bear-hugs", name: "Bear hugs", occasion: "baby", format: "portrait",
     front: page("lavender", [
        art("moon", 370, 40, 84, "#facc15"),
        art("bear", 160, 112, 180),
        text("Oh baby!", 20, 330, 460, 80, "marker", "#5b21b6"),
        text("big hugs for a tiny new arrival", 60, 446, 380, 20, "avenir", "#6d28d9")],
        {pattern: "stars", pattern_color: "#a78bfa", pattern_alpha: .3}),
     inside: {paper: "white", font: "chalk", color: "#5b21b6", sign: "marker"}},
    {key: "cheers", name: "Cheers", occasion: "congrats", format: "landscape",
     front: page("night", [
        art("cheers", 56, 104, 210, "#f5c542"),
        text("Congratulations!", 250, 140, 430, 64, "roundhand", "#fde68a"),
        text("YOU DID IT", 300, 256, 330, 22, "futura", "#ffffff", {spacing: 8.0}),
        art("sparkle", 610, 40, 50), art("sparkle", 292, 330, 36), art("confetti", 470, 336, 180)],
        {pattern: "stars", pattern_color: "#fde68a", pattern_alpha: .28}),
     inside: {paper: "cream", font: "georgia", color: "#2c1c60", sign: "roundhand"}},
    {key: "star-turn", name: "Star turn", occasion: "congrats", format: "portrait",
     front: page("sunshine", [
        art("star", 150, 80, 200),
        text("Way to go!", 20, 322, 460, 82, "marker", "#b45309", {rot: -4.0}),
        text("you're a star", 50, 454, 400, 32, "noteworthy", "#92400e")],
        {border: "dots", border_color: "#f5b301"}),
     inside: {paper: "white", font: "noteworthy", color: "#92400e", sign: "marker"}},
    {key: "sunshine", name: "Sunshine", occasion: "getwell", format: "portrait",
     front: page("sky", [
        art("sun", 300, 56, 140),
        art("rainbow", 50, 170, 260),
        text("Get well\nsoon", 30, 338, 440, 78, "bradley", "#1e3a8a", {line: 1.05}),
        text("sending sunshine your way", 60, 560, 380, 20, "avenir", "#1d4ed8")],
        {border: "stitch", border_color: "#60a5fa"}),
     inside: {paper: "white", font: "bradley", color: "#1e3a8a", sign: "bradley"}},
    {key: "quiet-sympathy", name: "Quiet leaves", occasion: "sympathy", format: "portrait",
     front: page("dove", [
        art("sprig", 200, 88, 100, "#7c9a92"),
        text("With deepest\nsympathy", 40, 326, 420, 50, "baskerville", "#334155", {italic: true, line: 1.15}),
        art("flourish", 150, 482, 200, "#94a3b8")],
        {border: "line", border_color: "#94a3b8"}),
     inside: {paper: "white", font: "baskerville", color: "#334155", sign: "baskerville"}},
    {key: "candlelight", name: "Candlelight", occasion: "sympathy", format: "portrait",
     front: page("charcoal", [
        art("candle", 205, 110, 90),
        text("Thinking of you", 20, 384, 460, 62, "roundhand", "#f5ecd7"),
        text("and holding you close", 50, 484, 400, 22, "baskerville", "#d1d5db", {italic: true})]),
     inside: {paper: "dove", font: "baskerville", color: "#334155", sign: "roundhand"}},
    {key: "wish-you-were-here", name: "Postcard", occasion: "hello", format: "landscape",
     front: page("white", [
        photo("coast", 50, 60, 300, 300, "polaroid", {rot: -4.0}),
        text("Wish you\nwere here", 370, 112, 300, 64, "roundhand", "#0e7490", {line: 1.05}),
        text("a little hello from the coast", 380, 300, 280, 18, "typewriter", "#475569"),
        art("bird", 566, 380, 90, "#38bdf8")],
        {pattern: "grid", pattern_color: "#0ea5e9", pattern_alpha: .12}),
     inside: {paper: "white", font: "typewriter", color: "#334155", sign: "roundhand"}},
    {key: "hello-sunshine", name: "Hello, sunshine", occasion: "hello", format: "portrait",
     front: page("peach", [
        art("flower", 60, 80, 120), art("flower", 320, 64, 120, "#fb923c"), art("tulip", 214, 70, 74),
        text("Hello,\nsunshine", 30, 296, 440, 76, "chancery", "#9a3412", {line: 1.05}),
        text("just thinking of you", 60, 506, 380, 20, "avenir", "#c2410c")],
        {pattern: "polka", pattern_color: "#fb923c", pattern_alpha: .1}),
     inside: {paper: "white", font: "avenir", color: "#9a3412", sign: "chancery"}}
]
pub fn template_of(key) => (let hit = [for (t in TEMPLATES where t.key == key) t], if (len(hit) == 0) TEMPLATES[0] else hit[0])

// ---- assembling a full card -----------------------------------------------------

// sample photos ship with the Photo Studio demo; `photos` maps their keys to URLs
pub fn resolve_photo(item, photos) => if (item.kind == "photo" and photos[item.src] != null) {*:item, src: photos[item.src]} else item
fn with_ids(items, prefix, photos) => [for (i, it in items) {*:resolve_photo(it, photos), id: prefix ++ string(i + 1)}]

// the verse spans most of the page; its size keeps a 36-character line on one line
pub fn verse_box(f) => {x: f.w * .08, y: f.h * .24, w: f.w * .84, size: min(26.0, f.w * .84 / 17.5)}
pub fn inside_pages(t, f, verse) => (let ins = t.inside, let box = verse_box(f), let motif = occasion_of(t.occasion).motif,
    let mw = min(f.w, f.h) * .2, let ink = A.paper_of(ins.paper).ink, {
    left: {*:page(ins.paper, with_ids([art(motif, (f.w - mw) / 2, f.h * .4, mw, null, {opacity: .9})], "l", {})),
        border: t.front.border, border_color: t.front.border_color},
    right: {*:page(ins.paper, with_ids([
        text(verse, box.x, box.y, box.w, box.size, ins.font, ins.color, {line: 1.45}),
        text("With love,\nYour name", box.x, f.h * .64, box.w, 34, ins.sign, ins.color, {line: 1.25})], "r", {})),
        border: t.front.border, border_color: t.front.border_color},
    back: page(ins.paper, with_ids([
        art(motif, (f.w - mw * .6) / 2, f.h * .36, mw * .6, null, {opacity: .55}),
        art("lambda", f.w / 2 - 16, f.h - 128, 32, ink, {opacity: .7}),
        text("MADE WITH LAMBDA GREETINGS", f.w * .1, f.h - 84, f.w * .8, 11, "avenir", ink, {spacing: 3.0, opacity: .7})], "b", {}))})

pub fn assemble(t, photos) => (let f = format_of(t.format), let inside = inside_pages(t, f, VERSES[t.occasion][0]),
    {version: 1, title: t.name, template: t.key, occasion: t.occasion, format: t.format,
     pages: {front: {*:t.front, items: with_ids(t.front.items, "f", photos)}, left: inside.left, right: inside.right, back: inside.back}})

// a fresh card with nothing on it but paper
pub fn blank(fmt) => {version: 1, title: "Untitled card", template: "", occasion: "hello", format: fmt,
    pages: {front: page("white"), left: page("white"), right: page("white"), back: page("white")}}
