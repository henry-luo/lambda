import m: ~~.~~.demo.slides.mod_model
import ins: ~~.~~.demo.slides.mod_insert
import c: lambda.slide.common

fn page(id) => <slide id:id,title:id,<text id:"label",id><notes "Notes " ++ id>>
fn ids(st) => [for (s in st.plan.slides) s.id]
let start = m.initial(<presentation title:"Reorder",*[for (id in ["a","b","c","d","e"]) page(id)]>)
let down = m.reorder_slide(start,0,3)^
let up = m.reorder_slide(down,4,0)^
let undone = m.history(up)
let redone = m.history(undone,true)
let numbered = ins.insert(start,"number",{all_slides:true})^
let moved_numbers = m.reorder_slide(numbered,0,4)^;
[
    ids(down)==["b","c","d","a","e"],
    down.index==3,
    m.scenes(down.doc)[3]==m.scenes(start.doc)[0],
    len(down.undo)==1,
    ids(up)==["e","b","c","d","a"],
    up.index==0,
    len(up.undo)==2,
    undone.doc==down.doc and undone.index==3,
    redone.doc==up.doc and redone.index==0,
    m.reorder_slide(up,0,0)^==up,
    m.reorder_slide(up,-1,2)^==up,
    ids(m.reorder_slide(start,2,100)^)==["a","b","d","e","c"],
    ids(m.reorder_slide(start,3,-100)^)==["d","a","b","c","e"],
    m.move_slide(start,3)^.doc==down.doc,
    all([for (i,s in m.scenes(moved_numbers.doc)) m.text(c.children(s,'text')[1])==string(i+1)]),
    m.initial(parse(format(up.doc,'mark'),'mark')^).doc==up.doc
]
