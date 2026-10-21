// A template-owned physics scene keeps its simulated world when a re-render
// leaves the authored physics unchanged, as form controls keep native edits;
// changed authored physics wins and the world restarts from it (S12.1.5v2).
import dom
import physics: lambda.physics

fn stage(gravity) element => <scene3d id: "world", width: 160, height: 120, camera: "main", background: "#202936", style: "display:block",
    *[physics.world({state: 'paused', gravity: [0.0, gravity, 0.0]}),
      <camera id: "main", type: 'perspective', position: [4.0, 3.0, 6.0], target: [0.0, 1.0, 0.0]>,
      <light type: 'ambient', intensity: 0.6>,
      physics.body("floor", [physics.collider("floor-shape", 'box', {size: [6.0, 0.2, 6.0]}),
          <mesh <geometry type: 'box', size: [6.0, 0.2, 6.0]><material type: 'lambert', color: "#64748b">>],
          {type: 'fixed', position: [0.0, -0.1, 0.0]}),
      physics.body("box", [physics.collider("box-shape", 'box', {size: [1.0, 1.0, 1.0]}),
          <mesh <geometry type: 'box', size: [1.0, 1.0, 1.0]><material type: 'lambert', color: "#fbbf24">>],
          {position: [0.0, 4.0, 0.0]})]>

// `redraws` re-renders the scene unchanged; `gravity` is authored physics
view <lab> state redraws: 0, gravity: -9.81 {
    <div id: "lab", 'data-redraws': string(redraws), 'data-gravity': string(gravity),
        <button id: "redraw", "redraw">
        <button id: "heavier", "heavier">
        stage(gravity)>
}
on click(evt) {
    let id = dom.get_attribute(evt.target, "id")
    if (id == "redraw") { redraws = redraws + 1 }
    else if (id == "heavier") { gravity = -19.62 }
}
<html
    <head <style "html,body{margin:0;background:#ffffff} button{font-size:10px;margin:1px}">
        <script type: "module", "
import { operate } from '../demo/scene3d/physics/_control.ls';
globalThis.physicsRead = (request) => {
    globalThis.physicsState = null;
    return operate(document.getElementById('world'), request).then(value => { globalThis.physicsState = value; });
};
globalThis.boxHeight = () => physicsState.bodies.find(b => b.id === 'box').position[1];
">>
    <body apply(<lab>)>
>
