// Exercise intrinsic images and cropped sheets through the production actor tree.
// The odd-width column sits on half pixels: its center blends PNG values 66 and 38.
import data: ~~.mod_data
import scene: ~~.mod_scene
let BASE = url_resolve(data.entry_uri()^, "../")
let VISUALS = data.visuals(BASE)^
let IMAGES = data.read(BASE, "data/images.json")^
let CAMERA = {x: 1000, y: 0, angle: 0}
let ACTORS = [for (i, actor_type in [2028, 2001, 3004, 2014])
    {id: i, type: actor_type, x: 80 * (i + 1), y: 0, floor: -120, facing: 0,
        ai: if (actor_type == 3004) {state: "idle"} else null, dead_at: null, collected: false}];
<html <head <link rel:"stylesheet", href:url_resolve(BASE, "doom.css")>>
    <body <main id:"doom", 'data-mode':"paused", style:"width:360px;height:180px;margin:0;",
        <div id:"scene", style:"left:0;top:0;",
            *[for (actor in ACTORS) scene.thing_tree(actor, CAMERA, VISUALS, IMAGES, BASE)]
        >
    >>
>
