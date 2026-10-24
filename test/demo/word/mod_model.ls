// WordCraft Session / CommandSpec port over Lambda's source-model editor.
// S1.4, S12.1.3, D7.2.5: immutable model state owns edits; Radiant owns geometry.
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_source_pos
import lambda.editor.mod_transaction
import lambda.editor.mod_editor
import commands: lambda.editor.mod_commands
import decorations: lambda.editor.mod_decorations
import html: lambda.edit.html
import rendering: lambda.edit.model
import inline_format: lambda.editor.mod_inline_format
import image: lambda.editor.mod_image
import document: .mod_document
import page: .mod_page
import columns: .mod_columns
import review: .mod_review
import insert: .mod_insert
import stories: .mod_stories
import graphics: .mod_graphics
import changes: .mod_changes
import objects: .mod_objects
import transform: .mod_transform
import object_layout: .mod_object_layout
import groups: .mod_group
import group_data: .mod_group_data
import shape_format: .mod_shape_format
import equation: .mod_equation
import tables: .mod_table
import table_design: .mod_table_design
import table_layout: .mod_table_layout
import references: .mod_references

// The same IDs drive the ribbon, shortcuts, and headless model tests.
pub let registry = [
    *[for (entry in [{id:"saveSource",label:"Save source"},{id:"deleteSource",label:"Delete source"},
        {id:"insertCitation",label:"Insert citation"},{id:"editCitation",label:"Edit citation"},{id:"removeCitation",label:"Remove citation"},
        {id:"style",label:"Citation style"},{id:"bibliography",label:"Bibliography"},{id:"refresh",label:"Update citations and bibliography"}])
        {id:"references." ++ entry.id,label:entry.label}],
    {id:"edit.undo",label:"Undo",input:"historyUndo",key:"z"},
    {id:"edit.redo",label:"Redo",input:"historyRedo",key:"y"},
    {id:"format.bold",label:"Bold",input:"formatBold",key:"b",mark:'strong'},
    {id:"format.italic",label:"Italic",input:"formatItalic",key:"i",mark:'em'},
    {id:"format.underline",label:"Underline",input:"formatUnderline",key:"u",mark:'u'},
    {id:"format.strikethrough",label:"Strikethrough",input:"modelToggleMark",mark:'del'},
    {id:"format.subscript",label:"Subscript",input:"modelToggleMark",mark:'sub'},
    {id:"format.superscript",label:"Superscript",input:"modelToggleMark",mark:'sup'},
    {id:"format.font",label:"Font",property:"font-family",param:"name"},
    {id:"format.size",label:"Font size",property:"font-size",param:"size"},
    {id:"format.color",label:"Text color",property:"color",param:"color"},
    {id:"format.highlight",label:"Highlight",property:"background-color",param:"color"},
    {id:"format.clear",label:"Clear formatting"},
    {id:"para.style",label:"Paragraph style"},
    {id:"para.set",label:"Paragraph settings"},
    {id:"para.indents",label:"Paragraph indentation"},
    {id:"layout.pageSetup",label:"Page setup"},
    {id:"layout.columns",label:"Columns"},
    {id:"insert.pageBreak",label:"Page break"},
    {id:"insert.blankPage",label:"Blank page"},
    {id:"insert.coverPage",label:"Cover page"},
    {id:"insert.bookmark",label:"Bookmark"},
    {id:"insert.deleteBookmark",label:"Delete bookmark"},
    {id:"insert.symbol",label:"Symbol"},
    {id:"para.alignLeft",label:"Align left",property:"text-align",value:"left"},
    {id:"para.alignCenter",label:"Center",property:"text-align",value:"center"},
    {id:"para.alignRight",label:"Align right",property:"text-align",value:"right"},
    {id:"para.justify",label:"Justify",property:"text-align",value:"justify"},
    {id:"para.lineSpacing",label:"Line spacing",property:"line-height",param:"value"},
    {id:"para.bullets",label:"Bullets",input:"insertUnorderedList"},
    {id:"para.numbering",label:"Numbering",input:"insertOrderedList"},
    {id:"para.indent",label:"Indent list",input:"formatIndent"},
    {id:"para.outdent",label:"Outdent list",input:"formatOutdent"},
    {id:"insert.table",label:"Insert table",input:"insertTable"},
    {id:"table.insertRowBelow",label:"Add row",input:"insertTableRow"},
    {id:"table.insertRowAbove",label:"Insert row above"},
    {id:"table.insertColumnRight",label:"Add column",input:"insertTableColumn"},
    {id:"table.insertColumnLeft",label:"Insert column left"},
    {id:"table.deleteRow",label:"Delete row",input:"deleteTableRow"},
    {id:"table.deleteColumn",label:"Delete column",input:"deleteTableColumn"},
    {id:"table.deleteTable",label:"Delete table"},
    *[for (entry in [{id:"mergeCells",label:"Merge cells"},{id:"splitCells",label:"Split cells"},{id:"splitTable",label:"Split table"},
        {id:"autoFit",label:"AutoFit"},{id:"cellSize",label:"Cell size"},{id:"distributeRows",label:"Distribute rows"},
        {id:"distributeColumns",label:"Distribute columns"},{id:"cellMargins",label:"Cell margins"},{id:"textDirection",label:"Text direction"},
        {id:"properties",label:"Table properties"},{id:"sort",label:"Sort table"},{id:"repeatHeader",label:"Repeat header rows"},
        {id:"convertText",label:"Convert table to text"},{id:"formula",label:"Formula"}]) {id:"table." ++ entry.id,label:entry.label}],
    {id:"table.style",label:"Table style"},
    {id:"table.designOption",label:"Table style option"},
    {id:"table.shading",label:"Cell shading"},
    {id:"table.cellAlign",label:"Cell alignment"},
    {id:"table.selectCell",label:"Select cell"},
    {id:"table.selectRow",label:"Select row"},
    {id:"table.selectColumn",label:"Select column"},
    {id:"table.selectTable",label:"Select table"},
    {id:"table.borders",label:"Cell borders"},
    {id:"table.clear",label:"Clear cell contents"},
    {id:"table.nextCell",label:"Next cell"},
    {id:"table.previousCell",label:"Previous cell"},
    {id:"insert.link",label:"Insert link",input:"insertLink"},
    {id:"insert.removeLink",label:"Remove link"},
    {id:"insert.picture",label:"Insert picture"},
    {id:"picture.size",label:"Picture settings"},
    {id:"object.transform",label:"Resize / rotate object"},
    {id:"shape.format",label:"Shape formatting"},
    {id:"equation.format",label:"Equation formatting"},
    {id:"object.move",label:"Move object"},
    {id:"object.group",label:"Group objects"},{id:"object.ungroup",label:"Ungroup objects"},
    *[for (entry in [{id:"layout",label:"Object layout"},{id:"position",label:"Object position"},
        {id:"align",label:"Align object"},{id:"layer",label:"Arrange object"},{id:"flip",label:"Flip object"}])
        {id:"object." ++ entry.id,label:entry.label}],
    {id:"layout.headerFooter",label:"Header and footer"},
    *[for (entry in graphics.catalog) for (item in [{id:entry.insert,label:"Insert " ++ entry.label},{id:entry.update,label:entry.label ++ " settings"}]) item],
    {id:"insert.emoji",label:"Insert emoji"},
    {id:"review.trackChanges",label:"Track text changes"},
    {id:"review.accept",label:"Accept change"},{id:"review.reject",label:"Reject change"},
    {id:"review.acceptAll",label:"Accept all changes"},{id:"review.rejectAll",label:"Reject all changes"},
    {id:"review.addComment",label:"New comment"},
    {id:"review.resolveComment",label:"Resolve comment"},
    {id:"review.deleteComment",label:"Delete comment"},
    {id:"insert.horizontalRule",label:"Horizontal rule",input:"insertHorizontalRule"},
    {id:"text.insert",label:"Typing",input:"insertText"},
    {id:"text.enter",label:"New paragraph",input:"insertParagraph"},
    {id:"text.backspace",label:"Backspace",input:"deleteContentBackward"},
    {id:"text.delete",label:"Delete",input:"deleteContentForward"},
    {id:"edit.replace",label:"Replace"},{id:"edit.replaceAll",label:"Replace all"}
]
pub fn spec(id) => [for (s in registry where s.id == id) s][0]
pub fn shortcut(key,shift=false) =>
    if (key == "z" and shift) "edit.redo" else [for (s in registry where s.key == key) s.id][0]

