import m: ~~.~~.demo.slides.mod_model
import resources: ~~.~~.demo.mod_resources

let deck = <presentation master:"brand",
    <master id:"brand",<text id:"footer",role:'footer',"Shared footer">>
    <slide id:"a",<group id:"group",x:100.0,y:100.0,width:500.0,height:300.0,
        <text id:"child",x:20.0,y:30.0,width:200.0,height:80.0,<b "Rich content">>
        <shape id:"other",x:250.0,y:30.0,width:80.0,height:80.0>>
        <cue start:'entry',<parallel <effect target:"child",kind:'fade-in'><effect target:"other",kind:'zoom-in'>>>
        <notes "Keep these notes">>
    <slide id:"b",<text id:"body","Still inherits">>>
let start = {*:m.initial(deck,"file:///old/"),selected:"s0-o1-0"}
let changed = m.properties(start,{x:55.0})^
let reordered = m.reorder(changed,true)^
let rich = m.set_text(reordered,"<b \"Revised rich text\">",true)^
let deleted = m.remove_object({*:rich,selected:"s0-o1"})^
let clone_base = m.initial(<presentation <slide
    <group id:"g",<text id:"child","One">><text id:"copy-1-0","Already taken">>>)
let cloned = m.paste({*:clone_base,selected:"s0-o0"},m.scene(clone_base).objects[0].source)^
let opened = m.open_deck(start,m.starter(),"file:///new/")^
let restored = m.history(opened)
let picture = <presentation <master id:"photo",<image src:"images/a.png",width:200.0,height:100.0>>
    <slide master:"photo",<group <image src:"../b.png">>>>
let resolved = resources.slide_sources(picture,"file:///slides/decks/")
let checks = [
    m.chosen(changed).x == 55.0,
    content(m.chosen(changed).source)[0] == <b "Rich content">,
    m.scenes(changed.doc)[0].master == false,
    changed.plan.slides[1].objects[0].id == "footer",
    len(content(changed.doc)) == len(content(deck)),
    m.chosen(reordered).id == "child" and m.chosen(reordered).dom_key == "s0-o1-1",
    m.text(m.chosen(rich).source) == "Revised rich text",
    len(m.scene(deleted).cues) == 0 and len(m.scene(deleted).objects) == 1,
    m.notes(deleted) == "Keep these notes",
    len(m.scene(cloned).targets) == 5,
    m.scene(cloned).objects[2].source.id == "copy-2",
    restored.doc == start.doc and restored.base == "file:///old/",
    m.properties(changed,{width:-1.0}) ^ { true } ~ { false },
    m.set_text(rich,"<b",true) ^ { true } ~ { false },
    content(resolved)[0][0].src == "file:///slides/decks/images/a.png",
    content(resolved)[1][0][0].src == "file:///slides/b.png",
    picture[0][0].src == "images/a.png"
];
checks
