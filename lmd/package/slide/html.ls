import c: .common
import samples: .sample
import theme: .theme

pub let css = "
html, body { margin: 0; width: 100%; min-height: 100%; }
.slide-page { background: #111827; font-family: Arial, sans-serif; }
.slide-stage { position: relative; overflow: hidden; margin: 0 auto; }
.slide-canvas { position: absolute; left: 0; top: 0; overflow: hidden; transform-origin: 0 0; }
.slide-layer { position: absolute; left: 0; top: 0; overflow: hidden; }
.slide-object { position: absolute; box-sizing: border-box; transform-origin: center center; }
.slide-effect { width: 100%; height: 100%; transform-origin: center center; }
.slide-text { width: 100%; height: 100%; white-space: pre-wrap; }
.slide-text p { margin: 0 0 0.6em; }
.slide-image { width: 100%; height: 100%; }
.slide-controls { display: flex; align-items: center; justify-content: center; flex-wrap: wrap;
    gap: 8px; padding: 14px 24px; background: #102333; border-top: 1px solid #2b4353; color: #dce8ee; }
.slide-controls button, .slide-console button, .slide-overview button, .slide-toolbar-toggle { appearance: none; box-sizing: border-box; font-family: inherit;
    font-size: 13px; font-weight: 600; line-height: 20px; padding: 8px 16px;
    border: 1px solid #365263; border-radius: 8px; background: #1b3445; color: #e5eef2;
    cursor: pointer; box-shadow: 0 1px 2px #081724; }
.slide-controls button:hover { background: #28495c; border-color: #5c8495; }
.slide-controls button:active { background: #122b3c; }
.slide-controls button:focus-visible { outline: 2px solid #8be2c6; outline-offset: 3px; }
.slide-controls button[data-slide-command=next] { background: #8be2c6; border-color: #8be2c6;
    color: #102b35; font-weight: 700; padding-left: 24px; padding-right: 24px; }
.slide-controls button[data-slide-command=next]:hover { background: #b0efdb; border-color: #b0efdb; }
.slide-controls button[data-slide-command=next]:active { background: #69c8ab; }
.slide-controls button[data-slide-command=restart] { margin-left: 8px; background: transparent; }
.slide-controls button[data-slide-command=restart]:hover { background: #28495c; }
.slide-speed { display: flex; align-items: center; gap: 10px; margin-left: 8px;
    padding: 8px 12px; border: 1px solid #2b4353; border-radius: 8px;
    font-size: 12px; font-weight: 600; line-height: 20px; color: #afc5d0; }
.slide-speed-input { width: 104px; height: 20px; margin: 0; padding: 0; cursor: pointer; accent-color: #8be2c6; }
.slide-speed-input:focus-visible { outline: 2px solid #8be2c6; outline-offset: 3px; }
.slide-speed-value { min-width: 36px; color: #8be2c6; text-align: right; }
.slide-status { margin-left: 8px; padding: 8px 14px; border: 1px solid #2b4353; border-radius: 8px;
    font-size: 13px; font-weight: 600; line-height: 20px; letter-spacing: 0.5px; color: #afc5d0; }
.slide-player { position: relative; }
.slide-workspace { display: flex; align-items: flex-start; }
.slide-audience { position: relative; flex-shrink: 0; }
.slide-toolbar-toggle { position: absolute; top: 6px; left: 6px; z-index: 20; opacity: 0.7; padding: 4px 8px; font-size: 11px; }
.slide-picker { width: 70px; box-sizing: border-box; padding: 8px; border: 1px solid #365263; border-radius: 6px;
    background: #1b3445; color: #e5eef2; font-size: 13px; }
.slide-picker-label { font-size: 12px; color: #afc5d0; }
.slide-console { display: none; box-sizing: border-box; flex-shrink: 0; padding: 16px; overflow: auto;
    background: #102333; color: #e5eef2; border-left: 1px solid #365263; font: 14px Arial, sans-serif; }
.slide-console h2 { font-size: 18px; margin: 0 0 16px; }
.slide-console h3 { font-size: 14px; margin: 20px 0 10px; color: #8be2c6; }
.slide-console h4 { font-size: 15px; margin: 0 0 10px; }
.slide-notes { line-height: 1.5; }
.slide-timer-controls { display: flex; flex-wrap: wrap; align-items: center; gap: 8px; }
.slide-timer { display: block; width: 100%; font: 28px monospace; color: #8be2c6; }
.slide-console button { padding: 4px 8px; font-size: 12px; }
.slide-preview-stage { position: relative; overflow: hidden; }
.slide-preview-canvas { position: absolute; left: 0; top: 0; transform-origin: 0 0; pointer-events: none; }
.slide-preview-end { display: none; color: #afc5d0; }
.slide-overview { display: none; position: absolute; left: 8px; right: 8px; top: 8px; bottom: 8px;
    box-sizing: border-box; z-index: 12; padding: 12px; overflow: auto; background: #102333; color: #e5eef2; border-radius: 12px; }
.slide-overview h2 { margin: 0 0 8px; font: 18px Arial, sans-serif; }
.slide-overview-list { display: flex; flex-wrap: wrap; gap: 12px; margin-top: 8px; }
.slide-overview-list button { width: 140px; min-height: 48px; text-align: left; white-space: normal; }
.slide-overview-list button[aria-current=true] { border-color: #8be2c6; color: #8be2c6; }
.slide-blackout { display: none; position: absolute; left: 0; top: 0; width: 100%; height: 100%; background: #000000; z-index: 15; }
.slide-pointer { display: none; position: absolute; left: 0; top: 0; width: 14px; height: 14px; border-radius: 50%;
    background: #ff3344; border: 2px solid #ffffff; box-sizing: border-box; box-shadow: 0 0 10px #ff3344; pointer-events: none; z-index: 14; }
.slide-player[data-slide-blackout=true] .slide-toolbar-toggle { visibility: hidden; }
"

// compare sampled channels before formatting; steady channels need no CSS patch.
pub fn effect_properties(v, previous = null) {
    let invisible = v.visible < 0.5 or v.opacity <= 0.0
    let visibility_changed = previous == null or invisible != (previous.visible < 0.5 or previous.opacity <= 0.0)
    let clip_changed = previous == null or v.clip != previous.clip or
        (v.clip > 0.0 and v.clip_direction != previous.clip_direction)
    let clip_css = if (not clip_changed) null else if (v.clip <= 0.0) "none" else {
        let amount = c.fmt(v.clip * 100.0) ++ "%"
        "inset(" ++ (if (v.clip_direction == 'left') "0 0 0 " ++ amount
            else if (v.clip_direction == 'right') "0 " ++ amount ++ " 0 0"
            else if (v.clip_direction == 'top') amount ++ " 0 0 0" else "0 0 " ++ amount ++ " 0") ++ ")"
    }
    {*: if (previous == null or v.opacity != previous.opacity) {opacity: c.fmt(v.opacity)} else {},
        *: if (visibility_changed) {visibility: if (invisible) "hidden" else "visible",
            ["pointer-events"]: if (invisible) "none" else "auto"} else {},
        *: if (previous == null or v.tx != previous.tx or v.ty != previous.ty or
            v.rotation != previous.rotation or v.sx != previous.sx or v.sy != previous.sy)
            {transform: "translate(" ++ c.px(v.tx) ++ "," ++ c.px(v.ty) ++ ") rotate(" ++
                c.fmt(v.rotation) ++ "deg) scale(" ++ c.fmt(v.sx) ++ "," ++ c.fmt(v.sy) ++ ")"} else {},
        *: if (clip_changed) {["clip-path"]: clip_css} else {}}
}

pub fn effect_style(v) => join([for (key, value in effect_properties(v)) string(key) ++ ":" ++ value ++ ";"], "")

fn shape_content(obj, visual, instance) {
    let src = obj.source
    let paint_fill = visual.paint
    let stroke = if (src.kind == 'line') paint_fill else c.value(src.stroke, "none")
    let stroke_width = c.num(src, "stroke_width", 1.0)
    let kind = c.value(src.kind, 'rect')
    let paint = {id: instance ++ "-paint-" ++ obj.dom_key, fill: paint_fill,
        stroke: stroke, ["stroke-width"]: stroke_width};
    <svg width: "100%", height: "100%", preserveAspectRatio: "none", viewBox: "0 0 " ++ c.fmt(obj.width) ++ " " ++ c.fmt(obj.height),
        if (kind == 'rect') <rect *: paint, width: obj.width, height: obj.height, rx: c.num(src, "radius", 0.0)>
        else if (kind == 'line') <line *: paint, x1: 0.0, y1: 0.0, x2: obj.width, y2: obj.height>
        else <ellipse *: paint, cx: obj.width / 2.0, cy: obj.height / 2.0,
            rx: if (kind == 'circle') min(obj.width, obj.height) / 2.0 else obj.width / 2.0,
            ry: if (kind == 'circle') min(obj.width, obj.height) / 2.0 else obj.height / 2.0>
    >
}

fn object_content(obj, visual, visuals, instance, theme) {
    let src = obj.source
    if (obj.tag == 'group') for (child in obj.children) render_object(child, visuals, instance, theme)
    else if (obj.tag == 'shape') shape_content(obj, visual, instance)
    else if (obj.tag == 'image') <img class: "slide-image", src: src.src,
        style: "object-fit:" ++ c.as_text(c.value(src.fit, 'contain')) ++ ";">
    else if (obj.tag == 'text') {
        let size = obj.font_size
        let color = c.as_text(c.value(visual.paint, theme.foreground));
        <div class: "slide-text", style: "font-size:" ++ c.px(size) ++ ";color:" ++ color ++ ";", *content(src)>
    } else for (child in content(src)) child
}

pub fn placement_properties(obj) => {left: c.px(obj.x), top: c.px(obj.y),
    width: c.px(obj.width), height: c.px(obj.height),
    transform: "rotate(" ++ c.fmt(obj.rotation) ++ "deg) scale(" ++ c.fmt(obj.scale) ++ ")"}

pub fn placement_style(obj) => join([for (key, value in placement_properties(obj))
    string(key) ++ ":" ++ value ++ ";"], "")

fn render_object(obj, visuals, instance, theme) {
    let matches = [for (v in visuals where v.id == obj.id) v]
    let v = matches[0]
    let hidden = v.visible < 0.5 or v.opacity <= 0.0
    let position = placement_style(obj)
    // percentage-sized SVG/images need definite bounds; groups also anchor local children.
    let content_style = "width:100%;height:100%;" ++ (if (obj.tag == 'group') "position:relative;" else "") ++
        c.as_text(c.value(obj.source.style, ""));
    <div id: instance ++ "-place-" ++ obj.dom_key, class: "slide-object", style: position,
        <div id: instance ++ "-" ++ obj.dom_key, class: "slide-effect", style: effect_style(v),
            ["data-slide-object"]: obj.id, ["aria-hidden"]: if (hidden) "true" else "false", *: (if (hidden) {inert: ""} else {}),
            <div class: c.as_text(c.value(obj.source.class, "")), style: content_style,
                object_content(obj, v, visuals, instance, theme)
            >
        >
    >
}

pub fn layer_background(plan, scene) => c.as_text(theme.background(scene))

// `active` marks the layer the player currently shows (state, not structure).
pub fn layer(plan, scene, visuals, instance, style = "", active = false) {
    <section id: instance ++ "-s" ++ string(scene.index), class: "slide-layer",
        ["aria-label"]: c.as_text(c.value(scene.source.title, scene.id)),
        *: (if (active) {["data-slide-active"]: "true"} else {}),
        style: "width:" ++ c.px(plan.width) ++ ";height:" ++ c.px(plan.height) ++
            ";background:" ++ layer_background(plan, scene) ++ ";font-family:" ++ scene.palette.font_family ++ ";" ++ style,
        for (obj in scene.objects) render_object(obj, visuals, instance, scene.palette)
    >
}

pub fn fit(plan, viewport_w, viewport_h) {
    let scale = min(viewport_w / plan.width, viewport_h / plan.height)
    let x = (viewport_w - plan.width * scale) / 2.0
    let y = (viewport_h - plan.height * scale) / 2.0
    {scale: scale, x: x, y: y, transform: "translate(" ++ c.px(x) ++ "," ++ c.px(y) ++ ") scale(" ++ c.fmt(scale) ++ ")"}
}

pub fn stage(plan, address, options = {}) {
    let scene = plan.slides[address.slide]
    let viewport_w = c.value(options.width, plan.width)
    let viewport_h = c.value(options.height, plan.height)
    let fitted = fit(plan, viewport_w, viewport_h)
    let instance = c.as_text(c.value(options.instance, "slide"));
    <div class: "slide-stage", style: "width:" ++ c.px(viewport_w) ++ ";height:" ++ c.px(viewport_h) ++ ";",
        <div class: "slide-canvas", style: "width:" ++ c.px(plan.width) ++ ";height:" ++ c.px(plan.height) ++
            ";transform:" ++ fitted.transform ++ ";",
            layer(plan, scene, samples.scene(scene, address.cue, address.time_ms), instance, "", true)
        >
    >
}

pub fn document(plan, body) {
    let base = if (plan.base_uri != null) <base href: plan.base_uri> else null;
    <html lang: "en", <head *[<meta charset: "UTF-8">, <title plan.title>, base, <style css>]>
        <body class: "slide-page", body>>
}