pub let leaves = inline_format.leaves
pub fn first_pos(doc) => {path:leaves(doc)[0].path or blocks(doc)[0].path,offset:0}
// page roles do not repeat on Enter; declared object attributes survive rich paste.
let word_schema = {*:html.schema,p:{*:html.schema.p,attrs:[*html.schema.p.attrs,
    *[for (name in ['data-word-page-break','data-word-blank-page','data-word-cover']) {name:name,keep_on_split:false}]]},
    table:{*:html.schema.table,attrs:[*html.schema.table.attrs,*[for (name in [*table_design.attributes,'data-word-repeat-header']) {name:name}]]},
    a:{*:html.schema.a,attrs:[*html.schema.a.attrs,{name:'data-word-citation'}]},
    div:{*:html.schema.div,attrs:[*html.schema.div.attrs,{name:'data-word-bibliography',keep_on_split:false}]},
    img:{*:html.schema.img,attrs:[*html.schema.img.attrs,
        *[for (name in ['style','data-word-rotation',object_layout.key,group_data.key,*[for (entry in graphics.catalog) entry.key]]) {name:name}]]}}
pub fn open_editor(doc) => {*:edit_open(doc,word_schema,text_selection(first_pos(doc),first_pos(doc))),transaction_filter:changes.filter}
pub fn initial() => open_editor(document.sample())

