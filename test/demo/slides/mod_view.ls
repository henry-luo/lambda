import m: .mod_model
import c: lambda.slide.common
import html: lambda.slide.html
import sample: lambda.slide.sample

pub fn button(action,label,cls="") => <button id:action,type:"button",class:cls,'data-action':action,label>
fn group(label,children) => <div class:"ribbon-group",<div class:"ribbon-tools",*children><span class:"group-label",label>>
fn select(id,label,items,property="") => <label class:"field",<span label><select id:id,'data-property':property,
    for (value in items) <option value:string(value),string(value)>>>
fn field(key,label,kind="text") => <label class:"field",<span label><input id:"prop-" ++ key,type:kind,'data-property':key>>
pub fn ribbon(tab) {
    if (tab == "Home") [
        group("SLIDES",[button("add-slide","＋ New slide","large"),button("duplicate-slide","Duplicate slide"),button("delete-slide","Delete slide")]),
        group("CLIPBOARD",[button("copy","Copy"),button("paste","Paste"),button("duplicate","Duplicate")]),
        group("TEXT",[button("bold","B","strong"),button("italic","I","italic"),button("align-left","Left"),button("align-center","Center"),button("align-right","Right")]),
        group("ARRANGE",[button("front","Bring to front"),button("back","Send to back"),button("center-object","Center on slide"),button("delete","Delete object")])]
    else if (tab == "Insert") [
        group("TEXT",[button("insert-text","T  Text box","large")]),
        group("SHAPES",[button("insert-rect","▭ Rectangle","large"),button("insert-ellipse","◯ Ellipse","large"),button("insert-line","╱ Line","large")]),
        group("PICTURES",[button("insert-image","▧ Picture","large"),<span class:"ribbon-hint","Set the image path in Format.">])]
    else if (tab == "Design") [
        group("THEMES",[for (theme in ["light","dark","corporate","midnight","paper"])
            button("theme-" ++ theme,theme,"theme theme-" ++ theme)]),
        group("SLIDE LAYOUT",[select("layout","Layout",['blank','title','title-body','two-column','three-column','section','quote','image-left','image-right'],"slide-layout")]),
        group("CANVAS",[button("grid","Grid"),button("design-fit","Fit to window")])]
    else [
        group("OBJECT ENTRANCE",[button("animate-fade-in","Fade in"),button("animate-fly-in","Fly in"),button("animate-zoom-in","Zoom in"),button("animate-appear","Appear")]),
        group("BUILD",[select("animation-start","Start",["click","entry"]),button("clear-animations","Clear slide animations")]),
        group("TRANSITION",[select("transition","Transition",['cut','fade','push','slide','morph'],"slide-transition"),field("slide-transition_duration","Duration (ms)","number")])]
}
pub fn canvas(plan,index:int,key) element^ {
    let scene = plan.slides[index]^;
    html.layer(plan,scene,[for (obj in scene.targets) sample.baseline(obj)],key,"",true)
}
pub fn thumbnails(st) array^ => [for (i,scene in st.plan.slides) <button type:"button",class:if (i == st.index) "thumbnail current" else "thumbnail",
    'data-slide':string(i),'aria-current':string(i == st.index),'aria-label':"Slide " ++ string(i+1) ++ ": " ++ c.value(scene.source.title,scene.id),
    <span class:"thumb-number",string(i+1)>
    <span class:"thumb-paper",style:"height:" ++ c.px(160.0*st.plan.height/st.plan.width) ++ ";",
        <span class:"thumb-render",style:"transform:scale(" ++ c.fmt(160.0/st.plan.width) ++ ");",
            canvas(st.plan,i,"thumb" ++ string(i))^>>
    <span class:"thumb-caption",c.value(scene.source.title,scene.id)>>]
pub fn object_options(st) => [<option value:"","Select an object">,*[for (obj in m.scene(st).targets)
    <option value:obj.dom_key,string(obj.tag) ++ " · " ++ obj.id>]]
pub fn inline_editor(obj,background) => <textarea id:"canvas-text",'aria-label':"Edit slide text",
    style:"font-size:" ++ c.px(obj.font_size) ++ ";color:" ++ obj.paint ++ ";" ++ c.value(obj.source.style,"") ++
        ";background:" ++ background ++ ";",m.text(obj.source)>
