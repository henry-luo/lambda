// A template-owned physics scene keeps its simulated world across re-renders,
// as form controls keep native edits. An authored edit overrides only what it
// changes (S12.1.5v2, Lambda_Pkg_Physics.md §6.2): gravity, a body's position
// and a collider's density reach the running world; adding a body restarts it.
import dom
import physics: lambda.physics

fn box(id, y, density, color) element => physics.body(id, [physics.collider(id ++ "-shape", 'box', {size: [1.0, 1.0, 1.0], density: density}),
    <mesh <geometry type: 'box', size: [1.0, 1.0, 1.0]><material type: 'lambert', color: color>>], {position: [0.0, y, 0.0]})

fn stage(gravity, lift, density, second) element => <scene3d id: "world", width: 160, height: 120, camera: "main", background: "#202936", style: "display:block",
    *[physics.world({state: 'paused', gravity: [0.0, gravity, 0.0]}),
      <camera id: "main", type: 'perspective', position: [5.0, 4.0, 9.0], target: [0.0, 2.0, 0.0]>,
      <light type: 'ambient', intensity: 0.6>,
      physics.body("floor", [physics.collider("floor-shape", 'box', {size: [6.0, 0.2, 6.0]}),
          <mesh <geometry type: 'box', size: [6.0, 0.2, 6.0]><material type: 'lambert', color: "#64748b">>],
          {type: 'fixed', position: [0.0, -0.1, 0.0]}),
      box("box", lift, density, "#fbbf24"),
      *(if (second) [box("second", 2.0, 1.0, "#60a5fa")] else [])]>

// `redraws` re-renders the scene unchanged; the other fields are authored physics
view <lab> state redraws: 0, gravity: -9.81, lift: 4.0, density: 1.0, second: false {
    <div id: "lab", 'data-redraws': string(redraws), 'data-gravity': string(gravity), 'data-lift': string(lift),
        'data-density': string(density), 'data-second': string(second),
        <button id: "redraw", "redraw">
        <button id: "heavier", "heavier">
        <button id: "lift", "lift">
        <button id: "dense", "dense">
        <button id: "second", "second">
        stage(gravity, lift, density, second)>
}
on click(evt) {
    let id = dom.get_attribute(evt.target, "id")
    if (id == "redraw") { redraws = redraws + 1 }
    else if (id == "heavier") { gravity = -19.62 }
    else if (id == "lift") { lift = 6.0 }
    else if (id == "dense") { density = 3.0 }
    else if (id == "second") { second = true }
}
<html
    <head <style "html,body{margin:0;background:#ffffff} button{font-size:10px;margin:1px}">
        <script type: "module", "
import { operate } from '../demo/scene3d/physics/_control.ls';
globalThis.physicsRead = (request) => {
    globalThis.physicsState = null;
    return operate(document.getElementById('world'), request).then(value => { globalThis.physicsState = value; });
};
globalThis.boxBody = () => physicsState.bodies.find(b => b.id === 'box');
globalThis.boxHeight = () => boxBody().position[1];
">>
    <body apply(<lab>)>
>