pub fn plain_text(n) => if (is_text(n)) (if (changes.deleted(n.marks)) "" else n.text) else if (not is_node(n)) ""
    else join([for (child in n.content) plain_text(child)],
        if (contains(['ul','ol','table','thead','tbody','li'],n.tag) or html.schema[n.tag].content[0].role == 'block') "\n"
        else if (n.tag == 'tr') "\t" else "")
pub fn word_count(doc) => len([for (s in split(replace(replace(replace(plain_text(doc),"\n"," "),"\t"," "),"\r"," ")," ") where s != "") s])

let text_blocks = rendering.surface_block_tags
pub let block_selector = join([for (tag in text_blocks) string(tag)],",")
pub fn blocks(n,path=[]) {
    let children = if (not is_node(n)) [] else [for (i,child in n.content) for (entry in blocks(child,[*path,i])) entry];
    // a cell with real paragraphs formats those paragraphs; a bare-text cell is its own block.
    if (is_node(n) and contains(text_blocks,n.tag) and len(children)==0) [{path:path,node:n}] else children
}
pub fn headings(doc) => [for (entry in descendants(doc) where contains(['h1','h2','h3','h4','h5','h6'],entry.node.tag))
    {path:entry.path,label:doc_text(entry.node),level:string(entry.node.tag)}]

pub let selection_path = inline_format.selection_path
pub fn current_node(editor,tag) {
    let path = commands.ancestor_tag(editor.doc,selection_path(editor.selection),tag)
    if (path == null) null else {path:path,node:node_at(editor.doc,path)}
}
pub let table_context = tables.context
pub fn current_block(editor) => [for (entry in blocks(editor.doc)
    where path_is_prefix(entry.path,selection_path(editor.selection))) entry][last]
pub fn select_paragraph(editor) {
    // span formatting runs while leaving neighboring paragraphs outside the range.
    let block = current_block(editor)
    let entries = if (block==null) [] else leaves(block.node,block.path)
    let first = entries[0]
    let tail = entries[last];
    if (block==null) editor else edit_set_selection(editor,text_selection(
        pos(if (first==null) block.path else first.path,0),
        pos(if (tail==null) block.path else tail.path,if (tail==null) 0 else len(tail.leaf.text))))
}
pub let current_marks = inline_format.current_marks
pub let css_get = inline_format.css_get
pub let css_set = inline_format.css_set
pub let span_css = inline_format.span_css
pub let char_value = inline_format.char_value
let styled_marks = inline_format.styled_marks
let leaf_segment = inline_format.leaf_segment
let char_transaction = inline_format.transaction

