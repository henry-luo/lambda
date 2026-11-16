# Midtown

An open-city driving sandbox written in Lambda Script on native Scene3D. Drive
around Lumen Bay, a procedural city of thirty-six blocks: towers downtown, offices
and flats, two parks, a plaza with a fountain, a construction yard with a slewing
crane, a stunt lot, and a harbour promenade beyond the south barrier.

```sh
./lambda.exe demo midtown
# Equivalent direct entry:
./lambda.exe view test/demo/midtown/midtown.ls
```

![Midtown on 4th Avenue](../previews/midtown.jpg)

The genre and feature set follow open-world driving games such as the
[OpenLiberty](https://github.com/openfw-game/OpenLiberty) Godot project's vehicle
prototype: an engine with automatic gears and a reverse speed limit, smoothed
steering, a chase camera that swings with the car, headlights, and day and night.
OpenLiberty loads the user's own copy of a commercial game's files; Midtown uses
none of them. Its city, rules, models and textures are original and generated in Lambda.

## The open road

Union Street runs east through a gap in the perimeter barrier to the gateway roundabout. From there a
3 km loop road crosses a river truss bridge, climbs through farmland with hedgerows, farmsteads and a
row of wind turbines, passes through a lit ridge tunnel, follows cliff tops and a beach past a
lighthouse, and returns over a cable-stayed estuary bridge. Open-road traffic drives the loop both
ways through the roundabout; Auto drive follows it when engaged outside town; **J** jumps the car to
the next place along it. The land is a carved heightfield (`mod_land`, `mod_region`), the roads are
sampled splines (`mod_roads`), and `mod_world` answers driving height and solids everywhere. Design
and rulings: [Midtown Open Road](../../../vibe/Lambda_Demo_Midtown_Open_Road.md).

## Driving

- **W / ↑** accelerate, **S / ↓** brake and then reverse, **A D / ← →** steer,
  **Space** handbrake (the rear steps out, so the car drifts), **C** hold to look back.
- **V** cycles the chase, wide, bumper and overhead cameras. Drag the scene to look
  around; the view drifts back behind the car once it moves.
- **N** cycles day, dusk and night. Dusk and night light the windows, shop fronts,
  street lamps and their pools of light, and switch the headlights on; **L** or **Z**
  toggles them.
- **M** starts a courier run: drive to the green beacon to collect a parcel, then
  to the amber beacon before the clock runs out. Each delivery pays a fare plus the
  seconds to spare, multiplied by the streak, and the next pickup appears at once.
- **O** hands the car to Auto drive, which follows the lanes and signals like
  traffic. Any driving key takes the wheel back. **R** puts the car back on the
  nearest lane, **P** or **Esc** pauses, **H** lists the keys.
- On small or touch screens, pedals and steering buttons appear at the bottom.

Traffic keeps to the right-hand lane, picks a turn at every junction, stops for red
and amber lights, queues behind whatever is ahead (the player included), and waits
for oncoming cars before turning left. A bumped car stops for a moment. Pedestrians
walk the sidewalk loop around their block, swinging their arms and legs, and hop
clear of a car bearing down on them. The car's rear lamp on the side its wheels are
turned toward blinks amber while it turns. The stunt lot has two ramps that launch the car across the neighbouring
streets, crate pyramids, a cone slalom and barrels; the yard and some sidewalks have
more props. Buildings, parked cars, lamp posts, trees and signal poles are solid.

## How it works

| Module | Role |
| --- | --- |
| `mod_city.ls` | The plan: grid, block kinds, buildings, furniture, signals, parked cars, props, ground height, collision boxes and their cell index, place names. |
| `mod_vehicle.ls` | Car dynamics: gearbox, speed-sensitive steering, tyre grip and drifting, curbs, ramps and airtime, separating-axis collisions with buildings and traffic. |
| `mod_traffic.ls` | Lane-following traffic and signal timing; also places the player's car on the nearest lane for Reset and Auto drive. |
| `mod_peds.ls`, `mod_mission.ls`, `mod_camera.ls` | Pedestrians, courier runs, camera rigs. |
| `mod_mesh.ls`, `mod_car_mesh.ls`, `mod_textures.ls` | Buffer geometry for the city, people and props; lofted car bodies drawn in one call each through a five-cell material atlas; procedural SVG facades, storefronts, road and ground textures. |
| `mod_land.ls`, `mod_roads.ls`, `mod_region.ls`, `mod_world.ls` | The open road: terrain, coast and river (one branch-free formula over numbers or whole-grid arrays); the loop road's spline, elevation and corridor index; carving, driving height and solids beyond the city; one ground and naming function for the car everywhere. |
| `mod_land_mesh.ls`, `mod_land_scene.ls`, `mod_scenery*.ls`, `mod_route.ls`, `mod_cells.ls` | Terrain, road, roundabout, bridge and tunnel meshes; woods, hedgerows, farms, turbines, lighthouse and boats; open-road traffic on two lane paths; the shared cell index. |
| `mod_scene.ls` | The native scene: merged city meshes, instanced furniture, the physics world, clips, lighting looks and post effects. |
| `mod_hud.ls`, `mod_present.ls`, `midtown.ls` | HUD markup, the per-frame step and sync, and the page with its event handlers. |

The simulation is pure Lambda: each frame `advance()` steps the car, traffic,
people, courier and camera. Everything that moves on screen is a physics body,
including the camera, so `sync()` hands the whole frame to Radiant as one batch of
kinematic pose targets; no scene element is edited per frame (a camera attribute
edit would relayout the page). Bodies apply their targets on the next physics tick
at 120 Hz, so the camera and the car move together. The turn indicators are bodies
too: a lit one rides with the car and an unlit one waits below the city. Walking
needs no per-frame work: each pedestrian's limbs hang from hip and shoulder pivots
inside its body, and one looping `walk` clip swings them from each person's own gait
phase. The car pushes the crates,
cones, barrels and bins, which are dynamic bodies; their contact events count the
props scattered. Signal lamps, the crane, harbour ripples and beacon pulses are
native clips on the scene clock, which the traffic also reads, so cars and lamps
agree. The chase camera casts a native ray to stay in front of buildings.
Lighting looks and headlights are attribute edits made once per change; the HUD's
moving readouts update through the presentation channel, without layout.

The scene sets `'max-pixel-ratio': 1.0`: on a Retina display it renders one
pixel per CSS pixel and the GPU upsamples the finished frame once (with the GPU
compositor, while it composites the frame into the window). Its
multisampled HDR targets at full density would exceed Radiant's 512 MiB graphics
budget. Measured live on a Retina MacBook (release build, full 1280×800 window),
the Lambda step and sync take about 1 ms a frame and the page holds about
31 fps. Radiant sets that rate: the synchronous readback of the scene image
waits about 6 ms for the GPU, and the translucent HUD panels are recomposited
over the moving scene each frame. The window now composites the scene on the GPU
by default instead of reading it back (`RADIANT_GPU_COMPOSITOR=off` restores the readback)
([GPU compositor](../../../vibe/radiant/Radiant_Design_GPU_Compositor.md)) and repaints
only the HUD readouts that changed; the page then holds about 56 fps, with a median
frame of one 60 Hz vsync. `MIDTOWN_PROFILE=1` prints each frame's interval, step
and sync times, the scene's lag and the speed cap; `MIDTOWN_SPAWN=x,z,heading` starts
the car elsewhere, for example `MIDTOWN_SPAWN=-178,168,1.5708` in front of the stunt ramp.

Out of town the frame stays light: vehicles and people beyond 900 m of the camera take no pose
commands (their bodies keep their last pose, lost in the haze); town traffic and pedestrians rest while
the car is more than 400 m beyond the barrier; traffic far from the car steps every second (town) or
fourth (open road) frame with the time it is owed. Every car is a single draw. The minimap's region
layer is an SVG image laid out at a third of the map's scale and enlarged by its holder's transform:
the turning map redraws it each frame, and at full size that cost about 2.5 ms. Measured headless
(release, 1280×800), a frame costs about 3 ms of CPU and GPU in town, and the Lambda step and sync
about 2 ms in town and 1.5 ms on the open road.

The native scene pauses itself once its physics falls two seconds behind real time
("simulation pacing debt exceeded"). Two things keep a slow machine short of that.
The physics world allows 24 substeps of 1/120 s per frame, so frames as slow as 200 ms
still catch up. A speed governor watches the lag: past a quarter second the car cannot
speed up, past 0.6 s it eases down toward 8 m/s, and the cap lifts once the scene
catches up, with a "Speed held" toast when it engages. Heavy load from other processes,
such as test suites running in another checkout, is the usual cause.

## Verification

```sh
./test/test_lambda_gtest.exe --gtest_filter='*scene3d_midtown*'
./test/test_radiant_view_gtest.exe --gtest_filter='Scene3dTest.Midtown*'
# Direct replays after changing just the Lambda demo (also tests/mobile.json):
LAMBDA_HEADLESS_GLFW_WINDOW=1 ./lambda.exe view test/demo/midtown/midtown.ls \
  --headless --event-file test/demo/midtown/tests/desktop.json \
  --event-result temp/midtown/desktop-result.json
```

The golden also has an `open road` section: the loop's length and structures, the grade limit, the
deck heights the car drives on, a drive across the truss bridge and through the tunnel without a
scrape, a simulated minute of open-road traffic, Auto drive round the loop, a car fished out of the
river, and the place names. `tests/openroad.json` replays jumping out of town, Auto drive across the
bridge, the coast at night and a reset. Replays and captures run headless.

The golden `test/lambda/scene3d/midtown.ls` checks the city, the car's dynamics,
a simulated minute of traffic with no overlaps and no car off the road, pedestrian
dodging, the courier cycle, the cameras and scene validation on all tiers. The
desktop replay checks start, driving, courier, the night look and its headlights,
cameras, Auto drive, pause, the keys card and reset against native render stats and
pixel changes; the mobile replay drives with the touch pedals at phone width.
Recapture the index tile with
`LAMBDA_HEADLESS_GLFW_WINDOW=1 sh test/demo/previews/capture.sh midtown`.
