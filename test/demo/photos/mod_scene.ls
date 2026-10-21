import model: lambda.editor.raster.model
import editor: lambda.editor.raster
import tools: lambda.editor.raster.tools
import effects: lambda.editor.raster.effects
import styles: lambda.editor.raster.styles
import transforms: lambda.editor.raster.transform
import tone: lambda.editor.raster.tone

pub fn button(id,action,label) => <button id:id,'data-action':action,type:"button",label>
fn field(id,label,value="",kind="text") => <label *[<span label>,<input id:id,type:kind,value:string(value)>]>
pub fn layer_rows(st) => [for (row in editor.layers(st.doc))
    <div class:if (row.id in st.selected) "layer-row selected" else "layer-row",style:("padding-left:" ++ string(8+row.depth*16) ++ "px"),
        <button class:"visibility",'data-visibility':row.id,title:"Toggle visibility",if (row.node.visible == false) "○" else "●">
        <button class:"layer-name",'data-layer':row.id,title:row.node.name,
            <span class:"layer-kind",if (name(row.node) == 'stack') "▤" else if (name(model.primary(row.node)) == 'text') "T"
                else if (name(model.primary(row.node)) == 'vector') "◇" else "▧"> <span (row.node.name)>>
        if (model.mask(row.node) != null) <button class:if (st.target == "mask" and row.id in st.selected) "mask selected" else "mask",
            'data-mask':row.id,title:"Edit layer mask","M">
        if (row.node.locked == true) <span title:"Locked","L">
    >]
