// Shared character-format transactions, promoted from the Word demo (D7.2.5).
// Selection mapping keeps the selected characters intact when a leaf is split.
import .mod_doc
import .mod_step
import .mod_source_pos
import .mod_transaction

pub fn leaves(n,path=[]) => [for (entry in descendants(n,path) where is_text(entry.node)) {path:entry.path,leaf:entry.node}]

pub fn selection_path(sel) => if (sel.kind == 'text') sel.head.path else selection_paths(sel)[0] or []

pub fn current_marks(editor) array => if (editor.stored_marks != null) editor.stored_marks
    else node_at(editor.doc,selection_path(editor.selection)).marks or []
pub fn css_get(css,key) => [for (decl in split(css or "",";"),let pair=split(decl,":")
    where lower(trim(pair[0])) == key) trim(join(drop(pair,1),":"))][last]
pub fn css_set(css,key,value) => join([*[for (decl in split(css or "",";") where trim(decl) != "" and
    lower(trim(split(decl,":")[0])) != key) trim(decl)],
    *if (value == null or value == "") [] else [key ++ ": " ++ value]],"; ")
pub fn span_css(marks) string {
    let attrs = mark_value(marks,'span')
    if (attrs is array) attrs_get(attrs,'style') or "" else ""
}
pub fn char_value(editor,key,fallback) => css_get(span_css(current_marks(editor)),key) or fallback
pub fn styled_marks(marks,key,value) {
    let attrs = mark_value(marks,'span')
    let old = if (attrs is array) attrs else []
    with_mark(marks,'span',attrs_set(old,'style',css_set(attrs_get(old,'style'),key,value)))
}

pub fn leaf_segment(entry,sel) {
    if (sel.kind == 'all' or any([for (path in selection_paths(sel)) path_is_prefix(path,entry.path)])) {from:0,to:len(entry.leaf.text)}
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
pub fn transaction(editor,transform) {
    let sel = editor.selection
    let tx = tx_begin(editor.doc,sel)
    if (sel.kind == 'text' and pos_equal(sel.anchor,sel.head))
        tx_set_meta(tx_set_meta(tx,"storedMarks",transform(current_marks(editor))),"addToHistory",false)
    else {
        let entries = leaves(editor.doc)
        format_leaves(tx,entries,sel,transform,len(entries)-1)
    }
}
