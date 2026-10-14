# Physics playground

Open the gallery:

```sh
./lambda.exe view test/demo/physics/index.ls
```

| Demo | What to try |
|---|---|
| [Domino circuit](domino.ls) | Start a 32-tile cascade; counter-nudge the middle to send a second front toward the first. |
| [Salvage bay](salvage.ls) | Spin nine compound assemblies with off-centre impulses, then switch between gravity and weightlessness. |
| [Marble sorter](sorter.ls) | Follow 18 marbles past a kinematic paddle into three sensor gates. Counters record each marble's first arrival. |
| [Bounce lab](bounce.ls) | Compare five restitution values in the live scene and in native batch-generated height curves. |
| [Basic rigid bodies](rigid.ls) | The original minimal three-box example. |

Each playground scene has pause/play, a two-tick step (1/60 s), reset, camera
presets, drag-to-orbit, Shift-drag-to-pan and wheel zoom. Space toggles playback
while the scene has keyboard focus. Reset leaves a scene paused; the primary
action starts the experiment again. The sorter does not classify by colour:
its gates count the trajectories produced by the paddle and collisions.

The descriptions in `_scenes.ls` are shared by the viewer and the pure batch
checks (**S12.1.1v2**). Document-owned worlds advance on the native physics clock
(**S12.1.5v2**); `playground.js` only sends controls through the Radiant host
boundary (**D7.5.3**), updates telemetry and moves the camera. Three's existing
OrbitControls supplies camera interaction; Radiant renders every scene.

`_scenes.ls` derives visual and collider dimensions from the same part lists.
The shared sphere geometry in `../scene3d/_geometry.ls` is also used by Ringworld.

Deterministic batch checks and the five native viewer cases are registered in
the existing harnesses:

```sh
./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/*physics_demos'
./test/test_radiant_view_gtest.exe --gtest_filter='Scene3dTest.PhysicsDemo*:Scene3dTest.PhysicsGallery*'
```

Viewer cases cover point impulses, compound rotation, gravity changes,
kinematic motion, unique sensor arrivals, restitution, chart output, camera
preservation, reset, and explicit garbage collection. Their definitions live
in `test/scene3d/events/PhysicsDemo*.json` and `PhysicsGallery*.json`. The gallery
replay visits all four experiments with native mouse clicks and advances the
document clock to verify playback and telemetry across repeated navigation.
