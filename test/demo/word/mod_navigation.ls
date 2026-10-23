// Navigation reads source structure; pane choices never enter document history (D7.2.5).
import lambda.editor.mod_doc
import lambda.editor.mod_source_pos
import dom
import history: lambda.dom.edit_history
import model: .mod_model
import page: .mod_page

pub let tabs = [{id:"headings",label:"Headings"},{id:"pages",label:"Pages"},{id:"results",label:"Results"}]
pub fn next_tab(tab,key) {
    let index = [for (i,choice in tabs where choice.id==tab) i][0] or 0
    tabs[if (key=="Home") 0 else if (key=="End") len(tabs)-1
        else (index+(if (key=="ArrowLeft") -1 else 1)+len(tabs))%len(tabs)].id
}
pub fn pages(doc) {
    let starts = page.starts(doc);
    [for (i,children in page.sheets(doc)) {number:i+1,start:starts[i],content:children}]
}
pub fn current_page(editor) {
    let index = model.selection_path(editor.selection)[0] or 0;
    [for (entry in pages(editor.doc) where entry.start<=index) entry.number][last] or 1
}
pub fn current_heading(editor) => [for (heading in model.headings(editor.doc)
    where path_compare(heading.path,model.selection_path(editor.selection))<=0) heading.path][last]
pub fn page_position(doc,page_number) {
    let entry = if (page_number is int and page_number>=1) pages(doc)[page_number-1] else null
    if (entry==null) null else {
        let point = model.first_pos(node('doc',entry.content));
        pos([entry.start+(point.path[0] or 0),*slice(point.path,1)],point.offset)
    }
}
pub fn excerpt(doc,hit) {
    let block = model.current_block({doc:doc,selection:hit})
    let words = doc_text(block.node)
    let offset = sum([for (leaf in model.leaves(block.node,block.path)
        where path_compare(leaf.path,hit.anchor.path)<0) len(leaf.leaf.text)])+hit.anchor.offset
    let end = sum([for (leaf in model.leaves(block.node,block.path)
        where path_compare(leaf.path,hit.head.path)<0) len(leaf.leaf.text)])+hit.head.offset
    let start = max(0,offset-28)
    let finish = min(len(words),end+55);
    {before:(if (start>0) "…" else "") ++ slice(words,start,offset),match:slice(words,offset,end),
        after:slice(words,end,finish) ++ (if (finish<len(words)) "…" else "")}
}
fn leaf_text_node(node) => if (node==null or dom.node_type(node)==3) node else leaf_text_node(dom.child_nodes(node)[0])
fn rendered_point(editor,host,point) {
    let leaves = model.leaves(editor.doc)
    let index = [for (i,leaf in leaves where leaf.path==point.path) i][0]
    let span = if (index==null) null else dom.query_selector_all(host,".word-text-run")[index]
    let text_node = leaf_text_node(span);
    if (text_node==null) null else {node:text_node,offset:history.utf16_from_codepoint(leaves[index].leaf.text,point.offset,0,0)}
}
pub pn select_readonly(editor,host) {
    // model-edit completion validates an editable host; reading uses the ordinary Selection API.
    let anchor = rendered_point(editor,host,editor.selection.anchor)
    let head = rendered_point(editor,host,editor.selection.head)
    if (anchor!=null and head!=null)
        dom.set_base_and_extent(dom.document_selection(host),anchor.node,anchor.offset,head.node,head.offset)
}