pub fn effect_rows(st) {
    let row = if (len(st.selected)==1) editor.lookup(st.doc,st.selected[0]) else null;
    [for (i,effect in effects.items(row.node)) <div class:if (i == st.effect_index) "effect-row selected" else "effect-row",
        <button 'data-select-effect':string(i),if (effect.enabled == false) "○ " ++ effect.name else "● " ++ effect.name>>]
}
pub fn style_rows(st) {
    let row = if (len(st.selected)==1) editor.lookup(st.doc,st.selected[0]) else null;
    [for (i,style in styles.items(row.node)) <button 'data-select-style':string(i),
        class:if (i == st.style_index) "selected" else "",(if (style.enabled == false) "○ " else "● ") ++ string(style.kind)>]
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
pub fn curve_chart(curves,channel,selected=1) {
    let points = tone.points(curves,channel)
    let color = {rgb:"#e8e9ec",r:"#ef7777",g:"#6acb8b",b:"#72a7ed"}[channel]
    let path = join([for (i,p in points) (if (i == 0) "M " else "L ") ++ string(p[0]*256) ++ " " ++ string((1-p[1])*160)]," ");
    <svg id:"curve-chart",viewBox:"0 0 256 160",width:256,height:160,preserveAspectRatio:"none",'aria-label':"Editable " ++ upper(channel) ++ " curve", *[
        <path d:join([for (v in [0.25,0.5,0.75]) "M " ++ string(v*256) ++ " 0 V 160 M 0 " ++ string(v*160) ++ " H 256"]," "),stroke:"#41444a",fill:"none">,
        <path d:"M 0 160 L 256 0",stroke:"#626976",fill:"none">,
        <path d:path,stroke:color,fill:"none",'stroke-width':2>,
        *[for (i,p in points) <circle 'data-curve-point':string(i),cx:p[0]*256,cy:(1-p[1])*160,r:4,
            fill:if (i == selected) "#ffffff" else color,stroke:"#172130",'stroke-width':1>]]>
}
pub fn tree() => <main id:"photos",tabindex:"0",'data-error':"", *[
    <header class:"menubar",<strong "λ Photos"> <nav 'aria-label':"Document commands", *[
        button("new","new","New"),button("open","open","Open…"),button("place","place","Place image…"),
        button("save","save","Save project…"),button("save-psd","psd","Export PSD…"),button("export","export","Export image…")]> <span class:"spacer"> <span id:"document-name","Untitled"> <span id:"dirty","">>,
    <nav class:"document-tabs",'aria-label':"Open documents",*[<div id:"tabs",role:"tablist">,<span class:"spacer">,
        button("new-tab","new-tab","New tab"),button("open-tab","open-tab","Open in tab…"),button("close-tab","tab-close","Close tab")]>,
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
                <canvas class:"raster-selection",width:1,height:1,'aria-label':"Selection coverage"> <div class:"raster-target">
                <div class:"raster-crop-shade",*[for (edge in ["top","bottom","left","right"]) <div 'data-crop-shade':edge>]>
                <div class:"raster-crop">
                <div class:"raster-transform",'aria-label':"Layer transform controls", *[
                    *[for (handle in transforms.handles) <button type:"button",'data-raster-handle':handle.id,
                        class:"transform-handle handle-" ++ handle.id,title:"Resize " ++ handle.id,
                        'aria-label':"Resize " ++ handle.id,style:"left:" ++ string(handle.x*100) ++ "%;top:" ++ string(handle.y*100) ++ "%">],
                    <button type:"button",'data-raster-handle':"rotate",class:"transform-handle handle-rotate",title:"Rotate; Shift snaps to 15°",'aria-label':"Rotate layer","↻">]>>
            <div id:"crop-controls",class:"crop-controls",style:"display:none", *[
                <span "Drag to crop">,button("crop-apply","crop-apply","Apply crop"),button("crop-cancel","crop-cancel","Cancel") ]>>,
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
                <p "Transform: drag handles to resize or rotate. Shift keeps corner proportions / snaps rotation; Alt resizes about the center. Drag the image to scale; Shift-drag rotates.">,
                <div class:"fields", *[field("transform-angle","Rotate degrees",0,"number"),field("transform-x","Scale X",1,"number"),field("transform-y","Scale Y",1,"number")]>,
                <div class:"button-grid", *[button("transform-apply","transform-apply","Apply transform"),button("transform-reset","transform-reset","Reset transform"),
                    button("flip-x","flip-x","Flip horizontal"),button("flip-y","flip-y","Flip vertical"),button("rasterize","layer.rasterize","Rasterize layer")]>]>,
            <section class:"actions-panel", *[<h2 "Actions">,<p id:"action-status","Record edits on one selected layer">,
                field("action-name","Action name","My action"),
                <div class:"button-grid", *[button("action-start","action-start","Record"),button("action-stop","action.stop","Stop"),
                    button("action-cancel","action.cancel","Cancel recording"),button("action-replay","action-replay","Replay on layer"),
                    button("action-save","action-save","Save action…"),button("action-open","action-open","Load action…")]>]>,
            <section class:"batch-panel", *[<h2 "Batch action">,
                <p "Run the last action on explicit source/target jobs. Maximum 32 jobs; each output publishes atomically.">,
                <label "Jobs (Mark data)" <textarea id:"batch-jobs",rows:5,"[]">>,
                <label "On failure" <select id:"batch-policy",<option value:"continue","Continue remaining jobs"> <option value:"stop","Stop at first failure">>>,
                button("batch-run","batch-run","Run last action"),<output id:"batch-status",role:"status">]>,
            <section class:"retouch-panel", *[<h2 "Retouch">,<p "Clone / Heal: Alt-click a source. Brush opacity controls coverage.">,
                <p id:"retouch-source","No source selected">,
                <div class:"fields", *[field("retouch-strength","Tone / heal strength",0.5,"number"),field("retouch-radius","Neighborhood radius",3,"number")]>,
                <label "Source alignment" <select id:"retouch-aligned",<option value:"aligned","Aligned"> <option value:"reset","Reset each stroke">>>]>,
            <section class:"histogram-panel",<h2 "Histogram"> <div id:"histogram">>,
            <section class:"channels-panel", *[<h2 "Channels">,
                <label "Preview" <select id:"channel-view",<option value:"composite","RGB composite"> <option value:"r","Red"> <option value:"g","Green"> <option value:"b","Blue"> <option value:"a","Alpha">>>,
                <label "Saved coverage" <select id:"saved-channel">>,field("channel-name","Channel name","Saved selection"),
                <label "Replace component" <select id:"channel-component",*[for (component in ["r","g","b","a","gray"]) <option value:component,component>]>>,
                <div class:"button-grid", *[button("channel-save","channel-save","Save selection"),button("channel-capture","channel-capture","Capture component"),
                    button("channel-load","channel-load","Load selection"),button("channel-preview","channel-preview","Preview saved"),
                    button("channel-replace","channel-replace","Replace component"),button("channel-rename","channel-rename","Rename channel"),button("channel-delete","channel-delete","Delete channel")]>]>,
            <section class:"artwork-panel", *[<h2 "Text, shapes & paths">,
                <p "Artwork stays editable. Use Rasterize before painting its pixels.">,
                <div class:"fields", *[field("art-width","Local width",200,"number"),field("art-height","Local height",100,"number"),
                    field("art-fill","Fill","#ffffff"),field("art-stroke","Stroke","transparent"),field("art-stroke-width","Stroke width",0,"number")]>,
                <label "Shape" <select id:"art-shape",*[for (kind in ["rectangle","ellipse","path"]) <option value:kind,kind>]>>,
                field("art-radius","Corner radius",0,"number"),
                <label "Path commands (Mark arrays: M, L, Q, C, Z)" <textarea id:"art-path",rows:3,"[[\"M\",0,0],[\"L\",100,0],[\"L\",0,100],[\"Z\"]]">>,
                <label "Text" <textarea id:"art-text",rows:3,"Hello Photos">>,
                <div class:"fields", *[field("art-font","Font asset ID","photos-font"),field("art-size","Font size",32,"number"),field("art-leading","Line height",1.2,"number")]>,
                <label "Text alignment" <select id:"art-align",*[for (align in ["left","center","right"]) <option value:align,align>]>>,
                field("art-font-path","Embed font file","lmd/package/doc/latex/fonts/Sans/cmunss.woff2"),
                <div class:"button-grid", *[button("add-text","text-add","Add text"),button("edit-text","text-edit","Apply text"),
                    button("add-shape","vector-add","Add shape"),button("edit-shape","vector-edit","Apply shape"),button("embed-font","font-embed","Embed font")]>]>,
            <section class:"styles-panel", *[<h2 "Layer styles">,<div id:"styles">,
                <label "Style" <select id:"style-kind",*[for (kind in ["shadow","glow","overlay"]) <option value:kind,kind>]>>,
                <div class:"fields", *[field("style-color","Color","#000000"),field("style-opacity","Opacity",0.7,"number"),
                    field("style-radius","Blur radius",4,"number"),field("style-dx","Offset X",4,"number"),field("style-dy","Offset Y",4,"number")]>,
                <div class:"button-grid", *[button("style-add","style-add","Add style"),button("style-edit","style-edit","Apply style"),
                    button("style-toggle","style.toggle","Toggle style"),button("style-remove","style.remove","Remove style")]>]>,
            <section class:"effects-panel", *[<h2 "Editable effects">,<div id:"effects">,
                <div class:"fields", *[for (control in effects.controls) field("effect-" ++ control.key,control.label,control.value,"number")]>,
                <div class:"button-grid", *[button("effect-add","effect-add","Add effect"),button("effect-toggle","effect.toggle","Toggle effect"),
                    button("effect-remove","effect.remove","Remove effect"),button("effect-up","effect-up","Move effect up"),button("effect-down","effect-down","Move effect down")]>]>,
            <section class:"arrange-panel", *[<h2 "Align & distribute">,
                <label "Relative to" <select id:"align-relative",<option value:"canvas","Canvas"> <option value:"selection","Selected layers">>>,
                <div class:"button-grid", *[for (mode in ["left","center","right","top","middle","bottom"])
                    button("align-" ++ mode,"align-" ++ mode,mode),
                    button("distribute-x","distribute-x","Equal horizontal gaps"),button("distribute-y","distribute-y","Equal vertical gaps")]>]>,
            <section class:"levels-panel", *[<h2 "Levels">,
                <p "Set input endpoints, midtone gamma and output endpoints. Apply updates the selected effect.">,
                <div class:"fields",*[for (control in tone.levels) field("levels-" ++ control.key,control.label,control.value*control.scale,"number")]>,
                <div class:"button-grid",*[button("levels-add","levels-add","Add Levels"),button("levels-apply","levels-apply","Apply to effect")]>]>,
            <section id:"curve-editor",class:"curves-panel",tabindex:"0", *[<h2 "Curves">,
                <label "Channel" <select id:"curve-channel",*[for (channel in tone.channels) <option value:channel,upper(channel)>]>>,
                <div id:"curve-graph",curve_chart({},"rgb")>,<p "Input → · Output ↑. Click adds a point; drag to adjust. Curves use straight segments.">,
                <label "Points [input,output], from 0 to 1" <textarea id:"curve-points",rows:3,"[[0,0],[1,1]]">>,
                <div class:"button-grid",*[button("curve-reset","curve-reset","Reset channel"),button("curve-remove","curve-remove","Remove point"),
                    button("curves-add","curves-add","Add Curves"),button("curves-apply","curves-apply","Apply to effect")]>]>]>]>,
    <footer class:"statusbar",<output id:"status",role:"status",'aria-live':"polite","Opening sample…"> <span id:"dimensions","">>,
    <section id:"file-dialog",class:"dialog",style:"display:none",role:"dialog",'aria-modal':"true",'aria-label':"File operation", *[
        <h2 id:"file-title","Open image">,field("file-path","Path"),
        <p id:"file-help","PNG, JPEG and supported layered PSD files">,
        <div id:"export-options",style:"display:none", *[
            <div class:"fields", *[
                <label "Format" <select id:"export-format",<option value:"png","PNG"> <option value:"jpeg","JPEG">>>,
                <label "Aspect ratio" <select id:"export-aspect",<option value:"locked","Keep original"> <option value:"free","Free size">>>,
                field("export-width","Width",1024,"number"),field("export-height","Height",768,"number") ]>,
            <label id:"export-quality-row","JPEG quality (0–100)" <input id:"export-quality",type:"number",min:0,max:100,value:"90">>,
            <label "Transparency" <select id:"export-alpha",<option value:"preserve","Preserve (PNG)"> <option value:"flatten","Flatten over background">>>,
            <div id:"export-background-row",field("export-background","Opaque background","#ffffff")> ]>,
        <div class:"button-grid", *[button("file-cancel","file-cancel","Cancel"),button("file-confirm","file-confirm","Continue")]>]>,
    <section id:"discard-dialog",class:"dialog",style:"display:none",role:"dialog",'aria-modal':"true",'aria-label':"Unsaved changes", *[
        <h2 "Save changes first?">,<p "This document has unsaved edits.">,
        button("discard-cancel","discard-cancel","Cancel"),button("discard-save","discard-save","Save project…"),
        button("discard-confirm","discard-confirm","Discard changes")]>]>
