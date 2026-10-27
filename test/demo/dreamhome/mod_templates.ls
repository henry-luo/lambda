// Starter homes for the gallery. Each is drawn on wall centre lines: walls
// first (outer 25 cm, inner 12 cm), then rooms on those lines, then doors and
// windows by wall and distance along it, then furniture by centre and angle
// (0° puts a piece's back to the north; 90° turns it clockwise, back to the east).
import C: .mod_catalog
import M: .mod_model

fn walls_of(specs) => [for (i, s in specs) M.wall("w" ++ string(i + 1), float(s[0]), float(s[1]), float(s[2]), float(s[3]), float(s[4]))]
fn rooms_of(specs) => [for (i, s in specs) {id: "r" ++ string(i + 1), name: s[0], floor: s[1], pts: [for (p in s[2]) [float(p[0]), float(p[1])]]}]
// an opening is [key, wall, at, patch]; furniture is [key, x, y, rot, patch]
fn spec_item(i, s) => (let id = "i" ++ string(i + 1), let patch = if (len(s) > 4) s[4] else if (len(s) > 3 and s[3] is map) s[3] else {},
    if (C.is_opening(s[0])) M.piece(id, s[0], 0.0, 0.0, 0.0, {wall: s[1], at: float(s[2]), flip: false, *:patch})
    else M.piece(id, s[0], float(s[1]), float(s[2]), float(s[3]), patch))
fn build(key, title, blurb, walls, rooms, items) {
    let ws = walls_of(walls)
    let rs = rooms_of(rooms)
    let shell = {title: title, wall_height: 260.0, walls: ws, rooms: rs, items: [], seq: 1}
    let placed = [for (i, s in items) M.seat(shell, spec_item(i, s))];
    {key: key, title: title, blurb: blurb, home: {*:shell, items: placed, seq: len(ws) + len(rs) + len(placed) + 1}}
}

// ---- a two-bedroom city apartment ----------------------------------------------------

let APARTMENT = build("apartment", "City apartment", "Two bedrooms, an open kitchen and living room, 80 m²",
    [[0, 0, 1000, 0, 25], [1000, 0, 1000, 800, 25], [1000, 800, 0, 800, 25], [0, 800, 0, 0, 25],
     [600, 0, 600, 800, 12], [0, 500, 600, 500, 12], [600, 420, 1000, 420, 12], [300, 500, 300, 800, 12]],
    [["Living & kitchen", "oak", [[0, 0], [600, 0], [600, 500], [0, 500]]],
     ["Bedroom", "carpet", [[600, 0], [1000, 0], [1000, 420], [600, 420]]],
     ["Study", "ash", [[600, 420], [1000, 420], [1000, 800], [600, 800]]],
     ["Bathroom", "tile", [[0, 500], [300, 500], [300, 800], [0, 800]]],
     ["Hall", "herringbone", [[300, 500], [600, 500], [600, 800], [300, 800]]]],
    [["door", "w3", 550, {color: "#3f4a52"}], ["archway", "w6", 450], ["door", "w8", 150, {w: 80.0}], ["door", "w5", 700], ["door", "w5", 340],
     ["window", "w1", 150], ["wide-window", "w1", 430], ["window", "w1", 800], ["window", "w2", 620], ["window", "w4", 550],
     ["window", "w4", 150, {w: 70.0, h: 90.0, elev: 130.0}],
     // kitchen along the north wall, an island and stools
     ["fridge", 49, 48, 0], ["sink", 150, 43, 0], ["base-cabinet", 220, 43, 0], ["stove", 280, 44, 0], ["base-cabinet", 340, 43, 0],
     ["wall-cabinet", 280, 30, 0], ["wall-cabinet", 340, 30, 0], ["island", 225, 175, 0],
     ["bar-stool", 175, 245, 0], ["bar-stool", 225, 245, 0], ["bar-stool", 275, 245, 0],
     // dining by the window
     ["round-table", 480, 135, 0], ["chair", 480, 60, 0], ["chair", 480, 210, 180], ["chair", 405, 135, -90], ["chair", 555, 135, 90],
     ["pendant", 480, 135, 0],
     // the lounge
     ["rug", 215, 395, 90, {w: 190.0, d: 150.0}], ["sofa", 59, 395, -90], ["coffee-table", 200, 395, 90], ["armchair", 205, 300, 180],
     ["tv-stand", 560, 395, 90], ["tv", 568, 395, 90], ["floor-lamp", 45, 270, 0], ["plant", 555, 290, 0],
     ["ceiling-light", 300, 260, 0],
     // bedroom
     ["double-bed", 882, 215, 90], ["nightstand", 967, 100, 90], ["nightstand", 967, 330, 90], ["table-lamp", 967, 100, 90],
     ["table-lamp", 967, 330, 90], ["wardrobe", 636, 155, -90], ["plant", 650, 370, 0], ["ceiling-light", 800, 210, 0],
     ["picture", 640, 155, 90, {elev: 225.0, w: 60.0, h: 25.0}],
     // study
     ["single-bed", 885, 520, 90], ["nightstand", 967, 448, 90], ["desk", 790, 752, 180], ["office-chair", 790, 690, 0],
     ["bookshelf", 623, 560, -90], ["rug", 800, 600, 0, {w: 140.0, d: 100.0, color: "#8fa3b8"}], ["ceiling-light", 800, 610, 0],
     // bathroom
     ["bathtub", 105, 749, 180], ["toilet", 47, 575, -90], ["vanity", 200, 531, 0], ["towel-rail", 290, 720, 90],
     // hall
     ["sideboard", 450, 528, 0, {w: 120.0, color: "#ece7df"}], ["mirror", 450, 509, 0], ["small-plant", 410, 525, 0, {elev: 80.0}],
     ["ceiling-light", 450, 650, 0]])

