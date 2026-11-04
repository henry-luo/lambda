// model projection retains one wrapper per text leaf for source-selection mapping (S2.6.5).
import rendering: lambda.editor.format.model

let factories = {
    p:(a,k)=><p *:a, *k>, h1:(a,k)=><h1 *:a, *k>, h2:(a,k)=><h2 *:a, *k>,
    h3:(a,k)=><h3 *:a, *k>, h4:(a,k)=><h4 *:a, *k>, h5:(a,k)=><h5 *:a, *k>, h6:(a,k)=><h6 *:a, *k>,
    ul:(a,k)=><ul *:a, *k>, ol:(a,k)=><ol *:a, *k>, li:(a,k)=><li *:a, *k>,
    table:(a,k)=><table *:a, *k>, thead:(a,k)=><thead *:a, *k>, tbody:(a,k)=><tbody *:a, *k>, tfoot:(a,k)=><tfoot *:a, *k>,
    tr:(a,k)=><tr *:a, *k>, th:(a,k)=><th *:a, *k>, td:(a,k)=><td *:a, *k>,
    blockquote:(a,k)=><blockquote *:a, *k>, pre:(a,k)=><pre *:a, *k>, code:(a,k)=><code *:a, *k>,
    span:(a,k)=><span *:a, *k>, strong:(a,k)=><strong *:a, *k>, b:(a,k)=><b *:a, *k>,
    em:(a,k)=><em *:a, *k>, i:(a,k)=><i *:a, *k>, u:(a,k)=><u *:a, *k>,
    del:(a,k)=><del *:a, *k>, s:(a,k)=><s *:a, *k>, sub:(a,k)=><sub *:a, *k>, sup:(a,k)=><sup *:a, *k>,
    hr:(a,k)=><hr>, br:(a,k)=><br>, img:(a,k)=><img *:a>,
    div:(a,k)=><div *:a, *k>
}
fn make(tag,attrs,kids,link_class) any^ {
    if (tag == 'a') <span *:attrs,class:link_class,title:attrs.title or attrs.href,*kids>
    else (let factory = factories[tag] or factories.div, factory(attrs,kids))
}
fn wrap_marks(marks,i,child,attributes,mark_attrs,link_class) any^ => if (i >= len(marks)) child else {
    let mark = marks[i]
    let attrs = if (mark.value is array) attributes(mark.value) else {};
    wrap_marks(marks,i+1,make(mark.name,mark_attrs(attrs),[child],link_class),attributes,mark_attrs,link_class)
}

// callers supply document-specific attribute and mark policy, not application state.
pub fn project(n,child,attributes,mark_attrs,text_class="editor-text-run",link_class="editor-link") any^ {
    if (n.kind=='text') <span class:text_class,wrap_marks(n.marks,0,if (n.text=="") "\u200B" else n.text,attributes,mark_attrs,link_class)>
    else if (n.kind=='node') make(n.tag,attributes(n.attrs),rendering.surface_children(n,child),link_class)
    else ""
}
