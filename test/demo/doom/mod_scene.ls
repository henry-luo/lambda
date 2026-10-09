// CSS planes keep the upstream (x, y, z) -> (x, -z, -y) mapping.
import geo: .mod_geometry
import world: .mod_world
import sprites: .mod_sprites
import camera: .mod_camera

pub fn fmt(value) => string(round(value * 100000) / 100000)
fn px(value) => fmt(value) ++ "px"
fn property(key, value) => "--" ++ key ++ ":" ++ fmt(value) ++ ";"
pub fn image_url(base, path) => "url('" ++ url_resolve(base, path) ++ "')"
pub fn light(level) => max(0.12, 1 - max(0, min(31, (15 - level / 16) * 4 - 4)) / 32)
pub fn light_filter(point, viewpoint, spectator) {
    let relative = geo.subtract(point, viewpoint), forward = geo.forward(viewpoint.angle)
    let falloff = if (spectator != "player") 1 else
        max(0.15, min(1, 1 - (relative.x * forward.x + relative.y * forward.y) / 1500))
    "brightness(calc(var(--light, 1) * " ++ fmt(falloff) ++ "))"
}
fn light_class(special) => {'1': "light-flicker", '2': "light-blink-fast", '3': "light-blink",
    '8': "light-glow", '12': "light-blink-fast", '13': "light-blink", '17': "light-fire-flicker"}[string(special)] or ""
fn plane_class(plane) => plane.kind ++ (if (plane.wall.isScrolling) " scroll-texture" else "") ++
    (if (plane.mover != null and plane.kind == "wall") " unpegged" else "") ++
    (if (plane.kind == "floor" and starts_with(plane.texture or "", "NUKAGE")) " nukage" else "")
fn adjacent(a, b) => a.start == b.start or a.start == b.end or a.end == b.start or a.end == b.end
fn wall_descriptor(wall, id, mover = null, motion_base = 0) {
    let bounds = geo.bounds([wall.start, wall.end])
    {id: id, kind: "wall", wall: wall, sector_id: wall.sectorIndex, mover: mover, motion_base: motion_base,
        center: {x: (wall.start.x + wall.end.x) / 2, y: (wall.start.y + wall.end.y) / 2},
        radius: math.sqrt(geo.distance_squared(wall.start, wall.end)) / 2, bounds: bounds}
}
fn motion_for_wall(game, wall) {
    let door = world.motion(game.doors, wall.door_id)
    let crusher = [for (entry in game.crushers where wall.isUpperWall and
        (entry.sectorIndex == wall.frontSectorIndex or entry.sectorIndex == wall.backSectorIndex)) entry][0]
    if (door != null) {kind: "door", sector: door.sectorIndex, base: door.closedHeight}
    else if (crusher != null) {kind: "crusher", sector: crusher.sectorIndex, base: crusher.topHeight} else null
}
fn valid_wall(wall) => wall.texture != null and wall.texture != "" and wall.texture != "-" and
    wall.topHeight > wall.bottomHeight and geo.distance_squared(wall.start, wall.end) >= 1