pub fn selected_blocks(editor) {
    let sel = editor.selection
    let entries = blocks(editor.doc)
    if (sel.kind == 'all') entries
    else if (sel.kind == 'node' or sel.kind=='multi-node') [for (entry in entries where
        any([for (path in selection_paths(sel)) path_is_prefix(path,entry.path) or path_is_prefix(entry.path,path)])) entry]
    else {
        let lo = pos_min(sel.anchor,sel.head)
        let hi = pos_max(sel.anchor,sel.head);
        [for (entry in entries where path_is_prefix(entry.path,lo.path) or
            (path_compare(entry.path,lo.path)>0 and path_compare(entry.path,hi.path)<=0 and
             not (hi.offset == 0 and path_is_prefix(entry.path,hi.path)))) entry]
    }
}
fn block_transaction(tx,entries,key,value,style,i=0) {
    if (i >= len(entries)) tx else {
        let entry = entries[i]
        let next = if (style == null) tx_step(tx,step_set_attr(entry.path,'style',css_set(attrs_get(node_at(tx.doc_after,entry.path).attrs,'style'),key,value)))
            else tx_step(tx_step(tx,step_set_node_type(entry.path,style.tag)),step_set_attr(entry.path,'data-word-style',style.id))
        block_transaction(next,entries,key,value,style,i+1)
    }
}
pub fn paragraph_block(editor) => current_block(editor) or selected_blocks(editor)[0]
fn property_value(s,params) any^ {
    let value = if (s.value != null) s.value else params[s.param]
    if (s.property == "font-size") {
        if (not (value is number) or value < 6 or value > 96) raise error("Word: font size must be 6–96 pt")
        else string(value) ++ "pt"
    }
    else if (s.property == "line-height") {
        if (not contains([1,1.15,1.5,2],value)) raise error("Word: choose a supported line spacing") else string(value)
    }
    else if (s.property == "font-family") {
        // native families and missing imported families are valid; keep this one CSS declaration.
        if (not (value is string) or trim(value)=="" or
            any([for (separator in [";","{","}","\n","\r"]) contains(value,separator)]))
            raise error("Word: choose a font family") else value
    }
    else if (s.property == "color" or s.property == "background-color") {
        if (not contains(["#24334b","#2864ad","#b43d47","#28745d","#f9e896","#dceafa","transparent"],value))
            raise error("Word: choose a palette color") else value
    }
    else string(value)
}

// The generic editor selects inserted table units. Word-processing commands
// leave a text caret in the new cell, so typing and the next table action
// share that cell and undo/redo restore the same caret.
fn table_transaction(tx) {
    let sel = tx.sel_after
    if (sel.kind != 'node') tx else {
        let entry = leaves(node_at(tx.doc_after,sel.path),sel.path)[0]
        if (entry==null) tx else tx_set_selection(tx,text_selection(pos(entry.path,0),pos(entry.path,0)))
    }
}

fn valid_count(value,lo,hi) => value is number and value >= lo and value <= hi and value == floor(value)
fn valid_measure(value,lo,hi) => value is number and value >= lo and value <= hi
fn properties_transaction(tx,entries,properties,i=0) => if (i >= len(properties)) tx else
    properties_transaction(block_transaction(tx,entries,properties[i].key,properties[i].value,null),entries,properties,i+1)

// CSS lengths in imported documents may use pixels; dialog and ruler values use points.
pub fn points(value,fallback=0) {
    let s = trim(value or "")
    let units = [{unit:"pt",scale:1},{unit:"px",scale:0.75},{unit:"mm",scale:72/25.4},{unit:"cm",scale:72/2.54},{unit:"in",scale:72}]
    let unit = [for (u in units where ends_with(s,u.unit)) u][0]
    let magnitude = float(if (unit==null) s else slice(s,0,len(s)-len(unit.unit))) ^ { fallback };
    magnitude*(unit.scale or 1)
}
pub fn paragraph_indents(editor) {
    let css = attrs_get(paragraph_block(editor).node.attrs,'style');
    {indent:points(css_get(css,"margin-left")),first:points(css_get(css,"text-indent")),right:points(css_get(css,"margin-right"))}
}
pub fn paragraph_settings(editor) {
    let css = attrs_get(paragraph_block(editor).node.attrs,'style')
    {*:paragraph_indents(editor),align:css_get(css,"text-align") or "left",lines:float(css_get(css,"line-height") or "1.5"),
        before:points(css_get(css,"margin-top")),after:points(css_get(css,"margin-bottom"),11)}
}
let indent_properties = [{id:"indent",css:"margin-left",min:0},{id:"first",css:"text-indent",min: -72},{id:"right",css:"margin-right",min:0}]
fn indentation_properties(p) array^ {
    if (any([for (s in indent_properties where p[s.id]!=null) not valid_measure(p[s.id],s.min,200)]))
        raise error("Word: enter valid paragraph indentation")
    else [for (s in indent_properties where p[s.id]!=null) {key:s.css,value:string(p[s.id]) ++ "pt"}]
}
fn wrapped_position(point,paths) {
    let cell = [for (path in paths where path_is_prefix(path,point.path)) path][0];
    if (cell==null) point else {*:point,path:[*cell,0,*slice(point.path,len(cell))]}
}
fn indentation_transaction(editor,p) map^ {
    let properties = indentation_properties(p)^
    let entries = selected_blocks(editor)
    let wrapped = [for (e in entries where contains(['td','th'],e.node.tag)) e.path]
    let tx = reduce([tx_begin(editor.doc,editor.selection),*entries],(tx,e)=>{
        // CSS margins do not indent a table cell. Give its inline text a paragraph without changing the grid.
        let cell = contains(['td','th'],e.node.tag)
        let path = if (cell) [*e.path,0] else e.path
        let prepared = if (not cell) tx else tx_step(tx,step_replace(e.path,0,len(e.node.content),[node('p',e.node.content)]));
        properties_transaction(prepared,[{path:path}],properties)
    })
    let sel = editor.selection;
    tx_set_selection(tx,if (sel.kind!='text') sel else text_selection(wrapped_position(sel.anchor,wrapped),wrapped_position(sel.head,wrapped)))
}
fn paragraph_transaction(editor,p) map^ {
    if (not contains(["left","center","right","justify"],p.align) or not contains([1,1.15,1.5,2],p.lines) or
        not valid_measure(p.before,0,200) or not valid_measure(p.after,0,200))
        raise error("Word: enter valid paragraph spacing and indentation")
    else {
        let tx = indentation_transaction(editor,p)^
        let target = {*:editor,doc:tx.doc_after,selection:tx.sel_after};
        properties_transaction(tx,selected_blocks(target),[
            {key:"text-align",value:p.align},{key:"line-height",value:string(p.lines)},
            {key:"margin-top",value:string(p.before) ++ "pt"},{key:"margin-bottom",value:string(p.after) ++ "pt"}])
    }
}

