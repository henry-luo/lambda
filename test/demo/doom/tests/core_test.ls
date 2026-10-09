import geo: ~~.mod_geometry
import controls: ~~.mod_input
import scene: ~~.mod_scene
let square = [{x: 0, y: 0}, {x: 10, y: 0}, {x: 10, y: 10}, {x: 0, y: 10}]
let hole = [{x: 4, y: 4}, {x: 6, y: 4}, {x: 6, y: 6}, {x: 4, y: 6}]
let held = controls.set_key(controls.set_key(controls.empty(), "w", true), "ArrowRight", true)
let repeated = controls.set_key(held, "w", true)
let consumed = controls.consume(repeated)
let released = controls.set_key(consumed, "w", false)
let aliased = controls.set_key(controls.set_key(held, "ArrowUp", true), "w", false);
{
    polygon: [geo.point_in_polygon({x: 5, y: 5}, square), geo.point_in_polygon({x: -1, y: 5}, square),
        geo.point_in_polygon({x: 1, y: 1}, []), geo.point_in_sector({x: 5, y: 5}, [square, hole]),
        geo.point_in_sector({x: 2, y: 5}, [square, hole])],
    segment: [geo.circle_hits_segment({x: 5, y: 1}, 2, square[0], square[1]),
        geo.circle_hits_segment({x: 5, y: 2}, 2, square[0], square[1]),
        geo.segment_distance_squared({x: 3, y: 4}, square[0], square[0])],
    ray: [geo.ray_segment({x: 5, y: -5}, {x: 0, y: 1}, square[0], square[1], 20),
        geo.ray_segment({x: 5, y: -5}, {x: 1, y: 0}, square[0], square[1], 20),
        geo.ray_segment({x: 5, y: -5}, {x: 0, y: 1}, square[0], square[1], 5)],
    coordinates: geo.css_position(10, 20, 41),
    bounds: geo.bounds([{x: -10, y: 20}, {x: 30, y: -40}]),
    lighting: [for (point in [{x:0, y:-100}, {x:0, y:750}, {x:0, y:3000}])
        scene.light_filter(point, {x:0, y:0, angle:0}, "player")],
    spectator_light: scene.light_filter({x:0, y:3000}, {x:0, y:0, angle:0}, "top"),
    input: [len(repeated.edges), len(consumed.edges), controls.axis(held, "forward", "back"),
        controls.axis(released, "forward", "back"), controls.command("Space"), controls.command("7"),
        controls.axis(aliased, "forward", "back")]
}
