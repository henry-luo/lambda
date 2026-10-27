# Dreamhome

`./lambda.exe demo dreamhome` is a home planner in the spirit of [Planner 5D](https://planner5d.com/) and [Sweet Home 3D](https://www.sweethome3d.com/). Draw walls and rooms on a floor plan, put in doors, windows and furniture from a catalog, and see the home in native 3D beside the plan: orbit over it with the walls cut down, or walk through it at eye height with the lamps lit for the evening.

## Using it

| Area | What it does |
| --- | --- |
| Homes | A furnished two-bedroom **City apartment** (80 m²), a **Garden studio** with an L-shaped main room (44 m²), or a **Blank plan**. Starting from one is one undo step |
| Tools | **Select**, **Wall** (drag to draw; ends snap to other walls and to 45° steps, Shift draws at any angle) and **Room** (drag a rectangle: a floor and its four walls, reusing a wall it shares) |
| Views | **2D**, **2D + 3D** (plan above, 3D below, as in Sweet Home 3D) or **3D** alone |
| Catalog | 49 pieces in eight categories (doors and windows, living, dining, kitchen, bedroom, bath, office, lighting and decor), with search. A click places a piece at a clear spot in the selected room, or in the room in the middle of the view; a door or window goes into the selected wall, or the wall nearest the middle of the view |
| Plan | Floors with their names and areas, walls with their lengths, doors with their swings, furniture seen from above. Drag a piece to move it (5 cm steps; Shift for free), its round knob to turn it (15° steps), its corner to resize it. Drag a wall to move it sideways with its corners and the rooms on them, or an end to reshape it. Drag a door or window along its wall, or onto another. Drag the floor to pan, the wheel zooms about the pointer |
| 3D | **Orbit**: drag to turn, Shift-drag to pan, wheel to zoom. **Walk through**: drag to look around, W A S D or the arrow keys to walk, or drag the blue walker in the plan. **Cut-away** lowers the walls to see in from above; **Evening** dims the daylight and lights the lamps. A click selects the piece, wall or room under it |
| Inspector | For a piece: size, height off the floor, turn, colour, duplicate, delete. For a door or window: width, height, sill, swing and hinge sides. For a wall: length, thickness, paint (or paint every wall). For a room: name and one of twelve floors. With nothing selected: the home's name, wall height, and lists of its rooms and furniture |
| Export | A floor plan image (PNG, 2400 px across), PDF or SVG with room areas and wall lengths; the 3D model as glTF binary (`.glb`) for Blender or a web viewer; or a `.mark` home file that **Open** reads back |

| Key | Action |
| --- | --- |
| ⌘Z / ⇧⌘Z / ⌘Y | Undo / redo |
| ⌘D | Duplicate the selected piece |
| ⌘S | Save the home file |
| R | Turn the selected piece 90° |
| Delete, Backspace | Delete the selected piece, wall or floor |
| Arrow keys | Nudge the selected piece 10 cm (Shift: 1 cm); in walk mode with nothing selected, walk |
| Escape | Deselect and return to the Select tool, or close a dialog |

Files are written to `temp/dreamhome/` unless you choose another folder, and are named after the home.

## How it is built

| File | Role |
| --- | --- |
| `dreamhome.ls` | The planner's state, its commands, the plan and 3D gestures, and the exports |
| `mod_catalog.ls` | The catalog: each piece is a size, a colour and a few parts (boxes, cylinders, cones, balls) in its own unit box; floors and paints |
| `mod_model.ls` | The home and its edits as pure functions: walls joined where their ends meet, doors and windows seated in walls, rooms, snapping |
| `mod_templates.ls` | The starter homes, drawn on wall centre lines |
| `mod_plan.ls` | The floor plan as SVG in centimetres; also the bare plan for the exports and the gallery |
| `mod_scene.ls` | The home as a native `scene3d`: walls split around their openings, triangulated floors, furniture, cameras, lamps, and picking under a click |
| `mod_thumb.ls` | The catalog's axonometric thumbnails |
| `mod_ui.ls`, `dreamhome.css` | The top bar, catalog, panes, inspector and dialogs |

A home is plain Lambda data in plan centimetres (x to the east, y to the south): `{title, wall_height, walls, rooms, items, seq}`. A wall is two ends, a thickness and a paint; a room is a polygon on wall centre lines and a floor; a piece is a catalog key, a centre, a turn, a size, a height off the floor and a colour, and a door or window also names its wall and its distance along it. Walls meet where their ends coincide and room corners sit on wall ends, so moving a corner moves every wall and room that shares it.

One recipe per piece draws it three ways. The 3D view scales four shared unit shapes into each part, with one standard material per colour and finish, so a furnished home stays a few hundred small meshes over four geometries; the plan draws the same parts from above, highest last; the catalog draws them as a small axonometric picture. Wood and stone floors, cloth, metal fixtures and transparent glass have distinct roughness. A room environment supplies reflections, neutral tone mapping keeps highlights, and the sun casts shadows through the wall openings. The renderer takes eight lights besides the ambient one, so the sun and the seven lamps nearest where the camera looks shine.

The planner is one `view` whose state holds the home, so every command re-renders it, plan and 3D together. Gestures are the exception (D4.5.1v4): a drag writes only the plan's own elements (a piece's `transform`, the walls layer, the `viewBox`) or the 3D camera's attributes, and the home changes once, on release, as one undo step. The undo history and file names are shared with Greetings in `../mod_document.ls`, and the colour arithmetic comes from Greetings' `mod_art.ls`.

