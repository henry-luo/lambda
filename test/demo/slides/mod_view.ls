import m: .mod_model
import c: lambda.slide.common
import html: lambda.slide.html
import slide: lambda.slide
import sample: lambda.slide.sample
import home: .mod_home_ui
import insert_ui: .mod_insert_ui
import transitions_ui: .mod_transitions_ui
import animations_ui: .mod_animations_ui
import spelling: lambda.edit.spelling
import ribbon_ui: ~~.mod_ribbon

pub fn button(action,label,cls="") => <button id:action,type:"button",class:cls,'data-action':action,label>
fn select(id,label,items,property="") => <label class:"field",<span label><select id:id,'data-property':property,
    for (value in items) <option value:string(value),string(value)>>>
fn field(key,label,kind="text") => <label class:"field",<span label><input id:"prop-" ++ key,type:kind,'data-property':key>>
pub fn ribbon(tab,fonts=[]) {
    if (tab == "Home") [home.tree(fonts)]
    else if (tab == "Insert") [insert_ui.tree()]
    else if (tab == "Transitions") [transitions_ui.tree()]
    else if (tab == "Design") [<div class:"design-ribbon editor-ribbon",
        ribbon_ui.group("Themes",[for (theme in ["light","dark","corporate","midnight","paper"])
            button("theme-" ++ theme,theme,"theme theme-" ++ theme)]);
        ribbon_ui.group("Slide Layout",[select("layout","Layout",['blank','title','title-body','two-column','three-column','section','quote','image-left','image-right'],"slide-layout")]);
        ribbon_ui.group("Canvas",[button("grid","Grid"),button("design-fit","Fit to window")])>]
    else [animations_ui.tree()]
}
pub fn canvas(plan,index:int,key) element^ {
    let scene = plan.slides[index]^;
    html.layer(plan,scene,[for (obj in scene.targets) sample.baseline(obj)],key,"",true)
}
pub fn printout(st,deck) element^ {
    let pages = slide.slides(deck)^
    let document = html.document({*:st.plan,base_uri:st.base},<main class:"slide-print",
        for (i,page in pages) <article style:if (i>0) "break-before:page;" else "",page>>);
    // fixed slide-sized sheets keep final builds together without speaker notes.
    <html *:map(document),for (child in content(document))
        if (child is element and name(child)=='head') <head *:map(child),*content(child);
            <style "@page { size:" ++ c.px(st.plan.width) ++ " " ++ c.px(st.plan.height) ++ "; margin:0; }">>
        else child>
}
pub fn thumbnails(st) array^ => [for (i,scene in st.plan.slides) <div role:"button",tabindex:"0",class:if (i == st.index) "thumbnail current" else "thumbnail",
    'data-slide':string(i),'data-slide-id':scene.id,'aria-current':string(i == st.index),'aria-label':"Slide " ++ string(i+1) ++ ": " ++ c.value(scene.source.title,scene.id),
    <span class:"thumb-number",string(i+1)>
    <span class:"thumb-paper",style:"height:" ++ c.px(c.value(st.thumbnail_width,160.0)*st.plan.height/st.plan.width) ++ ";",
        <span class:"thumb-render",inert:"",'aria-hidden':"true",style:"transform:scale(" ++ c.fmt(c.value(st.thumbnail_width,160.0)/st.plan.width) ++ ");",
            canvas(st.plan,i,"thumb" ++ string(i))^>>
    <span class:"thumb-caption",c.value(scene.source.title,scene.id)>>]
pub fn object_options(st) => [<option value:"","Select an object">,*[for (obj in m.scene(st).targets)
    <option value:obj.dom_key,string(obj.tag) ++ " · " ++ obj.id>]]
pub fn inline_editor(obj,background) => <textarea id:"canvas-text",'aria-label':"Edit slide text",
    style:"font-size:" ++ c.px(obj.font_size) ++ ";color:" ++ obj.paint ++ ";" ++ c.value(obj.source.style,"") ++
        ";background:" ++ background ++ ";",m.text(obj.source)>
