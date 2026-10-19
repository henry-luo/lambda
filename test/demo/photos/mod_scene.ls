import model: lambda.editor.raster.model
import editor: lambda.editor.raster
import tools: lambda.editor.raster.tools
import effects: lambda.editor.raster.effects

pub fn button(id,action,label) => <button id:id,'data-action':action,type:"button",label>
fn field(id,label,value="",kind="text") => <label *[<span label>,<input id:id,type:kind,value:string(value)>]>
pub fn layer_rows(st) => [for (row in editor.layers(st.doc))
    <div class:if (row.id in st.selected) "layer-row selected" else "layer-row",style:("padding-left:" ++ string(8+row.depth*16) ++ "px"),
        <button class:"visibility",'data-visibility':row.id,title:"Toggle visibility",if (row.node.visible == false) "○" else "●">
        <button class:"layer-name",'data-layer':row.id,title:row.node.name,
            <span class:"layer-kind",if (name(row.node) == 'stack') "▤" else "▧"> <span (row.node.name)>>
        if (model.mask(row.node) != null) <button class:if (st.target == "mask" and row.id in st.selected) "mask selected" else "mask",
            'data-mask':row.id,title:"Edit layer mask","M">
        if (row.node.locked == true) <span title:"Locked","L">
    >]
pub fn effect_rows(st) {
    let row = if (len(st.selected)==1) editor.lookup(st.doc,st.selected[0]) else null;
    [for (i,effect in effects.items(row.node)) <div class:if (i == st.effect_index) "effect-row selected" else "effect-row",
        <button 'data-select-effect':string(i),if (effect.enabled == false) "○ " ++ effect.name else "● " ++ effect.name>>]
}
fn histogram_channel(counts,c) {
    let bins = [for (bin in 0 to 63) sum(slice(counts[c],bin*4,bin*4+4))]
    let maximum = max(1,max(bins));
    // one filled path per channel bounds live histogram DOM work.
    let outline = "M 0 72 " ++ join([for (bin in 0 to 63) "L " ++ string(bin*4) ++ " " ++ string(72-72*bins[bin]/maximum)]," ") ++ " L 256 72 Z";
    <path fill:["#ef7777","#6acb8b","#72a7ed"][c],opacity:0.55,d:outline>
}
pub fn histogram_chart(counts) => <svg viewBox:"0 0 256 72",width:256,height:72,'aria-label':"RGB histogram",
    *[for (c in [0,1,2]) histogram_channel(counts,c)]>