## Tests

```sh
./lambda.exe test/demo/dreamhome/tests/dreamhome_model.ls
./test/test_ui_automation_gtest.exe --suite dreamhome
```

The first is the golden test of the catalog, the starter homes, edits with joined corners, floor triangulation, the plan, the 3D scene (validated as `scene3d`, exported as GLB), PBR finishes and picking; it runs in `make test-lambda-baseline`. The second replays `test/ui/dreamhome/planner.json`: catalog search, a piece added, turned, undone, dragged and deleted, the room and wall tools, the walk-through and evening light, a click picking a piece in 3D, and a home saved, replaced and opened again. `pbr_orbit.json` checks that eight real pointer moves change the image while retaining the projection, environment and shadow maps (D4.5.1v4 / D4.5.2). Scene3D needs graphics: run headless replays with `LAMBDA_HEADLESS_GLFW_WINDOW=1`.

With a release executable, `LAMBDA_SCENE3D_MEASURE_FRAMES=1` enables the orbit fixture's 120-frame camera measurement. Run it alone with `--suite dreamhome --test pbr_orbit` and `LAMBDA_UI_TEST_JOBS=1`. Set `LAMBDA_EVENT_SIM_OUTPUT_DIR=temp` to write `temp/dreamhome-orbit-metrics.json`: whole-page frame and camera-write p50/p95 times, plus projection, snapshot, environment and shadow deltas. It requires one snapshot per frame and no projection, environment, shadow or GPU-memory growth. `LAMBDA_SCENE3D_CAPTURE_DIR=temp` also saves the initial page as `temp/dreamhome-pbr.png`.

The remaining native PBR properties are animated through the shared scene
track reader, and MTL PBR factors/maps now import as standard materials. See
the [Scene3D package guide](../../../doc/Lambda_Packages.md#27-scene3d--native-3d-scenes)
for their supported ranges and import limits. The separate
[physical-material fixture](../../scene3d/physical.ls) demonstrates the newer
clearcoat/sheen profile; Dreamhome continues to use standard finishes.
The original external glTF
acceptance models have a reproducible native/Three.js comparison (Python with
Pillow, Node/Puppeteer, graphics access and `CHROME_HEADLESS_SHELL` required):

```sh
python3 test/scene3d/oracle/gltf_samples.py --fetch-only
LAMBDA_HEADLESS_GLFW_WINDOW=1 python3 test/scene3d/oracle/gltf_samples.py --prepared
```

Downloads, licenses, source hashes, images and comparison results stay under
`temp/gltf-samples/`. `--lambda` selects a frozen executable for an isolated
run. The [implementation record](../../../vibe/impl/Radiant_Scene3d_PBR_Shadows.md#11-progress)
separates the qualified snapshot speedup from remaining whole-page frame cost.