pub fn shell(show_view) => <div id:"slides-editor",tabindex:"0",
    <header class:"titlebar",
        <div class:"brand",<span class:"brand-icon","S"><span "Slides">>
        <input id:"deck-title",'data-property':"deck-title",'aria-label':"Presentation title",value:"Untitled presentation">
        <span id:"save-state","Saved">
        <div class:"title-actions",button("undo","↶ Undo");button("redo","↷ Redo");button("save","Save");button("present","▶ Present","primary")>>
    <nav class:"tabs",'aria-label':"Ribbon tabs",
        button("new","New");button("open","Open");button("export","Export");
        <span class:"divider">;
        for (tab in ["Home","Insert","Design","Animations"]) <button type:"button",'data-tab':tab,tab>;
        <span class:"spacer">;
        <label class:"sample-label","Samples ";<select id:"samples",'aria-label':"Sample presentations",
            <option value:"","Choose a sample…"><option value:"northstar.slides","Northstar / Strategy 2027">
            <option value:"slide_presentation.slides","Animation basics">
            <option value:"slide_package_content.slides","Charts & builds">
            <option value:"presenter_tools.slides","Themes & masters">>>>
    <div id:"ribbon",class:"ribbon",*ribbon("Home")>
    <main class:"workspace",
        <aside class:"navigator",<div class:"panel-heading",<b "Slides"><span id:"slide-count","2 slides">>
            <div id:"thumbnails">
            <div class:"slide-order",button("slide-up","↑ Move up");button("slide-down","↓ Move down")>>
        <section class:"studio",
            <div class:"canvas-toolbar",<span id:"slide-heading","Slide 1">;<span class:"spacer">;
                button("zoom-out","−");<span id:"zoom-label","Fit">;button("zoom-in","＋");button("fit","Fit")>
            <div id:"stage",tabindex:"0",'aria-label':"Slide editing canvas",
                <div id:"paper",<div id:"slide-render">>>
            <div class:"notes-panel",<label for:"speaker-notes","SPEAKER NOTES"><textarea id:"speaker-notes",'data-property':"notes",placeholder:"Add speaker notes…">>>
        <aside class:"inspector",
            <div class:"panel-heading",<b "Format"><span id:"object-kind","Slide">>
            <label class:"field",<span "Selection"><select id:"object-list">>
            <div id:"slide-properties",
                <h3 "Slide settings">;
                field("slide-title","Slide title");field("slide-background","Background");
                <p class:"help","Select an object on the canvas to edit it. Drag to move, or use the corner to resize.">
                <p class:"help","Use Insert for text, shapes and pictures. Design changes themes and layouts.">>
            <div id:"object-properties",
                <div id:"text-properties",<label class:"field",<span id:"text-label","Text"><textarea id:"prop-text",'data-property':"text">>>
                <div class:"geometry",*[for (pair in [["x","X"],["y","Y"],["width","Width"],["height","Height"],["rotation","Rotation"],["opacity","Opacity"]]) field(pair[0],pair[1],"number")]>
                <div id:"font-properties",field("font_size","Font size","number");field("color","Text color")>
                <div id:"shape-properties",field("fill","Fill");field("stroke","Stroke");field("stroke_width","Stroke width","number");field("radius","Corner radius","number")>
                <div id:"image-properties",field("src","Image path or URL");select("prop-fit","Image fit",['contain','cover','fill'],"fit")>
                field("style","CSS style");
                <div class:"inspector-actions",button("apply-text","Apply text");button("remove-object","Delete object")>
                <p class:"help","Arrow keys nudge · Shift for 10 units · Cmd/Ctrl+D duplicates · Delete removes">>>>
    <footer class:"statusbar",<span id:"status",'aria-live':"polite","Ready to create">;<span class:"spacer">;<span id:"position","Slide 1 of 2">;<span id:"canvas-size","1280 × 720">>
    <div id:"file-dialog",class:"modal",role:"dialog",'aria-modal':"true",'aria-labelledby':"dialog-title",
        <div class:"dialog-card",<h2 id:"dialog-title","Save presentation">
            <p id:"dialog-copy","Save an editable .slides presentation.">
            <label class:"field",<span "File path"><input id:"file-path",value:"temp/slides/presentation.slides">>
            <p id:"dialog-error",role:"alert">
            <div class:"dialog-actions",button("file-cancel","Cancel");button("file-confirm","Save","primary")>>>
    <div id:"show-layer",class:"show-layer",<div class:"show-bar",button("end-show","← Back to editor");
        <span "Arrow keys / Space to advance · P for presenter tools · F for full screen">>;
        show_view>
>
