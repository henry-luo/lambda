// D7.2.5: Word's ribbon contours, typography, presets and group layout are shared.
import ribbon: ~~.mod_ribbon
import icons: ~~.word.mod_ribbon_icons
import text_format: ~~.mod_text_format
import presets: ~~.mod_shape_presets

fn tool(id,label,graphic,cls="",action=null) => ribbon.tool(id,label,graphic,cls,action)
fn menu(id,label,graphic,cls="") => ribbon.menu(id,label,graphic,cls,id in ["font-color","text-highlight","shape-fill","shape-outline"])
fn row(children) => ribbon.row(children)
fn stack(children) => ribbon.stack(children)
fn selector(id,label,items,property) => <select id:id,class:"home-select",title:label,'aria-label':label,'data-property':"home-" ++ property,
    for (item in items) <option value:item.value,item.label>>
pub fn tree(fonts=[]) => <div class:"home-ribbon editor-ribbon",
    ribbon.group("Undo",ribbon.history_tools((action,label,graphic)=>tool("home-" ++ action,label,graphic,"icon-only",action)));
    ribbon.group("Clipboard",[row([
        <div class:"paste-tool",tool("paste","Paste","paste","home-large");menu("paste-options","Paste options","down","icon-only")>,
        stack([tool("cut","Cut","cut"),tool("copy","Copy","copy"),tool("format-painter","Format Painter","format_painter")])])]);
    ribbon.group("Slides",[row([
        tool("add-slide","New Slide","blank_page","home-large"),
        stack([menu("home-layout","Layout","columns"),tool("reset-layout","Reset","repeat_header"),menu("slide-actions","Slide actions","document")])])]);
    ribbon.group("Font",[
        row([selector("home-font-family","Font family",text_format.font_options(fonts,"Arial, sans-serif"),"font-family"),
            <input id:"home-font-size",class:"home-font-size",type:"number",min:"1",step:"1",list:"home-font-sizes",title:"Font size",'aria-label':"Font size",'data-property':"home-font_size">,
            <datalist id:"home-font-sizes",for (size in text_format.font_sizes) <option value:string(size)>>,
            tool("grow-font","Grow Font","grow_font","icon-only"),tool("shrink-font","Shrink Font","shrink_font","icon-only"),
            menu("change-case","Change Case","change_case","icon-only"),tool("clear-format","Clear Formatting","clear","icon-only")]),
        row([*[for (choice in [{id:"bold",label:"Bold",icon:"bold"},{id:"italic",label:"Italic",icon:"italic"},{id:"underline",label:"Underline",icon:"underline"},
            {id:"strike",label:"Strikethrough",icon:"strike"},{id:"subscript",label:"Subscript",icon:"subscript"},{id:"superscript",label:"Superscript",icon:"superscript"}]) tool(choice.id,choice.label,choice.icon,"icon-only")],
            menu("character-spacing","Character Spacing","tracking","icon-only"),menu("text-highlight","Text Highlight Color","highlighter","icon-only"),
            menu("font-color","Font Color","font_color","icon-only")])]);
    ribbon.group("Paragraph",[
        row([tool("bullets","Bullets","bullets","icon-only"),tool("numbering","Numbering","numbering","icon-only"),
            menu("list-options","List Styles","down","icon-only"),
            tool("outdent","Decrease Indent","outdent","icon-only"),tool("indent","Increase Indent","indent","icon-only"),
            menu("line-spacing","Line Spacing","line_spacing","icon-only")]),
        row([*[for (entry in [{id:"align-left",icon:"align_left",label:"Align Left"},{id:"align-center",icon:"align_center",label:"Center"},
            {id:"align-right",icon:"align_right",label:"Align Right"},{id:"align-justify",icon:"justify",label:"Justify"}]) tool(entry.id,entry.label,entry.icon,"icon-only")],
            menu("vertical-align","Align Text Vertically","align_text","icon-only")])]);
    ribbon.group("Drawing",[row([
        tool("home-picture","Picture","album","home-large","insert-image"),menu("home-shapes","Shapes","shapes","home-large"),
        tool("home-text-box","Text Box","text_box","home-large","insert-text"),
        menu("arrange","Arrange","object_position","home-large"),menu("quick-styles","Quick Styles","format_shape","home-large"),
        stack([menu("shape-fill","Shape Fill","shape_fill"),menu("shape-outline","Shape Outline","shape_outline")])])])
