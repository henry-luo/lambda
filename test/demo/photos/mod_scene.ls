import model: lambda.editor.raster.model
import editor: lambda.editor.raster

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
pub fn tree() => <main id:"photos",tabindex:"0",'data-error':"", *[
    <header class:"menubar",<strong "λ Photos"> <nav 'aria-label':"Document commands", *[
        button("new","new","New"),button("open","open","Open…"),button("place","place","Place image…"),
        button("save","save","Save PSD…"),button("export","export","Export PNG…")]> <span class:"spacer"> <span id:"document-name","Untitled"> <span id:"dirty","">>,
    <section class:"options", *[button("undo","history.undo","Undo"),button("redo","history.redo","Redo"),
        <span class:"separator">,button("add-layer","layer.new","New layer"),button("duplicate","layer.duplicate","Duplicate"),
        button("group","layer.group","Group"),button("ungroup","layer.ungroup","Ungroup"),
        button("merge","layer.merge","Merge"),button("delete","layer.delete","Delete"),<span class:"spacer">,
        button("fit","fit","Fit"),button("actual","actual","100%"),button("zoom-out","zoom-out","−"),
        <output id:"zoom","Fit">,button("zoom-in","zoom-in","+")]>,
    <div class:"workspace", *[
        <nav class:"tool-rail",'aria-label':"Tools", *[
            <button id:"tool-move",'data-tool':"move",title:"Move layer (V)","↖" <small "Move">>,
            <button id:"tool-pan",'data-tool':"pan",title:"Pan workspace (H)","✥" <small "Hand">>,
            <div class:"swatch",title:"Foreground color">]>,
        <section id:"stage",class:"raster-stage",tabindex:"0",'aria-label':"Image workspace",
            <div class:"raster-surface",<canvas id:"preview",class:"raster-canvas",width:1,height:1,'aria-label':"Layer composition"> <div class:"raster-target">>>,
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
            <section class:"adjustments", *[<h2 "Quick adjustments">,
                <div class:"button-grid", *[button("exposure-up","exposure-up","Exposure +"),button("exposure-down","exposure-down","Exposure −"),
                    button("mono","mono","Monochrome"),button("sharpen","sharpen","Sharpen"),
                    button("fill-white","fill-white","Fill white"),button("clear","pixels.clear","Clear target")]>]>,
            <section class:"image-size", *[<h2 "Image & canvas">,
                <div class:"fields", *[field("image-width","Width",1024,"number"),field("image-height","Height",768,"number")]>,
                <div class:"button-grid", *[button("resize-image","resize-image","Resize image"),button("resize-canvas","resize-canvas","Resize canvas"),
                    button("flatten","document.flatten","Flatten")]>]>,
            <section class:"history-panel",<h2 "History"> <div id:"history"> >]>]>,
    <footer class:"statusbar",<output id:"status",role:"status",'aria-live':"polite","Opening sample…"> <span id:"dimensions","">>,
    <section id:"file-dialog",class:"dialog",style:"display:none",role:"dialog",'aria-modal':"true",'aria-label':"File operation", *[
        <h2 id:"file-title","Open image">,field("file-path","Path"),
        <p id:"file-help","PNG, JPEG and supported layered PSD files">,
        <div class:"button-grid", *[button("file-cancel","file-cancel","Cancel"),button("file-confirm","file-confirm","Continue")]>]>,
    <section id:"discard-dialog",class:"dialog",style:"display:none",role:"dialog",'aria-modal':"true",'aria-label':"Unsaved changes", *[
        <h2 "Save changes first?">,<p "This document has unsaved edits.">,
        button("discard-cancel","discard-cancel","Cancel"),button("discard-save","discard-save","Save PSD…"),
        button("discard-confirm","discard-confirm","Discard changes")]>]>
