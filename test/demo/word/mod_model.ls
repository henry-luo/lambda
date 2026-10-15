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
import document: .mod_document
import page: .mod_page
import review: .mod_review

// The same IDs drive the ribbon, shortcuts, and headless model tests.
pub let registry = [
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
    {id:"layout.pageSetup",label:"Page setup"},
    {id:"insert.pageBreak",label:"Page break"},
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
    {id:"table.style",label:"Table style"},
    {id:"table.cellAlign",label:"Cell alignment"},
    {id:"table.nextCell",label:"Next cell"},
    {id:"table.previousCell",label:"Previous cell"},
    {id:"insert.link",label:"Insert link",input:"insertLink"},
    {id:"insert.removeLink",label:"Remove link"},
    {id:"insert.picture",label:"Insert picture"},
    {id:"picture.size",label:"Picture settings"},
    {id:"review.addComment",label:"New comment"},
    {id:"review.resolveComment",label:"Resolve comment"},
    {id:"review.deleteComment",label:"Delete comment"},
    {id:"insert.horizontalRule",label:"Horizontal rule",input:"insertHorizontalRule"},
    {id:"text.insert",label:"Typing",input:"insertText"},
    {id:"text.enter",label:"New paragraph",input:"insertParagraph"},
    {id:"text.backspace",label:"Backspace",input:"deleteContentBackward"},
    {id:"text.delete",label:"Delete",input:"deleteContentForward"},
    {id:"edit.replaceAll",label:"Replace all"}
]
pub fn spec(id) => [for (s in registry where s.id == id) s][0]
pub fn shortcut(key,shift=false) =>
    if (key == "z" and shift) "edit.redo" else [for (s in registry where s.key == key) s.id][0]

pub fn leaves(n,path=[]) => if (is_text(n)) [{path:path,leaf:n}]
    else if (is_node(n)) [for (i,child in n.content) for (entry in leaves(child,[*path,i])) entry] else []
pub fn first_pos(doc) => {path:leaves(doc)[0].path or blocks(doc)[0].path,offset:0}
pub fn open_editor(doc) => edit_open(doc,html.schema,text_selection(first_pos(doc),first_pos(doc)))
pub fn initial() => open_editor(document.sample())

pub fn plain_text(n) => if (is_text(n)) n.text else if (not is_node(n)) ""
    else join([for (child in n.content) plain_text(child)],
        if (contains(['ul','ol','table','thead','tbody','li'],n.tag) or html.schema[n.tag].content[0].role == 'block') "\n"
        else if (n.tag == 'tr') "\t" else "")
pub fn word_count(doc) => len([for (s in split(replace(replace(replace(plain_text(doc),"\n"," "),"\t"," "),"\r"," ")," ") where s != "") s])

let text_blocks = ['p','h1','h2','h3','h4','h5','h6','td','th','pre']
pub fn blocks(n,path=[]) => if (not is_node(n)) []
    else if (contains(text_blocks,n.tag)) [{path:path,node:n}]
    else [for (i,child in n.content) for (entry in blocks(child,[*path,i])) entry]
pub fn headings(doc) => [for (entry in blocks(doc) where contains(['h1','h2','h3','h4','h5','h6'],entry.node.tag))
    {path:entry.path,label:doc_text(entry.node),level:string(entry.node.tag)}]

pub fn selection_path(sel) => if (sel.kind == 'text') sel.head.path else if (sel.kind == 'node') sel.path else []
pub fn current_node(editor,tag) {
    let path = commands.ancestor_tag(editor.doc,selection_path(editor.selection),tag)
    if (path == null) null else {path:path,node:node_at(editor.doc,path)}
}
pub fn table_context(editor) => commands.selected_table_context(editor)
pub fn current_block(editor) => [for (entry in blocks(editor.doc)
    where path_is_prefix(entry.path,selection_path(editor.selection))) entry][last]
pub fn current_marks(editor) array => if (editor.stored_marks != null) editor.stored_marks
    else node_at(editor.doc,selection_path(editor.selection)).marks or []
pub fn css_get(css,key) => [for (decl in split(css or "",";"),let pair=split(decl,":")
    where lower(trim(pair[0])) == key) trim(join(drop(pair,1),":"))][last]
pub fn css_set(css,key,value) => join([*[for (decl in split(css or "",";") where trim(decl) != "" and
    lower(trim(split(decl,":")[0])) != key) trim(decl)],key ++ ": " ++ value],"; ")
pub fn span_css(marks) string {
    let attrs = mark_value(marks,'span')
    if (attrs is array) attrs_get(attrs,'style') or "" else ""
}
pub fn char_value(editor,key,fallback) => css_get(span_css(current_marks(editor)),key) or fallback
fn styled_marks(marks,key,value) {
    let attrs = mark_value(marks,'span')
    let old = if (attrs is array) attrs else []
    with_mark(marks,'span',attrs_set(old,'style',css_set(attrs_get(old,'style'),key,value)))
}

