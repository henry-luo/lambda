// Thin WordCraft-style ribbon: every interactive item emits a command ID.
import lambda.editor.mod_editor
import lambda.editor.mod_step
import model: .mod_model
import document: .mod_document
import files: lambda.edit.files
import page: .mod_page
import columns: .mod_columns
import review: .mod_review
import insert: .mod_insert
import shapes: .mod_shapes
import stories: .mod_stories
import equation: .mod_equation
import wordart: .mod_wordart
import graphics: .mod_graphics
import objects: .mod_objects
import iconmodel: .mod_icon
import smartart: .mod_smartart
import chartmodel: .mod_chart
import changes: .mod_changes
import controls: lambda.ui.controls
import icons: ~~.mod_icons
import ribbon_icons: .mod_ribbon_icons
import context: ~~.mod_context_menu
import focused: .mod_context
import tables: .mod_table
import table_design: .mod_table_design
import table_layout: .mod_table_layout
import mini: .mod_selection_toolbar
import views: .mod_view
import navmodel: .mod_navigation
import references: .mod_references
import surface: .mod_surface
import print: .mod_print
import object_layout: .mod_object_layout
import transform: .mod_transform
import group_data: .mod_group_data
import shape_format: .mod_shape_format
import equation_ui: .mod_equation_ui
import home: .mod_home
import home_ui: .mod_home_ui

pub let icon = icons.icon
// retain an imported document's requested family even when it is not installed here.
pub fn font_options(families,current) => [for (family in sort(unique([*(families or []),current]),(name)=>lower(name)))
    {value:family,label:family}]
let text_colors = take(home.font_colors,4)

// choose one word boundary; intrinsic button width keeps both lines fully visible.
fn ribbon_label(label) {
    let words = split(label," ")
    let lines = if (len(words)<2) [label] else {
        let at = sort(1 to (len(words)-1),(i)=>abs(len(join(slice(words,0,i)," "))-len(join(slice(words,i)," "))))[0];
        [join(slice(words,0,at)," "),join(slice(words,at)," ")]
    };
    [for (line in lines) <span class:"ribbon-label-line",line>]
}
view <word_button> {
    let graphic = if (~.icon is string) ribbon_icons.icon(~.icon,16) else ~.icon;
    <button id:~.id, type:"button", class:"tool " ++ (~.class or ""), title:~.title or ~.label,
        role:if (~.nav_tab!=null) "tab" else if (contains(~.class or "","context-item")) "menuitem" else null,
        ["aria-label"]:~.title or ~.label, ["aria-pressed"]:if (~.nav_tab!=null) null else string(~.pressed or false),
        *:if (~.nav_tab!=null) {tabindex:if (~.pressed) "0" else "-1",["aria-selected"]:string(~.pressed or false),
            ["aria-controls"]:"navigation-" ++ ~.nav_tab} else {},
        *:if (~.menu!=null) {["aria-haspopup"]:"menu",["aria-expanded"]:string(~.pressed or false),["aria-controls"]:"word-context-menu"} else {},
        *:if (~.disabled) {disabled:""} else {},
        if (~.preview!=null) ~.preview else null;
        if (graphic != null) <span class:"tool-icon",["aria-hidden"]:"true",graphic> else null;
        <span class:"tool-label", *if (~.content!=null) [~.content] else if (contains(~.class or "","ribbon-large") or contains(~.class or "","table-large-tool"))
            ribbon_label(~.label) else [~.label]>;
        if (~.menu!=null) <span class:"menu-chevron",["aria-hidden"]:"true",icon("down",10)> else null;
        if (~.shortcut!=null) <span class:"context-key",~.shortcut> else null>
}
on mousedown() { return 'prevent-default' }
on click(evt) { emit("word_command",{id:~.command,params:~.params or {},selection:~.selection,node:evt.target,event:evt}) }
on keydown(evt) {
    if (~.nav_tab!=null and contains(["ArrowLeft","ArrowRight","Home","End"],evt.key)) {
        emit("word_command",{id:"ui.navigationTab",params:{tab:navmodel.next_tab(~.nav_tab,evt.key),focus:true},node:evt.target,event:evt})
        return 'prevent-default'
    }
    if (~.menu!=null and evt.key=="ArrowDown") {
        emit("word_command",{id:~.command,params:~.params,node:evt.target,event:evt})
        return 'prevent-default'
    }
    return 'pass'
}
pub fn button(id,command,label,icon=null,params={},pressed=false,disabled=false,css="",title=null) =>
    apply(<word_button id:id, command:command, label:label, icon:icon, params:params,
        pressed:pressed, disabled:disabled, class:css,title:title>)
pub fn command(id,label=null,icon=null,params={},pressed=false,disabled=false) =>
    button(id,id,label or model.spec(id).label,icon,params,pressed,disabled)
fn menu_item(id,command,label,graphic,disabled=false,shortcut=null,params={},selection=null) =>
    apply(<word_button id:"context-" ++ id,command:command,label:label,icon:graphic,
        disabled:disabled,class:"context-item",shortcut:shortcut,params:params,selection:selection>)
fn menu_button(id,label,graphic,menu,opened,css="",disabled=false) =>
    apply(<word_button id:id,command:"ui.editingMenu",label:label,icon:graphic,menu:menu,
        params:{menu:menu},pressed:opened==menu,disabled:disabled,class:"ribbon-menu-trigger " ++ css>)
fn margin_preview(p) => <svg class:"margin-preview",width:"42",height:"54",viewBox:"0 0 44 56",["aria-hidden"]:"true",
    *[for (i in 0 to (if (p.mirrored) 1 else 0),
        let m=page.resolved(p,if (i==0 and p.mirrored) 2 else 1),
        let w=if (p.mirrored) 19 else 29,let x=if (p.mirrored) 2+i*21 else 7,
        let left=x+w*m.left/210,let top=4+48*m.top/297)
        <g <rect x:string(x),y:"4",width:string(w),height:"48",fill:"#edf2fa",stroke:"#8292aa",'stroke-width':"0.8">
            <rect x:string(left),y:string(top),width:string(w*(1-(m.left+m.right)/210)),
                height:string(48*(1-(m.top+m.bottom)/297)),fill:"#fff",stroke:"#6594d0",'stroke-width':"0.8">>]>
fn editing_menu(st) {
    let finding = st.context.menu=="find"
    let borders = st.context.menu=="borders"
    let layout_menu = st.context.menu
    let no_hits = len(model.search(st.editor.doc,st.query,st.match_case))==0
    let no_table = st.read_only or model.table_context(st.editor)==null;
    context.frame("word-context-menu","word",
        <div id:"word-context-menu",class:"context-menu" ++ (if (layout_menu=="margins") " margins-menu" else
            if (starts_with(layout_menu or "","equation-") and not contains(["equation-convert","equation-justify"],layout_menu)) " equation-gallery-menu" else ""),role:"menu",
            ["aria-label"]:if (starts_with(layout_menu or "","home-")) "Home formatting" else if (finding) "Find" else if (borders) "Borders" else if (layout_menu=="columns") "Columns" else if (layout_menu=="margins") "Margins"
                else if (starts_with(layout_menu or "","equation-")) "Equation gallery"
                else if (starts_with(layout_menu or "","shape-")) "Shape formatting" else if (starts_with(layout_menu or "","object-")) "Arrange object" else "Select",
            style:"left:" ++ string(st.context.x) ++ "px;top:" ++ string(st.context.y) ++ "px",
            if (starts_with(layout_menu or "","home-")) home_ui.menu(st,home_widgets())
            else if (starts_with(layout_menu or "","equation-")) equation_ui.menu(st,equation_widgets())
            else if (starts_with(layout_menu or "","shape-")) shape_menu(st)
            else if (starts_with(layout_menu or "","object-")) object_menu(st)
            else if (layout_menu=="margins") <div
                *[for (entry in page.margin_presets) apply(<word_button id:"margins-" ++ entry.id,
                    command:"layout.pageSetup",label:entry.label,preview:margin_preview(entry.values),
                    content:<span <strong entry.label>; <span class:"margin-metrics",*[for (line in page.margin_summary(entry.values)) <span line>]>>,
                    pressed:page.margin_preset_active(st.editor.doc,entry),disabled:st.read_only,
                    class:"context-item margin-preset",params:entry.values,selection:st.context.item_selection>)];
                <div class:"context-rule",role:"separator">;
                menu_item("custom-margins","ui.margins","Custom Margins…","margins",st.read_only)>
            else if (layout_menu=="columns") <div class:"columns-menu",
                *[for (entry in columns.presets) apply(<word_button id:"context-columns-" ++ string(entry.count),
                    command:"layout.columns",label:entry.label,icon:"columns_" ++ string(entry.count),
                    pressed:columns.settings(st.editor.doc).count==entry.count,disabled:st.read_only,
                    class:"context-item columns-preset",params:{count:entry.count},selection:st.context.item_selection>)];
                <div class:"context-rule",role:"separator">;
                menu_item("more-columns","ui.columns","More Columns…","columns",st.read_only)>
            else if (layout_menu=="table-delete") <div *[for (kind in ["Row","Column","Table"])
                menu_item("delete-" ++ lower(kind),"table.delete" ++ kind,"Delete " ++ lower(kind),"trash",no_table,null,{},st.context.item_selection)]>
            else if (layout_menu=="table-autofit") <div *[for (entry in [{id:"contents",label:"AutoFit to Contents"},
                {id:"window",label:"AutoFit to Window"},{id:"fixed",label:"Fixed Column Width"}])
                menu_item("autofit-" ++ entry.id,"table.autoFit",entry.label,"autofit",no_table,null,{mode:entry.id},st.context.item_selection)]>
            else if (layout_menu=="table-select") <div *[for (scope in ["Cell","Row","Column","Table"])
                menu_item("select-" ++ lower(scope),"table.select" ++ scope,"Select " ++ lower(scope),"select_" ++ lower(scope),no_table,null,{},st.context.item_selection)]>
            else if (borders) <div *[for (preset in tables.border_presets)
                menu_item("border-" ++ preset,"table.borders",{all:"All Borders",outside:"Outside Borders",inside:"Inside Borders",
                    top:"Top Border",bottom:"Bottom Border",left:"Left Border",right:"Right Border",none:"No Borders"}[preset],
                    "border_" ++ preset,no_table,null,{*:st.table_pen,preset:preset})]>
            else if (finding) <div
                menu_item("find","ui.find","Find…","search",false,"⌘F");
                menu_item("find-next","edit.findNext","Find next","down",no_hits);
                menu_item("find-previous","edit.findPrevious","Find previous","up",no_hits);
                <div class:"context-rule",role:"separator">;
                menu_item("replace","ui.replace","Replace…","replace",st.read_only)>
            else <div
                menu_item("select-all","clipboard.selectAll","Select all","select_all",false,"⌘A");
                menu_item("select-paragraph","ui.selectParagraph","Select paragraph","align_left",st.read_only or model.current_block(st.editor)==null);
                <div class:"context-rule",role:"separator">;
                *[for (scope in ["Cell","Row","Column","Table"])
                    menu_item("select-" ++ lower(scope),"table.select" ++ scope,"Select " ++ lower(scope),"select_" ++ lower(scope),no_table)]>
        >,false)
}
fn object_menu(st) {
    let menu = st.context.menu
    let selected = model.current_node(st.editor,'img')
    let p = object_layout.settings(selected.node)
    let disabled = st.read_only or selected==null
    let dimensions = transform.settings(selected.node)
    let selection = st.context.item_selection;
    if (menu=="object-position") <div
        menu_item("position-inline","object.layout","In Line with Text","wrap_inline",disabled,null,{values:{wrap:"inline"}},selection);
        <div class:"context-rule",role:"separator">;
        <div class:"context-caption","Position within page margins">;
        <div class:"object-position-grid",*[for (v in object_layout.positions)
            button("object-position-" ++ v.id,"object.position",v.label,position_preview(v),{preset:v.id},false,disabled,"position-choice")] >;
        menu_item("layout-options","ui.objectLayout","More Layout Options…","settings",disabled)>
    else if (menu=="object-wrap") <div
        *[for (w in object_layout.wraps) apply(<word_button id:"object-wrap-" ++ w.id,command:"object.layout",label:w.label,icon:w.icon,
            params:{values:{wrap:w.id}},pressed:p.wrap==w.id,disabled:disabled,class:"context-item",selection:selection>)];
        <div class:"context-rule",role:"separator">;
        *[for (side in ["left","right"]) apply(<word_button id:"object-wrap-" ++ side,command:"object.layout",
            label:"Wrap on the " ++ side,icon:"wrap_square",params:{values:{wrap:"square",side:side}},
            pressed:p.wrap=="square" and p.side==side,disabled:disabled,class:"context-item",selection:selection>)];
        menu_item("wrap-options","ui.objectLayout","More Layout Options…","settings",disabled)>
    else if (menu=="object-forward" or menu=="object-backward") <div
        *[for (entry in (if (menu=="object-forward") [{id:"forward",label:"Bring Forward"},{id:"front",label:"Bring to Front"}]
            else [{id:"backward",label:"Send Backward"},{id:"back",label:"Send to Back"}]))
            menu_item("layer-" ++ entry.id,"object.layer",entry.label,if (menu=="object-forward") "bring_forward" else "send_backward",disabled,null,{action:entry.id},selection)]>
    else if (menu=="object-align") <div <div class:"context-caption","Align to page margins">;
        *[for (mode in ["left","center","right","top","middle","bottom"])
            menu_item("align-" ++ mode,"object.align","Align " ++ mode,"object_align",disabled,null,{mode:mode},selection)]>
    else if (menu=="object-group") <div
        menu_item("group-objects","object.group","Group Selected Objects","object_group",st.read_only or len(st.object_picks)<2);
        menu_item("ungroup-objects","object.ungroup","Ungroup","object_ungroup",disabled or group_data.settings(selected.node)==null)>
    else <div
        *[for (choice in [{id:"right",label:"Rotate Right 90°",delta:90},{id:"left",label:"Rotate Left 90°",delta:-90}])
            menu_item("object-rotate-" ++ choice.id,"object.transform",choice.label,"rotate_" ++ choice.id,disabled,null,
                {*:dimensions,rotation:transform.normalized(dimensions.rotation+choice.delta)},selection)];
        <div class:"context-rule",role:"separator">;
        *[for (choice in [{id:"x",label:"Flip Horizontal"},{id:"y",label:"Flip Vertical"}])
            menu_item("flip-" ++ choice.id,"object.flip",choice.label,"object_flip",disabled,null,{axis:choice.id},selection)];
        menu_item("object-rotation","ui.objectLayout","More Layout Options…","settings",disabled)>
}
fn position_preview(p) => <svg width:"26",height:"30",viewBox:"0 0 26 30",["aria-hidden"]:"true",
    <rect x:"3",y:"2",width:"20",height:"26",rx:"1.5",fill:"#fff",stroke:"#9caabd">;
    <path d:"M6 7h14 M6 11h14 M6 15h14 M6 19h14 M6 23h14",stroke:"#d4dce7",'stroke-width':"1">;
    <rect x:string({left:6,center:10,right:14}[p.h]),y:string({top:5,middle:12,bottom:19}[p.v]),
        width:"6",height:"6",rx:"1",fill:"#cbe1ff",stroke:"#3478cc">>
