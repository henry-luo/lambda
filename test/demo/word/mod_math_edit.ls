// Word's in-place equation caret. The field tree is reparsed from the stored source after every
// edit, and each edit is one equation.format transaction, so undo, the LaTeX pane and exports agree.
import lambda.editor.mod_doc
import lambda.editor.mod_source_pos
import image: lambda.editor.mod_image
import field: .mod_math_field
import equation: .mod_equation

pub fn active(st) => st.math!=null and st.math.path==image.selected(st.editor).path and
    st.math.revision==st.editor.model_revision
// a caret session on the selected equation; point is the click as fractions of the image box.
pub fn start(editor,point=null,caret=null) {
    let selected = image.selected(editor)
    let p = equation.settings(selected.node)
    let parsed = if (p==null or p.linear) null else (field.parse_field(p.source) ^ { null })
    let geometry = if (parsed==null) null else (equation.caret_geometry(p,parsed.row) ^ { null });
    if (geometry==null) null else {path:selected.path,revision:editor.model_revision,row:parsed.row,geometry:geometry,
        caret:if (caret!=null) field.settle(parsed.row,caret)
            else if (point!=null) field.nearest(geometry.slots,point.x*geometry.width,point.y*geometry.height)
            else field.positions(parsed.row)[last]}
}
pub fn source(row) => field.source(row)
// the caret line as percentages of the equation box, which the object frame overlays.
pub fn caret(st) {
    let g = if (active(st)) field.caret_box(st.math.geometry.slots,st.math.caret) else null
    let box = st.math.geometry;
    if (g==null) null else {left:100*g.x/box.width,top:100*g.top/box.height,height:100*(g.bottom-g.top)/box.height}
}
fn step(m,dir) {
    let next = field.move(m.row,m.caret,dir);
    if (next==null) {kind:"leave",after:dir=="right"} else {kind:"move",caret:next}
}
// one key → move, edit (new tree and caret), exit (keep the equation selected) or leave (caret beside it).
pub fn key(st,evt) {
    let m = st.math
    let k = evt.key;
    if (evt.metaKey==true or evt.ctrlKey==true or evt.altKey==true) null
    else if (k=="ArrowLeft") step(m,"left")
    else if (k=="ArrowRight") step(m,"right")
    else if (k=="Home" or k=="End") {kind:"move",caret:field.move(m.row,m.caret,if (k=="Home") "home" else "end")}
    else if (k=="ArrowUp" or k=="ArrowDown") {kind:"move",caret:field.vertical(m.row,m.geometry.slots,m.caret,k=="ArrowDown") or m.caret}
    else if (k=="Backspace" or k=="Delete") {kind:"edit",*:field.erase(m.row,m.caret,k=="Delete")}
    else if (k=="Escape") {kind:"exit"}
    else if (k=="Enter" or k=="Tab") {kind:"leave",after:true}
    else if (len(k)==1) {kind:"edit",*:field.type_text(m.row,m.caret,k)}
    else null
}
// leaving puts a text caret on the equation's side, inside a neighbouring run when there is one.
pub fn beside(editor,after) {
    let path = image.selected(editor).path
    let parent = parent_path(path)
    let i = last_index(path) + (if (after) 1 else -1)
    let sibling = node_at(editor.doc,parent).content[i]
    let point = if (sibling!=null and is_text(sibling)) pos([*parent,i],if (after) 0 else len(sibling.text))
        else pos(parent,last_index(path)+(if (after) 1 else 0));
    text_selection(point,point)
}
// ribbon structures arrive with empty slots and the caret in the first one, as Word inserts them.
pub fn insert(st,entry) map^ {
    let parts = split(entry.template or "","#")
    let fragment = if (entry.token!=null) entry.token ++ field.caret_mark else parts[0] ++ field.caret_mark ++ (parts[1] or "")
    field.insert_source(st.math.row,st.math.caret,fragment)^
}
