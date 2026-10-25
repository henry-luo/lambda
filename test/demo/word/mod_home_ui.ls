// Word's Home tab: Undo, Clipboard, Font, Paragraph, Styles, and Editing groups.
// Split buttons repeat the last choice; their chevrons open the shared ribbon menu.
import lambda.editor.mod_editor
import lambda.editor.mod_step
import model: .mod_model
import home: .mod_home
import document: .mod_document
import ribbon_icons: .mod_ribbon_icons
import ribbon: ~~.mod_ribbon

pub let emphasis = [{id:"bold",mark:'strong'},{id:"italic",mark:'em'},{id:"underline",mark:'u'},
    {id:"strikethrough",icon:"strike",mark:'del'},{id:"subscript",mark:'sub'},{id:"superscript",mark:'sup'}]
pub let alignments = [{id:"para.alignLeft",icon:"align_left",value:"left"},{id:"para.alignCenter",icon:"align_center",value:"center"},
    {id:"para.alignRight",icon:"align_right",value:"right"},{id:"para.justify",icon:"justify",value:"justify"}]

// the bar under a color command shows the color its main button applies next.
fn color_icon(graphic,color) => <span class:"color-split-icon",ribbon_icons.icon(graphic,16);
    <span class:"color-bar",style:"background-color:" ++ (if (color=="none") "transparent" else color)>>
fn split(ui,id,command,label,graphic,params,menu,opened,pressed=false,disabled=false) => <div class:"split-tool",
    ui.button(id,command,label,graphic,params,pressed,disabled,"icon-only")
    ui.menu(id ++ "-menu",label ++ " options",null,menu,opened,"icon-only split-chevron",disabled)>
fn tool(ui,id,command,label,graphic,params={},pressed=false,disabled=false) =>
    ui.button(id,command,label,graphic,params,pressed,disabled,"icon-only")
pub fn font_size(editor) => model.points(model.char_value(editor,"font-size","11pt"),11)
fn block_css(editor,key) => model.css_get(attrs_get(model.current_block(editor).node.attrs,'style'),key)