fn shape_menu(st) {
    let item = shape_format.describe(model.current_node(st.editor,'img').node)
    let p = item.values
    let disabled = st.read_only or item==null
    let menu = st.context.menu
    let property = if (menu=="shape-text-fill") (if (item.kind=="shape") "text_color" else "fill")
        else if (menu=="shape-text-outline") (if (item.kind=="shape") "text_outline" else "outline")
        else if (menu=="shape-fill") "fill" else "stroke";
    if (menu=="shape-edit") <div class:"shape-change-gallery",*[for (s in shapes.catalog)
        button("shape-change-" ++ s.id,"shape.format",s.label,shapes.graphic({*:shapes.defaults,kind:s.id,width:40,height:28}),
            {values:{kind:s.id}},p.kind==s.id,disabled,"shape-change-choice")]>
    else if (contains(["shape-fill","shape-outline","shape-text-fill","shape-text-outline"],menu)) <div
        <div class:"context-caption",if (contains(["shape-text-fill","shape-fill"],menu)) "Fill colors" else "Outline colors">;
        *[for (c in shape_format.colors) apply(<word_button id:menu ++ "-" ++ c.id,command:"shape.format",label:c.label,
            preview:<span class:"shape-color-chip",style:"background:" ++ c.value>,params:{values:{[property]:c.value,
                *:if (menu=="shape-outline" and p.weight==0) {weight:1.5} else if (menu=="shape-text-outline")
                    {[if (item.kind=="shape") "text_style" else "style"]:if (contains(["outlined","hollow"],if (item.kind=="shape") p.text_style else p.style))
                        (if (item.kind=="shape") p.text_style else p.style) else "outlined"} else {}}},
            pressed:p[symbol(property)]==c.value,disabled:disabled,class:"context-item">)];
        if (menu=="shape-fill") menu_item("shape-no-fill","shape.format","No Fill","clear",disabled,null,{values:{fill:"none"}})
        else if (menu=="shape-outline") <div
            menu_item("shape-no-outline","shape.format","No Outline","clear",disabled,null,{values:{weight:0}});
            <div class:"context-rule",role:"separator">;
            <div class:"shape-line-options",
                shape_select("shape-line-weight","Weight",[for (v in [0,0.5,1,1.5,2,3,4,6,8,12]) {value:v,label:string(v) ++ " px"}],p.weight,"weight",disabled)
                shape_select("shape-line-dash","Dashes",[for (v in ["solid","dashed","dotted","dash-dot"]) {value:v,label:v}],p.dash,"dash",disabled)>> else null;
        <div class:"context-rule",role:"separator">;
        menu_item("shape-more-colors","ui.shapePane","More colors…","shading",disabled,null,{open:true})>
    else if (menu=="shape-effects") <div *[for (e in [{value:"none",label:"No Shadow"},{value:"shadow",label:"Outer Shadow"}])
        menu_item("shape-effect-" ++ e.value,"shape.format",e.label,"shape_effects",disabled,null,{values:{effect:e.value}})]>
    else if (menu=="shape-align-text") <div *[for (v in ["top","middle","bottom"])
        menu_item("shape-text-" ++ v,"shape.format",{top:"Top",middle:"Middle",bottom:"Bottom"}[v],"align_text",disabled,null,{values:{vertical:v}})]>
    else if (menu=="shape-text-direction") <div *[for (v in [{id:"horizontal",label:"Horizontal",value:0},
        {id:"right",label:"Rotate All Text 90°",value:90},{id:"left",label:"Rotate All Text 270°",value:-90}])
        menu_item("shape-direction-" ++ v.id,"shape.format",v.label,"text_direction",disabled,null,{values:{direction:v.value}})]>
}
fn shape_field(id,label,value,property,disabled=false,draft={}) => <label class:"shape-field",<span label>
    apply(<control.input id:id,class:"ribbon-select",type:"text",label:label,
        value:if (draft[symbol(property)]==null) value else draft[symbol(property)],disabled:disabled,
        on_change:{action:"ui.shapeField",params:{property:property},param:"value"}>)>
fn shape_select(id,label,values,value,property,disabled=false) => <label class:"shape-field",<span label>
    apply(<control.select id:id,class:"ribbon-select",label:label,options:values,value:string(value),disabled:disabled,
        on_change:{action:"ui.shapeField",params:{property:property},param:"value"}>)>