fn picture_transaction(editor,id,p) map^ {
    let image = current_node(editor,'img')
    let rotation = p.rotation or 0
    if (not valid_measure(p.width,16,650) or not (p.height == 0 or valid_measure(p.height,16,2000)))
        raise error("Word: picture width must be 16–650 px; height must be automatic or 16–2000 px")
    else if (id == "picture.size" and image == null) raise error("Word: select a picture first")
    else if (id == "insert.picture" and (not (p.src is string) or trim(p.src)=="")) raise error("Word: choose a picture")
    else if (not (rotation is number) or rotation< -180 or rotation>180) raise error("Word: picture rotation must be −180–180°")
    else if (id=="picture.size" and not group_data.proportional(image.node,p.width,p.height)) raise error("Word: resize groups proportionally")
    else {
        let tx = if (id == "picture.size") tx_begin(editor.doc,editor.selection)
            else commands.cmd_insert_image(editor,p.src,p.alt or "")
        if (tx == null) raise error("Word: a picture cannot be inserted at this selection") else {
            let path = if (id == "picture.size") image.path else tx.sel_after.path
            let resized = tx_step(image.resize_transaction(tx,path,p.width,if (p.height == 0) null else p.height),step_set_attr(path,'alt',p.alt or ""))
            objects.rotate_transaction(resized,path,rotation)
        }
    }
}
fn replace_entry(editor,entry,kids) {
    let parent = parent_path(entry.path)
    let index = last_index(entry.path)
    let tx = tx_step(tx_begin(editor.doc,editor.selection),step_replace(parent,index,index+1,kids))
    let first = leaves(node_at(tx.doc_after,[*parent,index]),[*parent,index])[0]
    tx_set_selection(tx,text_selection(pos(first.path,0),pos(first.path,0)))
}
fn link_transaction(editor,id,p) map^ {
    let link = current_node(editor,'a')
    if (link == null) raise error("Word: place the caret in a link first")
    else if (id == "insert.removeLink") replace_entry(editor,link,link.node.content)
    else {
        let tx = tx_step(tx_step(tx_begin(editor.doc,editor.selection),step_set_attr(link.path,'href',p.href)),
            step_set_attr(link.path,'title',p.title or ""))
        if (p.label == null or p.label == "" or p.label == doc_text(link.node)) tx
        else tx_set_selection(tx_step(tx,step_replace(link.path,0,len(link.node.content),
            [text_marked(p.label,leaves(link.node)[0].leaf.marks or [])])),
            text_selection(pos([*link.path,0],len(p.label)),pos([*link.path,0],len(p.label))))
    }
}

