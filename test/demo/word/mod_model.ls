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
    {id:"para.style",label:"Paragraph style"},
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
    {id:"table.insertColumnRight",label:"Add column",input:"insertTableColumn"},
    {id:"table.deleteRow",label:"Delete row",input:"deleteTableRow"},
    {id:"table.deleteColumn",label:"Delete column",input:"deleteTableColumn"},
    {id:"insert.link",label:"Insert link",input:"insertLink"},
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
pub fn first_pos(doc) => {path:leaves(doc)[0].path,offset:0}
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

fn selection_path(sel) => if (sel.kind == 'text') sel.head.path else if (sel.kind == 'node') sel.path else []
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
fn format_leaf(tx,entry,segment,key,value) {
    let leaf = entry.leaf
    let marks = styled_marks(leaf.marks,key,value)
    if (segment.from == 0 and segment.to == len(leaf.text)) tx_step(tx,step_add_mark(entry.path,'span',mark_value(marks,'span')))
    else {
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
fn format_leaves(tx,entries,sel,key,value,i) {
    if (i < 0) tx else {
        let segment = leaf_segment(entries[i],sel)
        let next = if (segment == null or segment.to <= segment.from) tx else format_leaf(tx,entries[i],segment,key,value)
        format_leaves(next,entries,sel,key,value,i-1)
    }
}
fn char_transaction(editor,key,value) {
    let sel = editor.selection
    let tx = tx_begin(editor.doc,sel)
    if (sel.kind == 'text' and pos_equal(sel.anchor,sel.head))
        tx_set_meta(tx_set_meta(tx,"storedMarks",styled_marks(current_marks(editor),key,value)),"addToHistory",false)
    else {
        let entries = leaves(editor.doc)
        format_leaves(tx,entries,sel,key,value,len(entries)-1)
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
        let next = if (style == null) tx_step(tx,step_set_attr(entry.path,'style',css_set(attrs_get(entry.node.attrs,'style'),key,value)))
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

pub fn execute(editor,id,params={}) map^ {
    let s = spec(id)
    if (s == null) raise error("Word: unknown command " ++ id)
    else if (id == "edit.replaceAll") {
        let hits = search(editor.doc,params.find or "")
        if (len(hits)==0) editor else edit_apply(editor,
            replace_hits(tx_begin(editor.doc,editor.selection),hits,params.replace or "",len(hits)-1))
    }
    else if (id == "para.style") {
        let style = [for (style in document.styles where style.id == params.style) style][0]
        if (style == null) raise error("Word: unknown paragraph style") else
            edit_apply(editor,block_transaction(tx_begin(editor.doc,editor.selection),selected_blocks(editor),null,null,style))
    }
    else if (s.property != null) {
        let value = property_value(s,params)^
        let tx = if (starts_with(id,"para."))
            block_transaction(tx_begin(editor.doc,editor.selection),selected_blocks(editor),s.property,value,null)
            else char_transaction(editor,s.property,value)
        edit_apply(editor,tx)
    }
    else {
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
pub fn search(doc,needle) => if (needle == "") [] else [for (entry in blocks(doc),
    let runs = leaves(entry.node,entry.path))
    for (offset in decorations.find_in_leaf(doc_text(entry.node),needle))
    text_selection(offset_pos(runs,offset),offset_pos(runs,offset+len(needle)))]
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