>
fn item(action,label,graphic=null,disabled=false,prefix="") => ribbon.item(action,label,graphic,disabled,prefix)
fn slide_items(disabled=false,prefix="") => [item("duplicate-slide","Duplicate Slide","copy",false,prefix),item("delete-slide","Delete Slide","trash",disabled,prefix)]
pub fn slide_context(st) {
    let prefix = "slide-context-"
    let end = len(st.plan.slides)-1;
    [item("add-slide","New Slide","blank_page",false,prefix),*slide_items(end==0,prefix),
        <div class:"home-menu-separator",role:"separator">,
        item("slide-up","Move Up","up",st.index==0,prefix),
        item("slide-down","Move Down","down",st.index==end,prefix),
        <div class:"home-menu-separator",role:"separator">,
        item("present-current","Present from Here","play",false,prefix)]
}
fn swatch(color) => <span class:"home-swatch",style:"background:" ++ color ++ ";">
fn colors(prefix,choices,none_label="Automatic") => [item(prefix ++ ":none",none_label),
    <div class:"home-color-grid",for (color in choices) <button type:"button",role:"menuitem",title:color.label,'aria-label':color.label,
        'data-action':prefix ++ ":" ++ color.value,swatch(color.value)>>]
pub fn menu_tree(key) {
    if (key == "paste-options") [item("paste","Keep Source Formatting","paste"),item("paste-text","Keep Text Only","text_box")]
    else if (key == "slide-actions") slide_items()
    else if (key == "home-layout") [for (layout in ["blank","title","title-body","two-column","three-column","section","quote","image-left","image-right"])
        item("layout:" ++ layout,replace(layout,"-"," "),"columns")]
    else if (key == "change-case") [for (choice in text_format.cases) item("case:" ++ choice.id,choice.label)]
    else if (key == "character-spacing") [for (entry in [{label:"Tight",value:-1},{label:"Normal",value:0},{label:"Loose",value:2},{label:"Very Loose",value:4}])
        item("tracking:" ++ string(entry.value),entry.label)]
    else if (key == "font-color") colors("text-color",text_format.font_colors)
    else if (key == "text-highlight") colors("highlight",text_format.highlight_colors,"No Color")
    else if (key == "shape-fill") colors("fill",presets.colors,"No Fill")
    else if (key == "shape-outline") [*colors("outline",presets.colors,"No Outline"),
        <div class:"home-menu-caption","Weight">,*[for (weight in [0.5,1,2,3,5,8]) item("weight:" ++ string(weight),string(weight) ++ " px")]]
    else if (key == "line-spacing") [for (spacing in text_format.line_spacings) item("spacing:" ++ string(spacing),string(spacing) ++ " ×")]
    else if (key == "vertical-align") [for (entry in [{value:"top",label:"Top"},{value:"middle",label:"Middle"},{value:"bottom",label:"Bottom"}])
        item("vertical:" ++ entry.value,entry.label,"align_text")]
    else if (key == "list-options") [<div class:"home-menu-caption","Bullets">,
        *[for (style in text_format.bullet_styles) item("bullets:" ++ style.id,style.glyph ++ "  " ++ style.label)],
        <div class:"home-menu-caption","Numbering">,*[for (style in text_format.number_styles) item("numbering:" ++ style.id,style.label)]]
    else if (key == "home-shapes") [for (entry in [{kind:"rect",label:"Rectangle"},{kind:"ellipse",label:"Ellipse"},{kind:"circle",label:"Circle"},{kind:"line",label:"Line"}])
        item("insert-" ++ entry.kind,entry.label,"shapes")]
    else if (key == "quick-styles") [for (style in presets.styles) item("shape-style:" ++ style.id,style.label,
        <span class:"home-style-preview",style:"background:" ++ style.values.fill ++ ";border:2px solid " ++ style.values.stroke ++ ";">)]
    else if (key == "arrange") [item("front","Bring to Front","bring_forward"),item("back","Send to Back","send_backward"),
        *[for (entry in [{id:"left",label:"Align Left"},{id:"center",label:"Align Center"},{id:"right",label:"Align Right"},
            {id:"top",label:"Align Top"},{id:"middle",label:"Align Middle"},{id:"bottom",label:"Align Bottom"}]) item("object-align:" ++ entry.id,entry.label,"object_align")],
        item("rotate:-90","Rotate Left 90°","object_flip"),item("rotate:90","Rotate Right 90°","object_flip"),
        item("selection-pane","Selection Pane","selection_pane"),item("duplicate","Duplicate","copy"),item("delete","Delete Object","trash")]
    else []
}
