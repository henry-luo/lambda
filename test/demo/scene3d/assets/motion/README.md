# Motion fixtures

These original fixtures are dedicated to the public domain under CC0. `_generate.py` regenerates every file with Python 3's standard library.

- `jump.bvh`: a 22-joint humanoid with CMU-style joint names, in centimetres, jumping for one second at 30 frames per second. The root has six channels and every other joint three, all in `Zrotation Xrotation Yrotation` order; the toes, head and hands end in End Sites.
- `expected.json`: world joint positions in centimetres at frames 0, 8, 15 and 30, computed by the generator's own forward-kinematics pass from the same source description. `test/lambda/scene3d/motion_bvh.ls` checks the BVH converter and `motion.pose` against them.

- `jump.fbx`, `jump-7500.fbx`, `jump-ascii.fbx`: the same skeleton and jump as FBX — binary 7.4 with raw arrays, binary 7.5 (64-bit record headers) with zlib-compressed arrays, and ASCII 7.4. Each joint is a `LimbNode` model in centimetres with `RotationOrder` YXZ (BVH's `Zrotation Xrotation Yrotation`), and one `AnimationStack` holds linear `Lcl Rotation` curves for every joint plus `Lcl Translation` curves for the hips at 30 frames per second. `test/lambda/scene3d/motion_fbx_raw.ls` checks that the three raw trees agree (vibe/Lambda_Pkg_Motion.md §4, §7.4).
