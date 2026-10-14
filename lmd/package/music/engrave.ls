import c: .common
import d: .draw
import notes: .notes
import decoration: .decorations
import spans: .spans
import lyric_draw: .lyric_draw

fn beam_id(system,index) => "beam/"++string(system.index)++"/"++string(index)
// shared stems/beams follow any sounding member of their chord or beam group.
pub fn highlight_bindings(layout) map => map([for (system in layout.systems,e in system.events)
    (e.id,unique([e.id,e.anchor,*[for (i,ids in system.beams where contains(ids,e.anchor)) beam_id(system,i)],
        *(layout.lyric_bindings[e.id] or [])]))])

fn system_scene(system,layout) map^ {
    let profile=layout.profile
    let beams=[for (ids in system.beams) notes.beam_geometry(ids,system.events,profile)^]
    let events=[for (e in system.events) (
        let shapes=d.move_all(notes.draw(e,system.events,beams,profile)^,0.0,system.y),
        {kind:"event",id:e.id,part:e.part,staff:e.staff,voice:e.voice,onset:e.onset,
         description:if (e.rest) e.note_type++" rest" else e.pitch.step++string(e.pitch.octave)++" "++e.note_type,
         shapes:shapes,bounds:if (len(shapes)>0) d.union([for (s in shapes) s.bounds]) else d.bounds(e.x,e.y+system.y,e.x,e.y+system.y)})];
    {page:system.page,index:system.index,events:events,
     shapes:[*d.move_all(decoration.system(system,layout.parts,profile)^,0.0,system.y),
        *[for (i,b in beams) {kind:"beam",id:beam_id(system,i),description:"Shared beam",
            shapes:d.move_all(notes.beams(b,profile),0.0,system.y)}],*events]}
}
pub fn finish(layout) map^ {
    let scenes=[for (s in layout.systems) system_scene(s,layout)^]
    let connections=spans.resolve(layout.score)^
    let segments=[for (s in connections) *spans.draw(s,layout)^]
    let lyrics=lyric_draw.build(layout)^
    let titles=if (layout.options.show_title) [
        d.text(layout.profile,layout.score.title,layout.pages[0].width/2,4.0,2.4,"middle")^,
        d.text(layout.profile,layout.score.composer,layout.pages[0].width-3.0,7.0,1.4,"end")^] else []
    let unit=layout.unit
    let warnings=c.warnings([*layout.diagnostics,*decoration.warnings(layout.score)],layout.options)^;
    {*:layout,diagnostics:warnings,pages:[for (page in layout.pages) {*:page,shapes:[
        *if (page.index==0) titles else [],*[for (s in scenes where s.page==page.index) *s.shapes],
        *[for (segment in segments where segment.page==page.index) *segment.shapes],
        *[for (g in lyrics where g.page==page.index) g]]}],
     lyric_bindings:map([for (system in layout.systems,e in system.events)
        (e.id,[for (g in lyrics where contains(g.members,e.id)) g.id])]),
     lyric_passes:map([for (g in lyrics) (g.id,g.passes)]),
     events:map([for (s in scenes,e in s.events) (
        let bounds=d.union([e.bounds,*[for (g in lyrics where g.system==s.index and contains(g.members,e.id)) g.bounds]]),
        (e.id,{page:s.page,system:s.index,part:e.part,staff:e.staff,voice:e.voice,onset:e.onset,
            bounds:d.bounds(bounds.left*unit,bounds.top*unit,bounds.right*unit,bounds.bottom*unit)}))])}
}
