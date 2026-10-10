import ui: lambda.ui.dtna
import model: .mod_model
import presets: .mod_presets

// apply the imported button view before embedding it in the retained native scene (S12.1.3).
fn button(id, action, label, primary=false) => apply(<dtna.button id:id,'data-action':action,
    variant:if (primary) 'primary' else 'default', label>)
fn buttons(children) => <div class:"two-buttons", *children>
fn field(id, caption, value="", kind="text") => <label *[caption,<input id:id,type:kind,value:value>]>
fn slider(key, caption) => <label class:"adjustment", *[
    <span class:"adjustment-label", *[caption,<output id:("value-" ++ key),"0">]>,
    <input id:("control-" ++ key),type:"range",'data-adjust':key,
        min:string(model.limits[key][0]),max:string(model.limits[key][1]),step:"0.01",value:string(model.recipe()[key]),'aria-label':caption>,
    <button class:"reset-control",'data-reset':key,title:("Reset " ++ caption),"Reset">]>
fn dialog(id,title,children) => <section id:id,class:"dialog",role:"dialog",'aria-modal':"true",
    'aria-label':title,style:"display:none", *[<h2 title>,*children]>
pub fn tree() => <main id:"photo",tabindex:0,'data-inspector':"false", *[
    <header class:"toolbar", *[
        <div class:"brand",<span class:"brand-mark","λ"> <strong "Photo Studio"> <span class:"badge","RADIANT">>,
        <div class:"toolbar-group", *[button("inspector-toggle","toggle-inspector","Tools"),button("open","open","Open image"),button("undo","undo","Undo"),
            button("redo","redo","Redo"),button("reset","reset","Reset")]>,
        <div class:"toolbar-group", *[button("compare","compare","Hold to compare"),
            button("recipe","recipe","Save edits"),button("export","export","Export PNG",true)]>]>,
    <div class:"workspace", *[
        <nav class:"tool-rail",'aria-label':"Editing tools",
            <button id:"tool-adjust",'data-tool':"adjust",class:"tool selected",<span "◐"> <small "Adjust">>
            <button id:"tool-filters",'data-tool':"filters",class:"tool",<span "▦"> <small "Filters">>
            <button id:"tool-crop",'data-tool':"crop",class:"tool",<span "⌗"> <small "Crop">>>,
        <aside class:"inspector", *[
            <section id:"panel-adjust", *[<h2 "Light & color">,<p "Make it yours. Every edit stays reversible.">,
                slider("exposure","Exposure"),slider("brightness","Brightness"),slider("contrast","Contrast"),
                slider("saturation","Saturation"),slider("temperature","Temperature"),slider("gamma","Gamma"),
                button("reset-adjust","reset-adjust","Reset adjustments")]>,
            <section id:"panel-filters",style:"display:none", *[<h2 "Looks">,<p "A starting point for your own style.">,
                <div class:"preset-grid", *[for (preset in presets.catalog)
                    <button id:("preset-" ++ preset.id),'data-preset':preset.id,class:"preset",
                        <canvas id:("thumb-" ++ preset.id),width:"96",height:"64"> <span (preset.label)>>]>,
                slider("intensity","Intensity")]>,
            <section id:"panel-crop",style:"display:none", *[<h2 "Crop & transform">,<p "Drag the frame or choose an aspect ratio.">,
                <label "Aspect ratio" <select id:"crop-ratio",'aria-label':"Crop aspect ratio",
                    <option value:"0","Freeform"> <option value:"original","Original">
                    <option value:"1","Square · 1:1"> <option value:"1.333333333333","Landscape · 4:3">
                    <option value:"1.5","Classic · 3:2"> <option value:"1.777777777778","Wide · 16:9">>>,
                buttons([button("rotate","rotate","Rotate 90°"),button("flip-x","flip-x","Flip horizontal"),button("flip-y","flip-y","Flip vertical")]),
                slider("angle","Straighten"),
                <div class:"crop-fields", *[for (key in ["left","top","right","bottom"])
                    <label *[key,<input id:("crop-" ++ key),'data-crop-edge':key,type:"number",min:"0",max:"100",step:"0.1",
                        'aria-label':("Crop " ++ key ++ " percent")>]>]>,
                buttons([button("crop-apply","crop-apply","Apply crop",true),button("crop-cancel","crop-cancel","Cancel")])] >]>,
        <section class:"stage-column", *[
            <div class:"stage-top",<span id:"source-name","Coast"> <span id:"source-size","Loading…">>,
            <div id:"stage",'aria-label':"Image workspace", *[
                <div id:"image-plane", *[<canvas id:"preview",width:"1",height:"1",'aria-label':"Edited photo">,
                    <div id:"crop-frame",'data-handle':"move",style:"display:none", *[
                        <div class:"third vertical one">,<div class:"third vertical two">,
                        <div class:"third horizontal one">,<div class:"third horizontal two">,
                        *[for (handle in ["nw","n","ne","e","se","s","sw","w"])
                            <span class:("handle " ++ handle),'data-handle':handle>]]>]>,
                <div id:"compare-label",style:"display:none","ORIGINAL COLORS">]>,
            <footer class:"stage-bottom", *[<span id:"history-info","No edits">,<div class:"zoom-controls", *[
                button("zoom-out","zoom-out","−"),<output id:"zoom-label","Fit">,button("zoom-in","zoom-in","+"),
                button("fit","fit","Fit"),button("actual","actual","100%")]>]>]>]>,
    <footer class:"statusbar",<output id:"status",role:"status",'aria-live':"polite","Loading image…"> <span "Lambda / Radiant · local editing">>,
    dialog("open-dialog","Open an image",[
        <p "Choose a sample or enter the path to a local JPEG or PNG.">,
        buttons([button("sample-coast","sample-coast","Coast"),button("sample-mountains","sample-mountains","Mountains")]),
        field("open-path","Local image path"),buttons([button("open-confirm","open-confirm","Open",true),button("open-cancel","close-dialog","Cancel")])]),
    dialog("export-dialog","Export PNG",[
        <p "Export uses the original pixels and your current edits.">,field("export-path","Destination","temp/photo-export.png"),
        buttons([field("export-width","Width (0 = original)","0","number"),field("export-height","Height (0 = original)","0","number")]),
        <label class:"check-label",<input id:"aspect-lock",type:"checkbox",checked:true> "Lock aspect ratio">,
        buttons([button("export-confirm","export-confirm","Export PNG",true),button("export-cancel","close-dialog","Cancel")])]),
    dialog("recipe-dialog","Save or restore edits",[
        <p "Recipes contain reversible edits. Open the matching source before restoring.">,field("recipe-path","Recipe path","temp/photo-recipe.json"),
        buttons([button("recipe-save","recipe-save","Save recipe",true),button("recipe-load","recipe-load","Restore")]),button("recipe-cancel","close-dialog","Close")])
]>