fn shape_format_ribbon(editor,read_only,opened,picks,draft) {
    let item = shape_format.describe(model.current_node(editor,'img').node)
    let p = item.values
    let is_shape = item.kind=="shape"
    let disabled = read_only or item==null;
    <div class:"ribbon-groups shape-format-ribbon",
        if (is_shape) group("Insert Shapes",[<div class:"ribbon-command-stack",
            menu_button("shape-edit-menu","Edit Shape","edit_shape","shape-edit",opened,"",disabled)
            button("shape-edit-text","ui.shapePane","Edit Text","text_box",{open:true},false,disabled)
            button("shape-text-box","insert.shape","Text Box","text_box",{*:shapes.defaults,fill:"none",text:"Text box",text_color:"#24334b"},false,disabled)>]) else null
        if (is_shape) group("Shape Styles",[<div class:"ribbon-command-row",
            <div class:"shape-style-gallery",*[for (s in take(shape_format.styles,4))
                button("shape-style-" ++ s.id,"shape.format",s.label,shapes.graphic({*:shapes.defaults,*:s.values,width:44,height:40,text:"Abc",size:12,inset:2}),
                    {preset:s.id},p.fill==s.values.fill and p.stroke==s.values.stroke,disabled,"shape-style-card")]>
            <div class:"ribbon-command-stack",
                menu_button("shape-fill-menu","Shape Fill","shape_fill","shape-fill",opened,"",disabled)
                menu_button("shape-outline-menu","Shape Outline","shape_outline","shape-outline",opened,"",disabled)
                menu_button("shape-effects-menu","Shape Effects","shape_effects","shape-effects",opened,"",disabled)>>]) else null
        group("Word Art Styles",[<div class:"ribbon-command-row",
            <div class:"shape-text-gallery",*[for (s in (if (is_shape) ["solid","outlined","hollow","shadowed"] else wordart.styles))
                button("shape-text-style-" ++ s,"shape.format",s ++ " text",wordart.graphic({*:wordart.defaults,text:"A",width:44,height:44,size:32,style:s},"ribbon-art-" ++ s ++ "-"),
                    {values:{[if (is_shape) "text_style" else "style"]:s}},(if (is_shape) p.text_style else p.style)==s,disabled,"shape-text-card")]>
            <div class:"ribbon-command-stack",
                menu_button("shape-text-fill-menu","Text Fill","text_fill","shape-text-fill",opened,"",disabled)
                menu_button("shape-text-outline-menu","Text Outline","shape_outline","shape-text-outline",opened,"",disabled)
                button("shape-text-settings","ui.shapePane","Text Options","text_box",{open:true},false,disabled)>>])
        if (is_shape) group("Text",[<div class:"ribbon-command-stack",
            menu_button("shape-direction-menu","Text Direction","text_direction","shape-text-direction",opened,"",disabled)
            menu_button("shape-align-text-menu","Align Text","align_text","shape-align-text",opened,"",disabled);
            <div class:"ribbon-command-row",*[for (v in ["left","center","right"])
                button("shape-text-align-" ++ v,"shape.format",v ++ " text","align_" ++ v,{values:{align:v}},p.align==v,disabled,"icon-only")]>>]) else null
        arrange_ribbon(editor,read_only,opened,picks)
        group("Size",[<div class:"shape-size-fields",
            shape_field("shape-format-height","Height (px)",p.height,"height",disabled,draft)
            shape_field("shape-format-width","Width (px)",p.width,"width",disabled,draft)>])
        group("Format",[large("open-shape-format-pane","ui.shapePane","Format Pane","format_shape",{open:true},false,disabled)])>
}
pub fn shape_pane(st) {
    let item = shape_format.describe(model.current_node(st.editor,'img').node)
    let p = item.values
    let disabled = st.read_only
    let draft = shape_format.draft_values(st.editor,st.shape_draft)
    let is_shape = item.kind=="shape";
    <aside id:"shape-format-pane",class:"selection-pane shape-format-pane",["aria-label"]:"Format Shape",
        <div class:"navigation-heading",<strong class:"pane-heading",if (item.kind=="wordart") "Format Word Art" else "Format Shape">
            button("close-shape-pane","ui.shapePane","Close Format Pane","close",{},false,false,"icon-only")>;
        if (item==null) <p class:"pane-hint","Select a shape or Word Art to format it."> else <div
            <h3 "Fill & Line">;
            <div class:"shape-pane-grid",
                shape_field("shape-pane-fill",if (is_shape) "Fill (hex or none)" else "Fill (hex)",p.fill,"fill",disabled,draft)
                shape_field("shape-pane-outline","Outline",if (is_shape) p.stroke else p.outline,if (is_shape) "stroke" else "outline",disabled,draft)
                if (is_shape) <div class:"shape-pane-wide",
                    shape_select("shape-pane-fill-style","Fill type",[{value:"solid",label:"Solid"},{value:"gradient",label:"Gradient"}],p.fill_style,"fill_style",disabled)
                    shape_field("shape-pane-fill2","Gradient end color",p.fill2,"fill2",disabled,draft)
                    shape_field("shape-pane-opacity","Opacity (%)",p.opacity,"opacity",disabled,draft)> else null>;
            <h3 "Text Options">;
            shape_field("shape-pane-text","Text",p.text,"text",disabled,draft)
            shape_select("shape-pane-font","Font",font_options(st.fonts,p.font),p.font,"font",disabled);
            <div class:"shape-pane-grid",
                shape_field("shape-pane-size","Font size (px)",p.size,"size",disabled,draft)
                shape_field("shape-pane-text-color","Text fill",if (is_shape) p.text_color else p.fill,if (is_shape) "text_color" else "fill",disabled,draft)>;
            if (is_shape) <div
                <div class:"shape-text-emphasis",
                    button("shape-text-bold","shape.format","Bold","bold",{values:{bold:not p.bold}},p.bold,disabled,"icon-only")
                    button("shape-text-italic","shape.format","Italic","italic",{values:{italic:not p.italic}},p.italic,disabled,"icon-only")>;
                shape_field("shape-pane-inset","Text inset (px)",p.inset,"inset",disabled,draft)> else
                shape_select("shape-pane-transform","Text transform",[for (v in wordart.transforms) {value:v,label:v}],p.transform,"transform",disabled);
            <h3 "Size & Properties">;
            <div class:"shape-pane-grid",
                shape_field("shape-pane-width","Width (px)",p.width,"width",disabled,draft)
                shape_field("shape-pane-height","Height (px)",p.height,"height",disabled,draft)
                shape_field("shape-pane-rotation","Rotation (°)",p.rotation,"rotation",disabled,draft)>;
            shape_field("shape-pane-alt","Alt text",p.alt,"alt",disabled,draft)>>
}
fn arrange_ribbon(editor,read_only,opened,picks=[]) {
    let disabled = read_only or model.current_node(editor,'img')==null;
    group("Arrange",[
        menu_button("object-position-menu","Position","object_position","object-position",opened,"ribbon-large",disabled),
        menu_button("object-wrap-menu","Wrap Text","wrap_square","object-wrap",opened,"ribbon-large",disabled),
        menu_button("object-forward-menu","Bring Forward","bring_forward","object-forward",opened,"ribbon-large",disabled),
        menu_button("object-backward-menu","Send Backward","send_backward","object-backward",opened,"ribbon-large",disabled),
        large("object-selection-pane","ui.selectionPane","Selection Pane","selection_pane"),
        menu_button("object-align-menu","Align","object_align","object-align",opened,"ribbon-large",disabled),
        menu_button("object-group-menu","Group","object_group","object-group",opened,"ribbon-large",read_only or (disabled and len(picks)<2)),
        menu_button("object-rotate-menu","Rotate","rotate_right","object-rotate",opened,"ribbon-large",disabled or transform.settings(model.current_node(editor,'img').node)==null)])
}
pub fn selection_pane(st) => <aside id:"selection-pane",class:"selection-pane",
    <div class:"navigation-heading",<strong class:"pane-heading","Selection">
        button("close-selection-pane","ui.selectionPane","Close Selection Pane","close",{},false,false,"icon-only")>;
    <p class:"pane-hint","Select an object to arrange it. Check several objects to group them. Use the eye to hide or show.">;
    <div class:"object-list",*[for (e in reverse(sort(object_layout.entries(st.editor.doc),(e)=>e.page*100000+object_layout.layer(e.layout))))
        <div class:"object-list-row",
            button("pick-object-" ++ string(e.index),"ui.objectPick","Include in group","check",{path:e.path},
                contains(st.object_picks,e.path),st.read_only or transform.settings(e.node)==null,"icon-only object-pick");
            button("select-object-" ++ string(e.index),"ui.objectSelect",e.label,"shapes",{path:e.path},
                st.editor.selection.kind=='node' and st.editor.selection.path==e.path,false,"object-list-select");
            button("show-object-" ++ string(e.index),"ui.objectVisibility",if (e.layout.hidden) "Show object" else "Hide object",
                if (e.layout.hidden) "eye_off" else "eye",{path:e.path},false,st.read_only,"icon-only")>];
        if (len(object_layout.entries(st.editor.doc))==0) <p class:"pane-hint","No objects in this document."> else null>>
pub fn context_menu(st) => if (st.context==null) null else if (st.context.menu!=null) editing_menu(st) else {
    let selected = if (st.read_only) st.context.copy_text!="" else context.has_selection(st.editor.selection)
    let disabled = st.read_only
    let item = focused.describe({*:st.editor,selection:st.context.item_selection or st.editor.selection},st.context.story);
    context.frame("word-context-menu","word",
        <div id:"word-context-menu",class:"context-menu",role:"menu",["aria-label"]:item.label ++ " actions",["data-context-kind"]:item.kind,
            style:"left:" ++ string(st.context.x) ++ "px;top:" ++ string(st.context.y) ++ "px",
            if (len(item.actions)>0) <div class:"context-specific",role:"group",["aria-label"]:item.label,
                <div class:"context-caption",item.label>;
                *[for (entry in item.actions) menu_item(entry.id,entry.command,entry.label,entry.icon,
                    disabled or entry.disabled,null,if (starts_with(entry.command,"table.select"))
                        {*:entry.params,path:st.context.cell_path} else entry.params,st.context.item_selection)];
                <div class:"context-rule",role:"separator">> else null;
            menu_item("undo","edit.undo","Undo","undo",disabled or not edit_can_undo(st.editor),"⌘Z");
            menu_item("redo","edit.redo","Redo","redo",disabled or not edit_can_redo(st.editor),"⇧⌘Z");
            if (st.context.story!=null) null else <div class:"context-common",
            <div class:"context-rule",role:"separator">
            menu_item("cut","clipboard.cut","Cut","cut",disabled or not selected,"⌘X");
            menu_item("copy","clipboard.copy","Copy","copy",not selected,"⌘C");
            menu_item("paste","clipboard.paste","Paste","paste",disabled,"⌘V");
            menu_item("delete","clipboard.delete","Delete","trash",disabled or not selected);
            menu_item("select-all","clipboard.selectAll","Select All","select_all",false,"⌘A");
            if (not item.text) null else <div class:"context-text",
            <div class:"context-rule",role:"separator">
            <div class:"context-format",*[for (entry in take(home_ui.emphasis,3))
                button("context-" ++ entry.id,"format." ++ entry.id,model.spec("format." ++ entry.id).label,
                    entry.id,{},edit_mark_active(st.editor,entry.mark),disabled,"context-item icon-only")]>
            menu_item("paragraph","ui.paragraph","Paragraph…","settings",disabled);
            if (model.current_node(st.editor,'a')==null) menu_item("link","ui.link","Link…","link",disabled,"⌘K") else null;
            menu_item("comment","ui.comment","New Comment…","comment_add",disabled or not model.can_comment(st.editor))>>
        >,false)
}
fn mark_button(editor,id,graphic) => button(id,id,model.spec(id).label,graphic,{},
    edit_mark_active(editor,model.spec(id).mark),false,"icon-only")
fn group(label,items) => <div class:"ribbon-group group-" ++ replace(lower(label)," ","-"),role:"group",["aria-label"]:label,
    <div class:"group-tools", *items> <div class:"group-label", label>>
fn large(id,action,label,graphic,params={},pressed=false,disabled=false,title=null) =>
    button(id,action,label,graphic,params,pressed,disabled,"ribbon-large",title)
fn selector(id,command,values,current,css="",param="value",disabled=false) =>
    apply(<control.select id:id,class:"ribbon-select " ++ css,label:model.spec(command).label,
        options:values,value:current,disabled:disabled,on_change:{action:command,param:param}>)
fn pen_selector(property,label,values,current,disabled) => <label class:"table-pen-field",<span label>
    apply(<control.select id:"table-pen-" ++ property,class:"ribbon-select",label:label,options:values,value:current,disabled:disabled,
        on_change:{action:"ui.tablePen",params:{property:property},param:"value"}>)>
