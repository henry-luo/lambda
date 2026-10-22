// Text revisions are source marks; accepting/rejecting edits never restores an old document (S1.4, D7.2.5).
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_source_pos
import lambda.editor.mod_transaction
import inline_format: lambda.editor.mod_inline_format
import rendering: lambda.edit.model
import objects: .mod_objects

pub let defaults = {enabled:false,author:"Me"}
pub fn settings(doc) {
    let p = objects.settings(doc,"data-word-tracking",defaults)
    if (p.enabled is bool and p.author is string and trim(p.author)!="") p else defaults
}
fn span(marks) => if (mark_value(marks,'span') is array) mark_value(marks,'span') else []
pub fn change_id(marks) => attrs_get(span(marks),'data-word-change')
pub fn change_kind(marks) => attrs_get(span(marks),'data-word-change-kind')
pub fn deleted(marks) => change_kind(marks)=="delete"
fn author(marks) => attrs_get(span(marks),'data-word-change-author')
fn clean_marks(marks) {
    let attrs = [for (a in span(marks) where not contains(['data-word-change','data-word-change-kind','data-word-change-author'],a.name)) a]
    if (len(attrs)==0) without_mark(marks,'span') else with_mark(marks,'span',attrs)
}
fn revision_marks(marks,id,kind,by) => with_mark(clean_marks(marks),'span',
    [*span(clean_marks(marks)),{name:'data-word-change',value:id},{name:'data-word-change-kind',value:kind},{name:'data-word-change-author',value:by}])
pub fn entries(doc) {
    let leaves = [for (entry in inline_format.leaves(doc) where change_id(entry.leaf.marks)!=null) entry]
    let ids = unique([for (entry in leaves) change_id(entry.leaf.marks)]);
    [for (id in ids) {id:id,author:author([for (entry in leaves where change_id(entry.leaf.marks)==id) entry.leaf.marks][0]),
        inserted:join([for (entry in leaves where change_id(entry.leaf.marks)==id and change_kind(entry.leaf.marks)=="insert") entry.leaf.text],""),
        removed:join([for (entry in leaves where change_id(entry.leaf.marks)==id and deleted(entry.leaf.marks)) entry.leaf.text],"")}]
}
pub fn selection(doc,id) {
    let leaves = [for (entry in inline_format.leaves(doc) where change_id(entry.leaf.marks)==id) entry]
    if (len(leaves)==0) null else text_selection(pos(leaves[0].path,0),pos(leaves[last].path,len(leaves[last].leaf.text)))
}
pub fn configure(editor,p) map^ {
    if (not (p.enabled is bool) or not (p.author is string) or trim(p.author)=="" or len(split(p.author,""))>80)
        raise error("Word: enter an author name (1–80 characters)")
    else tx_step(tx_begin(editor.doc,editor.selection),step_set_attr([],'data-word-tracking',format({*:p,author:trim(p.author)},'json')^))
}
let text_tags = ['p','h1','h2','h3','h4','h5','h6','td','th','pre']
fn flat(n) => is_node(n) and contains(text_tags,n.tag) and all([for (child in n.content) is_text(child)])
// Replacing paragraphs or inline objects is outside text tracking; reject rather than silently lose revisions.
fn structure(n) => if (is_text(n)) n.text else if (flat(n)) {tag:n.tag,content:"text"}
    else {tag:n.tag,content:[for (child in n.content) structure(child)]}
fn common_edge(a,b,prefix=0,reverse=false) {
    let size = min(len(a),len(b))-prefix
    let different = [for (i in 0 to size-1 where (if (reverse) a[len(a)-1-i]!=b[len(b)-1-i] else a[i]!=b[i])) i]
    if (len(different)==0) size else different[0]
}
fn segment(children,start_at,end_at) => [for (i,child in children,let offset=sum([for (j in 0 to i-1) len(children[j].text)]),
    let a=max(0,start_at-offset),let b=min(len(child.text),end_at-offset) where b>a) text_marked(slice(child.text,a,b),child.marks)]
fn length_of(children) => sum([for (child in children) len(child.text)])
fn point_offset(children,path,point) => sum([for (i in 0 to point.path[len(path)]-1) len(children[i].text)])+point.offset
fn point_at(children,path,offset,i=0) => if (i>=len(children)-1 or offset<len(children[i].text))
    pos([*path,i],min(offset,len(children[i].text))) else point_at(children,path,offset-len(children[i].text),i+1)