fn cell_alignment_transaction(editor,p) map^ {
    let ctx = table_context(editor)
    if (ctx == null) raise error("Word: place the caret inside a table first")
    else if (not contains(table_layout.alignments,p.value))
        raise error("Word: choose a cell alignment")
    else {
        let cells = tables.selected(editor)
        properties_transaction(tx_begin(editor.doc,editor.selection),cells,[
            {key:"text-align",value:if (contains(["topLeft","centerLeft","bottomLeft"],p.value)) "left"
                else if (contains(["topRight","centerRight","bottomRight"],p.value)) "right" else "center"},
            {key:"vertical-align",value:if (starts_with(p.value,"top")) "top" else if (starts_with(p.value,"bottom")) "bottom" else "middle"}])
    }
}
fn table_navigate(editor,forward) map^ {
    let ctx = table_context(editor)
    if (ctx == null) raise error("Word: place the caret inside a table first") else {
        let cells = blocks(ctx.table,ctx.table_path)
        let index = [for (i,entry in cells where path_equal(entry.path,ctx.cell_path)) i][0]
        if (forward and index == len(cells)-1) edit_apply(editor,table_layout.transaction(editor,"table.insertRowBelow")^)
        else if (not forward and index == 0) editor
        else {
            let cell = cells[index + if (forward) 1 else -1]
            let first = leaves(cell.node,cell.path)[0]
            let point = pos(if (first==null) cell.path else first.path,0)
            edit_set_selection(editor,text_selection(point,point))
        }
    }
}

pub fn can_comment(editor) => editor.selection.kind=='text' and not pos_equal(editor.selection.anchor,editor.selection.head)
pub fn cell_alignment(editor) {
    let ctx = table_context(editor)
    let css = attrs_get(node_at(editor.doc,ctx.cell_path).attrs,'style')
    let vertical = css_get(css,"vertical-align") or "top"
    let horizontal = css_get(css,"text-align") or "left"
    if (vertical=="middle" and horizontal=="center") "center" else
        (if (vertical=="middle") "center" else vertical) ++ (if (horizontal=="right") "Right" else if (horizontal=="center") "Center" else "Left")
}
pub fn comment_selection(editor,id) {
    let entries = [for (entry in leaves(editor.doc) where review.has_anchor(entry.leaf.marks,id)) entry]
    if (len(entries)==0) null else text_selection(pos(entries[0].path,0),pos(entries[last].path,len(entries[last].leaf.text)))
}
pub fn next_comment_id(editor,show_resolved,forward) {
    let entries = [for (c in review.comments(editor.doc) where show_resolved or not c.resolved) c]
    let current = [for (i,c in entries where review.has_anchor(current_marks(editor),c.id)) i][0]
    let start = current or (if (forward) -1 else 0)
    if (len(entries)==0) null else entries[(start+(if (forward) 1 else -1)+len(entries))%len(entries)].id
}
fn comment_transaction(editor,id,p) map^ {
    let entries = review.comments(editor.doc)
    if (id == "review.addComment") {
        if (not can_comment(editor) or not (p.text is string) or trim(p.text)=="") raise error("Word: select text and enter a comment")
        else {
            let comment_id = review.next_id(editor.doc)
            let quote = join([for (entry in leaves(editor.doc),let segment=leaf_segment(entry,editor.selection)
                where segment!=null and segment.to>segment.from) slice(entry.leaf.text,segment.from,segment.to)],"")
            review.add(char_transaction(editor,(marks)=>review.anchor(marks,comment_id)),editor.doc,
                {id:comment_id,author:trim(p.author or "Me"),text:trim(p.text),quote:quote,resolved:false})^
        }
    }
    else if (not any([for (c in entries) c.id==p.id])) raise error("Word: this comment no longer exists")
    else if (id == "review.resolveComment") review.with_comments(tx_begin(editor.doc,editor.selection),
        [for (c in entries) if (c.id==p.id) {*:c,resolved:p.resolved==true} else c])^
    else {
        let tx = char_transaction({*:editor,selection:all_selection()},(marks)=>review.without_anchor(marks,p.id))
        review.with_comments(tx_set_selection(tx,editor.selection),[for (c in entries where c.id!=p.id) c])^
    }
}