fn surface_descriptor(sector, kind, game) {
    let bounds = geo.bounds(sector.boundaries[0])
    let lift = world.motion(game.lifts, sector.sectorIndex)
    let door = world.motion(game.doors, sector.sectorIndex)
    let crusher = world.motion(game.crushers, sector.sectorIndex)
    let mover = if (kind == "floor" and lift != null) {kind: "lift", sector: sector.sectorIndex}
        else if (kind == "ceiling" and door != null) {kind: "door", sector: sector.sectorIndex}
        else if (kind == "ceiling" and crusher != null) {kind: "crusher", sector: sector.sectorIndex} else null
    let width = bounds.max_x - bounds.min_x, height = bounds.max_y - bounds.min_y
    {id: kind ++ "-" ++ string(sector.sectorIndex), kind: kind, sector_id: sector.sectorIndex,
        bounds: bounds, width: width, height: height, boundaries: sector.boundaries, mover: mover,
        z: if (kind == "floor") sector.floorHeight else sector.ceilingHeight,
        texture: if (kind == "floor") sector.floorTexture else sector.ceilingTexture,
        center: {x: (bounds.min_x + bounds.max_x) / 2, y: (bounds.min_y + bounds.max_y) / 2},
        radius: math.sqrt(width ** 2 + height ** 2) / 2}
}
pub fn prepare(map_data, game) {
    let walls = [for (wall in map_data.walls, let mover = motion_for_wall(game, wall)
        where valid_wall(wall) and not wall.isLiftWall)
        wall_descriptor(wall, "wall-" ++ string(wall.id), mover, mover.base or 0)]
    let tracks = [for (door in game.doors, wall in map_data.walls,
        let faces = [for (face_wall in map_data.walls where face_wall.door_id == door.sectorIndex) face_wall]
        where wall.isSolid and not wall.isDoor and wall.bottomHeight == door.floorHeight and
            wall.topHeight == door.closedHeight and any([for (face in faces) adjacent(wall, face)]) and
            wall.texture != null and wall.texture != "-")
        wall_descriptor({*: wall, bottomHeight: door.closedHeight, topHeight: door.openHeight},
            "track-" ++ string(door.sectorIndex) ++ "-" ++ string(wall.id))]
    let shafts = [for (lift in game.lifts, i in 0 to (len(lift.shaftWalls) - 1), let wall = lift.shaftWalls[i])
        wall_descriptor({*: wall, sectorIndex: lift.sectorIndex, bottomHeight: lift.lowerHeight, topHeight: lift.upperHeight},
            "shaft-" ++ string(lift.sectorIndex) ++ "-" ++ string(i),
            if (wall.isPlatformFace) {kind: "lift", sector: lift.sectorIndex} else null, lift.upperHeight)]
    let surfaces = [for (sector in map_data.polygons, kind in ["floor", "ceiling"],
        let surface = surface_descriptor(sector, kind, game) where surface.width > 0 and surface.height > 0) surface]
    {*: map_data, planes: [*walls, *tracks, *shafts, *surfaces],
        actor_sectors: [for (actor in game.actors) world.sector_at(map_data, actor).sectorIndex]}
}
pub fn plane_visible(plane, player, range_limit = 2250, horizontal_fov = math.pi / 2) {
    let relative = geo.subtract(plane.center, player)
    let forward = geo.forward(player.angle), right = geo.right(player.angle)
    let depth = relative.x * forward.x + relative.y * forward.y
    let side = relative.x * right.x + relative.y * right.y
    let tangent = math.tan(horizontal_fov / 2)
    // Bounding-circle tests conservatively retain every plane crossing the
    // 90-degree frustum, including surfaces that contain the camera.
    math.sqrt(relative.x ** 2 + relative.y ** 2) - plane.radius <= range_limit and
        depth + plane.radius >= 0 and abs(side) <= depth * tangent + plane.radius * math.sqrt(1 + tangent ** 2)
}
fn clip_shape(surface) => "shape(evenodd from 0% 0%, " ++ join([for (boundary in surface.boundaries)
    join([for (i in 0 to (len(boundary) - 1)) (if (i == 0) "move to " else "line to ") ++
        fmt((boundary[i].x - surface.bounds.min_x) * 100 / surface.width) ++ "% " ++
        fmt((surface.bounds.max_y - boundary[i].y) * 100 / surface.height) ++ "%"], ", ") ++ ", close"], ", ") ++ ")"
