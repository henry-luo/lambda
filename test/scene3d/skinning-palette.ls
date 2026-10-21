// Skinning reads its bone matrices from a palette texture, so a skeleton may
// hold up to 256 bones (Lambda_Pkg_Motion.md M0). Each ribbon segment is bound
// to one child bone of a shared root; the clip lifts only the last eight bones,
// so the high palette rows move their segments and every other segment stays.
import s: lambda.scene3d

fn segment_x(k, count) => -1.2 + 2.4 * (k + 0.5) / count

// one quad per segment, bound to child bone k + 1 (the root is bone 0); `bound`
// overrides the bone so an over-sized skeleton still has valid skin indices
fn ribbon(prefix, count, bound) element {
    let w = 2.4 / count
    let corners = [[-0.5, -0.15], [0.5, -0.15], [0.5, 0.15], [-0.5, 0.15]]
    let positions = [for (k in 0 to count - 1) for (v in corners) for (c in 0 to 2)
        if (c == 0) segment_x(k, count) + v[0] * w else if (c == 1) v[1] else 0.0]
    let indices = [for (k in 0 to count - 1) for (i in [0, 1, 2, 0, 2, 3]) 4 * k + i]
    let joints = [for (k in 0 to count - 1) for (v in 0 to 3) for (c in 0 to 3)
        if (c > 0) 0 else if (bound == null) k + 1 else bound]
    let weights = [for (k in 0 to count - 1) for (v in 0 to 3) for (c in 0 to 3) if (c == 0) 1.0 else 0.0];
    s.mesh(s.geometry(positions, {indices: indices, 'skin-indices': joints, 'skin-weights': weights}),
        s.material('basic', {color: "#ff0000"}), {id: prefix ++ "-ribbon", skeleton: prefix ++ "-rig"})
}

fn rig(prefix, count) element =>
    <skeleton id: prefix ++ "-rig",
        <bone id: prefix ++ "-root", *[for (k in 0 to count - 1)
            <bone id: prefix ++ "-b" ++ string(k), position: [segment_x(k, count), 0.0, 0.0]>]>>

// the last eight bones rise by 0.6 over one second; a seek moves only a
// playing action, so the clip autoplays, and it lasts two seconds so the
// raised pose holds at 1s instead of wrapping back to the start
fn lift(prefix, count) element => s.clip(prefix ++ "-lift", [for (k in count - 8 to count - 1)
    s.track(prefix ++ "-b" ++ string(k) ++ ".position", [0.0, 1.0],
        [segment_x(k, count), 0.0, 0.0, segment_x(k, count), 0.6, 0.0], 'vector')],
    {autoplay: true, duration: 2.0})

fn viewport(prefix, bones, bound) element {
    let count = bones - 1;
    s.scene([
        s.camera({id: prefix ++ "-camera", position: [0.0, 0.0, 3.0]}),
        rig(prefix, count), ribbon(prefix, count, bound), lift(prefix, count)
    ], {id: prefix, camera: prefix ++ "-camera", width: 256, height: 96, viewBox: "0 0 256 96",
        background: "#ffffff", style: "display:block"})
}

let chain65 = s.normalize(viewport("chain65", 65, null))^
let chain256 = s.normalize(viewport("chain256", 256, null))^
// over the cap: the package validator would refuse it, so the native renderer's
// own diagnostic is what the fixture checks
let chain257 = viewport("chain257", 257, 1);
<html
    <head <style "html,body{margin:0;background:#ffffff}">>
    <body chain65 chain256 chain257>
>