fn leaf_segment(entry,sel) {
    if (sel.kind == 'all' or (sel.kind == 'node' and path_is_prefix(sel.path,entry.path))) {from:0,to:len(entry.leaf.text)}
    else if (sel.kind != 'text') null
    else {
        let lo = pos_min(sel.anchor,sel.head)
        let hi = pos_max(sel.anchor,sel.head);
        if (path_compare(entry.path,lo.path) < 0 or path_compare(entry.path,hi.path) > 0) null
        else {from:if (path_equal(entry.path,lo.path)) lo.offset else 0,
              to:if (path_equal(entry.path,hi.path)) hi.offset else len(entry.leaf.text)}
    }
}
fn fragment(words,marks) => if (words == "") [] else [text_marked(words,marks)]

// A split preserves the selected characters, including a backward selection.
// Generic replace-step mapping collapses positions inside a replaced leaf.
fn split_pos(p,entry,segment,step) {
    if (not path_equal(p.path,entry.path)) step_map(step,p)
    else {
        let before = if (segment.from > 0) 1 else 0
        let index = if (p.offset < segment.from) 0 else if (p.offset <= segment.to) before else before + 1
        let offset = if (p.offset < segment.from) p.offset else if (p.offset <= segment.to) p.offset-segment.from else p.offset-segment.to
        pos([*parent_path(entry.path),last_index(entry.path)+index],offset)
    }
}
fn format_leaf(tx,entry,segment,transform) {
    let leaf = entry.leaf
    let marks = transform(leaf.marks)
    if (marks == leaf.marks) tx else {
        let pieces = [*fragment(slice(leaf.text,0,segment.from),leaf.marks),
            text_marked(slice(leaf.text,segment.from,segment.to),marks),
            *fragment(slice(leaf.text,segment.to),leaf.marks)]
        let step = step_replace(parent_path(entry.path),last_index(entry.path),last_index(entry.path)+1,pieces)
        let sel = tx.sel_after
        let next = tx_step(tx,step)
        if (sel.kind != 'text') next
        else tx_set_selection(next,text_selection(split_pos(sel.anchor,entry,segment,step),split_pos(sel.head,entry,segment,step)))
    }
}
fn format_leaves(tx,entries,sel,transform,i) {
    if (i < 0) tx else {
        let segment = leaf_segment(entries[i],sel)
        let next = if (segment == null or segment.to <= segment.from) tx else format_leaf(tx,entries[i],segment,transform)
        format_leaves(next,entries,sel,transform,i-1)
    }
}
fn char_transaction(editor,transform) {
    let sel = editor.selection
    let tx = tx_begin(editor.doc,sel)
    if (sel.kind == 'text' and pos_equal(sel.anchor,sel.head))
        tx_set_meta(tx_set_meta(tx,"storedMarks",transform(current_marks(editor))),"addToHistory",false)
    else {
        let entries = leaves(editor.doc)
        format_leaves(tx,entries,sel,transform,len(entries)-1)
    }
}