pub fn shell(show_view,inline_view=null) => <div id:"slides-editor",tabindex:"0",
    <header class:"titlebar",
        <div class:"brand",<span class:"brand-icon","S"><span "Slides">>
        <div class:"title-stack",
            <div class:"title-line",
                <input id:"deck-title",'data-property':"deck-title",'aria-label':"Presentation title",value:"Untitled presentation">
                <span id:"save-state","Saved">>
            <nav class:"tabs",'aria-label':"Ribbon tabs",
                button("new","New");button("open","Open");button("export","Export");
                <span class:"divider">;
                for (tab in ["Home","Insert","Design","Transitions","Animations"]) <button type:"button",'data-tab':tab,tab>;
                <span class:"spacer">;
                <label class:"sample-label","Samples ";<select id:"samples",'aria-label':"Sample presentations",
                    <option value:"","Choose a sample…"><option value:"northstar.slides","Northstar / Strategy 2027">
                    <option value:"slide_presentation.slides","Animation basics">
                    <option value:"slide_package_content.slides","Charts & builds">
                    <option value:"presenter_tools.slides","Themes & masters">>>>>
        <div class:"title-actions",button("present","▶ Present","primary")>>
    <div id:"ribbon",class:"ribbon",*ribbon("Home")>
    <div id:"inline-parking",style:"display:none;",inline_view>;
    <main class:"workspace",
        <aside id:"slide-navigator",class:"navigator",'aria-label':"Slides",<div class:"panel-heading",<b "Slides"><span id:"slide-count","2 slides">>
            <div id:"thumbnails",tabindex:"-1">
            <div id:"slide-drop-marker",class:"slide-drop-marker",'aria-hidden':"true">;
            <div class:"slide-order",button("slide-up","↑ Move up");button("slide-down","↓ Move down")>>
        <div id:"navigator-divider",class:"navigator-divider",role:"separator",tabindex:"0",'aria-label':"Resize slides panel",title:"Drag to resize slides panel",
            'aria-controls':"slide-navigator",'aria-orientation':"vertical",'aria-valuemin':"160",'aria-valuemax':"480",'aria-valuenow':"208">;
        <section class:"studio",
            <div class:"canvas-toolbar",<span id:"slide-heading","Slide 1">;<span class:"spacer">;
                button("zoom-out","−");<span id:"zoom-label","Fit">;button("zoom-in","＋");button("fit","Fit")>
            <div id:"stage",tabindex:"0",'aria-label':"Slide editing canvas",
                <div id:"paper",<div id:"slide-render">>>
            <div class:"notes-panel",<label for:"speaker-notes","SPEAKER NOTES"><textarea id:"speaker-notes",'data-property':"notes",placeholder:"Add speaker notes…">>>
        animations_ui.pane();
        <aside class:"inspector",
            <div class:"panel-heading",<b "Format"><span id:"object-kind","Slide">>
            <label class:"field",<span "Selection"><select id:"object-list">>;
            transitions_ui.sound_file();
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
                button("edit-inserted","Edit object…","edit-inserted");
                <div class:"inspector-actions",button("apply-text","Apply text");button("remove-object","Delete object")>
                <p class:"help","Arrow keys nudge · Shift for 10 units · Cmd/Ctrl+D duplicates · Delete removes">>>>
    <footer class:"statusbar",<span id:"status",'aria-live':"polite","Ready to create">;<span class:"spacer">;<span id:"position","Slide 1 of 2">;<span id:"canvas-size","1280 × 720">>
    spelling.widget("slides-notes");spelling.widget("slides-text");spelling.widget("slides-canvas");
    <div id:"home-menu",class:"home-menu",role:"menu",'aria-label':"Home ribbon options">
    <div id:"insert-dialog",class:"modal insert-dialog",role:"dialog",'aria-modal':"true",'aria-labelledby':"insert-title">
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