fn alignment_icon(value) {
    let x = if (ends_with(value,"Left")) 5 else if (ends_with(value,"Right")) 10 else 7.5
    let y = if (starts_with(value,"top")) 5 else if (starts_with(value,"bottom")) 13 else 9;
    <svg class:"app-icon",width:"20",height:"20",viewBox:"0 0 24 24",["aria-hidden"]:"true",
        <path d:"M3 3h18v18H3z",fill:"none",stroke:"#a7b6ca",'stroke-width':1.2>
        <path d:"M5 " ++ string(y) ++ "h14 M" ++ string(x) ++ " " ++ string(y+3) ++ "h9 M5 " ++ string(y+6) ++ "h14",
            fill:"none",stroke:"currentColor",'stroke-width':1.4>>
}
fn size_field(axis,label,value,disabled) => <label class:"table-size-field",<span label>
    apply(<control.input id:"table-cell-" ++ axis,class:"ribbon-select",type:"text",
        label:label ++ " in points",placeholder:"Auto",value:value,disabled:disabled,
        on_change:{action:"ui.tableSize",params:{axis:axis},param:"value"}>); <span "pt">>
fn table_layout_ribbon(editor,read_only,opened_menu,gridlines) {
    let p = table_layout.settings(editor)
    let picked = tables.selected(editor)
    let first = picked[0];
    <div class:"ribbon-groups table-layout-ribbon",
        group("Table",[
            menu_button("table-select-menu","Select","pointer","table-select",opened_menu,"",read_only),
            button("table-gridlines","ui.tableGridlines","View Gridlines","table_gridlines",{},gridlines,read_only),
            button("table-properties","ui.tableLayoutDialog","Properties","table_properties",{kind:"table_properties"},false,read_only)])
        group("Rows & Columns",[<div class:"table-insert-tools",
            menu_button("table-delete-menu","Delete","trash","table-delete",opened_menu,"table-large-tool",read_only);
            *[for (entry in [{id:"insertRowAbove",label:"Insert Above",icon:"insert_above"},{id:"insertRowBelow",label:"Insert Below",icon:"insert_below"},
                {id:"insertColumnLeft",label:"Insert Left",icon:"insert_left"},{id:"insertColumnRight",label:"Insert Right",icon:"insert_right"}])
                button("table." ++ entry.id,"table." ++ entry.id,entry.label,entry.icon,{},false,read_only,"table-large-tool")] >])
        group("Merge",[
            command("table.mergeCells","Merge Cells","merge_cells",{},false,read_only or len(picked)<2),
            button("table.splitCells","ui.tableLayoutDialog","Split Cells","split_cells",{kind:"table_split"},false,read_only or len(picked)!=1),
            command("table.splitTable","Split Table","split_table",{},false,read_only or first.row==0)])
        group("Cell Size",[<div class:"table-sizing",
            <div class:"table-size-fields",size_field("height","Height",p.height,read_only);size_field("width","Width",p.width,read_only)>
            menu_button("table-autofit-menu","AutoFit","autofit","table-autofit",opened_menu,"table-large-tool",read_only);
            <div class:"table-distribute",
                command("table.distributeRows","Distribute Rows","distribute_rows",{},false,read_only)
                command("table.distributeColumns","Distribute Columns","distribute_columns",{},false,read_only)>>])
        group("Alignment",[<div class:"table-alignment-tools",
            <div class:"cell-alignment-grid",role:"group",["aria-label"]:"Cell alignment",*[for (value in table_layout.alignments)
                button("cell-align-" ++ value,"table.cellAlign",value,alignment_icon(value),{value:value},
                    model.cell_alignment(editor)==value,read_only,"icon-only")]>
            button("table.textDirection","table.textDirection","Text Direction","text_direction",{},p.direction!="horizontal-tb",read_only,"table-large-tool")
            button("table-cell-margins","ui.tableLayoutDialog","Cell Margins","cell_margins",{kind:"table_margins"},false,read_only,"table-large-tool")>])
        group("Data",[<div class:"table-data-tools",
            button("table-sort","ui.tableLayoutDialog","Sort","sort",{kind:"table_sort"},false,read_only,"table-large-tool")
            button("table.repeatHeader","table.repeatHeader","Repeat Header Rows","repeat_header",{},p.repeat,read_only,"table-large-tool")
            button("table-convert-text","ui.tableLayoutDialog","Convert to Text","table_text",{kind:"table_text"},false,read_only,"table-large-tool")
            button("table-formula","ui.tableLayoutDialog","Formula","formula",{kind:"table_formula"},false,read_only or len(picked)!=1,"table-large-tool")>])>
}
fn table_design_ribbon(editor,read_only,opened_menu,pen) {
    let p = table_design.settings(model.current_node(editor,'table').node)
    let shading = table_design.shading_value(tables.selected(editor)[0].node);
    <div class:"ribbon-groups table-design-ribbon",
        group("Table Style Options",[<div class:"table-style-options",*[for (entry in table_design.options)
            apply(<control.checkbox id:"table-option-" ++ entry.id,class:"table-style-option",label:entry.label,checked:p[entry.id],disabled:read_only,
                on_change:{action:"table.designOption",params:{option:entry.id},param:"value"},entry.label>)]>])
        group("Table Styles",[<div class:"table-style-gallery",role:"group",["aria-label"]:"Table styles",*[for (entry in table_design.styles)
            button("table-style-" ++ entry.id,"table.style",entry.label,table_design.thumbnail({*:p,style:entry.id}),
                {style:entry.id},p.style==entry.id,read_only,"table-style-card")]>,
            selector("table-style","table.style",[for (s in table_design.styles) {value:s.id,label:s.label}],p.style,"table-style-select","style",read_only)])
        group("Shading",[<label class:"table-pen-field",<span "Cell shading">
            selector("table-shading","table.shading",table_design.color_options(shading),shading,"","color",read_only)>,
            button("table-shading-more","ui.tableShading","More colors…","shading",{},false,read_only)])
        group("Draw Borders",[
            pen_selector("style","Line style",[for (value in tables.border_styles) {value:value,label:value}],pen.style,read_only),
            pen_selector("width","Line weight",[for (value in [0.25,0.5,0.75,1,1.5,2,3,4.5,6]) {value:value,label:string(value) ++ " pt"}],pen.width,read_only),
            pen_selector("color","Pen color",table_design.color_options(pen.color,false),pen.color,read_only)])
        group("Borders",[menu_button("table-borders-menu","Borders","border_all","borders",opened_menu,"",read_only),
            button("cell-borders","ui.cellBorders","Borders…","settings",{},false,read_only)])>
}
fn font_step(editor,grow) => home.font_step(home_ui.font_size(editor),grow)

pub fn selection_toolbar(st) => if (not mini.visible(st)) null else {
    let editor = st.editor
    let align = model.css_get(attrs_get(model.current_block(editor).node.attrs,'style'),"text-align") or "left";
    mini.frame([
        <div class:"mini-row",
            selector("mini-font-family","format.font",font_options(st.fonts,model.char_value(editor,"font-family","Georgia")),
                model.char_value(editor,"font-family","Georgia"),"font-family","name");
            selector("mini-font-size","format.size",[for (s in home.font_sizes) {value:s,label:string(s)}],
                replace(model.char_value(editor,"font-size","11pt"),"pt",""),"font-size","size");
            button("mini-grow","format.size","Grow font","plus",{size:font_step(editor,true)},false,false,"icon-only");
            button("mini-shrink","format.size","Shrink font","minus",{size:font_step(editor,false)},false,false,"icon-only");
            button("mini-clear","format.clear","Clear formatting","clear",{},false,false,"icon-only")>,
        <div class:"mini-row",
            *[for (entry in take(home_ui.emphasis,4)) button("mini-" ++ entry.id,"format." ++ entry.id,model.spec("format." ++ entry.id).label,
                entry.icon or entry.id,{},edit_mark_active(editor,entry.mark),false,"icon-only")];
            <span class:"mini-divider">;
            button("mini-highlight","format.highlight","Highlight","highlight",{color:"#f9e896"},false,false,"icon-only");
            selector("mini-color","format.color",text_colors,model.char_value(editor,"color","#24334b"),"mini-color","color");
            <span class:"mini-divider">;
            *[for (choice in home_ui.alignments) button("mini-" ++ choice.icon,choice.id,model.spec(choice.id).label,
                choice.icon,{},align==choice.value,false,"icon-only")]>])
}

pub fn view_buttons(layout,read_only,status=false) => [for (choice in views.modes)
    button(if (status) "status-view-" ++ choice.id else choice.ribbon,choice.command,choice.label,choice.icon,
        if (status) {enabled:true} else {},views.active(layout,read_only)==choice.id,false,if (status) "icon-only" else "ribbon-large")]
fn equation_widgets() => {button:button,menu:menu_button,group:group,menu_item:menu_item}
fn home_widgets() => {*:equation_widgets(),selector:selector,mark_button:mark_button,font_options:font_options}
pub fn equation_pane(st) => equation_ui.pane(st,equation_widgets())

