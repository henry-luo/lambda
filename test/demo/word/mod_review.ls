// Thread metadata and text marks travel with the immutable document (S1.4, D7.2.5).
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_transaction

pub fn comments(doc) => parse((attrs_get(doc.attrs,'data-word-comments') ^ { null }) or "[]",'json') ^ { [] }
pub fn next_id(doc) => "c" ++ string(((attrs_get(doc.attrs,'data-word-comment-seq') ^ { null }) or 0)+1)
fn span_attrs(marks) {
    let value = mark_value(marks,'span')
    if (value is array) value else []
}
pub fn ids(marks) => [for (id in split((attrs_get(span_attrs(marks),'data-word-comment') ^ { null }) or ""," ") where id!="") id]
pub fn has_anchor(marks,id) => (contains(ids(marks),id) ^ { null }) or false
pub fn anchor(marks,id) => with_mark(marks,'span',attrs_set(span_attrs(marks),'data-word-comment',join([*ids(marks),id]," ")))
pub fn without_anchor(marks,id) {
    let kept = [for (value in ids(marks) where value!=id) value]
    let attrs = attrs_set(span_attrs(marks),'data-word-comment',if (len(kept)==0) null else join(kept," "))
    if (len(attrs)==0) without_mark(marks,'span') else with_mark(marks,'span',attrs)
}
// clearing character formatting must retain review anchors.
pub fn clear_format(marks) => if (len(ids(marks))==0) [] else
    [mark('span',[{name:'data-word-comment',value:join(ids(marks)," ")}])]
pub fn with_comments(tx,entries) map^ => tx_step(tx,step_set_attr([],'data-word-comments',format(entries,'json')^))
pub fn add(tx,doc,entry) map^ => tx_step(with_comments(tx,[*comments(doc),entry])^,
    step_set_attr([],'data-word-comment-seq',((attrs_get(doc.attrs,'data-word-comment-seq') ^ { null }) or 0)+1))
pub fn css(doc) {
    let entries = comments(doc)
    let active = join([for (c in entries where not c.resolved) ":not([data-word-comment~=\"" ++ c.id ++ "\"])"],"")
    join([for (c in entries where c.resolved) ".word-content [data-word-comment~=\"" ++ c.id ++ "\"]" ++ active ++
        " { background-color:transparent; border-bottom-color:transparent; }"],"\n")
}
