# Fishing

A cozy top-down fishing game in native Lambda. Row a little boat around Willow
Cove, cast, wait for a bite and strike. Everything is generated in code (the
cove, the boat and angler, every fish and the sea otter), and the game runs in
Radiant using `lambda.scene3d`.

```sh
./lambda.exe demo fishing
# Equivalent direct entry:
./lambda.exe view test/demo/fishing/fishing.ls
```

![Willow Cove](../previews/fishing.jpg)

| Key | Action |
| --- | --- |
| ↑ / W | Row forward |
| ↓ / S | Back water |
| ← → / A D | Turn |
| Space, Enter, or a click or tap on the water | Cast · reel in · strike |
| Drag | Orbit the camera round the boat (across swings it round, up and down raise and lower it) |
| T | Strike assist on/off |

Letting go of the arrows doesn't stop the oars mid-air: the stroke under way
finishes, still pulling, and then the oars ease down to rest. Rowing again picks
up at once.

Space casts while the boat is drifting slowly. Within a few seconds a fish
hurries over to the bobber, nibbles once or twice (the float twitches and
rings), then takes it: the float is dragged under. Strike within about two
seconds to hook it. Pressing while it only nibbles is just too soon: the fish
stays, and a toast says to wait for the float to go under. The angler then
fights it to the boatside, lifts it out for a look, and lets it go, and the
catch badge counts it. Rowing reels the line in first. The strike assist strikes
for you a beat after the bite.

## The cove

- **Water**: a GLSL film over the lit floor. It has a depth tint, a fresnel sky
  sheen, a broken foam lip at the beach, and toon sun glints cut from an
  animated wave normal, glittering in drifting bands. A second film lays thin
  drifting caustics on the floor, brightest in the shallows.
- **Ripples**: drawn like the wavy lines the moving surface throws, not as
  ruled circles. Each is one finely divided disc of vertex colour and alpha: a
  broad bright leading crest with a soft glow and a faint trough behind it, and
  on the larger ripples one weaker crest well back. Each ring meanders on its own, so no two run parallel,
  and its strength swells and fades round the ring, so it thickens, thins and
  nearly breaks. The bands widen as they spread, round ripples turn a little as
  they go, and they fade. A splash's foam crown is a soft band broken into
  patches. Three ripple emitters (small, medium, large), a
  foam crown and splash droplets are spawned where the oar blades bite and leave
  the water, behind the boat, at the bobber, under nibbles and bites, where a
  hooked fish thrashes, where fish rise to sip, around the otter, and where it
  dives, surfaces and rolls. The boat's own ripples are ovals stretched along
  the hull: one lapping round it, centred toward the stern, while it rests, and
  a trail behind the stern while it moves. Their emitters ride the boat, so
  each oval takes the hull's heading when it is born.
- **Plants and stones**: 70 noise-sculpted boulders with shaded crevices, lichen
  on top and algae below. 179 water-lily pads (notched, cupped, veined, fresh
  green to old gold), about one in five carrying a white, pink or yellow lily.
  Five lotus stands with round wavy leaves (some held above the water on
  stems), tall layered pink flowers with seed pods, and buds. Under water, a
  carpet of broad leafy sheets covers most of the floor: about 800 patches of a
  dozen overlapping leaves each, in mixed greens and teals. Among them grow
  broad strap weed a hand's width across, sword plants with big pointed leaves
  on short stalks, large lettuce-like rosettes, kelp, purple sea fans, moss
  balls, tube sponges, starfish and urchins. The underwater plants are
  instanced and lit by their own shader. A wave travels up every leaf, its bend
  growing with height and lagging further up, with a slower cross-wave and a
  slowly veering current. Each clump and each leaf has its own phase, so nothing
  moves in step, and each clump gets its own shade so the carpet reads as a
  patchwork. They are seen through the moving surface: crossing swells make
  them waver more the deeper they lie, deeper water veils them in the cove's
  colour, and each sheet's rim fades into the floor, so their outlines are soft. Reeds and bank tufts sway in shared wind clips, and every plant
  stays below the surface.