pub fn ribbon(editor,tab,nav,read_only,show_changes=true,opened_menu=null,font_families=[],layout="print",focused=false,table_pen=table_design.pen,table_gridlines=false,object_picks=[],shape_draft=null,show_ribbon_group_labels=false,home_state=home.defaults) {
    if (tab == "Home") home_ui.ribbon(editor,read_only,opened_menu,font_families,home_state,home_widgets())
    else if (tab == "Insert") <div class:"ribbon-groups insert-ribbon",
        group("Pages",[<div class:"ribbon-command-row",
            large("insert-cover","ui.cover","Cover Page","cover",{},false,read_only);
            <div class:"ribbon-command-stack",
                command("insert.blankPage","Blank Page","blank_page",{},false,read_only)
                button("insert-page-break","insert.pageBreak","Page Break","page_break",{},false,read_only)>>])
        group("Tables",[large("insert.table","ui.table","Table","table",{},false,read_only)])
        group("Illustrations",[
            <div class:"ribbon-command-row",
                large("insert-picture","ui.picture","Pictures","album",{},false,read_only)
                large("insert-shape","ui.shape","Shapes","shapes",{},false,read_only)
                large("insert-icons","ui.icon","Icons","emoji",{},false,read_only,"Icons / Emojis")
                large("insert-smartart","ui.smartArt","Diagram","smartart",{},false,read_only)
                large("insert-chart","ui.chart","Chart","chart",{},false,read_only)>,
            <div class:"ribbon-secondary-row",
                button("picture-settings","ui.pictureSettings","Picture Settings","settings",{},false,read_only or model.current_node(editor,'img')==null)
                button("shape-settings","ui.shapeSettings","Shape Settings","settings",{},false,read_only or shapes.settings(model.current_node(editor,'img').node)==null)>])
        group("Links",[<div class:"ribbon-command-stack",
            button("insert-link","ui.link",if (model.current_node(editor,'a')==null) "Link" else "Edit Link","link",{},false,read_only);
            <div class:"ribbon-command-row",
                button("insert-bookmark","ui.bookmarks","Bookmark","bookmark")
                button("follow-link","ui.followLink","Go to Bookmark","expand",{},false,
                    insert.link_bookmark(attrs_get(model.current_node(editor,'a').node.attrs,'href'))==null,"icon-only")>;
            command("insert.removeLink","Remove Link","unlink",{},false,read_only or model.current_node(editor,'a')==null)>])
        group("Comments",[large("insert-comment","ui.comment","Comment","comment_add",{},false,read_only or not model.can_comment(editor))])
        group("Header & Footer",[<div class:"ribbon-command-row",
            large("insert-header","ui.headerFooter","Header","header",{},false,read_only)
            large("insert-footer","ui.headerFooter","Footer","footer",{},false,read_only)
            large("insert-page-number","ui.pageNumber","Page Number","page_number",{},false,read_only)>])
        group("Text",[<div class:"ribbon-command-row",
            large("insert-wordart","ui.wordArt","Word Art","wordart",{},false,read_only)
            large("object-settings","ui.objectSettings","Edit Object","settings",{},false,read_only or
                graphics.descriptor(model.current_node(editor,'img').node)==null)>])
        group("Symbols",[<div class:"ribbon-command-row",
            large("insert-equation","ui.equation","Equation","equation",{},false,read_only);
            <div class:"ribbon-command-stack",
                button("insert-symbol","ui.symbol","Symbol","symbol",{},false,read_only)
                command("insert.horizontalRule","Rule","rule",{},false,read_only)>>])
    >
    else if (tab=="References") <div class:"ribbon-groups references-ribbon",
        group("Citations",[<div class:"ribbon-command-row",
            large("insert-citation","ui.citation","Insert Citation","citation",{},false,read_only)
            large("manage-sources","ui.sources","Manage Sources","sources");
            <div class:"ribbon-command-stack",
                <label class:"citation-style-field",<span "Style">
                    selector("citation-style","references.style",references.styles,references.settings(editor.doc).style,"citation-style","style",read_only)>;
                button("edit-citation","ui.editCitation","Edit Citation","citation",{},false,read_only or references.current(editor)==null)
                button("edit-source","ui.source","Edit Source","sources",{},false,read_only or references.current(editor)==null)>>])
        group("Bibliography",[<div class:"ribbon-command-row",
            large("insert-bibliography","references.bibliography","Bibliography","bibliography",{},false,read_only or len(references.entries(editor.doc))==0);
            <div class:"ribbon-command-stack",
                button("update-citations","references.refresh","Update Citations & Bibliography","refresh",{},false,read_only)
                button("new-source","ui.source","New Source","source_add",{new:true},false,read_only)>>])>
    else if (tab == "Review") <div class:"ribbon-groups review-ribbon",
        group("Comments",[<div class:"ribbon-command-row",
            large("new-comment","ui.comment","New Comment","comment_add",{},false,read_only or not model.can_comment(editor));
            <div class:"ribbon-command-stack",button("show-comments","ui.comments","Comments Pane","comment")
                button("show-resolved","ui.resolvedComments","Show Resolved","check")>>])
        group("Navigate",[<div class:"ribbon-command-stack",
            button("previous-comment","ui.previousComment","Previous Comment","up")
            button("next-comment","ui.nextComment","Next Comment","down")>])
        group("Tracking",[<div class:"ribbon-command-row",
            large("track-changes","review.trackChanges","Track Changes","tracking",{enabled:not changes.settings(editor.doc).enabled},changes.settings(editor.doc).enabled,read_only);
            <div class:"ribbon-command-stack",button("tracking-options","ui.tracking","Tracking Options","settings",{},false,read_only)
                button("show-change-markup","ui.showChanges","Show Markup","eye",{},show_changes)>>])
        group("Changes",[<div class:"ribbon-command-row",
            large("show-revisions","ui.revisions",string(len(changes.entries(editor.doc))) ++ " Changes","changes");
            <div class:"ribbon-command-stack",
                command("review.acceptAll","Accept All","check",{},false,read_only or len(changes.entries(editor.doc))==0)
                command("review.rejectAll","Reject All","reject",{},false,read_only or len(changes.entries(editor.doc))==0)>>])
    >
    else if (tab=="Equation") equation_ui.ribbon(editor,read_only,opened_menu,equation_widgets())
    else if (tab=="Shape Format") shape_format_ribbon(editor,read_only,opened_menu,object_picks,shape_format.draft_values(editor,shape_draft))
    else if (tab=="Object Format") <div class:"ribbon-groups object-format-ribbon",
        arrange_ribbon(editor,read_only,opened_menu,object_picks)
        group("Size & Appearance",[large("object-layout-options","ui.objectLayout","Layout Options","settings",{},false,read_only),
            large("object-format-settings","ui.pictureSettings","Format Object","shapes",{},false,read_only)])>
    else if (tab == "Layout") <div class:"ribbon-groups layout-ribbon",
        group("Page",[large("page-setup","ui.pageSetup","Page Setup","document",{},false,read_only),
            selector("paper-size","layout.pageSetup",[for (p in page.papers) {value:p.id,label:p.id}],page.settings(editor.doc).paper,"","paper")])
        group("Orientation",[<div class:"ribbon-command-row",*[for (choice in ["portrait","landscape"]) large("orientation-" ++ choice,"layout.pageSetup",if (choice=="portrait") "Portrait" else "Landscape",choice,{orientation:choice},
            page.settings(editor.doc).orientation==choice,read_only)]>])
        group("Margins",[menu_button("margins-menu","Margins","margins","margins",opened_menu,"ribbon-large",read_only)])
        group("Columns",[menu_button("columns-menu","Columns","columns","columns",opened_menu,"ribbon-large",read_only)])
        group("Breaks",[<div class:"ribbon-command-row",
            large("insert.pageBreak","insert.pageBreak","Page Break","page_break",{},false,read_only)
            button("layout-paragraph","ui.paragraph","Paragraph…","paragraph",{},false,read_only)>])
        arrange_ribbon(editor,read_only,opened_menu,object_picks)
    >
    else if (tab == "Table Design") table_design_ribbon(editor,read_only,opened_menu,table_pen)
    else if (tab == "Table") table_layout_ribbon(editor,read_only,opened_menu,table_gridlines)
    else <div class:"ribbon-groups view-ribbon",
        group("Document views",view_buttons(layout,read_only))
        group("Show",[<div class:"ribbon-command-stack",
            button("view-navigation","view.navigationPane","Navigation","sidebar",{},nav)
            button("view-ribbon-labels","view.ribbonGroupLabels","Ribbon Group Labels","text_box",{},show_ribbon_group_labels)
            button("view-focus","view.focusMode","Focus","focus",{},focused)>])
        group("Zoom",[button("zoom-out","view.zoomOut","Zoom out","minus"),button("zoom-reset","view.zoomReset","100%","zoom_reset"),
            button("zoom-in","view.zoomIn","Zoom in","plus")])
    >
}

pub fn navigation(editor,query,index,match_case=false,replacement="",search_mode="find",read_only=false,tab="headings") {
    let hits = model.search(editor.doc,query,match_case)
    let headings = model.headings(editor.doc)
    let selected_heading = if (tab=="headings") navmodel.current_heading(editor) else null
    let selected_page = if (tab=="pages") navmodel.current_page(editor) else null;
    <aside id:"navigation", class:"navigation",
        <div class:"navigation-heading", <strong class:"pane-heading", "Navigation">
            button("close-navigation","ui.closeNavigation","Close Navigation","close",{},false,false,"icon-only")>
        <div class:"search-field", <span class:"search-icon",icon("search",16)> apply(<control.input id:"find-query",placeholder:"Find in document",
            value:query,label:"Find in document",on_change:"find.query">)>
        <div class:"navigation-tabs",role:"tablist",["aria-label"]:"Navigation",
            *[for (choice in navmodel.tabs) apply(<word_button id:"nav-tab-" ++ choice.id,command:"ui.navigationTab",
                params:{tab:choice.id},label:choice.label,nav_tab:choice.id,pressed:tab==choice.id>)]>
        <div class:"search-summary", id:"search-summary", if (query=="") "A map for your ideas" else string(len(hits)) ++ " matches">
        if (query != "" or search_mode=="replace") <div class:"find-controls",
            button("find-match-case","ui.matchCase","Match case",null,{},match_case); button("find-clear","ui.clearFind","Clear");
            button("find-prev","edit.findPrevious","Previous","up",{},false,len(hits)==0); button("find-next","edit.findNext","Next","down",{},false,len(hits)==0);
            apply(<control.input id:"replace-text",placeholder:"Replace with",value:replacement,
                label:"Replace with",on_change:"find.replacement">)
            button("replace-one","edit.replace","Replace",null,{},false,read_only or len(hits)==0);
            button("replace-all","edit.replaceAll","Replace all",null,{},false,read_only or len(hits)==0)>
        else null;
        <div id:"navigation-" ++ tab,class:"navigation-content",role:"tabpanel",["aria-labelledby"]:"nav-tab-" ++ tab,
            if (tab=="headings") <div class:"outline",
                if (len(headings)==0) <p class:"navigation-empty","Use heading styles to build an outline."> else null;
                *[for (i,heading in headings) button("heading-" ++ string(i),"ui.heading",if (trim(heading.label)=="") "Untitled heading" else heading.label,null,{path:heading.path},
                    heading.path==selected_heading,false,"outline-item level-" ++ heading.level)]>
            else if (tab=="pages") <div class:"navigation-pages",
                *[for (entry in navmodel.pages(editor.doc)) apply(<word_button id:"nav-page-" ++ string(entry.number),command:"ui.page",label:"Page " ++ string(entry.number),
                    preview:surface.thumbnail(editor.doc,entry.content,156,entry.number),params:{page:entry.number},pressed:entry.number==selected_page,class:"page-card">)]>
            else <div class:"navigation-results",
                if (len(hits)==0) <p class:"navigation-empty",if (query=="") "Search the document to see results here." else "No results found."> else null;
                *[for (i,hit in hits,let excerpt=navmodel.excerpt(editor.doc,hit))
                    apply(<word_button id:"nav-result-" ++ string(i),command:"ui.findResult",params:{index:i},
                        label:excerpt.before ++ excerpt.match ++ excerpt.after,
                        content:<span excerpt.before; <mark excerpt.match>; excerpt.after>,
                        pressed:index==i,class:"outline-item search-result">)]>>
    >
}

