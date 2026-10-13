import ui: lambda.ui.dtna
import model: .mod_model
import presets: .mod_presets
import layers: .mod_layers

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
fn object_slider(key,caption,low,high,step="1") => <label class:"object-control", *[
    <span caption>,<input id:("object-" ++ key),type:"range",'data-object':key,
        min:string(low),max:string(high),step:step,'aria-label':caption>]>
fn asset_grid(catalog,kind) => <div class:"asset-grid", *[for (asset in catalog)
    <button id:("add-" ++ asset.id),'data-add':asset.id,class:("asset " ++ kind),title:asset.label,
        *[layers.icon(asset.id),<span (asset.label)>]>]>
pub fn tree() => <main id:"photo",tabindex:0,'data-inspector':"false", *[
    <header class:"toolbar", *[
        <div class:"brand",<span class:"brand-mark","λ"> <strong "Photo Studio"> <span class:"badge","RADIANT">>,
        <div class:"toolbar-group", *[button("inspector-toggle","toggle-inspector","Tools"),button("open","open","Open image"),button("undo","undo","Undo"),
            button("redo","redo","Redo"),button("reset","reset","Reset")]>,
        <div class:"toolbar-group", *[button("compare","compare","Hold to compare"),
            button("recipe","recipe","Save edits"),button("export","export","Export PNG",true)]>]>,
    <div class:"workspace", *[
        <nav class:"tool-rail",'aria-label':"Editing tools", *[for (tool in layers.tools)
            <button id:("tool-" ++ tool.id),'data-tool':tool.id,class:"tool",title:tool.label,
                <span (tool.icon)> <small (tool.label)>>]>,
        <aside id:"inspector",class:"inspector", *[
            <section id:"panel-adjust", *[<h2 "Light & color">,<p "Make it yours. Every edit stays reversible.">,
                slider("exposure","Exposure"),slider("brightness","Brightness"),slider("contrast","Contrast"),
                slider("saturation","Saturation"),slider("temperature","Temperature"),slider("gamma","Gamma"),
                <h3 "Levels">,<div id:"histogram",'aria-label':"Photo luminance histogram">,
                slider("black","Black point"),slider("white","White point"),
                slider("shadows","Shadows"),slider("highlights","Highlights"),
                button("reset-adjust","reset-adjust","Reset adjustments")]>,
            <section id:"panel-filters",style:"display:none", *[<h2 "Looks">,<p "A starting point for your own style.">,
                <div class:"preset-grid", *[for (preset in presets.catalog)
                    <button id:("preset-" ++ preset.id),'data-preset':preset.id,class:"preset",
                        <canvas id:("thumb-" ++ preset.id),width:"96",height:"64"> <span (preset.label)>>]>,
                slider("intensity","Intensity")]>,
            <section id:"panel-effects",style:"display:none", *[<h2 "Finishing touches">,<p "Add atmosphere or bring the details into focus.">,
                slider("vignette","Vignette"),slider("softness","Soft focus"),slider("sharpen","Sharpen"),
                button("reset-effects","reset-effects","Reset effects")]>,
            <section id:"panel-text",style:"display:none", *[<h2 "Say it your way">,<p "A title, a memory, a little context. Add text and make it yours.">,
                field("text-entry","Your text","Your story"),button("add-text","add-text","Add text",true)]>,
            <section id:"panel-shapes",style:"display:none", *[<h2 "Simple forms">,<p "Build a composition with a few bold shapes.">,
                asset_grid(layers.shapes,"shape")]>,
            <section id:"panel-stickers",style:"display:none", *[<h2 "A little personality">,<p "Original vector stickers. Crisp at any size.">,
                asset_grid(layers.stickers,"sticker")]>,
            <section id:"panel-crop",style:"display:none", *[<h2 "Crop & transform">,<p "Drag the frame or choose an aspect ratio.">,
                <label "Aspect ratio" <select id:"crop-ratio",'aria-label':"Crop aspect ratio",
                    <option value:"0","Freeform"> <option value:"original","Original">
                    <option value:"1","Square · 1:1"> <option value:"1.333333333333","Landscape · 4:3">
                    <option value:"1.5","Classic · 3:2"> <option value:"1.777777777778","Wide · 16:9">
                    <option value:"0.8","Portrait · 4:5"> <option value:"0.5625","Story · 9:16">>>,
                buttons([button("rotate","rotate","Rotate 90°"),button("flip-x","flip-x","Flip horizontal"),button("flip-y","flip-y","Flip vertical")]),
                slider("angle","Straighten"),
                <div class:"crop-fields", *[for (key in ["left","top","right","bottom"])
                    <label *[key,<input id:("crop-" ++ key),'data-crop-edge':key,type:"number",min:"0",max:"100",step:"0.1",
                        'aria-label':("Crop " ++ key ++ " percent")>]>]>,
                buttons([button("crop-apply","crop-apply","Apply crop",true),button("crop-cancel","crop-cancel","Cancel")])]>,
            <section id:"panel-layers",style:"display:none", *[<h3 "Your layers">,
                <p id:"layer-empty","Add something, then drag it into place.">,
                <select id:"layer-list",'aria-label':"Select layer",<option value:"0","Choose a layer">>,
                <section id:"object-properties",style:"display:none", *[
                    <label id:"object-text-row","Text" <input id:"object-text",type:"text",'data-object':"text",maxlength:"160">>,
                    <label id:"object-font-row","Typeface" <select id:"object-font",'data-object':"font", *[
                        for (font in layers.fonts) <option value:font,font>]>>,
                    <label id:"object-align-row","Alignment" <select id:"object-align",'data-object':"align",
                        <option value:"start","Left"> <option value:"middle","Center"> <option value:"end","Right">>>,
                    <h3 "Color">,<div class:"swatches", *[for (color in layers.colors)
                        <button class:"swatch",'data-color':color,'aria-label':("Use " ++ color),style:("background:" ++ color)>]>,
                    <label "Hex color" <input id:"object-color",type:"text",'data-object':"color",maxlength:"7">>,
                    object_slider("opacity","Opacity",0,1,"0.01"),object_slider("rotation","Rotation",-180,180),
                    <div class:"object-dimensions", *[for (key in ["x","y","width","height"])
                        <label *[key,<input id:("object-" ++ key),type:"number",'data-object':key,min:"0",max:"100",step:"1">]>]>,
                    buttons([button("layer-back","layer-back","Send back"),button("layer-front","layer-front","Bring forward")]),
                    buttons([button("layer-duplicate","layer-duplicate","Duplicate"),button("layer-delete","layer-delete","Delete")])]>]>]>,
        <section class:"stage-column", *[
            <div class:"stage-top",<span id:"source-name","Coast"> <span id:"source-size","Loading…">>,
            <div id:"stage",'aria-label':"Image workspace", *[
                <div id:"image-plane", *[<canvas id:"preview",width:"1",height:"1",'aria-label':"Edited photo">,
                    <div id:"artwork",'aria-label':"Text, shapes and stickers">,
                    <div id:"selection-frame",'data-layer-handle':"move",style:"display:none",
                        <span id:"selection-label","Text"> <span class:"layer-resize",'data-layer-handle':"resize">>,
                    <div id:"crop-frame",'data-handle':"move",style:"display:none", *[
                        <div class:"third vertical one">,<div class:"third vertical two">,
                        <div class:"third horizontal one">,<div class:"third horizontal two">,
                        *[for (handle in ["nw","n","ne","e","se","s","sw","w"])
                            <span class:("handle " ++ handle),'data-handle':handle>]]>]>,
                <div id:"compare-label",style:"display:none","ORIGINAL COLORS">]>,
            <footer class:"stage-bottom", *[<span id:"history-info","No edits">,<div class:"zoom-controls", *[
                button("zoom-out","zoom-out","−"),<output id:"zoom-label","Fit">,button("zoom-in","zoom-in","+"),
                button("fit","fit","Fit"),button("actual","actual","100%")]>]>]>]>,
    <footer class:"statusbar",<output id:"status",role:"status",'aria-live':"polite","Loading image…"> <span "Photo Studio · made with Lambda">>,
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