pub fn ribbon(editor,read_only,opened,font_families,chrome,ui) {
    let block = model.current_block(editor)
    let style = attrs_get(block.node.attrs,'data-word-style') or "Normal"
    let align = block_css(editor,"text-align") or "left"
    let family = model.char_value(editor,"font-family","Georgia")
    let shaded = model.char_value(editor,"background-color","")==home.shading_gray
    let boxed = model.char_value(editor,"border","")!="";
    <div class:"ribbon-groups home-ribbon",
        // icon-only history row with Save and Print below it, like Google Docs' toolbar start.
        ui.group("Undo",ribbon.history_tools((action,label,graphic)=>{
            let command = (if (action in ["undo","redo"]) "edit." else "file.") ++ action;
            tool(ui,if (action=="save") "home-save" else command,command,label,graphic,{},false,
                if (action=="undo") not edit_can_undo(editor) else if (action=="redo") not edit_can_redo(editor) else false)}))
        ui.group("Clipboard",[<div class:"ribbon-command-row",
            <div class:"paste-split",
                ui.button("home-paste","clipboard.paste","Paste","paste",{},false,read_only,"ribbon-large")
                ui.menu("home-paste-menu","Paste options",null,"home-paste",opened,"icon-only split-chevron",read_only)>;
            <div class:"ribbon-command-stack clipboard-tools",
                tool(ui,"home-cut","clipboard.cut","Cut","cut",{},false,read_only)
                tool(ui,"home-copy","clipboard.copy","Copy","copy")
                tool(ui,"home-format-painter","ui.formatPainter","Format Painter","format_painter",{},chrome.painter!=null,read_only)>>])
        ui.group("Font",[
            <div class:"font-selects",
                ui.selector("font-family","format.font",ui.font_options(font_families,family),family,"font-family","name")
                ui.selector("font-size","format.size",[for (s in home.font_sizes) {value:s,label:string(s)}],
                    replace(model.char_value(editor,"font-size","11pt"),"pt",""),"font-size","size")
                tool(ui,"home-grow-font","format.size","Grow Font","grow_font",{size:home.font_step(font_size(editor),true)},false,read_only)
                tool(ui,"home-shrink-font","format.size","Shrink Font","shrink_font",{size:home.font_step(font_size(editor),false)},false,read_only)
                ui.menu("home-change-case","Change Case","change_case","home-case",opened,"icon-only",read_only)
                tool(ui,"format.clear","format.clear","Clear All Formatting","clear",{},false,read_only)
                tool(ui,"home-char-border","format.border","Character Border","char_border",{value:if (boxed) "none" else home.border_line},boxed,read_only)>,
            <div class:"font-tools",
                *[for (entry in emphasis) ui.mark_button(editor,"format." ++ entry.id,entry.icon or entry.id)];
                <span class:"ribbon-divider">;
                split(ui,"home-highlight","format.highlight","Text Highlight Color",color_icon("highlighter",chrome.highlight),
                    {color:chrome.highlight},"home-highlight",opened,false,read_only)
                split(ui,"home-font-color","format.color","Font Color",color_icon("font_color",chrome.color),
                    {color:chrome.color},"home-color",opened,false,read_only)
                tool(ui,"home-char-shading","format.shading","Character Shading","char_shading",
                    {color:if (shaded) "none" else home.shading_gray},shaded,read_only)>])
        ui.group("Paragraph",[
            <div class:"paragraph-tools",
                split(ui,"para.bullets","para.bullets","Bullets","bullets",{},"home-bullets",opened,model.current_node(editor,'ul')!=null,read_only)
                split(ui,"para.numbering","para.numbering","Numbering","numbering",{},"home-numbering",opened,model.current_node(editor,'ol')!=null,read_only)
                tool(ui,"para.outdent","para.outdent","Decrease Indent","outdent",{},false,read_only)
                tool(ui,"para.indent","para.indent","Increase Indent","indent",{},false,read_only)
                ui.menu("home-sort","Sort","sort","home-sort",opened,"icon-only",read_only)
                tool(ui,"home-show-marks","ui.showMarks","Show/Hide ¶","pilcrow",{},chrome.marks)>,
            <div class:"alignment-tools",
                *[for (choice in alignments) ui.button(choice.id,choice.id,model.spec(choice.id).label,choice.icon,{},align==choice.value,read_only,"icon-only")];
                ui.menu("line-spacing","Line and Paragraph Spacing","line_spacing","home-spacing",opened,"icon-only",read_only)
                split(ui,"home-shading","para.shading","Shading",color_icon("shading",chrome.shading),{color:chrome.shading},"home-shading",opened,false,read_only)
                ui.menu("home-borders","Borders","border_bottom","home-borders",opened,"icon-only",read_only)
                ui.button("paragraph-settings","ui.paragraph","Paragraph…","expand",{},false,read_only,"paragraph-dialog")>])
        ui.group("Styles",[<div class:"style-gallery",
            *[for (s in take(document.styles,4)) ui.button("style-" ++ s.id,"para.style",s.label,null,{style:s.id},s.id==style,false,"style-card style-" ++ s.id)]>,
            ui.selector("paragraph-style","para.style",[for (s in document.styles) {value:s.id,label:s.label}],style,"style-selector","style")])
        ui.group("Editing",[
            <div class:"find-split",
                ui.button("ribbon-find","ui.find","Find","search");
                ui.menu("ribbon-find-menu","Find options",null,"find",opened,"icon-only")>,
            ui.button("ribbon-replace","ui.replace","Replace","replace",{},false,read_only),
            ui.menu("ribbon-select","Select","pointer","select",opened)])
    >
}