pub fn execute(editor,id,params={}) map^ {
    let s = spec(id)
    if (s == null) raise error("Word: unknown command " ++ id)
    else if (starts_with(id,"references.")) edit_apply(editor,references.transaction(editor,id,params)^)
    else if (starts_with(id,"table.select")) edit_set_selection(editor,
        tables.selection(editor,lower(slice(id,12)),params.path))
    else if (id=="table.borders") edit_apply(editor,tables.borders(editor,params)^)
    else if (contains([for (name in table_layout.command_ids) "table." ++ name],id)) edit_apply(editor,table_layout.transaction(editor,id,params)^)
    else if (contains(["table.style","table.designOption"],id)) edit_apply(editor,table_design.transaction(editor,params)^)
    else if (id=="table.shading") edit_apply(editor,table_design.shade_transaction(editor,tables.selected(editor),params)^)
    else if (id=="table.clear") edit_apply(editor,tables.clear_transaction(editor))
    else if (id == "object.transform") edit_apply(editor,transform.transaction(editor,params)^)
    else if (id=="shape.format") edit_apply(editor,shape_format.transaction(editor,params)^)
    else if (id=="equation.format") edit_apply(editor,equation.format_transaction(editor,params)^)
    else if (id=="object.group") edit_apply(editor,groups.transaction(editor,params)^)
    else if (id=="object.ungroup") edit_apply(editor,groups.ungroup(editor,params)^)
    else if (contains(["object.layout","object.position","object.align","object.layer","object.flip"],id))
        edit_apply(editor,object_layout.transaction(editor,id,params)^)
    else if (id == "object.move") image.move_image(editor,params.path,params.target)
    else if (id == "insert.coverPage") edit_apply(editor,insert.cover_transaction(editor,params)^)
    else if (id == "insert.blankPage") edit_apply(editor,insert.blank_page_transaction(editor)^)
    else if (id == "insert.symbol") edit_apply(editor,insert.symbol_transaction(editor,params)^)
    else if (id == "insert.bookmark" or id == "insert.deleteBookmark")
        edit_apply(editor,insert.bookmark_transaction(editor,id,params,current_block(editor))^)
    else if (id == "edit.replace" or id == "edit.replaceAll") {
        // explicit replacements are transactions of their own, outside typing compression.
        let hits = if (id=="edit.replace") [editor.selection] else search(editor.doc,params.find or "",params.match_case != false)
        if (len(hits)==0) editor else edit_apply(editor,
            replace_hits(tx_begin(editor.doc,editor.selection),hits,params.replace or "",len(hits)-1))
    }
    else if (id == "layout.pageSetup") {
        let tx = page.transaction(editor.doc,editor.selection,{*:page.settings(editor.doc),*:params})^
        let checked = stories.validate(tx.doc_after,stories.settings(tx.doc_after))^
        let columns_fit = columns.validate(tx.doc_after,columns.settings(tx.doc_after))^
        edit_apply(editor,tx)
    }
    else if (id == "layout.columns") edit_apply(editor,columns.transaction(editor,params)^)
    else if (id == "layout.headerFooter") edit_apply(editor,stories.transaction(editor,params)^)
    else if (graphics.command(id)!=null) {
        let entry = graphics.command(id)
        let selected = if (starts_with(id,"insert.")) null else current_node(editor,'img')
        if (not starts_with(id,"insert.") and (selected==null or entry.settings(selected.node)==null))
            raise error("Word: select the matching object first")
        // property dialogs expose a subset; the source atom owns the remaining formatting.
        else edit_apply(editor,entry.transaction(editor,if (selected==null) params else {*:entry.settings(selected.node),*:params},selected)^)
    }
    else if (id=="insert.emoji") edit_apply(editor,insert.symbol_transaction(editor,params)^)
    else if (id == "insert.pageBreak") edit_apply(editor,insert.page_break_transaction(editor)^)
    else if (id=="review.trackChanges") edit_apply(editor,changes.configure(editor,{*:changes.settings(editor.doc),*:params})^)
    else if (contains(["review.accept","review.reject","review.acceptAll","review.rejectAll"],id))
        edit_apply(editor,changes.resolve(editor,if (ends_with(id,"All")) null else params.id,starts_with(id,"review.accept"))^)
    else if (starts_with(id,"review.")) edit_apply(editor,comment_transaction(editor,id,params)^)
    else if (id == "format.clear") edit_apply(editor,char_transaction(editor,review.clear_format))
    else if (id == "para.set") edit_apply(editor,paragraph_transaction(editor,params)^)
    else if (id == "para.indents") edit_apply(editor,indentation_transaction(editor,params)^)
    else if (id == "insert.picture" or id == "picture.size") edit_apply(editor,picture_transaction(editor,id,params)^)
    else if (id == "insert.removeLink" or (id == "insert.link" and current_node(editor,'a') != null))
        edit_apply(editor,link_transaction(editor,id,params)^)
    else if (id == "table.nextCell" or id == "table.previousCell") table_navigate(editor,id == "table.nextCell")^
    else if (id=="table.cellAlign") edit_apply(editor,cell_alignment_transaction(editor,params)^)
    else if (id == "para.style") {
        let style = [for (style in document.styles where style.id == params.style) style][0]
        if (style == null) raise error("Word: unknown paragraph style") else
            edit_apply(editor,block_transaction(tx_begin(editor.doc,editor.selection),selected_blocks(editor),null,null,style))
    }
    else if (s.property != null) {
        let value = property_value(s,params)^
        let tx = if (starts_with(id,"para."))
            block_transaction(tx_begin(editor.doc,editor.selection),selected_blocks(editor),s.property,value,null)
            else char_transaction(editor,(marks)=>styled_marks(marks,s.property,value))
        edit_apply(editor,tx)
    }
    else {
        let checked = if (id == "insert.table" and (not valid_count(params.rows,1,32) or not valid_count(params.cols,1,12)))
            raise error("Word: choose 1–32 rows and 1–12 columns") else true
        let payload = {*:params,mark:s.mark,value:true,
            kind:if (id == "para.numbering") 'ordered' else 'bullet'}
        let target = if (starts_with(id,"table.")) tables.command_editor(editor) else editor
        let run = edit_handle_request(target,edit_request_from_toolbar(s.input,payload))^
        if (not (run.result.failure is null)) raise error("Word: " ++ s.label ++ " is unavailable at this selection")
        else if (id == "insert.table" or starts_with(id,"table.")) edit_apply(editor,
            table_transaction({*:run.transaction,sel_before:editor.selection}))
        else run.editor
    }
}