fn remap_point(point,path,before,after,at,extra) {
    if (not path_is_prefix(path,point.path) or len(point.path)!=len(path)+1) point else {
        let offset = point_offset(before,path,point)
        point_at(after,path,offset+if (offset>=at) extra else 0)
    }
}
fn track_block(tx,path,before,after,by) {
    let a = split(doc_text(before),"")
    let b = split(doc_text(after),"")
    let prefix = common_edge(a,b)
    let suffix = common_edge(a,b,prefix,true)
    let at = len(join(take(a,prefix),""))
    let old_end = len(join(take(a,len(a)-suffix),""))
    let new_end = len(join(take(b,len(b)-suffix),""))
    let removed = segment(before.content,at,old_end)
    let added = segment(after.content,at,new_end)
    let existing = [for (child in [*removed,*added]) child.marks][0]
    let reuse = change_kind(existing)=="insert" and author(existing)==by and
        contains([for (entry in entries(tx.doc_before)) entry.id],change_id(existing))
    let seq = (attrs_get(tx.doc_after.attrs,'data-word-change-seq') or 0)+1
    let id = if (reuse) change_id(existing) else "r" ++ string(seq)
    // Deleting pending insertions cancels them; existing deletions remain until review resolves them.
    let kept = [for (child in removed where change_kind(child.marks)!="insert")
        if (deleted(child.marks)) child else text_marked(child.text,revision_marks(child.marks,id,"delete",by))]
    let inserted = [for (child in added) text_marked(child.text,revision_marks(child.marks,id,"insert",by))]
    let content = [*segment(before.content,0,at),*kept,*inserted,*segment(after.content,new_end,len(doc_text(after)))]
    let merged = rendering.merge_leaves(content)
    let children = if (len(merged)==0 or deleted(merged[last].marks)) [*merged,text("")] else merged
    let sel = tx.sel_after
    let next = tx_step(tx,step_replace(path,0,len(node_at(tx.doc_after,path).content),children))
    let selected = if (sel.kind!='text') next else tx_set_selection(next,text_selection(
        remap_point(sel.anchor,path,after.content,children,at,length_of(kept)),
        remap_point(sel.head,path,after.content,children,at,length_of(kept))))
    if (reuse or len(kept)+len(inserted)==0) selected else tx_step(selected,step_set_attr([],'data-word-change-seq',seq))
}
fn track_blocks(tx,entries,before,after,by,i) => if (i<0) tx else {
    let path = entries[i].path
    let old = node_at(before,path)
    let current = node_at(after,path)
    let changed = doc_text(old)!=doc_text(current)
    let next = if (changed) track_block(tx,path,old,current,by) else tx
    track_blocks(next,entries,before,after,by,i-1)
}
pub fn filter(editor,tx) map^ {
    let p = settings(tx.doc_before)
    if (not p.enabled or len(tx.steps)==0 or tx_get_meta(tx,"word-review")==true or
        tx_get_meta(tx,"composition").active==true) tx
    else if (structure(tx.doc_before)!=structure(tx.doc_after))
        raise error("Word: this structural edit cannot be tracked yet; turn off Track text changes to continue")
    else {
        let blocks = [for (entry in descendants(tx.doc_before) where flat(entry.node)) entry]
        track_blocks(tx,blocks,tx.doc_before,tx.doc_after,p.author,len(blocks)-1)
    }
}
fn resolve_leaves(tx,entries,id,accept,i) => if (i<0) tx else {
    let entry = entries[i]
    let matches = change_id(entry.leaf.marks)!=null and (id==null or change_id(entry.leaf.marks)==id)
    let remove = if (accept) deleted(entry.leaf.marks) else change_kind(entry.leaf.marks)=="insert"
    let next = if (not matches) tx else tx_step(tx,step_replace(parent_path(entry.path),last_index(entry.path),last_index(entry.path)+1,
        if (remove) [] else [text_marked(entry.leaf.text,clean_marks(entry.leaf.marks))]))
    resolve_leaves(next,entries,id,accept,i-1)
}
pub fn resolve(editor,id,accept) map^ {
    if (id!=null and not contains([for (entry in entries(editor.doc)) entry.id],id)) raise error("Word: this revision no longer exists")
    else {
        let leaves = inline_format.leaves(editor.doc)
        let tx = resolve_leaves(tx_set_meta(tx_begin(editor.doc,editor.selection),"word-review",true),leaves,id,accept,len(leaves)-1)
        let first = inline_format.leaves(tx.doc_after)[0]
        // resolving the only insertion leaves an empty block with a node-position caret.
        let target = if (first!=null) first.path else [for (entry in descendants(tx.doc_after) where flat(entry.node)) entry.path][0]
        if (target==null) tx else tx_set_selection(tx,text_selection(pos(target,0),pos(target,0)))
    }
}
pub let css = "
 .word-content [data-word-change-kind=insert] { color:#216b47; background:#e5f3e9; text-decoration:underline; }
 .word-content [data-word-change-kind=delete] { color:#ac3541; background:#fcebed; text-decoration:line-through; }
 .hide-changes [data-word-change-kind=delete] { display:none; }
 .hide-changes [data-word-change-kind=insert] { color:inherit; background:transparent; text-decoration:none; }
"
