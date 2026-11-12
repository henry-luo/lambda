// lambda demo paint — the app shell (Lambda_Pkg_Paint.md PT10): toolbar, brush palette, colour,
// history and view controls, the stage holding the canvas, and the status line. The toolbar is two
// explicit rows that do not wrap: a column measures a wrapping row too short (Radiant RI-007).
import model: .mod_model

pub let SWATCHES = ["#1c1c1e", "#5a5a5f", "#a1a1a6", "#ffffff", "#7a2e1f", "#c0392b", "#e67e22", "#f1c40f",
    "#6b8e23", "#27ae60", "#16a085", "#2980b9", "#1b3a6b", "#8e44ad", "#d35d8f", "#f2d0b6"]

fn tool(id, label, key, glyph) => <button type:"button", class:"tool", id:"tool-" ++ id, 'data-tool':id,
    title:label ++ " (" ++ key ++ ")", 'aria-label':label, <span class:"glyph", glyph> <span class:"label", label>>

pub fn brush_buttons(st) => [for (i, b in st.brushes) <button type:"button", class:"brush", 'data-brush':string(i), b.name>]
// the palette markup the presenter compares before it rebuilds the brush buttons.
pub fn brush_markup(st) => join([for (b in brush_buttons(st)) format(b, 'xml')], "") ^ { "" }

fn slider(id, label, low, high, value) => <label class:"slider", <span class:"caption", label>
    <input id:id, type:"range", min:string(low), max:string(high), step:"1", value:string(value)>
    <span class:"value", id:id ++ "-value", "">>

fn paper_select() => <label class:"slider", <span class:"caption", "Paper">
    <select id:"paper", 'aria-label':"Paper texture", *[for (p in model.PAPERS) <option value:p.kind, p.label>]>>

fn medium_select() => <label class:"slider", <span class:"caption", "Medium">
    <select id:"medium", 'aria-label':"Canvas medium", *[for (m in model.MEDIA) <option value:m.kind, m.label>]>>

fn pigment_select() => <label class:"slider", <span class:"caption", "Pigment">
    <select id:"wet-pigment", 'aria-label':"Watercolor pigment", *[for (i, p in model.WET_PIGMENTS) <option value:string(i), p.name>]>>

fn zoom_label() => <span id:"zoom-value", class:"zoom", "100%">

fn action(id, label, title) => <button type:"button", class:"action", id:"action-" ++ id, 'data-action':id, title:title, label>

pub fn tree(st) => <div id:"paint", class:"paint-app", tabindex:"-1",
    <header class:"bars",
        <div class:"bar",
        <div class:"group tools", tool("brush", "Brush", "B", "✎") tool("eraser", "Eraser", "E", "⌫")
            tool("picker", "Picker", "I", "◉") tool("hand", "Hand", "H", "✋")>
        <div class:"group sliders", slider("size", "Size", 1, 400, 12) slider("opacity", "Opacity", 1, 100, 100)>
        <div class:"group colors",
            <span id:"color-chip", class:"chip", title:"Current colour">
            <div class:"swatches", *[for (c in SWATCHES) <button type:"button", class:"swatch", 'data-color':c,
                title:c, style:"background:" ++ c>]>
            <div class:"hsv", slider("hue", "H", 0, 359, 230) slider("sat", "S", 0, 100, 63) slider("val", "V", 0, 100, 30)>>
        <div class:"group file", <input id:"file-path", type:"text", value:model.FILE, 'aria-label':"Painting file">
            action("open", "Open", "Open a .painting file (⌘O)") action("save", "Save", "Save as a .painting file (⌘S)")
            action("export", "Export PNG", "Write the painting as a PNG beside the file")>>
        <div class:"bar",
        <div class:"group brushes", id:"brushes", 'data-items':brush_markup(st), *brush_buttons(st)>
        <div class:"group paper", paper_select()>
        <div class:"group medium", medium_select() pigment_select() action("dry", "Dry", "Set the wet paint, so new strokes glaze over it")
            action("light", "Light", "Show or hide the oil paint's relief under a light")>
        <div class:"group history", action("undo", "Undo", "Undo (⌘Z)") action("redo", "Redo", "Redo (⇧⌘Z)")
            action("clear", "Clear", "Clear the canvas")>
        <div class:"group view", action("zoom-out", "−", "Zoom out (-)") zoom_label()
            action("zoom-in", "+", "Zoom in (+)") action("fit", "Fit", "Fit the canvas (0)") action("actual", "1:1", "Actual pixels (1)")>
        <div class:"group import", <input id:"myb-path", type:"text", placeholder:"path/to/brush.myb", 'aria-label':"MyPaint brush file">
            action("import", "Import", "Add a MyPaint .myb brush to the palette")>>>
    <main id:"stage", class:"stage tool-brush",
        <div id:"sheet", class:"sheet", <canvas id:"canvas", width:string(model.WIDTH), height:string(model.HEIGHT)>>>
    <footer id:"status", class:"status", "Ready">
>
