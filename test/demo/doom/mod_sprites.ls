// Sheet rows and timing follow cssDOOM sprites.js and sprites.css.
pub fn fuzz_seed(elapsed: float) => floor(elapsed * 25) % 10
pub fn definition(actor_type, visuals, images) {
    let sheet = images.sheets[visuals.THING_NAMES[string(actor_type)]]
    if (sheet != null) sheet else
        {path: "assets/sprites/" ++ visuals.THING_SPRITES[string(actor_type)] ++ ".png", frames: 1}
}
pub fn heading(actor, camera) {
    let full = 2 * math.pi
    let relative = ((math.atan2(camera.y - actor.y, camera.x - actor.x) - actor.facing) % full + full) % full
    let rotation = int(floor((relative + math.pi / 8) / (math.pi / 4))) % 8 + 1
    {row: if (rotation <= 5) rotation - 1 else 9 - rotation, mirror: if (rotation <= 5) 1 else -1}
}
pub fn pose(actor, camera, sheet, images, elapsed) {
    let layout = images.layouts[string(actor.type)]
    let dead = actor.dead_at != null
    let attacking = not dead and actor.ai.state == "attacking" and layout.atkRow >= 0
    let rotated = if (layout != null and not dead and not attacking) heading(actor, camera) else {row: 0, mirror: 1}
    let frames = if (dead and layout != null) layout.dieFrames else if (attacking) layout.atkFrames else sheet.frames
    let duration = if (dead) frames * 0.1 else if (attacking) 0.6 else frames * 0.25
    let stop = dead or attacking
    {row: if (dead and layout != null) layout.dieRow else if (attacking) layout.atkRow else rotated.row,
        mirror: rotated.mirror, frames: frames, duration: duration,
        animation: if (frames <= 1) "none" else (if (stop) "doom-sprite-stop" else "doom-sprite-cycle") ++
            " " ++ string(duration) ++ "s steps(" ++ string(max(1, if (stop) frames - 1 else frames)) ++ ") " ++
            (if (stop) "forwards" else "infinite"),
        end_x: if (sheet.w == null) 0 else sheet.w * (if (stop) 1 - frames else -frames),
        visible: (not actor.collected or dead) and not (actor.type == 2035 and dead and elapsed - actor.dead_at >= duration)}
}

pub fn transients(game) => [
    *[for (projectile in game.projectiles) {
        key: "projectile-" ++ string(projectile.id), kind: "projectile", position: projectile,
        path: "assets/sprites/" ++ projectile.sprite ++ ".png", w: projectile.size, h: projectile.size,
        columns: 1, animation: "doom-projectile-move " ++ string(projectile.lifetime) ++ "s linear both, doom-projectile-glow .15s ease-in-out infinite alternate",
        elapsed: game.time - projectile.born_at, projectile: projectile}],
    *[for (particle in game.particles or [], let kind = particle.kind) {
        key: "effect-" ++ string(particle.id), kind: kind, position: particle.position,
        path: "assets/sprites/" ++ (if (kind == "puff") "PUFF_SHEET" else if (kind == "explosion") "BAL1C0" else "TFOGA0") ++ ".png",
        w: if (kind == "puff") 15 else if (kind == "explosion") 50 else 42,
        h: if (kind == "puff") 15 else if (kind == "explosion") 44 else 56,
        columns: if (kind == "puff") 4 else 1,
        animation: if (kind == "puff") "doom-puff .2s steps(3) forwards" else if (kind == "explosion")
            "doom-explosion .3s steps(1) forwards" else "doom-fog 1.714s steps(1) forwards",
        elapsed: game.time - particle.at, projectile: null}]
]