- **Fish**: 44 skinned fish of eight species: koi, rainbow trout, perch,
  bluegill, common carp, and schools of minnows and blue tang. Each has a blunt
  head with a darker mouth, a gill-cover line, a lateral line, scale shimmer,
  eyes with iris, pupil and highlight, and fins with rays; carp have barbels. A
  looping clip of travelling body waves drives each one, and its rate and
  weight follow swimming speed in bands with hysteresis. Steering eases into
  turns and depth changes, and the body's pitch and roll ease too, so nothing
  snaps between frames. Three spotted rays glide near the floor on
  flapping wings. Fish wander, school, shy from the boat, rise to the surface,
  come to the bobber (biting species only), and cast shadows on the floor.
  They cruise in the top half-metre or so, where the water is clear enough to
  see them. Most loners start in view, and wanderers are curious: about three
  times in four they pick their next spot just outside the boat's shy radius.
  A spot that would land on the bank swings to the cove side, so wherever the
  boat stops, fish gather round within a few seconds. A startled school
  re-forms once the scare is over.
- **Sea otter**: one smooth skin with no seams, sculpted as a signed distance
  field. Round cones and ellipsoids are blended with a smooth minimum and
  polygonised with surface nets, so the head, neck, body, limbs and tail flow
  into each other. It has a broad round head with puffy whisker pads, a glossy
  dark nose, beady eyes with catch-lights, a mouth line, whiskers and round
  ears. A thick neck runs into a long, slim body, with short forelegs ending in
  mitten paws, small webbed hind flippers (the outer toe longest) and a flat
  tapering tail. It is about a metre from nose to tail. The fur is warm brown,
  with a pale cream face, throat and belly, and a light-brown cap over the
  crown. Each vertex takes its colour and up to four bone weights from the
  sculpt parts around it, blending only bones that join, so a flipper never
  drags the tail. The otter keeps to the boat's stern quarter, or its bow
  quarter while the boat backs up, well outside the oars' reach, and it is
  quick: it paddles fast to keep station, and if the boat bears down on it,
  ahead or astern, it paddles straight out of the way to the side. While the
  oars are out it keeps clear of them too. If it is within an oar's reach, or
  will be in a moment as the boat turns or backs, it paddles straight out from
  the boat, so a blade never sweeps through it. It floats on its back with its head bent
  down to its tummy, so its face looks up at you over a pink clam held on its
  chest, and paddles on its back, face up, to keep up. Now and then, and
  whenever the boat rows hard, it rolls over and duck-dives, flippers up out of
  the water. It swims under water with a wave running down its body and paired
  flipper strokes, then surfaces nose-up and rolls onto its back again. An idle
  dive takes it under the boat to the other side. A catch earns a happy roll and
  a splash. The sim moves and turns the whole body; its clips (float, paddle,
  dive, swim, surface and two short blends) bend the spine, tail and limbs, with
  the paws placed by IK.
- **Boat and fisher**: a lofted rowboat painted orange-red, with an orange top
  strake over a cream stripe, wooden floorboards above the waterline, and oars
  with orange-tipped blades. The fisher is a seated adult as one skinned mesh: a
  wide woven straw hat with a red band, a bearded face, a cream striped shirt
  under blue denim bib overalls with brass buckles, and brown boots. Round caps
  on the shoulders, elbows and knees keep the limbs joined at any bend. Each hand
  is a fist: the back of the hand, a row of knuckles, four fingers curled round
  the grip in three joints, and the thumb wrapped over them. The fisher
  faces the bow and push-rows: at the catch the handles come back to the chest
  with the blades forward. The drive pushes them out over the knees with a lean
  from the hips, sweeping the blades aft on edge, and the boat surges with each
  drive. At the finish the blades lift out and turn flat, then skim back as the
  hands draw in, slower than the drive. Rowing, casting, nibbles, bites, the
  strike and the release are clips sampled from pose functions. A two-bone IK
  solve closes each fist round an oar handle, the rod grip or the reel, and the
  wrist turns the fist so its grip lies along the oar or the rod. The whole hull,
  and the reach of the oars, stays off the jetty, the rocks and the shallows. A
  move that would put any part of it there is refused, and so is a turn that
  would swing the hull's side in, so the boat never rows into the jetty. The
  oars are short, and in the drive the blades bury themselves just under the
  surface. A blade biting in throws a ring and a few drops, a swirl or two
  trails it through the pull, and it lifts out with a splash. Under the
  surface the blade is drawn through the water: shallower by the refractive
  index, wavering with the swells and veiled in the water's colour, so it seems
  to bend at the waterline. The fishing line is a skinned ribbon on eight bones:
  the rod tip, five points along the smooth curve it sags and drifts in, the
  bobber and the hook.

## How it is built

The scene is built once and its DOM never changes, so it is never re-projected.
Each frame the game steps as a pure function (**S12.1.1v5**) and hands the host
three kinds of command through the explicit Radiant contracts (**D9.5.6v2**):