pub fn comments(editor,show_resolved,read_only) {
    let entries = review.comments(editor.doc)
    let visible = [for (c in entries where show_resolved or not c.resolved) c];
    <aside id:"comments-pane",class:"review-pane", ["aria-label"]:"Document comments",
        <div class:"review-heading", <strong "Comments"> button("close-comments","ui.comments","Close","close")>
        <p class:"review-summary",string(len([for (c in entries where not c.resolved) c])) ++ " open comments">
        if (len(visible)==0) <p class:"review-empty","Select some text and choose New comment in the Review tab."> else null;
        *[for (c in visible) <article id:"comment-" ++ c.id,class:"comment-card" ++ (if (c.resolved) " resolved" else ""),["data-comment-id"]:c.id,
            <div class:"comment-author", c.author; <span if (c.resolved) "Resolved" else "Open">>
            <blockquote class:"comment-quote",c.quote>
            <p class:"comment-text",c.text>
            <div class:"comment-actions",
                button("comment-go-" ++ c.id,"ui.commentText","Go to text","expand",{id:c.id});
                button("comment-resolve-" ++ c.id,"review.resolveComment",if (c.resolved) "Reopen" else "Resolve",null,{id:c.id,resolved:not c.resolved},false,read_only);
                button("comment-delete-" ++ c.id,"review.deleteComment","Delete","trash",{id:c.id},false,read_only)>
        >]
    >
}

pub fn revisions(editor,read_only=false) {
    let pending = changes.entries(editor.doc);
    <aside class:"review-pane revision-pane",["aria-label"]:"Tracked changes",
        <div class:"review-heading",<strong "Changes">button("close-revisions","ui.revisions","Close","close")>
        <p class:"review-summary",string(len(pending)) ++ " pending changes">
        if (len(pending)==0) <p class:"review-empty","Turn on Track text changes to record insertions and deletions."> else null;
        *[for (entry in pending) <article class:"comment-card revision-card",["data-revision-id"]:entry.id,
            <div class:"comment-author",entry.author>
            if (entry.removed=="") null else <p class:"revision-deleted",entry.removed>
            if (entry.inserted=="") null else <p class:"revision-inserted",entry.inserted>;
            <div class:"comment-actions",
                button("revision-go-" ++ entry.id,"ui.revisionText","Go to","expand",{id:entry.id})
                button("revision-accept-" ++ entry.id,"review.accept","Accept","check",{id:entry.id},false,read_only)
                button("revision-reject-" ++ entry.id,"review.reject","Reject","close",{id:entry.id},false,read_only)>>]>
}

fn dialog_field(id,label,value,numeric=false,min=0,max=2000,step=1) => <label class:"dialog-field",
    <span label> <input id:id, class:"edit-dialog-field", type:if (numeric) "number" else "text",
        value:string(value), *:if (numeric) {min:string(min),max:string(max),step:string(step)} else {}>>
fn dialog_select(id,label,values,current) => <label class:"dialog-field", <span label>
    <select id:id, class:"edit-dialog-field", ["aria-label"]:label,
        *[for (entry in values) <option value:string(entry.value), *:if (string(entry.value)==string(current)) {selected:""} else {}, entry.label>]>>
fn form_dialog(d:map,title:string,description:string,fields:array,action:string,label:string) => files.dialog_frame(string(d.kind),title,description,
    [if (d.error is null) null else <p id:"dialog-error",role:"alert",d.error>,*fields],
    [files.dialog_button("cancel","Cancel",false),files.dialog_button(action,label,true)])

fn icon_tabs(active) => <div class:"illustration-tabs",
    button("gallery-icons","ui.icon","Icons","star",{},active=="icon")
    button("gallery-emojis","ui.emoji","Emojis","emoji",{},active=="emoji")>
fn illustration_dialog(d) {
    let entry = graphics.kind(d.kind)
    let p = graphics.parameters(d.kind,d.values)
    let preview = entry.graphic(p) ^ { null }
    let src = if (preview==null) null else objects.source(preview) ^ { null };
    form_dialog(d,(if (d.editing) "Edit " else "Insert ") ++ entry.label,
        if (d.kind=="icon") "Choose an outline icon or switch to emojis. Icons keep their color and size settings."
        else if (d.kind=="smartart") "Choose an arrangement and enter one item per line (2–8 items). The first item heads the hierarchy."
        else "Enter Label, Value on each line (up to 20 categories). Use Preview to check changes before inserting.",[
        if (d.kind=="icon" and not d.editing) icon_tabs("icon") else null,
        if (d.kind=="icon") <div class:"icon-gallery",*[for (choice in iconmodel.catalog)
            button("icon-choice-" ++ choice.id,"ui.chooseGraphic",choice.label,icons.icon(choice.id,24),{name:choice.id},p.name==choice.id,false,"icon-choice")]>
        else if (d.kind=="smartart") <div class:"smartart-gallery",*[for (choice in smartart.layouts)
            button("smartart-choice-" ++ choice.id,"ui.chooseGraphic",choice.label,
                smartart.graphic({*:smartart.defaults,layout:choice.id,width:152,height:57}),{layout:choice.id},p.layout==choice.id,false,"smartart-choice")]>
        else <div class:"chart-gallery",*[for (choice in chartmodel.types)
            button("chart-choice-" ++ choice.id,"ui.chooseGraphic",choice.label,choice.icon,{type:choice.id},p.type==choice.id,false,"chart-choice")]>,
        if (src==null) null else <div class:"illustration-preview",<img src:src,alt:entry.label ++ " preview">>,
        if (d.kind=="chart") dialog_field("chart-title","Title",p.title) else null,
        if (d.kind=="icon") null else <label class:"dialog-field",<span if (d.kind=="chart") "Data · Label, Value" else "Text · one item per line">
            <textarea id:d.kind ++ "-" ++ (if (d.kind=="chart") "data" else "text"),class:"edit-dialog-field illustration-text",if (d.kind=="chart") p.data else p.text>>,
        <div class:"dialog-grid",
            dialog_field(d.kind ++ "-color","Color (#RRGGBB)",p.color)
            dialog_field(d.kind ++ "-rotation","Rotation (degrees)",p.rotation,true,-180,180)
            dialog_field(d.kind ++ "-width","Width (px)",p.width,true,16,650)
            dialog_field(d.kind ++ "-height","Height (px)",p.height,true,16,2000)>,
        button("illustration-preview","ui.previewGraphic","Preview","eye")],"illustration",if (d.editing) "Apply" else "Insert " ++ entry.label)
}

fn source_dialog(d:map) => form_dialog(d,if (d.source_id is string) "Edit Source" else "Create Source",
    "Add the publication details. Authors: Last, First; Last, First. Leave the year blank for an undated source.",[
    <div class:"dialog-grid",
        <label class:"dialog-field",<span "Type of source">
            apply(<control.select id:"source-kind",class:"edit-dialog-field",label:"Type of source",options:references.types,
                value:d.values.kind,on_change:{action:"ui.sourceKind",param:"value"}>)>;
        <label class:"dialog-field",<span "Author type">
            apply(<control.select id:"source-corporate",class:"edit-dialog-field",label:"Author type",
                options:[{value:"false",label:"Person(s)"},{value:"true",label:"Corporate author"}],value:string(d.values.corporate),
                on_change:{action:"ui.sourceCorporate",param:"value"}>)>>,
    <div class:"source-fields",*[for (f in references.visible_fields(d.values.kind,d.all_fields==true))
        dialog_field("source-" ++ f.key,f.label,d.values[f.key])]>,
    apply(<control.checkbox id:"source-all-fields",class:"table-style-option",label:"Show all bibliography fields",checked:d.all_fields==true,
        on_change:{action:"ui.sourceFields",param:"value"},"Show all bibliography fields">)],"source","OK")

fn sources_dialog(d:map,editor:map,read_only:bool) {
    let items = references.sources(editor.doc)
    let picker = d.kind=="citations";
    files.dialog_frame(d.kind,if (picker) "Insert Citation" else "Manage Sources",
        if (picker) "Choose a saved source or create a new one." else "Sources are saved with this document. Editing a source updates its citations and bibliography.",[
        if (d.error==null) null else <p id:"dialog-error",role:"alert",d.error>,
        if (picker) dialog_field("citation-locator","Page(s) · optional",d.values.locator) else null,
        <div class:"source-list",*[for (s in items,let used=references.uses(editor.doc,s.id)) <article class:"source-card",["data-source-type"]:s.kind,
            <strong s.title>
            <span class:"source-description",join([for (v in [s.author,s.year] where v!="") v]," · ")>
            <span class:"source-usage",string(used) ++ (if (used==1) " citation" else " citations")>
            <div class:"source-actions",
                if (picker) button("source-insert-" ++ s.id,"ui.insertCitation","Insert","citation",{id:s.id},false,read_only,"source-insert") else null;
                button("source-edit-" ++ s.id,"ui.source","Edit","settings",{id:s.id},false,read_only,"source-edit")
                button("source-delete-" ++ s.id,"references.deleteSource","Delete","trash",{id:s.id},false,read_only or used>0,"source-delete")>>]
            if (len(items)==0) <p class:"bookmark-empty","No sources yet. Create a source to start citing."> else null>,
        if (read_only) null else button("source-new","ui.source","Add New Source","source_add",{new:true,insert:picker})],
        [files.dialog_button("cancel","Close",false)])
}

// Chrome-style print page: measured sheet preview beside per-print settings that never edit the document.
fn print_select(id,label,options,key,value) => <label class:"dialog-field",<span label>
    apply(<control.select id:id,class:"edit-dialog-field",label:label,options:options,value:string(value),
        on_change:{action:"ui.printSetting",param:"value",params:{key:key}}>)>
fn print_input(id,label,placeholder,key,value) => <label class:"dialog-field print-detail",<span label>
    apply(<control.input id:id,class:"edit-dialog-field",type:"text",label:label,placeholder:placeholder,value:value,
        on_change:{action:"ui.printSetting",param:"value",params:{key:key}}>)>
fn print_option(id,label,key,checked) => apply(<control.checkbox id:id,class:"table-style-option",label:label,checked:checked,
    on_change:{action:"ui.printSetting",param:"value",params:{key:key}},label>)