// Search a paragraph's concatenated runs, then map matches back to source leaves.
fn offset_pos(entries,offset,i=0) {
    let size = len(entries[i].leaf.text)
    if (offset <= size or i == len(entries)-1) pos(entries[i].path,offset)
    else offset_pos(entries,offset-size,i+1)
}
// lowercase can change UTF-8 length; map folded matches back through whole source characters.
fn folded_chars(chars) {
    let parts = [for (c in chars) lower(c)]
    let source_ends = math.cumsum([for (c in chars) len(c)])
    let folded_ends = math.cumsum([for (p in parts) len(p)]);
    [for (i,c in chars) {source:if (i==0) 0 else source_ends[i-1],end:source_ends[i],
        folded:if (i==0) 0 else folded_ends[i-1],fold_end:folded_ends[i],text:parts[i]}]
}
fn search_spans(words,needle,match_case) {
    if (match_case) [for (offset in decorations.find_in_leaf(words,needle)) {from:offset,to:offset+len(needle)}]
    else {
        let chars = folded_chars(split(words,""))
        let query = lower(needle);
        [for (offset in decorations.find_in_leaf(join([for (c in chars) c.text],""),query),
            let first = [for (c in chars where c.folded<=offset and offset<c.fold_end) c][0],
            let end_char = [for (c in chars where c.folded<offset+len(query) and offset+len(query)<=c.fold_end) c][0])
            {from:first.source,to:end_char.end}]
    }
}
pub fn search(doc,needle,match_case=true) => if (needle == "") [] else [for (entry in blocks(doc),
    let runs = leaves(entry.node,entry.path))
    for (hit in search_spans(doc_text(entry.node),needle,match_case))
    text_selection(offset_pos(runs,hit.from),offset_pos(runs,hit.to))]
// Replace from the end so earlier source positions survive run joins; record
// the composed operation once, using the existing editor insertion primitive.
fn replace_hits(tx,hits,replacement,i) {
    if (i < 0) tx else {
        let next = commands.cmd_insert_text({doc:tx.doc_after,selection:hits[i],schema:html.schema},replacement)
        if (next == null) tx else replace_hits({*:tx,doc_after:next.doc_after,
            steps:[*tx.steps,*next.steps],sel_after:next.sel_after},hits,replacement,i-1)
    }
}
pub fn select_heading(editor,path) {
    let first = leaves(node_at(editor.doc,path),path)[0]
    // an empty heading owns its placeholder caret, just like an empty paragraph or list item.
    let point = pos(first.path or path,0)
    edit_set_selection(editor,text_selection(point,point))
}
pub fn clean(editor) => {*:editor,events:[]}