fn plane_style(plane, base, visible) {
    let common = "visibility:" ++ (if (visible) "visible" else "hidden") ++ ";" ++
        property("x", plane.center.x) ++ property("y", plane.center.y)
    if (plane.kind == "wall") {
        let wall = plane.wall
        common ++ property("start-x", wall.start.x) ++ property("start-y", wall.start.y) ++
            property("end-x", wall.end.x) ++ property("end-y", wall.end.y) ++
            property("floor-z", wall.bottomHeight) ++ property("ceiling-z", wall.topHeight) ++
            property("texture-offset-x", wall.xOffset or 0) ++ property("texture-offset-y", wall.yOffset or 0) ++
            "background-image:" ++ image_url(base, "assets/textures/" ++ wall.texture ++ ".png") ++ ";"
    } else {
        let bounds = plane.bounds
        common ++ property("min-x", bounds.min_x) ++ property("max-x", bounds.max_x) ++
            property("min-y", bounds.min_y) ++ property("max-y", bounds.max_y) ++ property("surface-z", plane.z) ++
            "clip-path:" ++ clip_shape(plane) ++ ";" ++
            (if (plane.texture == "F_SKY1") "background:#1a1a3a;" else if (plane.texture == "-" or plane.texture == null)
                "background:#444;" else "background-image:" ++ image_url(base, "assets/flats/" ++ plane.texture ++ ".png") ++ ";")
    }
}
pub fn thing_tree(actor, player, visuals, images, base) {
    let sprite = sprites.definition(actor.type, visuals, images)
    let pose = sprites.pose(actor, player, sprite, images, 0)
    let attributes = {id: "sprite-" ++ string(actor.id), class: "sprite",
        'data-type': visuals.THING_NAMES[string(actor.type)],
        'data-state': if (actor.dead_at != null) "dead" else actor.ai.state,
        style: "transform:rotateY(" ++ fmt(player.angle) ++ "rad) scaleX(" ++ fmt(pose.mirror) ++ ");"};
    <div id:("actor-" ++ string(actor.id)), class:"thing", style:(property("x", actor.x) ++ property("y", actor.y) ++
        property("floor-z", actor.floor)),
        // Static sprites keep their intrinsic image size; sheets still crop background frames.
        if (sprite.w == null) <img *:attributes, src:url_resolve(base, sprite.path), alt:"", draggable:"false">
        else <div *:attributes, style:(attributes.style ++ "width:" ++ px(sprite.w) ++ ";height:" ++ px(sprite.h) ++
            ";left:" ++ px(-sprite.w / 2) ++ ";top:" ++ px(-sprite.h) ++ ";background-size:" ++ px(sprite.w * sprite.cols) ++ " " ++
            px(sprite.h * sprite.rows) ++ ";background-image:" ++ image_url(base, sprite.path) ++
            ";background-position-y:" ++ px(-pose.row * sprite.h) ++ ";--frame-end:" ++ px(pose.end_x) ++
            ";animation:" ++ pose.animation ++ ";")>
    >
}
pub fn camera_transform(player, perspective = 320, viewport_height = 336) => "translate3d(0px," ++ px((player.offset_y or 0) * viewport_height) ++ "," ++ px(perspective) ++ ") rotateX(" ++
    fmt(player.pitch) ++ "rad) rotateY(" ++ fmt(-player.angle) ++ "rad) translate3d(" ++
    px(-player.x) ++ "," ++ px(player.z) ++ "," ++ px(player.y) ++ ")"
pub fn thing_transform(actor, floor_height) => "translate3d(" ++ px(actor.x) ++ "," ++ px(-floor_height) ++ "," ++ px(-actor.y) ++ ")"
pub fn fuzz_filter() => <svg width:"0", height:"0", style:"display:block", 'aria-hidden':"true",
    <defs <filter id:"fuzz", x:"-10%", y:"-10%", width:"120%", height:"120%",
        <feTurbulence id:"fuzz-noise", type:"turbulence", baseFrequency:"0.8 0.03", numOctaves:"3", seed:"0", result:"noise">
        <feDisplacementMap in:"SourceGraphic", in2:"noise", scale:"4", xChannelSelector:"R", yChannelSelector:"G", result:"displaced">
        <feColorMatrix type:"matrix", in:"displaced", values:"0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0", result:"shadow">
        <feComposite in:"noise", in2:"displaced", operator:"in", result:"clippedNoise">
        <feColorMatrix type:"matrix", in:"clippedNoise", values:"0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 .4 0 0 0 0", result:"noiseAlpha">
        <feComposite in:"shadow", in2:"noiseAlpha", operator:"arithmetic", k1:"0", k2:"1", k3:"1", k4:"0">
    >>