pub fn tree() => <main id:"photos",tabindex:"0",'data-error':"", *[
    <header class:"menubar",<strong "λ Photos"> <nav 'aria-label':"Document commands", *[
        button("new","new","New"),button("open","open","Open…"),button("place","place","Place image…"),
        button("save","save","Save project…"),button("save-psd","psd","Export PSD…"),button("export","export","Export PNG…")]> <span class:"spacer"> <span id:"document-name","Untitled"> <span id:"dirty","">>,
    <section class:"options", *[button("undo","history.undo","Undo"),button("redo","history.redo","Redo"),
        <span class:"separator">,button("add-layer","layer.new","New layer"),button("duplicate","layer.duplicate","Duplicate"),
        button("group","layer.group","Group"),button("ungroup","layer.ungroup","Ungroup"),
        button("merge","layer.merge","Merge"),button("delete","layer.delete","Delete"),<span class:"spacer">,
        button("fit","fit","Fit"),button("actual","actual","100%"),button("zoom-out","zoom-out","−"),
        <output id:"zoom","Fit">,button("zoom-in","zoom-in","+")]>,
    <div class:"workspace", *[
        <nav class:"tool-rail",'aria-label':"Tools", *[
            *[for (tool in tools.catalog) <button id:"tool-" ++ tool.id,'data-tool':tool.id,
                title:tool.label ++ " (" ++ upper(tool.key) ++ ")",*[tool.icon,<small (tool.label)>]>]]>,
        <section id:"stage",class:"raster-stage",tabindex:"0",'aria-label':"Image workspace",
            <div class:"raster-surface",<canvas id:"preview",class:"raster-canvas",width:1,height:1,'aria-label':"Layer composition">
                <canvas class:"raster-selection",width:1,height:1,'aria-label':"Selection coverage"> <div class:"raster-target">>>,
        <aside class:"dock", *[
            <section class:"properties", *[<h2 "Properties">,
                <p id:"target-label","Select a layer">,
                field("layer-name","Name"),
                <label "Opacity" <input id:"opacity",type:"range",min:"0",max:"1",step:"0.01",value:"1">>,
                <div class:"fields", *[field("position-x","X",0,"number"),field("position-y","Y",0,"number")]>,
                <div class:"button-grid", *[button("front","front","Move up"),button("back","back","Move down"),
                    button("lock","layer.lock","Lock / unlock"),button("add-mask","mask.add","Add mask"),
                    button("toggle-mask","mask.toggle","Toggle mask"),button("remove-mask","mask.remove","Remove mask")]>]>,
            <section class:"layer-panel", *[<header <h2 "Layers"> <span id:"layer-count","0">>,<div id:"layers">]>,
            <section class:"paint-panel", *[<h2 "Paint & selection">,
                <p id:"tool-label","Move">,
                <div class:"fields", *[field("foreground","Foreground","#1a59f2"),field("background","Background","#ffffff"),
                    field("brush-size","Size",28,"number"),field("brush-hardness","Hardness",0.8,"number"),
                    field("brush-spacing","Spacing",0.2,"number"),field("brush-flow","Flow",1,"number"),
                    field("brush-opacity","Stroke opacity",1,"number"),field("tolerance","Color tolerance",0.1,"number")]>,
                <label "Selection mode" <select id:"selection-mode", *[for (mode in ["replace","add","subtract","intersect"]) <option value:mode,mode>]>>,
                <label "Gradient" <select id:"gradient-kind",<option value:"linear","Linear"> <option value:"radial","Radial">>>,
                field("feather-radius","Feather radius",4,"number"),
                <div class:"button-grid", *[button("select-all","selection.all","Select all"),button("deselect","selection.clear","Deselect"),
                    button("invert-selection","selection.invert","Invert"),button("feather","selection-feather","Feather"),
                    button("selection-mask","mask.from_selection","Selection → mask"),button("fill-color","paint.fill","Fill foreground"),
                    button("mask-invert","mask.invert","Invert mask"),button("mask-apply","mask.apply","Apply mask"),
                    button("mask-select","selection.from_mask","Select mask"),button("crop-selection","document.crop","Crop to selection"),
                    button("copy","clipboard.copy","Copy pixels"),button("copy-merged","clipboard.copy_merged","Copy merged"),
                    button("cut","clipboard.cut","Cut pixels"),button("paste","clipboard.paste","Paste pixels"),
                    button("swap-colors","swap-colors","Swap colors")]>]>,
            <section class:"adjustments", *[<h2 "Quick adjustments">,
                <div class:"button-grid", *[button("exposure-up","exposure-up","Exposure +"),button("exposure-down","exposure-down","Exposure −"),
                    button("mono","mono","Monochrome"),button("sharpen","sharpen","Sharpen"),
                    button("fill-white","fill-white","Fill white"),button("clear","pixels.clear","Clear target")]>]>,
            <section class:"image-size", *[<h2 "Image & canvas">,
                <div class:"fields", *[field("image-width","Width",1024,"number"),field("image-height","Height",768,"number")]>,
                <div class:"button-grid", *[button("resize-image","resize-image","Resize image"),button("resize-canvas","resize-canvas","Resize canvas"),
                    button("flatten","document.flatten","Flatten")]>]>,
            <section class:"history-panel",<h2 "History"> <div id:"history"> >,
            <section class:"transform-panel", *[<h2 "Transform & blend">,
                <div class:"button-grid", *[button("clipping","layer.clipping","Toggle clipping"),button("isolation","layer.isolation","Toggle isolation")]>,
                <label "Blend mode" <select id:"blend", *[for (mode in ["normal","multiply","screen","overlay","darken","lighten","difference","exclusion"]) <option value:mode,mode>]>>,
                <p "Transform tool: drag to scale; Shift-drag to rotate.">,
                <div class:"fields", *[field("transform-angle","Rotate degrees",0,"number"),field("transform-x","Scale X",1,"number"),field("transform-y","Scale Y",1,"number")]>,
                <div class:"button-grid", *[button("transform-apply","transform-apply","Apply transform"),button("transform-reset","transform-reset","Reset transform"),
                    button("flip-x","flip-x","Flip horizontal"),button("flip-y","flip-y","Flip vertical"),button("rasterize","layer.rasterize","Rasterize layer")]>]>,
            <section class:"actions-panel", *[<h2 "Actions">,<p id:"action-status","Record edits on one selected layer">,
                field("action-name","Action name","My action"),
                <div class:"button-grid", *[button("action-start","action-start","Record"),button("action-stop","action.stop","Stop"),
                    button("action-cancel","action.cancel","Cancel recording"),button("action-replay","action-replay","Replay on layer"),
                    button("action-save","action-save","Save action…"),button("action-open","action-open","Load action…")]>]>,
            <section class:"retouch-panel", *[<h2 "Retouch">,<p "Clone / Heal: Alt-click a source. Brush opacity controls coverage.">,
                <p id:"retouch-source","No source selected">,
                <div class:"fields", *[field("retouch-strength","Tone / heal strength",0.5,"number"),field("retouch-radius","Neighborhood radius",3,"number")]>,
                <label "Source alignment" <select id:"retouch-aligned",<option value:"aligned","Aligned"> <option value:"reset","Reset each stroke">>>]>,
            <section class:"histogram-panel",<h2 "Histogram"> <div id:"histogram">>,
            <section class:"effects-panel", *[<h2 "Editable effects">,<div id:"effects">,
                <div class:"fields", *[for (control in effects.controls) field("effect-" ++ control.key,control.label,control.value,"number")]>,
                <div class:"button-grid", *[button("effect-add","effect-add","Add effect"),button("effect-toggle","effect.toggle","Toggle effect"),
                    button("effect-remove","effect.remove","Remove effect"),button("effect-up","effect-up","Move effect up"),button("effect-down","effect-down","Move effect down")]>]>]>]>,
    <footer class:"statusbar",<output id:"status",role:"status",'aria-live':"polite","Opening sample…"> <span id:"dimensions","">>,
    <section id:"file-dialog",class:"dialog",style:"display:none",role:"dialog",'aria-modal':"true",'aria-label':"File operation", *[
        <h2 id:"file-title","Open image">,field("file-path","Path"),
        <p id:"file-help","PNG, JPEG and supported layered PSD files">,
        <div class:"button-grid", *[button("file-cancel","file-cancel","Cancel"),button("file-confirm","file-confirm","Continue")]>]>,
    <section id:"discard-dialog",class:"dialog",style:"display:none",role:"dialog",'aria-modal':"true",'aria-label':"Unsaved changes", *[
        <h2 "Save changes first?">,<p "This document has unsaved edits.">,
        button("discard-cancel","discard-cancel","Cancel"),button("discard-save","discard-save","Save project…"),
        button("discard-confirm","discard-confirm","Discard changes")]>]>
