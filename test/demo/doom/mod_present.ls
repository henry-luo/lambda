// Stable DOM wrappers live in the view session, never in pure game values.
import dom
import scene: .mod_scene
import world: .mod_world
import sprites: .mod_sprites
import weapons: .mod_weapons
import camera: .mod_camera

pub pn style(node, property, value) bool^ {
    if (node == null or not dom.presentation_style_set_property(node, property, value))
        raise error("DOOM presentation rejected " ++ property)
    return true
}
pub pn text(node, value) {
    let child = dom.first_child(node)
    if (child != null and dom.node_type(child) == 3) dom.set_node_value(child, value)
    else dom.set_text_content(node, value)
}
pub pn bind(owner, map_data, game) {
    return {camera: dom.get_element_by_id(owner, "scene"), viewport: dom.get_element_by_id(owner, "viewport"),
        sectors: [for (i in 0 to (len(map_data.sectors) - 1)) dom.get_element_by_id(owner, "sector-" ++ string(i))],
        planes: [for (plane in map_data.planes) dom.get_element_by_id(owner, plane.id)],
        actors: [for (entry in game.actors) {node: dom.get_element_by_id(owner, "actor-" ++ string(entry.id)),
            sprite: dom.get_element_by_id(owner, "sprite-" ++ string(entry.id))}],
        overlay: dom.get_element_by_id(owner, "overlay"), message: dom.get_element_by_id(owner, "overlay-message"),
        ammo: dom.get_element_by_id(owner, "ammo"), health: dom.get_element_by_id(owner, "health"),
        armor: dom.get_element_by_id(owner, "armor"), keys: dom.get_element_by_id(owner, "keys"),
        face: dom.get_element_by_id(owner, "face"), weapon: dom.get_element_by_id(owner, "weapon"),
        flash: dom.get_element_by_id(owner, "hurt-flash"), status: dom.get_element_by_id(owner, "doom-status"),
        player_marker: dom.get_element_by_id(owner, "player-marker"), player_sprite: dom.get_element_by_id(owner, "player-sprite"),
        player_fov: dom.get_element_by_id(owner, "player-fov"), fuzz_noise: dom.get_element_by_id(owner, "fuzz-noise"),
        has_fuzz: len([for (entry in game.actors where entry.type == 58) entry]) > 0}
}
fn height(game, mover) => if (mover == null) null else
    world.motion(if (mover.kind == "door") game.doors else if (mover.kind == "lift") game.lifts else game.crushers, mover.sector).height
