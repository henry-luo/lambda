// Invalid motion controllers fail natively with a specific diagnostic
// (Lambda_Pkg_Motion.md §9.2); none of these scenes is normalized first.
fn rig(id, controller) => <scene3d id: id, camera: "camera", width: 80, height: 60, style: "display:block",
    <camera id: "camera", type: 'perspective', position: [0.0, 0.0, 3.0]>
    <group id: id ++ "-group",
        <skeleton id: id ++ "-skeleton", <bone id: id ++ "-bone", position: [0.0, 0.0, 0.0], quaternion: [0.0, 0.0, 0.0, 1.0]>>
        <'animation-clip' id: id ++ "-a", duration: 1.0,
            <'keyframe-track' path: id ++ "-bone.quaternion", type: 'quaternion', interpolation: 'linear',
                times: [0.0, 1.0], values: [0.0, 0.0, 0.0, 1.0, 0.0, 0.7071, 0.0, 0.7071]>>
        <'animation-clip' id: id ++ "-b", duration: 1.0, autoplay: (id == "autoplay"),
            <'keyframe-track' path: id ++ "-bone.position", type: 'vector', interpolation: 'linear',
                times: [0.0, 1.0], values: [0.0, 0.0, 0.0, 0.0, 1.0, 0.0]>>
        controller>>;
<html
    <head <style "html,body{margin:0}">>
    <body
        rig("valid", <'motion-controller' id: "valid-motion", state: 'b',
            <'motion-state' name: 'a', clip: "valid-a">
            <'motion-state' name: 'b', clip: "valid-b", loop: 'once', next: 'a'>>)
        rig("missing", <'motion-controller' id: "missing-motion", <'motion-state' name: 'a', clip: "missing-z">>)
        rig("autoplay", <'motion-controller' id: "autoplay-motion", <'motion-state' name: 'b', clip: "autoplay-b">>)
        rig("shared", <'motion-controller' id: "shared-motion",
            <'motion-state' name: 'a', clip: "shared-a"> <'motion-state' name: 'again', clip: "shared-a">>)
        rig("duplicate", <'motion-controller' id: "duplicate-motion",
            <'motion-state' name: 'a', clip: "duplicate-a"> <'motion-state' name: 'a', clip: "duplicate-b">>)
        rig("next", <'motion-controller' id: "next-motion", <'motion-state' name: 'a', clip: "next-a", next: 'nowhere'>>)
        rig("request", <'motion-controller' id: "request-motion", state: 'z', <'motion-state' name: 'a', clip: "request-a">>)
        rig("loop", <'motion-controller' id: "loop-motion", <'motion-state' name: 'a', clip: "loop-a", loop: 'bounce'>>)>
>