fn print_dialog(d:map,editor:map) {
    let v = d.values
    let printed = print.document(editor.doc,v) ^ { editor.doc }
    let total = len(print.pages(printed,d.starts))
    // an unfinished custom range previews every page until it parses.
    let range_ok = print.page_numbers(v,total)!=null
    let shown = if (range_ok) v else {*:v,pages:"all"}
    let sheets = print.sheets(printed,shown,d.starts) ^ { [] }
    let lay = print.layout(printed,v)
    let width = if (lay.width>lay.height) 440 else 320;
    files.dialog_frame("print","Print","",[<div class:"print-layout",
        <div class:"print-preview" ++ (if (v.backgrounds) "" else " print-plain"),role:"region",["aria-label"]:"Print preview",
            *[for (sheet in sheets) surface.print_sheet(printed,sheet,lay,total,width,print.scale(v))]
            surface.print_measure(printed,print.scale(v))>
        <div class:"print-settings",
            <div class:"print-heading",<span class:"print-title","Print">
                <span class:"print-summary",string(len(sheets)) ++ (if (len(sheets)==1) " sheet" else " sheets") ++ " of paper">>
            <div class:"print-options",
                if (d.error==null) null else <p id:"dialog-error",role:"alert",d.error>;
                print_select("print-destination","Destination",print.destinations,"destination",v.destination)
                print_select("print-pages","Pages",print.page_choices,"pages",v.pages)
                if (v.pages!="custom") null else print_input("print-range","Page range","e.g. 1-5, 8","range",v.range);
                if (range_ok or v.range=="") null else <p class:"print-hint",role:"status",print.range_message(total)>;
                print_select("print-orientation","Layout",[{value:"portrait",label:"Portrait"},{value:"landscape",label:"Landscape"}],"orientation",v.orientation)
                print_select("print-paper","Paper size",[for (p in page.papers) {value:p.id,label:p.id}],"paper",v.paper)
                print_select("print-per-sheet","Pages per sheet",print.per_sheet_choices,"per_sheet",v.per_sheet)
                print_select("print-margins","Margins",print.margin_choices,"margins",v.margins)
                print_select("print-scale-mode","Scale",print.scale_choices,"scale_mode",v.scale_mode)
                if (v.scale_mode!="custom") null else print_input("print-scale","Scale (%)","10–200","scale",v.scale);
                <div class:"print-group-label","Options">
                print_option("print-headers","Headers and footers","headers",v.headers)
                print_option("print-backgrounds","Background colors","backgrounds",v.backgrounds)
                dialog_field("print-path","Save to",v.path)>
            <div class:"print-actions",files.dialog_button("cancel","Cancel",false) files.dialog_button("print","Save",true)>>>],[])
}

