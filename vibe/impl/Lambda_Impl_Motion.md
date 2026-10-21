# Lambda Motion — implementation record

**Design:** [Lambda_Pkg_Motion.md](../Lambda_Pkg_Motion.md) (the `<character>` schema, raw FBX/BVH mappings, converters, the `lambda.scene3d.motion` package and the Radiant host changes).
**Status:** M0–M5 implemented on 2026-10-21 on branch `motion` (worktree `.claude/worktrees/motion`), six commits on top of `b0d3c9425`; not merged. The user selected every recommended §13 option and asked for all six milestones.
**Formal linkage:** applies existing rulings only — **S12.1.1v3** (every package function is pure; playback is a host effect), **S12.4.1v3** and **D7.1.2v2** (inputs and textures through the input layer), **D4.1.3** (raw and converted values, decoded arrays included, belong to the destination Input), **S2.6.4** (ordered FBX properties travel in one `props` array), **S9.3.2** and **D2.6.1v3** (typed FBX arrays keep their element kind as `ArrayNum`), **S12.1.5v2** (the template owns the requested motion state, the host owns playback), **D7.5.3v2** and **D7.4.2v2** (controller events through the existing host path), **D7.3.5** (each milestone names its gate). No formal ruling changed.

## 1. Delivered

| Milestone | Commit | What landed | Gate |
|---|---|---|---|
| M0 Palette | `85aaf38cf` | Bone palette as an `RGBA32F` data texture (`native_gl_data_texture`, `texelFetch` in the vertex shader), dynamic per-mesh bone arrays, 256 bones per skeleton with a diagnostic beyond; validators, glTF and A3D caps moved together; clip track cap 1,024 in the scene parser. | `SkinningPaletteHolds256BonesAndRejectsMore`; Scene3D suite and native units |
| M1 Schema + BVH | `dbccb6f89` | `<bvh>` raw mapping (2-D float motion matrix), MIME detection, `input(…, {type: 'character'})` with the BVH converter, `SceneAsset` generalized to non-group roots, the schema types and `validate`, `load` (`scale`, `up`, `clip`), `pose`, `mannequin`, `scene`; quaternion helpers in `transform.ls`. | `motion_bvh` golden (FK reference within 1 µm, all tiers); `BvhJumpDrivesMannequinThroughCharacterSchema` |
| M2 FBX raw | `972d2306e` | `lambda/input/input-fbx.cpp`: binary 7.1–7.7 (64-bit headers from 7500) and ASCII 7.x into `<fbx>`; exact property kinds; `Class::Name` rewriting; record-bound, depth, per-array and 256 MiB decoded-budget checks; `lib/zlib_exact` shared with PSD. | `motion_fbx_raw` golden (binary 7.4, binary 7.5 with zlib arrays and ASCII agree); four `ModelInputTest.Fbx*` units; PSD units |
| M3 FBX → character | `f7a515519` | `lambda/input/input-character-fbx.cpp`: object graph (OO/OP, dangling IDs and cycles rejected), GlobalSettings axes and units into metres/Y-up/+Z, the full transform chain with rotation orders, body geometry unwelded per corner and split per material, embedded PNG/JPEG textures as data URIs, top-4 skin weights, one inverse bind per joint (cluster or BindPose), rigid props, curve baking. | `motion_fbx` golden (BVH and three FBX files agree at every key; joints, skinned vertices and the prop match the generator's FBX evaluator within 1 µm; eleven malformed inputs); `FbxJumpSkinsTexturedBodyThroughCharacterSchema` |
| M4 Operations | `7f1342854` | `lambda.scene3d.motion.humanoid` (55 VRM 1.0 roles; mixamo, cmu and vrm profiles), `humanoid`, `load`'s `rig`, `attach` (copy or retarget), `retarget` (world rotation change from rest, `rest_frame`), `in_place`, `mirror`, `trim`; `transform.inverse`. | `motion_ops` golden |
| M5 Controller | `8f11c7ad7` | `<motion-controller>`/`<motion-state>` in `radiant/scene3d_animation.cpp` (requested state, crossfade, `next` after `once`, `statechange` event, segment-exact seek), `scene3d_follow_rebuild` for template re-renders, content-only animation source hash, mixer track cap 1,024; `motion.control`, `motion.motion_state`, `model.ls` validation. | `MotionControllerCrossfadesChainsAndReplays`, `MotionControllerRejectsInvalidStates`; `motion_control` golden |

The branch adds about 2,970 lines of engine and package code (`lambda/`, `radiant/`, `lib/`, `lmd/`) and the rest in fixtures, tests and documentation.

## 2. Findings and deviations

- **Rotation orders.** BVH lists rotation channels outermost first and FBX names `RotationOrder` by application, so BVH `Zrotation Xrotation Yrotation` is FBX `YXZ` (enum 3). The jump written both ways agrees to 2·10⁻⁸ m, the float32 rounding of FBX key values.
- **BVH position channels (§13 decision 8)** replace `OFFSET` as proposed. They have not been compared with the pinned Three.js `BVHLoader`; see §4.
- **FBX `L` properties** are Lambda `i64` values (the proposal's table said `int64`; corrected there).
- **Joint matching (§8.3 step 1).** Matching by name before role paired CMU `Spine` (chest) with Mixamo `Spine` (spine). Roles now win wherever both joints are tagged; names remain the fallback. Recorded in the proposal's Appendix C.
- **`state` is a reserved word** in Lambda, so the state constructor is `motion.motion_state`, named after its element.
- **Template re-renders rebuild their DOM subtree.** The proposal assumed a regenerated `<scene3d>` keeps its nodes. Radiant replaces a view's result subtree and preserves state through `*_follow_rebuild` hooks; `scene3d_follow_rebuild` now carries the entry, its GL context and its playback to the corresponding viewport, and invalidates the cached mutation epoch so the next compile reconciles. The animation source hash now covers content only; bindings still require their bone and mesh nodes to stay connected, so a structurally different rebuild restarts playback as before.
- **Event-sim `update` and `seek`** act on the last compiled scene. The live host compiles before every frame; the controller fixture takes a `snapshot` after each click to do the same.
- **The mixer still capped clips at 256 tracks** after M0 raised the scene parser to 1,024; M5 raised the mixer to match.
- **Euler subdivision** splits an interval while the sum of its three axis changes reaches 180°, which bounds the composite rotation below half a turn (stricter than the proposal's per-axis 180°).
- **Cubic sampling.** When any key of a stack is cubic, every frame of the stack is sampled (a superset of sampling only cubic segments). Weighted tangents are evaluated unweighted, with a warning.
- **`trim`** snaps to keys within 1 µs: BVH frame times carry the error of their eight-decimal `Frame Time`.
- **Hip height** for root-motion scaling is the root's rest height above the lowest joint; BVH roots rest at the origin, so the root's absolute height would be zero.
- **Layering.** The input layer (`lambda-boundary-io`) cannot call runtime functions such as `array_num_get_number_value`; the converter reads typed arrays through the mark reader.
- **`InheritType`** other than 1 (RSrs) is rejected, including 0 (RrSs).
- **Events reach Lambda handlers (§13 decision 10).** `on statechange(evt)` in a Lambda view receives `controller`, `from`, `to` and `time`; `finished` and `loop` use the same path.

## 3. Acceptance against §12

| Milestone | Verified | Not verified |
|---|---|---|
| M0 | 256 bones render and a 257th is a diagnostic; Scene3D suite and native units pass; the palette texture is a counted GPU resource. | A separate 65-bone fixture (the 256-bone case covers the path). |
| M1 | Goldens on interp, JIT and auto; world joint positions match an independent forward-kinematics pass within 1 µm; mannequin pixels change between frames. | Agreement with Three.js `BVHLoader`. |
| M2 | Binary 7.4, binary 7.5 and ASCII trees agree modulo numeric kinds; every malformed-record case has a specific diagnostic; PSD tests pass. | — |
| M3 | Output validates; BVH and the three FBX files agree at every key; joints, skinned vertices and a rigid prop match the generator's own FBX evaluator (pivots, pre/post rotation, rotation orders, non-uniform scale, Z-up inches, rest ≠ bind, five influences, cubic keys, a 350° Euler turn); a generated CC0 textured character renders and animates. | ufbx comparison; a third-party CC0 character; the local-only Mixamo check (no Mixamo files on this machine). |
| M4 | Goldens on every tier; a CMU-named jump on a 1.5× Mixamo-named giant matches 1.5× the source world positions (feet stay on the ground); retargeting onto an A-pose reproduces the source's world rotation changes; every output validates. | Comparison with Three.js `SkeletonUtils`; real Mixamo and CMU files. |
| M5 | Fixed-time captures through jump → flip → rise → jump; an unrelated re-render keeps playback; a new cue re-triggers; seek replays the request and its `next` chain; GPU bytes and resources are unchanged across transitions; invalid controllers fail natively and in `model.ls`. | — |

## 4. Open items

- **Reference oracles.** The pinned Three.js 0.186.1 `BVHLoader`, `FBXLoader` and `SkeletonUtils`, and a ufbx test tool, need a download the user has not approved. The generator's independent evaluators stand in for now; decision 8 waits on `BVHLoader`.
- **Third-party characters.** A CC0 character from Quaternius or Kenney, pinned with a hash, and the local-only Mixamo acceptance check.
- **Physics across template rebuilds.** `scene3d_follow_rebuild` carries animation, not `Scene3dPhysics`, which is still recreated when a view re-renders.
- **Package import warnings.** Importing `lambda.scene3d.motion` logs shadow-lint warnings for `load` (as `lambda.scene3d.asset` already does).
- **Later stages** remain as listed in the proposal's §12.

## 5. Tests

| Test | Covers |
|---|---|
| `test/lambda/scene3d/motion_bvh.ls` | BVH raw tree, conversion, FK reference, mannequin and scene compilation, malformed BVH |
| `test/lambda/scene3d/motion_fbx_raw.ls` | FBX twins, kinds, names, malformed ASCII |
| `test/lambda/scene3d/motion_fbx.ls` | Cross-format agreement, body, skinning, the rig, BindPose fallback, eleven malformed graphs |
| `test/lambda/scene3d/motion_ops.ls` | Profiles, attach (copy and retarget), retarget, `rest_frame`, in_place, mirror, trim, misuse |
| `test/lambda/scene3d/motion_control.ls` | `control`, `motion_state`, scene validation of controllers |
| `test/test_input_model_gtest.cpp` `Fbx*` | Binary property kinds, malformed records and arrays, ASCII errors, detection |
| `test/scene3d/` cases | Palette, BVH mannequin, textured FBX body, controller playback, invalid controllers |
| `test/demo/scene3d/assets/motion/_generate.py` | CC0 fixtures: jump (BVH, FBX ×3), rig, giant, A-pose, and the reference evaluations |
