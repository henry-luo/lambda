import m: ~~.~~.demo.slides.mod_model
import ins: ~~.~~.demo.slides.mod_insert
import data: ~~.~~.demo.slides.mod_insert_data
import slide: lambda.slide
import h: ~~.~~.demo.slides.mod_home

let base = m.initial()
fn added(kind,p={}) => ins.insert(base,kind,p)^
fn current(st) => m.chosen(st).source
fn saved(st) => ins.settings(current(st))
fn markup(st) => format(current(st),'xml')^
let table = added("table",{rows:2,columns:2,data:"Name\tCount\nA\t4"})
let moved = m.properties(table,{x:40.0,y:60.0,width:500.0})^
let edited = ins.insert(moved,"table",{rows:3,columns:2},moved.selected)^
let graphics = [for (kind in ["chart","smartart","wordart","shape","equation"]) {
    let st = added(kind);
    saved(st).kind == kind and contains(markup(st),"<svg")
}]
let picture = added("picture",{path:"fixtures/picture.svg"})
let shot = added("screenshot",{path:"fixtures/picture.svg"})
let zoom = added("zoom",{target:"story",preview:"data:image/png;base64,AAAA"})
let link = added("link",{target:"story",label:"Our story"})
let external = added("link",{href:"https://example.org",label:"Website"})
let action = added("action",{command:"end",label:"Finish"})
let commented = ins.insert(base,"comment",{author:"Ada",text:"Review this idea"})^
let noted = m.set_notes(commented,"Speaker only")^
let uncommented = ins.remove_comment(noted,ins.comments(noted)[0].id)^
let footer = added("footer",{header:"Internal",footer:"Team",date_text:"2026",show_number:true,all_slides:true})
let chosen_footer = {*:footer,selected:"s0-o4"}
let animated_footer = m.animate(chosen_footer,"fade-in")^
let revised_footer = ins.insert(animated_footer,"footer",{footer:"Revised",show_number:false,all_slides:true})^
let numbered = added("number",{all_slides:true})
let reordered = m.move_slide(numbered,1)^
let small_eq = added("equation",{source:"x^2",size:24})
let large_eq = ins.insert(small_eq,"equation",{source:"x^2",size:48},small_eq.selected)^
let raw = m.initial(<presentation <slide title:"First"> <slide title:"Second">>)
let pinned = ins.insert(raw,"link",{target:raw.plan.slides[1].id,label:"Second"})^
let html_object = added("object",{format:"html",source:"<article><h2>Embedded</h2></article>"})
let art_one = added("wordart",{style:"gradient"})
let art_two = ins.insert(art_one,"wordart",{style:"gradient"})^
let art_first_edit = ins.insert({*:art_two,selected:art_one.selected},"wordart",{style:"gradient",fill:"#ff0000"},art_one.selected)^
let art_second_edit = ins.insert({*:art_first_edit,selected:art_two.selected},"wordart",{style:"gradient",fill:"#00ff00"},art_two.selected)^
let art_ids = [for (obj in m.scene(art_second_edit).objects where ins.settings(obj.source).kind == "wordart") ins.settings(obj.source).params.graphic_id]
let art_reopened = ins.insert(m.initial(parse(format(art_second_edit.doc,'mark')^,'mark')^),"wordart",{style:"gradient"})^
let svg_object = added("object",{format:"svg",source:"<svg viewBox=\"0 0 20 20\"><rect width=\"20\" height=\"20\"/></svg>"});
{
    graphics:graphics,
    table:[not contains(markup(table),"<html"),contains(markup(table),"Name"),contains(markup(table),"<table"),saved(edited).params.rows == 3,
        current(edited).id == current(moved).id,m.chosen(edited).x == 40.0,m.chosen(edited).width == 500.0,
        m.history(edited).doc == moved.doc],
    drawing:[ins.settings(m.chosen(h.command(added("shape"),"fill:#ff0000")^).source).params.fill == "#ff0000"],
    pictures:[current(picture).src == "fixtures/picture.svg",saved(shot).kind == "screenshot"],
    navigation:[contains(markup(link),"data-slide-destination=\"story\""),contains(markup(external),"href=\"https://example.org\""),
        contains(markup(action),"data-slide-command=\"end\""),contains(markup(zoom),"data:image/png"),
        saved(zoom).params.preview == null,m.scenes(pinned.doc)[1].id == raw.plan.slides[1].id,len(pinned.undo) == 1],
    comments:[len(ins.comments(noted)) == 1,m.notes(noted) == "Speaker only",len(ins.comments(uncommented)) == 0,
        m.notes(uncommented) == "Speaker only",not contains(format(slide.snapshot(commented.doc)^,'html'),"Review this idea")],
    fields:[all([for (scene in footer.plan.slides) len([for (obj in scene.objects where starts_with(obj.source.class,"slides-footer-")) obj]) == 3]),
        len(m.scene(revised_footer).cues) == 1,
        all([for (scene in reordered.plan.slides) m.text([for (obj in scene.objects where obj.source.class == "slides-page-number") obj.source][0]) == string(scene.index+1)]),
        m.chosen(large_eq).height > m.chosen(small_eq).height],
    content:[saved(added("object")).kind == "object",contains(markup(html_object),"<article>"),contains(markup(svg_object),"<svg"),
        m.text(current(added("date",{text:"25 October 2026"}))) == "25 October 2026",
        m.text(current(added("symbol",{text:"∑"}))) == "∑"],
    media:[contains(markup(added("video",{path:"film.mp4"})),"<video"),contains(markup(added("audio",{path:"sound.mp3",loop:true})),"loop=\"true\"")],
    svg_ids:[len(unique(art_ids)) == 2,not contains(art_ids,saved(art_reopened).params.graphic_id)],
    roundtrip:[all([for (kind in ["chart","smartart","wordart","shape","equation"]) {
        let st = added(kind);
        m.initial(parse(format(st.doc,'mark')^,'mark')^).doc == st.doc
    }]),ins.settings(m.chosen(h.command(added("symbol"),"clear-format")^).source).kind == "symbol",saved({*:m.initial(parse(format(table.doc,'mark'),'mark')^),selected:table.selected}) == saved(table)],
    invalid:[slide.compile(<presentation <slide <text metadata:"bad","Text">>>) ^ {true} ~ {false},ins.insert(base,"table",{rows:0}) ^ {true} ~ {false},ins.insert(base,"chart",{data:"bad"}) ^ {true} ~ {false},
        ins.insert(base,"link",{href:"javascript:bad()"}) ^ {true} ~ {false},ins.insert(base,"comment",{text:""}) ^ {true} ~ {false},
        ins.insert(base,"object",{format:"svg",source:"<div/>"}) ^ {true} ~ {false},ins.insert(base,"action",{command:"bad"}) ^ {true} ~ {false}]
}
