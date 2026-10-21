// Humanoid roles for lambda.scene3d.motion (vibe/Lambda_Pkg_Motion.md §8.2).
// The canonical roles are the VRM 1.0 humanoid bone names; each rig profile
// maps one source naming, without namespace prefixes, onto them.

// both sides of a profile from one side's [source suffix, role suffix] pairs
fn sides(pairs) => [for (p in pairs) *[("Left" ++ p[0], symbol("left" ++ p[1])), ("Right" ++ p[0], symbol("right" ++ p[1]))]]

let digits = [["Thumb", "Thumb"], ["Index", "Index"], ["Middle", "Middle"], ["Ring", "Ring"], ["Pinky", "Little"]]
let limbs = ["Shoulder", "UpperArm", "LowerArm", "Hand", "UpperLeg", "LowerLeg", "Foot", "Toes"]
let finger_joints = ["Proximal", "Intermediate", "Distal"]

// every VRM 1.0 role
pub let roles = ['hips', 'spine', 'chest', 'upperChest', 'neck', 'head', 'leftEye', 'rightEye', 'jaw',
    *[for (l in limbs) *[symbol("left" ++ l), symbol("right" ++ l)]],
    *[for (side in ["left", "right"]) for (d in digits) for (k in (if (d[1] == "Thumb") ["Metacarpal", "Proximal", "Distal"] else finger_joints))
        symbol(side ++ d[1] ++ k)]]

// the 15 bones VRM 1.0 requires; retargeting needs every one on both skeletons
pub let required = ['hips', 'spine', 'head', 'leftUpperArm', 'leftLowerArm', 'leftHand', 'rightUpperArm',
    'rightLowerArm', 'rightHand', 'leftUpperLeg', 'leftLowerLeg', 'leftFoot', 'rightUpperLeg', 'rightLowerLeg', 'rightFoot']

// Mixamo, after any `mixamorig:` prefix: Spine2 is the upper chest, fingers count from 1
pub let mixamo = map([("Hips", 'hips'), ("Spine", 'spine'), ("Spine1", 'chest'), ("Spine2", 'upperChest'),
    ("Neck", 'neck'), ("Head", 'head'),
    *sides([["Shoulder", "Shoulder"], ["Arm", "UpperArm"], ["ForeArm", "LowerArm"], ["Hand", "Hand"],
        ["UpLeg", "UpperLeg"], ["Leg", "LowerLeg"], ["Foot", "Foot"], ["ToeBase", "Toes"]]),
    *sides([for (d in digits) for (i in 1 to 3)
        ["Hand" ++ d[0] ++ string(i), d[1] ++ (if (d[1] == "Thumb") ["Metacarpal", "Proximal", "Distal"][i - 1] else finger_joints[i - 1])]])])

// CMU motion capture as distributed in BVH: the spine runs LowerBack, Spine, Spine1
pub let cmu = map([("Hips", 'hips'), ("LowerBack", 'spine'), ("Spine", 'chest'), ("Spine1", 'upperChest'),
    ("Neck", 'neck'), ("Head", 'head'),
    *sides([["Shoulder", "Shoulder"], ["Arm", "UpperArm"], ["ForeArm", "LowerArm"], ["Hand", "Hand"],
        ["UpLeg", "UpperLeg"], ["Leg", "LowerLeg"], ["Foot", "Foot"], ["ToeBase", "Toes"]])])

// names that already are roles
pub let vrm = map([for (r in roles) (string(r), r)])