>
pub fn tree(map_data, game, rules, visuals, images, base) =>
    <main id:"doom", tabindex:"0", 'data-mode':game.mode, 'data-map':game.level_name, 'data-spectator':game.spectator, 'data-skill':game.skill,
        fuzz_filter();
        <div id:"stage",
            <div id:"viewport", style:("background-image:" ++ image_url(base, "assets/textures/SKY1.png") ++ ";"),
                <div id:"scene", style:("transform:" ++ camera_transform(camera.pose(game)) ++ ";" ++
                    property("camera-x", game.player.x) ++ property("camera-y", game.player.y) ++ property("camera-angle", game.player.angle)),
                    *[for (sector_id in 0 to (len(map_data.sectors) - 1))
                        <div class:("sector " ++ light_class(map_data.sectors[sector_id].specialType)), id:("sector-" ++ string(sector_id)),
                            style:property("light", light(map_data.sectors[sector_id].lightLevel)),
                            *[for (plane in map_data.planes where plane.sector_id == sector_id)
                                <div id:plane.id, class:plane_class(plane), style:plane_style(plane, base, plane_visible(plane, game.player))>];
                            *[for (i, actor in game.actors where map_data.actor_sectors[i] == sector_id)
                                thing_tree(actor, camera.pose(game), visuals, images, base)]
                        >];
                    *[for (i, actor in game.actors where map_data.actor_sectors[i] == null)
                        thing_tree(actor, camera.pose(game), visuals, images, base)];
                    <div id:"player-marker", class:"thing", style:"visibility:hidden;",
                        <div class:"player-shadow">
                        <div id:"player-fov", class:"player-fov">
                        <div id:"player-sprite", class:"sprite", style:("width:45px;height:56px;left:-22.5px;top:-56px;background-image:" ++
                            image_url(base, images.sheets.player.path) ++ ";background-size:180px 280px;--frame-end:-180px;animation:doom-sprite-cycle .6s steps(4) infinite paused;")>
                    >
                >
                <div id:"weapon">
                <div id:"crosshair", "+">
                <div id:"hurt-flash">
            >
            <div id:"status-bar",
                <div <span "AMMO"> <strong id:"ammo", string(game.player.ammo.bullets)>>
                <div <span "HEALTH"> <strong id:"health", string(game.player.health) ++ "%">>
                <div id:"face", role:"img", 'aria-label':"DOOM marine",
                    style:("background-image:" ++ image_url(base, "assets/hud/FACE_SHEET.png") ++ ";")>
                <div <span "ARMOR"> <strong id:"armor", string(game.player.armor) ++ "%">>
                <div <span "KEYS"> <strong id:"keys", "—">>
            >
            <div id:"overlay", <h1 "DOOM"> <p id:"overlay-message", "Lambda Script / Radiant">
                <button id:"start", 'data-action':"pause", "START / RESUME">>
        >
        <nav <button id:"pause", 'data-action':"pause", "Pause / P">
            <button id:"restart", 'data-action':"restart", "Restart / R">
            <button id:"spectator", 'data-action':"spectator", "Camera / Tab">
            <select id:"map-picker", 'aria-label':"Episode map",
                *[for (i in 1 to 9, let map_name = "E1M" ++ string(i))
                    <option value:map_name, *:(if (map_name == game.level_name) {selected: ""} else {}), map_name>]
            >
            <select id:"skill-picker", 'aria-label':"Difficulty",
                *[for (i, difficulty_label in ["1 Easy", "2 Mild", "3 Normal", "4 Hard", "5 Nightmare"])
                    <option value:string(i + 1), *:(if (i + 1 == game.skill) {selected: ""} else {}), difficulty_label>]
            >
            <button id:"load-map", 'data-action':"map", "Load map">
        >
        <p id:"doom-status", "W/S move · A/D strafe · arrows turn · Shift run · E/Space use · Ctrl fire · 1–6 weapon">
    >
