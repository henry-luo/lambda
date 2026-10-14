// Endpoint keys describe notation identity, never screen position or playback time.
import c: .common
import t: .time
import d: .draw
import l: .layout

fn marks(event) => (
    let voice=event.part++"/"++string(event.staff)++"/"++event.voice,
    let ties=unique([*event.notation.ties,*event.notation.tied]),
    [*[for (action in ["stop","start"] where contains(ties,action))
        {kind:"tie",action:action,key:voice++"/tie/"++event.pitch.step++string(event.pitch.octave)++"/"++string(event.pitch.alter),id:event.id}],
     *[for (s in event.notation.slurs) {kind:"slur",action:s.kind,key:voice++"/slur/"++s.number,id:event.id,placement:s.placement}],
     *[for (s in event.notation.tuplets) {kind:"tuplet",action:s.kind,key:voice++"/tuplet/"++s.number,id:event.id,
        bracket:s.bracket,label:string(event.time_modification.actual)}]])
fn connect(acc,mark) map^ {
    let opened=acc.open[mark.key]
    let valid=c.require(contains(["start","stop","continue"],mark.action) and
        (if (mark.action=="start") opened==null else opened!=null),"unmatched "++mark.kind++" endpoint: "++mark.id)^;
    let contiguous=c.require(mark.kind!="tie" or mark.action!="stop" or
        t.add(opened.position,opened.duration)^==mark.position,"tie endpoints are not consecutive: "++mark.id)^;
    {open:{*:acc.open,[mark.key]:if (mark.action=="stop") null else if (mark.action=="start") mark else opened},
     spans:if (mark.action=="stop") [*acc.spans,{*:opened,start:opened.id,end:mark.id,hook:mark.hook}] else acc.spans}
}
pub fn resolve(score) array^ {
    let notes=[for (p in score.parts,bar in p.measures,e in bar.events,mark in marks(e))
        {*:mark,position:t.add(bar.start,e.onset)^,duration:e.duration}]
    let wedges=[for (p in score.parts,bar in p.measures,i,dir in bar.directions,e in dir.entries where e.kind=="wedge")
        {kind:"wedge",action:if (contains(["crescendo","diminuendo"],e.value)) "start" else e.value,
         key:p.id++"/"++string(dir.staff)++"/wedge/"++e.number,
         id:p.id++"/measure/"++string(bar.index)++"/direction/"++string(i),placement:dir.placement,value:e.value}]
    let endings=[for (p in score.parts,bar in p.measures,i,b in bar.barlines where b.ending!=null)
        {kind:"ending",action:if (b.ending.type=="discontinue") "stop" else b.ending.type,
         key:p.id++"/ending/"++b.ending.number,label:b.ending.number,hook:b.ending.type!="discontinue",
         id:p.id++"/measure/"++string(bar.index)++"/ending/"++string(i)}]
    let result=c.fold([*notes,*wedges,*endings],{open:{},spans:[]},(acc,mark)=>connect(acc,mark)^)
    let closed=c.require(all([for (k,v in result.open) v==null]),"unclosed tie, slur, tuplet, or hairpin")^
    result.spans
}
pub fn positioned(layout,id) => [for (system in layout.systems,e in [*system.events,*system.directions] where e.id==id)
    {*:e,system:system.index}][0]
pub fn draw(span,layout) array^ {
    let a=positioned(layout,span.start)
    let b=positioned(layout,span.end)
    let valid=c.require(a==null and b==null or a!=null and b!=null and a.system<=b.system,
        "spanner endpoints are not in display order")^
    let profile=layout.profile;
    if (a==null) [] else [for (system in layout.systems where system.index>=a.system and system.index<=b.system) (
        let first_segment=system.index==a.system,
        let end_segment=system.index==b.system,
        let x1=if (first_segment) a.x+(if (contains(["wedge","ending"],span.kind)) 0.0 else a.glyph.advance*0.6) else system.left+0.5,
        let x2=if (end_segment) b.x+(if (contains(["wedge","ending"],span.kind)) 0.0 else b.glyph.advance*0.4) else system.left+system.width-0.5,
        let below=span.placement=="below" or span.kind=="wedge" and span.placement!="above" or
            contains(["tie","slur"],span.kind) and span.placement==null and a.stem_direction=="up",
        let sign=if (below) 1.0 else -1.0,
        let staff_y=l.staff_y(system.staffs,a.part,a.staff),
        let y1=staff_y+(if (span.kind=="wedge") (if (below) 6.5 else -3.0) else if (span.kind=="tuplet") -2.7
            else if (first_segment or span.kind=="tie") a.local_y+sign*0.8 else if (below) 5.2 else -1.2),
        let y2=staff_y+(if (span.kind=="wedge" or span.kind=="tuplet") y1-staff_y
            else if (end_segment or span.kind=="tie") b.local_y+sign*0.8 else if (below) 5.2 else -1.2),
        let shapes=if (span.kind=="ending") [
            d.line(x1,staff_y-4.0,x2,staff_y-4.0,0.12),
            *if (first_segment) [d.line(x1,staff_y-4.0,x1,staff_y-2.5,0.12),
                d.text(profile,span.label++".",x1+0.5,staff_y-2.6,1.2)^] else [],
            *if (end_segment and span.hook) [d.line(x2,staff_y-4.0,x2,staff_y-2.5,0.12)] else []]
        else if (span.kind=="wedge") (
            let wide=span.value=="crescendo",
            let h1=if (wide and first_segment) 0.0 else 0.45,
            let h2=if (not wide and end_segment) 0.0 else 0.45,
            [d.line(x1,y1-h1,x2,y2-h2,0.12),d.line(x1,y1+h1,x2,y2+h2,0.12)])
        else if (span.kind=="tuplet") [
            d.text(profile,span.label,(x1+x2)/2,y1,1.3,"middle")^,
            *if (span.bracket=="no") [] else [d.line(x1,y1-0.4,(x1+x2)/2-0.8,y1-0.4,0.12),
                d.line((x1+x2)/2+0.8,y1-0.4,x2,y1-0.4,0.12),d.line(x1,y1-0.4,x1,y1+0.3,0.12),d.line(x2,y1-0.4,x2,y1+0.3,0.12)]]
        else [d.curve(x1,y1,x2,y2,sign*min(2.0,max(0.8,(x2-x1)*0.1)),
            if (span.kind=="tie") profile.rules.tieMidpointThickness else profile.rules.slurMidpointThickness)],
        {page:system.page,system:system.index,shapes:d.move_all(shapes,0.0,system.y)})]
}