pub fn dialog(dialog,file,editor=null,read_only=false,font_families=[]) => if (dialog == null) null
    else if (dialog.kind=="print") print_dialog(dialog,editor)
    else if (dialog.kind=="object_layout") form_dialog(dialog,"Object layout","Floating positions are measured from the object's paragraph. Objects move with their text.",[
        dialog_select("object-wrap","Text wrapping",[for (w in object_layout.wraps) {value:w.id,label:w.label}],dialog.values.wrap),
        <div class:"dialog-grid",
            dialog_select("object-side","Square wrapping side",[{value:"left",label:"Left"},{value:"right",label:"Right"}],dialog.values.side)
            dialog_field("object-distance","Distance from text (px)",dialog.values.distance,true,0,100,0.1)
            dialog_field("object-x","Horizontal position (px)",dialog.values.x,true,-10000,10000,0.1)
            dialog_field("object-y","Vertical position (px)",dialog.values.y,true,-10000,10000,0.1)>,
        <p class:"pane-hint","Positions apply to In Front of Text and Behind Text. Square and Top and Bottom follow document flow.">],"object_layout","Apply")
    else if (dialog.kind=="source") source_dialog(dialog)
    else if (contains(["sources","citations"],dialog.kind)) sources_dialog(dialog,editor,read_only)
    else if (dialog.kind=="citation") form_dialog(dialog,"Edit Citation",dialog.values.source.title,[
        dialog_field("citation-locator","Page(s) · optional",dialog.values.locator)],"citation","Apply")
    else if (dialog.kind=="table_properties") form_dialog(dialog,"Table properties","Set the table width and position on the page.",[
        dialog_field("table-preferred-width","Preferred width (%)",dialog.values.table_width,true,1,100),
        dialog_select("table-position","Alignment",[for (v in ["left","center","right"]) {value:v,label:v}],dialog.values.alignment),
        dialog_field("table-indent","Indent from left (pt)",dialog.values.indent,true,0,300)],"table_properties","Apply")
    else if (dialog.kind=="table_split") form_dialog(dialog,"Split cells","Split the selected cell. Other cells expand across the new grid lines.",[
        <div class:"dialog-grid",dialog_field("split-columns","Columns",dialog.values.columns,true,1,32)
            dialog_field("split-rows","Rows",dialog.values.rows,true,1,32)>],"table_split","Split")
    else if (dialog.kind=="table_margins") form_dialog(dialog,"Cell margins","Apply margins to the selected cells.",[
        <div class:"dialog-grid",*[for (side in ["top","right","bottom","left"])
            dialog_field("cell-margin-" ++ side,side ++ " (pt)",dialog.values[side],true,0,72)]>],"table_margins","Apply")
    else if (dialog.kind=="table_sort") form_dialog(dialog,"Sort table","Sort rows within their header, body, and footer groups. Split vertically merged cells before sorting.",[
        dialog_field("table-sort-column","Column",dialog.values.column,true,1,32),
        dialog_select("table-sort-type","Type",[{value:"text",label:"Text"},{value:"number",label:"Number"}],dialog.values.type),
        dialog_select("table-sort-order","Order",[{value:"false",label:"Ascending"},{value:"true",label:"Descending"}],dialog.values.descending),
        dialog_select("table-sort-header","First row",[{value:"true",label:"Keep header row"},{value:"false",label:"Sort all rows"}],dialog.values.header)],"table_sort","Sort")
    else if (dialog.kind=="table_text") form_dialog(dialog,"Convert table to text","Keep cell content and formatting, separated by:",[
        dialog_select("table-text-separator","Separate text with",[for (v in ["tabs","commas","paragraphs"]) {value:v,label:v}],dialog.values.separator)],"table_text","Convert")
    else if (dialog.kind=="table_formula") form_dialog(dialog,"Formula","Calculate SUM, AVERAGE, MIN, MAX, or COUNT using ABOVE, LEFT, BELOW, RIGHT, or A1:B3. Calculate again to update the result.",[
        dialog_field("table-formula-expression","Formula",dialog.values.expression),
        dialog_select("table-formula-decimals","Decimal places",[{value:"general",label:"General"},*[for (i in 0 to 6) {value:string(i),label:string(i)}]],dialog.values.decimals)],"table_formula","Calculate")
    else if (dialog.kind=="table_shading") form_dialog(dialog,"Cell shading","Apply a fill color to the selected cells. Enter none to clear direct shading.",[
        dialog_field("table-shading-color","Color (#RRGGBB or none)",dialog.values.color)],"table_shading","Apply")
    else if (contains(graphics.illustration_kinds,dialog.kind)) illustration_dialog(dialog)
    else if (dialog.kind=="emoji") form_dialog(dialog,"Insert emoji","Choose an emoji, or paste a Unicode emoji sequence. It inserts as editable text.",[
        icon_tabs("emoji"),
        <div class:"emoji-gallery",*[for (category_index,category in iconmodel.emojis) <section class:"symbol-category",
            <div class:"symbol-category-title",category.label>
            <div class:"symbol-grid",*[for (i,char in category.chars) button("emoji-" ++ string(category_index) ++ "-" ++ string(i),
                "ui.chooseEmoji",char,null,{char:char},dialog.values.char==char,false,"symbol-key emoji-key")]>>]>,
        dialog_field("emoji-text","Emoji",dialog.values.char)],"emoji","Insert emoji")
    else if (dialog.kind=="cell_borders") form_dialog(dialog,"Cell borders",
        "Apply borders to " ++ string(len(tables.selected(editor))) ++ " selected cell(s).",[
            dialog_select("border-preset","Apply to",[for (value in tables.border_presets) {value:value,label:value}],dialog.values.preset),
            dialog_select("border-style","Line style",[for (value in tables.border_styles) {value:value,label:value}],dialog.values.style),
            dialog_field("border-width","Width (pt)",dialog.values.width,true,0.25,6),
            dialog_field("border-color","Color",dialog.values.color)],"cell_borders","Apply")
    else if (dialog.kind == "unsaved") files.dialog_frame("unsaved","Keep your changes?","Save this document before continuing.",[],
        [files.dialog_button("cancel","Cancel",false),files.dialog_button("discard","Discard changes",false),files.dialog_button("save","Save",true)])
    else if (dialog.kind == "exists" or dialog.kind == "changed") files.dialog_frame(dialog.kind,"Replace the existing file?",
        if (dialog.kind == "changed") "The file changed on disk. Replacing it will overwrite that version." else dialog.path ++ " already exists.",[],
        [files.dialog_button("cancel","Cancel",false),files.dialog_button("overwrite","Replace",true)])
    else if (dialog.kind == "link") form_dialog(dialog,if (dialog.editing) "Edit link" else "Insert a link",
        "Link the selected text, or enter a new display label.",
        [dialog_field("link-address","Address",dialog.values.href),dialog_field("link-label","Display text",dialog.values.label),
            if (len(insert.bookmarks(editor.doc))==0) null else <div class:"internal-link-fields",
                dialog_select("link-bookmark","Place in this document",[for (b in insert.bookmarks(editor.doc)) {value:b.name,label:b.name}],
                    insert.link_bookmark(dialog.values.href) or "")
                files.dialog_button("internal_link","Link to bookmark",false)>],"link",if (dialog.editing) "Apply" else "Insert link")
    else if (dialog.kind == "cover") form_dialog(dialog,"Insert cover page","Add an editable title page at the beginning of your document.",[
        dialog_field("cover-title","Title",dialog.values.title),dialog_field("cover-subtitle","Subtitle",dialog.values.subtitle),
        dialog_field("cover-author","Author",dialog.values.author)],"cover","Insert cover")
    else if (dialog.kind == "bookmarks") files.dialog_frame("bookmarks","Bookmarks",
        "Name the current paragraph, then jump to it or link to it. One bookmark per paragraph; reusing a name moves it.",[
            if (dialog.error==null) null else <p id:"dialog-error",role:"alert",dialog.error>,
            <div class:"bookmark-list", *[for (b in insert.bookmarks(editor.doc)) <div class:"bookmark-row",
                <div class:"bookmark-description",<strong b.name> <span b.label>>
                button("bookmark-go-" ++ b.name,"ui.bookmarkText","Go to",null,{name:b.name})
                button("bookmark-delete-" ++ b.name,"insert.deleteBookmark","Delete",null,{name:b.name},false,read_only)>]
                if (len(insert.bookmarks(editor.doc))==0) <p class:"bookmark-empty","No bookmarks yet."> else null>,
            if (read_only) null else dialog_field("bookmark-name","Name (letters, digits, underscores)",dialog.values.name)],
        [files.dialog_button("cancel","Close",false),if (read_only) null else files.dialog_button("bookmark","Add / replace",true)])
    else if (dialog.kind == "symbol") form_dialog(dialog,"Insert symbol","Choose a character or enter up to 64 printable characters.",[
        <div class:"symbol-gallery", *[for (category_index,category in insert.symbols) <section class:"symbol-category",
            <div class:"symbol-category-title",category.name>
            <div class:"symbol-grid", *[for (i,c in category.chars)
                button("symbol-" ++ string(category_index) ++ "-" ++ string(i),"ui.chooseSymbol",c,null,{char:c},dialog.values.char==c,false,"symbol-key")]>>]>,
        dialog_field("symbol-text","Character(s)",dialog.values.char)],"symbol","Insert")
    else if (dialog.kind == "shape") form_dialog(dialog,if (dialog.editing) "Shape settings" else "Insert shape",
        "Shapes sit with the text and stay editable through Shape settings.",[
        <div class:"shape-gallery", *[for (entry in shapes.catalog) button("shape-choice-" ++ entry.id,"ui.chooseShape",entry.label,
            shapes.graphic({*:shapes.defaults,kind:entry.id,width:44,height:30}),{kind:entry.id},entry.id==dialog.values.kind,false,"shape-choice")]>,
        dialog_select("shape-kind","Shape",[for (entry in shapes.catalog) {value:entry.id,label:entry.label}],dialog.values.kind),
        <div class:"dialog-grid",
            dialog_field("shape-width","Width (px)",dialog.values.width,true,16,650)
            dialog_field("shape-height","Height (px)",dialog.values.height,true,16,2000)
            dialog_field("shape-fill","Fill (#RRGGBB)",dialog.values.fill)
            dialog_field("shape-stroke","Outline (#RRGGBB)",dialog.values.stroke)
            dialog_field("shape-weight","Outline weight",dialog.values.weight,true,0,12)
            dialog_field("shape-rotation","Rotation (degrees)",dialog.values.rotation,true,-180,180)
            dialog_field("shape-size","Text size (px)",dialog.values.size,true,8,72)
            dialog_field("shape-text-color","Text color",dialog.values.text_color)>,
        dialog_field("shape-text","Shape text",dialog.values.text)],"shape",if (dialog.editing) "Apply" else "Insert shape")
    else if (dialog.kind == "stories") form_dialog(dialog,"Header and footer",
        "Repeat text on each page. Use {PAGE} for its number and {NUMPAGES} for the total. Double-click a page header or footer to edit.",[
        <label class:"dialog-field",<span "Header"><textarea id:"stories-header",class:"edit-dialog-field",dialog.values.header>>,
        <label class:"dialog-field",<span "Footer"><textarea id:"stories-footer",class:"edit-dialog-field",dialog.values.footer>>,
        <div class:"dialog-grid",
            dialog_select("stories-header-align","Header alignment",[for (v in ["left","center","right"]) {value:v,label:v}],dialog.values.header_align)
            dialog_select("stories-footer-align","Footer alignment",[for (v in ["left","center","right"]) {value:v,label:v}],dialog.values.footer_align)
            dialog_field("stories-size","Font size (pt)",dialog.values.size,true,8,24)
            dialog_field("stories-distance","Screen edge distance (mm)",dialog.values.distance,true,3,30)>,
        dialog_select("stories-first","First page",[{value:"false",label:"Same header and footer"},{value:"true",label:"Leave first page blank"}],dialog.values.different_first)],"stories","Apply")
    else if (dialog.kind == "equation") form_dialog(dialog,if (dialog.editing) "Edit equation" else "Insert equation",
        "Enter LaTeX or choose an example. The equation stays editable after saving.",[
        <div class:"equation-examples",*[for (i,entry in equation.examples) button("equation-example-" ++ string(i),"ui.equationPreset",entry.label,null,{source:entry.source})]>,
        <label class:"dialog-field",<span "LaTeX"><textarea id:"equation-source",class:"edit-dialog-field equation-source",dialog.values.source>>,
        <div class:"dialog-grid",dialog_field("equation-size","Size (px)",dialog.values.size,true,10,72)
            dialog_field("equation-color","Color",dialog.values.color)>,
        dialog_select("equation-display","Typesetting",[{value:"true",label:"Display math"},{value:"false",label:"Inline math"}],dialog.values.display)],"equation",if (dialog.editing) "Apply" else "Insert equation")
    else if (dialog.kind == "wordart") form_dialog(dialog,if (dialog.editing) "Edit Word Art" else "Insert Word Art",
        "Choose text effects and a transform. Double-click the inserted Word Art to edit its text or appearance.",[
        <div class:"art-gallery",*[for (s in wordart.styles) button("art-choice-" ++ s,"ui.chooseArt",s,
            wordart.graphic({*:wordart.defaults,text:"Abc",width:88,height:36,size:24,style:s},"dialog-art-" ++ s ++ "-"),{style:s},s==dialog.values.style,false,"art-choice")]>,
        dialog_field("art-text","Text",dialog.values.text),
        <div class:"dialog-grid",
            dialog_select("art-style","Style",[for (s in wordart.styles) {value:s,label:s}],dialog.values.style)
            dialog_select("art-transform","Transform",[for (s in wordart.transforms) {value:s,label:s}],dialog.values.transform)
            dialog_select("art-font","Font",font_options(font_families,dialog.values.font),dialog.values.font)
            dialog_field("art-size","Font size (px)",dialog.values.size,true,12,120)
            dialog_field("art-width","Width (px)",dialog.values.width,true,80,650)
            dialog_field("art-height","Height (px)",dialog.values.height,true,40,400)
            dialog_field("art-rotation","Rotation (degrees)",dialog.values.rotation,true,-180,180)
            dialog_field("art-fill","Text color",dialog.values.fill)
            dialog_field("art-outline","Outline / gradient color",dialog.values.outline)>],"wordart",if (dialog.editing) "Apply" else "Insert Word Art")
    else if (dialog.kind=="tracking") form_dialog(dialog,"Track text changes",
        "Tracks text-only paragraphs and table cells. Insertions are underlined; deletions stay struck out until accepted. Character formatting is not tracked. Turn tracking off for structural edits, links, and embedded objects.",[
        dialog_field("tracking-author","Author",dialog.values.author),
        dialog_select("tracking-enabled","Tracking",[{value:"true",label:"On"},{value:"false",label:"Off"}],dialog.values.enabled)],"tracking","Apply")
    else if (dialog.kind == "comment") form_dialog(dialog,"New comment","The comment stays anchored as its text is edited.",[
        dialog_field("comment-author","Author",dialog.values.author),
        <label class:"dialog-field", <span "Comment"> <textarea id:"comment-text",class:"edit-dialog-field comment-input",dialog.values.text>>],"comment","Add comment")
    else if (dialog.kind == "table") form_dialog(dialog,"Insert table","Choose the table size. Tab moves between cells and adds a row at the end.",[
        <div class:"dialog-grid", dialog_field("table-rows","Rows",dialog.values.rows,true,1,32)
            dialog_field("table-columns","Columns",dialog.values.cols,true,1,12)>,
        dialog_select("table-header","First row",[{value:"true",label:"Header row"},{value:"false",label:"Regular cells"}],dialog.values.header)],"insert_table","Insert table")
    else if (dialog.kind == "paragraph") form_dialog(dialog,"Paragraph settings","Apply spacing and indentation to the selected paragraphs.",[
        <div class:"dialog-grid",
            dialog_select("paragraph-align","Alignment",[for (v in ["left","center","right","justify"]) {value:v,label:v}],dialog.values.align)
            dialog_select("paragraph-lines","Line spacing",[for (v in home.line_spacings) {value:v,label:string(v) ++ " lines"}],dialog.values.lines)
            dialog_field("paragraph-before","Space before (pt)",dialog.values.before,true,0,200)
            dialog_field("paragraph-after","Space after (pt)",dialog.values.after,true,0,200)
            dialog_field("paragraph-indent","Left indent (pt)",dialog.values.indent,true,0,200)
            dialog_field("paragraph-right","Right indent (pt)",dialog.values.right,true,0,200)
            dialog_field("paragraph-first","First line indent (pt)",dialog.values.first,true,-72,200)>],"paragraph","Apply")
    else if (dialog.kind == "columns") form_dialog(dialog,"Columns","Flow the whole document into equal-width columns. Headers and footers keep the full page width.",[
        <div class:"dialog-grid",
            dialog_field("columns-count","Number of columns",dialog.values.count,true,1,6)
            dialog_field("columns-gap","Spacing (mm)",dialog.values.gap,true,0,80)>,
        dialog_select("columns-line","Line between",[{value:"false",label:"None"},{value:"true",label:"Thin line"}],string(dialog.values.line))],"columns","Apply")
    else if (dialog.kind == "page_setup") form_dialog(dialog,if (dialog.margins==true) "Custom Margins" else "Page setup",
        "Apply to the whole document. Mirrored margins use inside/outside edges for facing pages, including exported PDFs.",[
        <label class:"dialog-field",<span "Multiple pages">
            apply(<control.select id:"page-mirror",class:"edit-dialog-field",label:"Multiple pages",
                options:[{value:"normal",label:"Normal"},{value:"mirrored",label:"Mirror margins"}],
                value:if (dialog.values.mirrored==true) "mirrored" else "normal",on_change:{action:"ui.marginMode",param:"value"}>)>,
        <div class:"dialog-grid",
            dialog_select("page-paper","Paper size",[for (p in page.papers) {value:p.id,label:p.id}],dialog.values.paper)
            dialog_select("page-orientation","Orientation",[for (v in ["portrait","landscape"]) {value:v,label:v}],dialog.values.orientation)
            dialog_field("page-top","Top margin (mm)",dialog.values.top,true,0,80,0.05)
            dialog_field("page-right",if (dialog.values.mirrored==true) "Outside margin (mm)" else "Right margin (mm)",dialog.values.right,true,0,80,0.05)
            dialog_field("page-bottom","Bottom margin (mm)",dialog.values.bottom,true,0,80,0.05)
            dialog_field("page-left",if (dialog.values.mirrored==true) "Inside margin (mm)" else "Left margin (mm)",dialog.values.left,true,0,80,0.05)>],"page_setup","Apply")
    else if (dialog.kind == "picture" or dialog.kind == "picture_settings") form_dialog(dialog,
        if (dialog.kind == "picture") "Insert picture" else "Picture settings",
        "Pictures are embedded in the document. Leave height blank to keep the original proportions.",[
            if (dialog.kind == "picture") dialog_field("picture-path","Picture file (PNG, JPEG, GIF, WebP, SVG)",dialog.values.path) else null,
            dialog_field("picture-alt","Alternative text",dialog.values.alt),
            <div class:"dialog-grid",dialog_field("picture-width","Width (px)",dialog.values.width,true,16,650)
                dialog_field("picture-height","Height (px, optional)",dialog.values.height,true,16,2000)>,
            dialog_field("picture-rotation","Rotation (degrees)",dialog.values.rotation,true,-180,180)],"picture",if (dialog.kind=="picture") "Insert picture" else "Apply")
    else files.dialog_frame(dialog.kind,
        if (dialog.kind == "open") "Open document" else if (dialog.kind == "export") "Export document" else "Save editable document",
        if (dialog.kind == "open") "Open .word.json, HTML, Markdown, text, or a supported DOCX. Imports create an editable copy."
        else if (dialog.kind == "export") "Choose .html, .txt, or .pdf. PDF uses this document's page setup."
        else "The .word.json format preserves the document and its formatting.",
        [files.field("word-file-path",dialog.path,if (dialog.kind == "open") "path/to/document" else "temp/word/document.word.json")],
        [files.dialog_button("cancel","Cancel",false),files.dialog_button("confirm",if (dialog.kind == "open") "Open" else if (dialog.kind == "export") "Export" else "Save",true)])
