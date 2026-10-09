// The session retains only live transient wrappers. Expiry follows simulation
// deadlines, so pausing and catch-up delivery cannot orphan a particle node.
import dom
import sprites: .mod_sprites
import scene: .mod_scene

fn px(value) => scene.fmt(value) ++ "px"
fn translate(position) => px(position.x) ++ " " ++ px(-position.z) ++ " " ++ px(-position.y)
fn projectile_properties(projectile) => "--start-x:" ++ px(projectile.start_x) ++ ";--start-y:" ++ px(-projectile.start_z) ++
    ";--start-z:" ++ px(-projectile.start_y) ++ ";--end-x:" ++ px(projectile.start_x + projectile.dx * projectile.speed * projectile.lifetime) ++
    ";--end-y:" ++ px(-projectile.start_z - projectile.dz * projectile.speed * projectile.lifetime) ++
    ";--end-z:" ++ px(-projectile.start_y - projectile.dy * projectile.speed * projectile.lifetime) ++ ";"
fn authored_style(entry, angle, base) => "width:" ++ px(entry.w) ++ ";height:" ++ px(entry.h) ++
    ";left:" ++ px(-entry.w / 2) ++ ";top:" ++ px(if (entry.kind == "fog") -entry.h else -entry.h / 2) ++
    ";background-image:" ++ scene.image_url(base, entry.path) ++ ";background-size:" ++ px(entry.w * entry.columns) ++ " " ++ px(entry.h) ++
    ";translate:" ++ translate(entry.position) ++ ";rotate:y " ++ scene.fmt(angle) ++ "rad;animation:" ++ entry.animation ++
    ";animation-delay:" ++ scene.fmt(-entry.elapsed) ++ "s;" ++
    (if (entry.projectile == null) "" else projectile_properties(entry.projectile))
pub pn synchronize(camera, previous, game, angle, base) any^ {
    let desired = sprites.transients(game)
    for (old in previous where not any([for (entry in desired) entry.key == old.key])) dom.remove(old.node)
    return [for (entry in desired) {
        let retained = [for (old in previous where old.key == entry.key) old][0]
        let node = if (retained != null) retained.node else dom.create_element(dom.owner_document(camera), "div")
        if (node == null) raise error("DOOM: could not allocate transient " ++ entry.key)
        if (retained == null) {
            dom.set_attribute(node, "id", entry.key)
            dom.set_attribute(node, "class", "transient " ++ entry.kind)
            dom.set_attribute(node, "style", authored_style(entry, angle, base))
            dom.append_child(camera, node)
        } else if (retained.angle != angle) {
            if (not dom.presentation_style_set_property(node, "rotate", "y " ++ scene.fmt(angle) ++ "rad"))
                raise error("DOOM: transient billboard update rejected")
        }
        {key: entry.key, node: node, angle: angle}
    }]
}