pub pn paint(owner, map_data, before, game, handles, previous_visibility, rules, visuals, images, base, write = style) any^ {
    let viewpoint = camera.pose(game)
    let previous_viewpoint = if (before == null) null else camera.pose(before)
    let pose_changed = viewpoint != previous_viewpoint
    // The shared SVG graph uses simulation time, so pause freezes its noise too.
    if (handles.has_fuzz and (before == null or sprites.fuzz_seed(game.time) != sprites.fuzz_seed(before.time)))
        dom.set_attribute(handles.fuzz_noise, "seed", string(sprites.fuzz_seed(game.time)))
    if (pose_changed) {
        write(handles.camera, "transform", scene.camera_transform(viewpoint))^
        write(handles.viewport, "background-position-x", scene.fmt(viewpoint.angle * 163) ++ "px")^
    }
    let visibility = if (previous_visibility != null and not pose_changed and game.spectator == before.spectator)
        previous_visibility else [for (plane in map_data.planes)
            (game.spectator == "player" or plane.kind != "ceiling") and
                (game.spectator != "player" or scene.plane_visible(plane, viewpoint, rules.MAX_RENDER_DISTANCE))]
    for (i, plane in map_data.planes) {
        if (previous_visibility == null or previous_visibility[i] != visibility[i])
            write(handles.planes[i], "visibility", if (visibility[i]) "visible" else "hidden")^
        // Keep distance lighting local to each visible plane. Inherited camera
        // variables otherwise recascade every static wall on each frame.
        if (visibility[i] and (pose_changed or previous_visibility == null or not previous_visibility[i])) {
            let filter = scene.light_filter(plane.center, viewpoint, game.spectator)
            if (before == null or previous_visibility == null or not previous_visibility[i] or
                filter != scene.light_filter(plane.center, previous_viewpoint, before.spectator))
                write(handles.planes[i], "filter", filter)^
        }
        if (plane.mover != null) {
            let current_height = height(game, plane.mover)
            let previous_height = if (before == null) null else height(before, plane.mover)
            if (before == null or current_height != previous_height)
                write(handles.planes[i], if (plane.kind == "wall") "--motion" else "--surface-z",
                    scene.fmt(if (plane.kind == "wall") plane.motion_base - current_height else current_height) ++
                        (if (plane.kind == "wall") "px" else ""))^
        }
        if (plane.kind == "wall" and (before == null or game.switches != before.switches) and
            (starts_with(plane.wall.texture, "SW1") or starts_with(plane.wall.texture, "SW2"))) {
            let texture = if (contains(game.switches, plane.wall.id))
                (if (starts_with(plane.wall.texture, "SW1")) "SW2" else "SW1") ++ slice(plane.wall.texture, 3)
                else plane.wall.texture
            write(handles.planes[i], "background-image", scene.image_url(base, "assets/textures/" ++ texture ++ ".png"))^
        }
    }
    for (i, actor in game.actors) {
        let prior = if (before == null) null else before.actors[i]
        let pair = handles.actors[i]
        if (prior == null or actor.dead_at != prior.dead_at or actor.ai.state != prior.ai.state)
            dom.set_attribute(pair.sprite, "data-state", if (actor.dead_at != null) "dead" else actor.ai.state)
        if (prior == null or actor.x != prior.x or actor.y != prior.y or actor.floor != prior.floor) {
            write(pair.node, "transform", scene.thing_transform(actor, world.floor_height(map_data, game, actor)))^
            let sector_id = world.sector_at(map_data, actor).sectorIndex
            let parent = if (sector_id == null) handles.camera else handles.sectors[sector_id]
            if (not dom.same_node(dom.parent_node(pair.node), parent)) dom.append_child(parent, pair.node)
        }
        let sheet = sprites.definition(actor.type, visuals, images)
        let pose = sprites.pose(actor, viewpoint, sheet, images, game.time)
        let previous_pose = if (prior == null) null else sprites.pose(prior, previous_viewpoint, sheet, images, before.time)
        if (actor.type != 58 and pose.visible) {
            let filter = scene.light_filter(actor, viewpoint, game.spectator)
            if (prior == null or not previous_pose.visible or
                filter != scene.light_filter(prior, previous_viewpoint, before.spectator))
                write(pair.sprite, "filter", filter)^
        }
        if (previous_pose == null or pose.visible != previous_pose.visible)
            write(pair.node, "display", if (pose.visible) "block" else "none")^
        if (sheet.w != null) {
            if (previous_pose == null or pose.row != previous_pose.row)
                write(pair.sprite, "background-position-y", scene.fmt(-pose.row * sheet.h) ++ "px")^
            if (previous_pose == null or pose.animation != previous_pose.animation) {
                write(pair.sprite, "--frame-end", scene.fmt(pose.end_x) ++ "px")^
                write(pair.sprite, "animation", pose.animation)^
            }
        }
        if (previous_pose == null or viewpoint.angle != previous_viewpoint.angle or pose.mirror != previous_pose.mirror)
            write(pair.sprite, "transform", "rotateY(" ++ scene.fmt(viewpoint.angle) ++ "rad) scaleX(" ++ scene.fmt(pose.mirror) ++ ")")^
    }
    if (before == null or game.spectator != before.spectator)
        write(handles.player_marker, "visibility", if (game.spectator == "player") "hidden" else "visible")^
    if (game.spectator != "player") {
        let player_actor = {*: game.player, facing: game.player.angle + math.pi / 2}
        let heading = if (game.spectator == "follow") {row: 4, mirror: 1} else sprites.heading(player_actor, viewpoint)
        write(handles.player_marker, "transform", scene.thing_transform(game.player, game.player.floor))^
        write(handles.player_sprite, "background-position-y", scene.fmt(-heading.row * 56) ++ "px")^
        write(handles.player_sprite, "transform", "rotateY(" ++ scene.fmt(viewpoint.angle) ++ "rad) scaleX(" ++ scene.fmt(heading.mirror) ++ ")")^
        write(handles.player_sprite, "animation-play-state", if (game.mode == "playing" and game.player.moving) "running" else "paused")^
        write(handles.player_fov, "transform", "translateY(-10px) rotateX(90deg) rotateZ(" ++ scene.fmt(game.player.angle) ++ "rad)")^
    }
    let weapon = rules.WEAPONS[string(game.player.weapon)]
    let weapon_pose = weapons.pose(game, rules, images)
    let previous_weapon_pose = if (before == null) null else weapons.pose(before, rules, images)
    let sprite = weapon_pose.sheet
    if (previous_weapon_pose == null or sprite.path != previous_weapon_pose.sheet.path) {
        write(handles.weapon, "width", scene.fmt(sprite.w * 2) ++ "px")^
        write(handles.weapon, "height", scene.fmt(sprite.h * 2) ++ "px")^
        write(handles.weapon, "margin-left", scene.fmt(-sprite.w) ++ "px")^
        write(handles.weapon, "background-image", scene.image_url(base, sprite.path))^
        write(handles.weapon, "background-size", scene.fmt(sprite.w * sprite.cols * 2) ++ "px " ++ scene.fmt(sprite.h * 2) ++ "px")^
        write(handles.weapon, "--weapon-frame-start", scene.fmt(-sprite.w * 2) ++ "px")^
        write(handles.weapon, "--weapon-frame-end", scene.fmt(-sprite.w * sprite.frames * 2) ++ "px")^
    }
    if (previous_weapon_pose == null or weapon_pose.animation != previous_weapon_pose.animation)
        write(handles.weapon, "animation", weapon_pose.animation)^
    if (previous_weapon_pose == null or weapon_pose.hidden != previous_weapon_pose.hidden)
        write(handles.weapon, "transform", if (weapon_pose.hidden) "translateY(110%)" else "translateY(0px)")^
    if (previous_weapon_pose == null or weapon_pose.play_state != previous_weapon_pose.play_state)
        write(handles.weapon, "animation-play-state", weapon_pose.play_state)^
    if (previous_weapon_pose == null or weapon_pose.opacity != previous_weapon_pose.opacity)
        write(handles.weapon, "opacity", scene.fmt(weapon_pose.opacity))^
    if (before == null or weapons.face_row(game.player.health) != weapons.face_row(before.player.health))
        write(handles.face, "background-position-y", scene.fmt(-weapons.face_row(game.player.health) * 62) ++ "px")^
    if (before == null or game.mode != before.mode)
        write(handles.face, "animation-play-state", if (game.mode == "playing") "running" else "paused")^
    let viewport_filter = weapons.viewport_filter(game)
    if (before == null or viewport_filter != weapons.viewport_filter(before)) write(handles.viewport, "filter", viewport_filter)^
    let ammo = if (weapon.ammoType == null) "—" else string(game.player.ammo[weapon.ammoType])
    if (before == null or game.player.ammo != before.player.ammo or game.player.weapon != before.player.weapon) text(handles.ammo, ammo)
    if (before == null or game.player.health != before.player.health) text(handles.health, string(game.player.health) ++ "%")
    if (before == null or game.player.armor != before.player.armor) text(handles.armor, string(game.player.armor) ++ "%")
    if (before == null or game.player.keys != before.player.keys)
        text(handles.keys, if (len(game.player.keys) == 0) "—" else join(game.player.keys, " "))
    if (before == null or game.mode != before.mode) {
        write(handles.overlay, "display", if (game.mode == "playing") "none" else "block")^
        text(handles.message, if (game.mode == "dead") "You died. Press R to restart."
            else if (game.mode == "paused") "Paused" else if (game.mode == "won") "Episode complete" else "Lambda Script / Radiant")
    }
    if (before == null or game.autoplay != before.autoplay) text(handles.status, scene.status_text(game))
    let pickup_flash = game.time < (game.player.pickup_until or 0)
    let flash_color = if (game.time < game.teleport_until) "#fff" else if (pickup_flash) "#ff0" else "#f00"
    let previous_flash_color = if (before == null) "" else if (before.time < before.teleport_until) "#fff"
        else if (before.time < (before.player.pickup_until or 0)) "#ff0" else "#f00"
    if (flash_color != previous_flash_color) write(handles.flash, "background-color", flash_color)^
    let flash = if (game.mode == "dead") 0.3 else if (game.time < game.teleport_until) 0.4 else
        if (game.time < game.player.flash_until or pickup_flash) 0.15 else 0
    write(handles.flash, "opacity", scene.fmt(flash))^
    // Publish an inspection snapshot at command/pause boundaries. Per-frame
    // diagnostic attributes otherwise invalidate the entire scene's selectors.
    if (before == null or game.mode != "playing" or game.mode != before.mode or game.generation != before.generation or
        game.spectator != before.spectator) {
        let diagnostics = {mode: game.mode, autoplay: game.autoplay, map: game.level_name, spectator: game.spectator, ticks: game.ticks,
            x: round(game.player.x), y: round(game.player.y), z: round(game.player.z),
            floor: round(game.player.floor), angle: scene.fmt(game.player.angle),
            'camera-height': round(game.camera_control.height),
            'camera-pan-x': round(game.camera_control.offset_x), 'camera-pan-y': round(game.camera_control.offset_y),
            'camera-angle': scene.fmt(game.camera_control.angle),
            health: game.player.health, generation: game.generation, skill: game.skill,
            ammo: game.player.ammo.bullets,
            kills: len([for (actor in game.actors where actor.ai != null and actor.collected) actor]),
            pickups: len([for (actor in game.actors where actor.ai == null and actor.collected) actor]),
            'doors-open': len([for (door in game.doors where door.passable) door]),
            'lifts-lowered': len([for (lift in game.lifts where lift.phase == "lowered") lift]),
            visible: len([for (shown in visibility where shown) shown]), planes: len(map_data.planes)}
        for (key, value at diagnostics) {
            let attribute = "data-" ++ string(key)
            if (dom.get_attribute(owner, attribute) != string(value)) dom.set_attribute(owner, attribute, string(value))
        }
    }
    return visibility
}