- one physics command of pose targets for every kinematic body (boat, fish,
  otter, camera rig);
- an animation batch when a clip must switch or a fish's tail-beat changes;
- a particle batch for the frame's ripples and splashes.

The HUD (catch badge, hint, toast) changes only when its text does, and the
toast and badge pop through the presentation channel.

| Module | Owns |
| --- | --- |
| `mod_world.ls` | Shore, floor depth and colour, rocks, pads, plants, jetty, navigation |
| `mod_glsl.ls` | Water, caustic and underwater-plant shader bodies |
| `mod_rig.ls` | Shared skinning helpers: aim and two-bone IK, automatic weights, smooth surfaces, tubes, ellipsoids |
| `mod_sculpt.ls` | Implicit-surface sculpting: blended round cones and ellipsoids polygonised with surface nets, brick by brick |
| `mod_flora.ls` | Boulders, water lilies, lotus, and the underwater carpet, strap weed, sword plants, rosettes, kelp, sea fans and moss balls |
| `mod_lagoon.ls` | The cove's merged meshes, instanced plants, water films, wind and tide clips |
| `mod_fish.ls` | Species, roster, skinned fish and ray buffers, skeletons and swim clips |
| `mod_otter.ls` | The sea otter's sculpt, colouring, face details, rig, poses and clips, and its body motion through dives |
| `mod_fisher.ls` | The fisher's skinned body and rig, and IK tracks from hand targets |
| `mod_boat.ls` | Hull, gear, oars, rod, line rig and bobber, and the boat body holding the fisher |
| `mod_angler.ls` | Pose functions with hand targets, the catch path, and the clips sampled from them |
| `mod_scene.ls` | Physics world, lights, emitters, camera rig, and the assembled scene |
| `mod_sim.ls` | The game step: rowing, the fishing sequence, fish, otter and camera |
| `mod_present.ls` | Pose targets, clip and particle batches, and HUD writes |
| `mod_hud.ls`, `fishing.ls` | The page, input handlers and the frame loop |

The demo reuses Midtown's numeric and mesh helpers (`mod_util`, `mod_mesh`,
`mod_camera`), plus procart's seeded `rng` and `noise`.

Development aids:

- `FISHING_PROFILE=1` prints each frame's Lambda step and host hand-off cost
  (about 1 ms and 0.2 ms on a debug build).
- `FISHING_ASSIST=1` starts with the strike assist on.
- `FISHING_STAGE=bite` starts with the line out and a koi nibbling, which
  reaches the strike in about a second.

## Verification

From the repository root, with native graphics access:

```sh
./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/*scene3d_fishing*'
./test/test_radiant_view_gtest.exe --gtest_filter='Scene3dTest.Fishing*'
LAMBDA_HEADLESS_GLFW_WINDOW=1 ./lambda.exe demo fishing --headless \
  --event-file test/demo/fishing/tests/catch.json --event-result temp/fishing-catch.json
```

The model golden (`test/lambda/scene3d/fishing.ls`) checks the cove layout and
flora counts, the roster and its skinned buffers, the angler's rod tip, bobber, blades, catch path
and clips, the otter's rig, clips and dive timelines, and the pure game step. The step runs
through the otter's idle dive under the boat and its dive to keep up with hard rowing, and checks
that a working blade stays over a metre from it while the boat turns on the spot each way, backs,
and sets off after it has drifted under a resting oar. Backing and turning into the jetty, no point of
the hull's outline and no blade gets onto its deck, and the boat can still row away. That covers rowing out, gliding to a stop, a
cast, a reel-in, and a staged bite that the assist lands, counts and releases. Without the assist,
a press during the nibble keeps the fish, and a strike a second and a half into the bite hooks it.
`tests/desktop.json` rows, turns, casts, reels in and rows away, checking each
mode change and that the scene draws and moves. `tests/catch.json` turns on the
assist, casts and waits for a natural bite to be caught and counted. `tests/mouse.json`
casts with a click, orbits the camera with a drag (which must not reel in), and
reels in with a second click. Refresh the
catalog tile with `sh test/demo/previews/capture.sh fishing`.

## Design reference

The genre and mechanics follow cozy top-down fishing games, such as *Slackwater*,
a short game shown on raea.io: row, cast, watch the float, strike. Its source is
not published, and nothing of it is used here. The cove, characters, palette,
models and code are original to this demo. The sea otter is modelled on the
real animal's anatomy, colouring and behaviour.
