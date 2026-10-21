// Presentation damage parity (vibe/radiant/Radiant_Design_Animation_Repaint.md §6): frame-driven
// presentation writes repaint only the written elements' old and new bounds, and each such frame
// must match a full render. Covers a 2D translate over gradients and text, an opacity fade, a
// preserve-3d card with backface-hidden faces, a box entering from off-screen, and every eighth
// frame an inline-style write that must fall back to a full repaint. The moving boxes are
// also paint layers (stage B): every tenth frame a text change inside the slider must
// recapture its layer, and hovering the slider changes its painted content.
import dom

let css = "body{margin:0;font:16px Helvetica,Arial,sans-serif;background:#f4f1ea}" ++
    ".stage{position:relative;width:900px;height:560px;overflow:hidden;background:linear-gradient(135deg,#dfe8ef,#f6efe2)}" ++
    ".stripes{position:absolute;left:40px;top:300px;width:820px;height:60px;background:repeating-linear-gradient(90deg,#c9bea8 0,#c9bea8 1px,#efe7d6 1px,#efe7d6 3px)}" ++
    ".slider{position:absolute;left:40px;top:40px;width:160px;height:90px;border-radius:10px;background:#0a84ff;color:#fff;" ++
    "box-shadow:0 4px 12px rgba(0,0,0,.3);padding:12px;box-sizing:border-box;transform:translate(0px,0px)}" ++
    ".slider:hover{background:#0050b0}" ++
    ".fader{position:absolute;left:600px;top:60px;width:200px;height:120px;background:radial-gradient(circle,#ffcc00,#ff6a00);opacity:1}" ++
    ".scene{position:absolute;left:330px;top:120px;width:240px;height:160px;perspective:900px}" ++
    ".card{position:absolute;inset:0;transform-style:preserve-3d;transform:rotateY(0deg)}" ++
    ".face{position:absolute;inset:0;backface-visibility:hidden;border-radius:8px;display:flex;align-items:center;justify-content:center;font-size:28px}" ++
    ".front{background:linear-gradient(90deg,#27493a,#5a8f6e);color:#fff}" ++
    ".back{background:repeating-linear-gradient(45deg,#6b2232 0,#6b2232 4px,#8a3446 4px,#8a3446 8px);color:#fff;transform:rotateY(180deg)}" ++
    ".comet{position:absolute;left:-150px;top:420px;width:120px;height:50px;border-radius:25px;background:#1f3557;color:#fff;" ++
    "text-align:center;line-height:50px;transform:translate(0px,0px)}" ++
    ".label{position:absolute;left:40px;top:500px}"

fn r2(v) => string(round(v * 100) / 100)

// the tree is built once and passed in: state changes must not regenerate (and so replace) it
let tree = <div id:"stage", class:"stage",
    <div class:"stripes">;
    <div id:"slider", class:"slider", <span id:"slider-label", "Translate">; <br>; "over gradients">;
    <div id:"fader", class:"fader">;
    <div class:"scene", <div id:"card", class:"card",
        <div class:"face front", "Front">; <div class:"face back", "Back">>>;
    <div id:"comet", class:"comet", "Comet">;
    <div id:"label", class:"label", "frame 0">>

view <stage> state frame_count:0, token:0 { ~.rendered }

on click(evt) {
    let stage = dom.get_element_by_id(dom.root_node(evt.target), "stage")
    token = dom.request_frame(stage, "tick")
    return 'handled'
}

on tick(evt) {
    if (evt.event_phase != 2 or evt.detail != token) { return 'pass' }
    let owner = evt.target
    let n = frame_count + 1
    let f = float(n)
    dom.presentation_style_set_property(dom.get_element_by_id(owner, "slider"), "transform",
        "translate(" ++ r2(f * 9.0) ++ "px," ++ r2(math.sin(f / 6.0) * 30.0) ++ "px)")
    dom.presentation_style_set_property(dom.get_element_by_id(owner, "fader"), "opacity", r2(0.5 + 0.5 * math.cos(f / 5.0)))
    dom.presentation_style_set_property(dom.get_element_by_id(owner, "card"), "transform", "rotateY(" ++ r2(f * 7.0) ++ "deg)")
    dom.presentation_style_set_property(dom.get_element_by_id(owner, "comet"), "transform", "translate(" ++ r2(f * 12.0) ++ "px,0px)")
    // an authored-style write in the same frame is not paint-only: that frame repaints in full
    if (n % 8 == 0) dom.style_set_property(dom.get_element_by_id(owner, "label"), "color", if (n % 16 == 0) "#9b2c2c" else "#1f3557")
    // a mutation inside a layer root: its retained raster must not survive it
    if (n % 10 == 0) dom.set_text_content(dom.get_element_by_id(owner, "slider-label"), "Frame " ++ string(n))
    frame_count = n
    token = if (n < 60) dom.request_frame(owner, "tick") else 0
    return 'handled'
}

<html <head <style css>> <body (apply(<stage rendered:tree>))>>
