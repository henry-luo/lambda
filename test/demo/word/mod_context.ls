// Focused-item actions reuse the ribbon's commands and source selection (D7.2.5).
import dom
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos
import adapter: lambda.editor.mod_dom_adapter
import model: .mod_model
import insert: .mod_insert
import review: .mod_review
import changes: .mod_changes
import shapes: .mod_shapes
import equation: .mod_equation
import wordart: .mod_wordart
import transform: .mod_transform

pub fn story(node) => if (dom.closest(node,".word-header")!=null) "header"
    else if (dom.closest(node,".word-footer")!=null) "footer" else null

pub fn at_pointer(editor,position,preserve_range=true) {
    let point = adapter.normalize_source_pos(editor.doc,position)
    let sel = editor.selection
    // retain a range only when the secondary click is inside it.
    let inside = point!=null and (sel.kind=='all' or
        (sel.kind=='node' and path_is_prefix(sel.path,point.path)) or
        (sel.kind=='text' and pos_compare(point,pos_min(sel.anchor,sel.head))>=0 and
            pos_compare(point,pos_max(sel.anchor,sel.head))<=0))
    let n = if (point==null) null else node_at(editor.doc,point.path)
    let atom = n!=null and editor.schema[n.tag].atomic==true and editor.schema[n.tag].selectable==true
    if (point==null or (preserve_range and inside)) editor else edit_set_selection(editor,
        if (atom) node_selection(point.path) else text_selection(point,point))
}

fn action(id,command,label,icon,params={},disabled=false) =>
    {id:id,command:command,label:label,icon:icon,params:params,disabled:disabled}

pub fn describe(editor,region=null) {
    let image = model.current_node(editor,'img')
    let graphic = [for (entry in [
        {kind:"shape",label:"Shape",command:"ui.shapeSettings",text:"Format Shape…",icon:"shapes",matches:shapes.settings(image.node)!=null},
        {kind:"equation",label:"Equation",command:"ui.equationSettings",text:"Edit Equation…",icon:"equation",matches:equation.settings(image.node)!=null},
        {kind:"wordart",label:"WordArt",command:"ui.wordArtSettings",text:"Edit WordArt…",icon:"wordart",matches:wordart.settings(image.node)!=null},
        {kind:"picture",label:"Picture",command:"ui.pictureSettings",text:"Picture Settings…",icon:"album",matches:image!=null}]
        where entry.matches) entry][0]
    let table = model.table_context(editor)
    let item = model.current_node(editor,'li')
    let link = model.current_node(editor,'a')
    let block = model.current_block(editor)
    let bookmark = insert.bookmark_name(block.node)
    let marks = model.current_marks(editor)
    let revision = changes.change_id(marks)
    let comments = [for (c in review.comments(editor.doc) where contains(review.ids(marks),c.id)) c]
    let dimensions = transform.settings(image.node)
    let kind = if (region!=null) region else if (graphic!=null) graphic.kind else if (table!=null) "table"
        else if (item!=null) "list" else if (link!=null) "link" else if (model.current_node(editor,'hr')!=null) "rule" else "text"
    let label = if (region=="header") "Header" else if (region=="footer") "Footer" else if (graphic!=null) graphic.label
        else if (kind=="table") "Table cell" else if (kind=="list") "List item" else if (kind=="link") "Link"
        else if (kind=="rule") "Horizontal line" else if (revision!=null) "Tracked change"
        else if (len(comments)>0) "Comment" else if (bookmark!=null) "Bookmark" else "Text"
    let actions = if (region!=null) [
        action("story-edit","ui.headerFooter","Edit " ++ label ++ "…",region),
        action("page-number","ui.pageNumber","Page Numbers…","page_number"),
        action("page-setup","ui.pageSetup","Page Setup…","margins")]
    else [
        *if (graphic!=null) [action("object",graphic.command,graphic.text,graphic.icon),
            *if (dimensions!=null and dimensions.width>0 and dimensions.height>0)
                [for (choice in [{id:"left",label:"Rotate Left 90°",delta: -90},{id:"right",label:"Rotate Right 90°",delta:90}])
                    action("rotate-" ++ choice.id,"object.transform",choice.label,"rotate_" ++ choice.id,
                        {*:dimensions,rotation:transform.normalized(dimensions.rotation+choice.delta)})] else [],
            action("remove-object","clipboard.delete","Delete " ++ graphic.label,"trash")] else [],
        *if (kind=="rule") [action("remove-rule","clipboard.delete","Delete Horizontal Line","trash")] else [],
        *if (table!=null) [
            *[for (scope in ["Cell","Row","Column","Table"])
                action("table-select-" ++ lower(scope),"table.select" ++ scope,"Select " ++ scope,"select_" ++ lower(scope))],
            action("table-borders","ui.cellBorders","Borders…","border_all"),
            *[for (entry in [
                {id:"row-above",command:"table.insertRowAbove",label:"Insert Row Above",icon:"up"},
                {id:"row-below",command:"table.insertRowBelow",label:"Insert Row Below",icon:"down"},
                {id:"column-left",command:"table.insertColumnLeft",label:"Insert Column Left",icon:"prev"},
                {id:"column-right",command:"table.insertColumnRight",label:"Insert Column Right",icon:"next"},
                {id:"delete-row",command:"table.deleteRow",label:"Delete Row",icon:"trash"},
                {id:"delete-column",command:"table.deleteColumn",label:"Delete Column",icon:"trash"},
                {id:"delete",command:"table.deleteTable",label:"Delete Table",icon:"trash"}])
                action("table-" ++ entry.id,entry.command,entry.label,entry.icon)],
            action("table-options","ui.tab","Table Style & Cell Alignment…","table",{tab:"Table"})] else [],
        *if (item!=null and graphic==null) [action("indent","para.indent","Increase Indent","indent"),
            action("outdent","para.outdent","Decrease Indent","outdent")] else [],
        *if (link!=null) [action("edit-link","ui.link","Edit Link…","link"),
            action("remove-link","insert.removeLink","Remove Link","unlink"),
            *if (insert.link_bookmark(attrs_get(link.node.attrs,'href'))!=null)
                [action("follow-link","ui.followLink","Go to Bookmark","bookmark")] else []] else [],
        *if (bookmark!=null) [action("bookmark","ui.bookmarks","Edit Bookmark…","bookmark"),
            action("remove-bookmark","insert.deleteBookmark","Remove Bookmark","trash",{name:bookmark})] else [],
        *[for (c in comments) for (entry in [
            action("comment-" ++ c.id,"ui.commentText","View Comment","comment",{id:c.id}),
            action("resolve-comment-" ++ c.id,"review.resolveComment",if (c.resolved) "Reopen Comment" else "Resolve Comment","check",{id:c.id,resolved:not c.resolved}),
            action("delete-comment-" ++ c.id,"review.deleteComment","Delete Comment","trash",{id:c.id})]) entry],
        *if (block!=null and attrs_get(block.node.attrs,'data-word-page-break')!=null)
            [action("page-setup","ui.pageSetup","Page Setup…","margins")] else [],
        *if (revision!=null) [action("accept-change","review.accept","Accept Change","check",{id:revision}),
            action("reject-change","review.reject","Reject Change","close",{id:revision})] else []]
    {kind:kind,label:label,actions:actions,text:graphic==null and region==null and kind!="rule"}
}