fn swatch(color) => <span class:"menu-swatch",style:"background-color:" ++ color>
fn color_menu(ui,id,command,colors,none_label,disabled) => <div
    ui.menu_item(id ++ "-none",command,none_label,null,disabled,null,{color:"none"});
    <div class:"context-rule",role:"separator">;
    <div class:"color-grid",role:"group",["aria-label"]:"Colors",*[for (c in colors)
        ui.button("context-" ++ id ++ "-" ++ slice(c.value,1),command,c.label,swatch(c.value),{color:c.value},false,disabled,"color-choice")]>>
pub fn menu(st,ui) {
    let menu = st.context.menu
    let editor = st.editor
    let ro = st.read_only;
    if (menu=="home-paste") <div
        ui.menu_item("paste-keep","clipboard.paste","Keep Source Formatting","paste",ro,"⌘V")
        ui.menu_item("paste-text","clipboard.pasteText","Keep Text Only","paste",ro)>
    else if (menu=="home-case") <div *[for (c in home.cases) ui.menu_item("case-" ++ c.id,"format.case",c.label,null,ro,null,{mode:c.id})]>
    else if (menu=="home-highlight") color_menu(ui,"highlight","format.highlight",home.highlight_colors,"No Color",ro)
    else if (menu=="home-color") color_menu(ui,"font-color","format.color",home.font_colors,"Automatic",ro)
    else if (menu=="home-shading") color_menu(ui,"shading","para.shading",home.shading_colors,"No Color",ro)
    else if (menu=="home-bullets" or menu=="home-numbering") {
        let kind = if (menu=="home-numbering") "ordered" else "bullet"
        let list_node = model.current_node(editor,if (kind=="ordered") 'ol' else 'ul').node
        let current = if (list_node==null) null else home.list_style(list_node) or (if (kind=="ordered") "decimal" else "disc");
        <div <div class:"context-caption",if (kind=="ordered") "Numbering Library" else "Bullet Library">;
            <div class:"list-style-grid",*[for (s in home.list_styles(kind))
                ui.button("context-list-" ++ s.id,"para.listStyle",s.label,<span class:"list-glyph",s.glyph>,{kind:kind,style:s.id},current==s.id,ro,"list-style-choice")]>>
    }
    else if (menu=="home-sort") <div
        ui.menu_item("sort-ascending","para.sort","Sort A to Z","sort",ro,null,{descending:false})
        ui.menu_item("sort-descending","para.sort","Sort Z to A","sort",ro,null,{descending:true})
        if (model.table_context(editor)==null) null else <div <div class:"context-rule",role:"separator">;
            ui.menu_item("sort-table","ui.tableLayoutDialog","Sort Table…","sort",ro,null,{kind:"table_sort"})>>
    else if (menu=="home-spacing") {
        let lines = float(block_css(editor,"line-height") or "1.5")
        let before = model.points(block_css(editor,"margin-top"))
        let after = model.points(block_css(editor,"margin-bottom"),11);
        <div *[for (v in home.line_spacings) ui.menu_item("spacing-" ++ replace(string(v),".","-"),"para.lineSpacing",
                if (v==floor(v)) string(v) ++ ".0" else string(v),if (lines==v) "check" else null,ro,null,{value:v})];
            <div class:"context-rule",role:"separator">;
            ui.menu_item("spacing-options","ui.paragraph","Line Spacing Options…","settings",ro);
            ui.menu_item("space-before","para.spaceBefore",if (before>0) "Remove Space Before Paragraph" else "Add Space Before Paragraph",
                null,ro,null,{value:if (before>0) 0 else 12});
            ui.menu_item("space-after","para.spaceAfter",if (after>0) "Remove Space After Paragraph" else "Add Space After Paragraph",
                null,ro,null,{value:if (after>0) 0 else 11})>
    }
    else if (menu=="home-borders") <div *[for (p in home.border_presets)
            ui.menu_item("para-border-" ++ p.id,"para.borders",p.label,
                "border_" ++ p.id,
                ro,null,{preset:p.id})];
        <div class:"context-rule",role:"separator">;
        ui.menu_item("horizontal-line","insert.horizontalRule","Horizontal Line","rule",ro)>
    else null
}
