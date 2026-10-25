import m: ~~.~~.demo.slides.mod_model
import slide: lambda.slide

let start = m.initial()
let added = m.add_object(start,"text")^
let edited = m.set_text(added,"Edited title")^
let moved = m.properties(edited,{x:300.0,y:200.0,width:420.0})^
let undone = m.history(moved)
let redone = m.history(undone,true)
let duplicated = m.paste(redone,m.chosen(redone).source)^
let animated = m.animate(duplicated,"fade-in")^
let removed = m.remove_object(animated)^
let extra = m.add_slide(removed,true)^
let reordered = m.move_slide(extra,-1)^
let deleted = m.delete_slide(reordered)^;
[
    len(start.plan.slides) == 2,
    m.chosen(added).tag == 'text',
    m.text(m.chosen(edited).source) == "Edited title",
    m.chosen(moved).x == 300.0,
    undone.doc == edited.doc,
    redone.doc == moved.doc,
    len(m.scene(duplicated).objects) == len(m.scene(redone).objects)+1,
    len(m.scene(animated).cues) == 1,
    len(m.scene(removed).cues) == 0,
    len(extra.plan.slides) == 3 and extra.index == 1,
    reordered.index == 0 and reordered.plan.slides[0].id == extra.plan.slides[1].id,
    len(deleted.plan.slides) == 2,
    len((m.delete_slide(m.initial(<presentation <slide>>))^).plan.slides) == 1,
    m.initial(parse(format(edited.doc,'mark'),'mark')^).doc == edited.doc
]
