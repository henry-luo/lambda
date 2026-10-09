import sprites: ~~.mod_sprites
import geo: ~~.mod_geometry
let images = input("test/demo/doom/data/images.json", 'json')^
let visuals = input("test/demo/doom/data/visuals.json", 'json')^
let marine = {x: 0, y: 0, facing: 0, type: 3004, ai: {state: "idle"}, dead_at: null, collected: false}
let sheet = sprites.definition(marine.type, visuals, images)
let directions = [for (i in 0 to 7, let angle = i * math.pi / 4)
    sprites.heading(marine, {x: math.cos(angle), y: math.sin(angle)})]
let attack = sprites.pose({*: marine, ai: {state: "attacking"}}, {x: 0, y: -10}, sheet, images, 1)
let dead = sprites.pose({*: marine, dead_at: 1, collected: true}, {x: 0, y: -10}, sheet, images, 1.2)
let barrel = {*: marine, type: 2035, ai: null, dead_at: 1}
let barrel_sheet = sprites.definition(2035, visuals, images)
let pickup = sprites.definition(2014, visuals, images);
{
    rotations: directions,
    negative_angle: sprites.heading({*: marine, facing: 2 * math.pi}, {x: 1, y: 0}),
    attack: [attack.row, attack.mirror, attack.frames, attack.duration, attack.end_x],
    death: [dead.row, dead.mirror, dead.frames, dead.duration, dead.end_x, dead.visible],
    barrel: [sprites.pose(barrel, marine, barrel_sheet, images, 1.49).visible,
        sprites.pose(barrel, marine, barrel_sheet, images, 1.5).visible],
    pickup: [pickup.w, pickup.h, pickup.frames, pickup.cols],
    player_sheet: [images.sheets.player.w, images.sheets.player.h, images.sheets.player.rows],
    fuzz: [for (elapsed in [0.0, 0.04, 0.2, 0.39, 0.4, 0.6]) sprites.fuzz_seed(elapsed)],
    static_sprite: sprites.definition(2028, visuals, images).frames
}
