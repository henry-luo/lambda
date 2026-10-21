// Motion M5: motion.control appends a <motion-controller> to a compiled
// character group, resolving state clip names to clip IDs, and the scene
// validator checks hand-written controllers (Lambda_Pkg_Motion.md §9.2).
import s: lambda.scene3d
import motion: lambda.scene3d.motion

let hero = motion.load("test/demo/scene3d/assets/motion/jump.bvh", {scale: 0.01, rig: 'cmu'})^
let rigged = motion.attach(motion.mannequin(hero)^, {rise: motion.trim(hero, 0.2, 0.6)^, flip: motion.mirror(hero)^})^
let compiled = motion.scene(rigged, {id: "hero"})^
let states = [motion.motion_state('jump', "jump"), motion.motion_state('flip', "flip", {time_scale: 0.5}),
    motion.motion_state('rise', "rise", {loop: 'once', next: 'jump', fade: 0.1})]
fn viewport(group) => s.scene([s.camera({id: "camera", position: [0.0, 1.0, 3.0]}), s.light('ambient'), group], {camera: "camera"});
fn controller(group) => [for (x in content(group) where name(x) == 'motion-controller') x][0];

"1 control appends a controller whose states name clip IDs";
let controlled = motion.control(compiled, states, {state: 'rise', cue: 2})^
let c = controller(controlled);
[[for (x in content(controlled)) name(x)][len(content(controlled)) - 1], c.id, c.state, c.cue, c.fade,
    [for (st in content(c)) [st.name, st.clip, st.loop, st.next, st.fade, st["time-scale"]]]];
[controller(motion.control(compiled, states)^).state, name(s.normalize(viewport(controlled))^)];

"2 control rejects states it cannot resolve";
[(motion.control(compiled, [motion.motion_state('walk', "walk")]) ^ { "unknown clip" }),
 (motion.control(motion.scene(rigged, {id: "auto", autoplay: "jump"})^, [motion.motion_state('jump', "jump")]) ^ { "autoplay clip" }),
 (motion.control(compiled, [motion.motion_state('a', "jump"), motion.motion_state('a', "flip")]) ^ { "duplicate names" }),
 (motion.control(compiled, [motion.motion_state('a', "jump", {next: 'b'})]) ^ { "unknown next" }),
 (motion.control(compiled, states, {state: 'dance'}) ^ { "unknown request" }),
 (motion.control(compiled, []) ^ { "no states" })];

"3 the scene validator checks hand-written controllers";
fn with_controller(extra) => <group *: {id: "hero"}, *[*[for (x in content(compiled)) x], extra]>;
[name(s.normalize(viewport(with_controller(<'motion-controller' id: "m", <'motion-state' name: 'a', clip: "hero-clip-0">>)))^),
 (s.normalize(viewport(with_controller(<'motion-controller' id: "m",
    <'motion-state' name: 'a', clip: "hero-clip-0"> <'motion-state' name: 'b', clip: "hero-clip-0">>))) ^ { "shared clip" }),
 (s.normalize(viewport(with_controller(<'motion-controller' id: "m", <'motion-state' name: 'a', clip: "hero-clip-0", loop: 'bounce'>>))) ^ { "bad loop" }),
 (s.normalize(viewport(with_controller(<'motion-controller' id: "m", state: 'z', <'motion-state' name: 'a', clip: "hero-clip-0">>))) ^ { "bad request" }),
 (s.normalize(viewport(with_controller(<'motion-controller' id: "m", <'motion-state' name: 'a', clip: "hero-skeleton">>))) ^ { "not a clip" }),
 (s.normalize(viewport(with_controller(<'motion-controller' id: "m">))) ^ { "no states" })]