// ---- a garden studio with an L-shaped main room -------------------------------------

let STUDIO = build("studio", "Garden studio", "One L-shaped room with a bath and garden doors, 42 m²",
    [[0, 0, 800, 0, 25], [800, 0, 800, 550, 25], [800, 550, 0, 550, 25], [0, 550, 0, 0, 25],
     [550, 0, 550, 300, 12], [550, 300, 800, 300, 12]],
    [["Studio", "ash", [[0, 0], [550, 0], [550, 300], [800, 300], [800, 550], [0, 550]]],
     ["Shower room", "slate", [[550, 0], [800, 0], [800, 300], [550, 300]]]],
    [["door", "w3", 120, {color: "#7d4f3a"}], ["slider", "w4", 300], ["door", "w6", 90, {w: 80.0}],
     ["wide-window", "w1", 260], ["window", "w1", 680, {w: 80.0, h: 80.0, elev: 140.0}], ["window", "w2", 430],
     // sleeping corner
     ["double-bed", 120, 120, 0, {color: "#c9b48f"}], ["nightstand", 13 + 24, 40, 0], ["table-lamp", 37, 40, 0],
     ["rug", 120, 250, 0, {w: 180.0, d: 90.0, color: "#a39b8e"}], ["wardrobe", 430, 42, 0, {w: 120.0}],
     // lounge by the garden doors
     ["corner-sofa", 170, 430, 180, {color: "#7d8ca3"}], ["coffee-table", 220, 410, 0], ["floor-lamp", 40, 510, 0],
     ["plant", 345, 505, 0],
     // kitchen and table
     ["base-cabinet", 770, 340, 90], ["sink", 770, 410, 90], ["stove", 769, 480, 90],
     ["fridge", 535, 501, 180], ["dining-table", 620, 410, 90, {w: 120.0, d: 75.0, color: "#e9e2d4"}],
     ["chair", 572, 380, -90], ["chair", 572, 440, -90], ["chair", 668, 380, 90, {color: "#c4704f"}],
     ["pendant", 620, 410, 0], ["ceiling-light", 260, 180, 0],
     // shower room
     ["shower", 750, 57, 0], ["toilet", 585, 60, 0], ["vanity", 660, 32, 0, {w: 70.0}], ["towel-rail", 790, 200, 90]])

pub let TEMPLATES = [APARTMENT, STUDIO,
    {key: "blank", title: "Blank plan", blurb: "Start from an empty site and draw your own rooms", home: M.blank("My home")}]
pub fn template_of(key) => [for (t in TEMPLATES where t.key == key) t][0] or TEMPLATES[0]