fn selected_blocks(editor) {
    let sel = editor.selection
    let entries = blocks(editor.doc)
    if (sel.kind == 'all') entries
    else if (sel.kind == 'node') [for (entry in entries where path_is_prefix(sel.path,entry.path) or path_is_prefix(entry.path,sel.path)) entry]
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
        if (not contains(["Arial","Georgia","Times New Roman","Courier New"],value)) raise error("Word: choose a supported font") else value
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

pub fn paragraph_settings(editor) {
    let css = attrs_get(current_block(editor).node.attrs,'style')
    {align:css_get(css,"text-align") or "left",lines:float(css_get(css,"line-height") or "1.5"),
        before:float(replace(css_get(css,"margin-top") or "0pt","pt","")),
        after:float(replace(css_get(css,"margin-bottom") or "11pt","pt","")),
        indent:float(replace(css_get(css,"margin-left") or "0pt","pt","")),
        first:float(replace(css_get(css,"text-indent") or "0pt","pt",""))}
}
fn paragraph_transaction(editor,p) map^ {
    if (not contains(["left","center","right","justify"],p.align) or not contains([1,1.15,1.5,2],p.lines) or
        not valid_measure(p.before,0,200) or not valid_measure(p.after,0,200) or
        not valid_measure(p.indent,0,200) or not valid_measure(p.first,-72,200))
        raise error("Word: enter valid paragraph spacing and indentation")
    else properties_transaction(tx_begin(editor.doc,editor.selection),selected_blocks(editor),[
        {key:"text-align",value:p.align},{key:"line-height",value:string(p.lines)},
        {key:"margin-top",value:string(p.before) ++ "pt"},{key:"margin-bottom",value:string(p.after) ++ "pt"},
        {key:"margin-left",value:string(p.indent) ++ "pt"},{key:"text-indent",value:string(p.first) ++ "pt"}])
}

fn picture_transaction(editor,id,p) map^ {
    let image = current_node(editor,'img')
    if (not valid_measure(p.width,16,650) or not (p.height == 0 or valid_measure(p.height,16,2000)))
        raise error("Word: picture width must be 16–650 px; height must be automatic or 16–2000 px")
    else if (id == "picture.size" and image == null) raise error("Word: select a picture first")
    else if (id == "insert.picture" and (not (p.src is string) or trim(p.src)=="")) raise error("Word: choose a picture")
    else {
        let tx = if (id == "picture.size") tx_begin(editor.doc,editor.selection)
            else commands.cmd_insert_image(editor,p.src,p.alt or "")
        if (tx == null) raise error("Word: a picture cannot be inserted at this selection") else {
            let path = if (id == "picture.size") image.path else tx.sel_after.path
            tx_step(tx_step(tx_step(tx,step_set_attr(path,'width',p.width)),
                step_set_attr(path,'height',if (p.height == 0) null else p.height)),step_set_attr(path,'alt',p.alt or ""))
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

fn table_edit(editor,id,p) map^ {
    let ctx = table_context(editor)
    if (ctx == null) raise error("Word: place the caret inside a table first")
    else if (id == "table.insertRowAbove") table_transaction(commands.cmd_add_table_row(editor,true))
    else if (id == "table.insertColumnLeft") table_transaction(commands.cmd_add_table_column(editor,true))
    else if (id == "table.deleteTable") replace_entry(editor,{path:ctx.table_path},[document.paragraph("")])
    else if (id == "table.style") {
        if (not contains([for (s in document.table_styles) s.id],p.style)) raise error("Word: choose a table style")
        else tx_step(tx_begin(editor.doc,editor.selection),step_set_attr(ctx.table_path,'data-word-table-style',p.style))
    }
    else if (not contains(["topLeft","topCenter","topRight","centerLeft","center","centerRight","bottomLeft","bottomCenter","bottomRight"],p.value))
        raise error("Word: choose a cell alignment")
    else {
        let cells = [for (entry in selected_blocks(editor) where contains(['td','th'],entry.node.tag)) entry]
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
        if (forward and index == len(cells)-1) edit_apply(editor,table_transaction(commands.cmd_add_table_row(editor)))
        else if (not forward and index == 0) editor
        else {
            let cell = cells[index + if (forward) 1 else -1]
            let first = leaves(cell.node,cell.path)[0]
            edit_set_selection(editor,text_selection(pos(first.path,0),pos(first.path,0)))
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
    else if (id == "edit.replaceAll") {
        let hits = search(editor.doc,params.find or "",params.match_case != false)
        if (len(hits)==0) editor else edit_apply(editor,
            replace_hits(tx_begin(editor.doc,editor.selection),hits,params.replace or "",len(hits)-1))
    }
    else if (id == "layout.pageSetup") edit_apply(editor,page.transaction(editor.doc,editor.selection,{*:page.settings(editor.doc),*:params})^)
    else if (id == "insert.pageBreak") {
        let tx = commands.cmd_insert_paragraph(editor)
        let block = if (tx==null) null else current_block({doc:tx.doc_after,selection:tx.sel_after})
        if (block==null) raise error("Word: a page break cannot be inserted at this selection")
        else edit_apply(editor,tx_step(tx,step_set_attr(block.path,'data-word-page-break',"true")))
    }
    else if (starts_with(id,"review.")) edit_apply(editor,comment_transaction(editor,id,params)^)
    else if (id == "format.clear") edit_apply(editor,char_transaction(editor,review.clear_format))
    else if (id == "para.set") edit_apply(editor,paragraph_transaction(editor,params)^)
    else if (id == "insert.picture" or id == "picture.size") edit_apply(editor,picture_transaction(editor,id,params)^)
    else if (id == "insert.removeLink" or (id == "insert.link" and current_node(editor,'a') != null))
        edit_apply(editor,link_transaction(editor,id,params)^)
    else if (id == "table.nextCell" or id == "table.previousCell") table_navigate(editor,id == "table.nextCell")^
    else if (contains(["table.insertRowAbove","table.insertColumnLeft","table.deleteTable","table.style","table.cellAlign"],id))
        edit_apply(editor,table_edit(editor,id,params)^)
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
        let run = edit_handle_request(editor,edit_request_from_toolbar(s.input,payload))
        if (run.result.failure != null) raise error("Word: " ++ s.label ++ " is unavailable at this selection")
        else if (id == "insert.table" or starts_with(id,"table.")) edit_apply(editor,table_transaction(run.transaction))
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
    edit_set_selection(editor,text_selection(pos(first.path,0),pos(first.path,0)))
}
pub fn clean(editor) => {*:editor,events:[]}
