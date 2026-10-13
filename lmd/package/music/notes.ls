// Beam planes, chord stems and glyph anchors share one geometry calculation.
import c: .common
import f: .fonts
import n: .notation
import d: .draw

pub fn stem(event,events,profile) map {
    let chord=[for (e in events where e.anchor==event.anchor) e]
    let anchor=[for (e in chord where not e.chord) e][0]
    let up=anchor.stem_direction=="up"
    let attachment=profile.anchors[anchor.symbol][if (up) "stemUpSE" else "stemDownNW"] or
        [if (up) anchor.glyph.advance else 0.0,0.0]
    let low=max([for (e in chord) e.y])
    let high=min([for (e in chord) e.y]);
    {x:anchor.x-anchor.dx+attachment[0]*anchor.visual_scale,
        y:(if (up) low else high)-attachment[1]*anchor.visual_scale,
        tip:if (up) high-3.5*anchor.visual_scale else low+3.5*anchor.visual_scale,up:up}
}
pub fn beam_geometry(ids,events,profile) map^ {
    let group=[for (id in ids) [for (e in events where e.id==id) e][0]]
    let valid=c.require(len(group)>=2 and all([for (e in group) e.stem_direction==group[0].stem_direction and
        e.staff==group[0].staff and e.part==group[0].part]),"cross-staff or mixed-stem beam is unsupported")^
    let stems=[for (e in group) stem(e,events,profile)]
    let a=stems[0]
    let b=stems[len(stems)-1]
    let slope=max(-0.25,min(0.25,(b.tip-a.tip)/(b.x-a.x)))
    let origins=[for (s in stems) s.tip-slope*(s.x-a.x)]
    let origin=if (a.up) min(origins) else max(origins);
    {ids:ids,events:group,up:a.up,x:a.x,y:origin,slope:slope,stems:stems}
}
pub fn beam_y(beam,x) => beam.y+beam.slope*(x-beam.x)
fn beam_segment(beam,x1,x2,level,profile) => (
    let sign=if (beam.up) 1.0 else -1.0,
    let offset=float(level-1)*(profile.rules.beamThickness+profile.rules.beamSpacing)*sign,
    let y1=beam_y(beam,x1)+offset,
    let y2=beam_y(beam,x2)+offset,
    let thickness=profile.rules.beamThickness*sign,
    d.polygon([[x1,y1],[x2,y2],[x2,y2+thickness],[x1,y1+thickness]]))
pub fn beams(beam,profile) array {
    [beam_segment(beam,beam.stems[0].x,beam.stems[len(beam.stems)-1].x,1,profile),
     *[for (i,e in beam.events,b in e.beams where b.level>1 and b.kind!="end" and
        (i+1<len(beam.events) or contains(["forward hook","backward hook"],b.kind))) (
        let x=beam.stems[i].x,
        let next=beam.stems[i+1],
        if (b.kind=="forward hook" or b.kind=="backward hook")
            beam_segment(beam,x,x+(if (b.kind=="forward hook") 1.1 else -1.1),b.level,profile)
        else beam_segment(beam,x,next.x,b.level,profile))]]
}
let articulations={accent:"Accent",staccato:"Staccato",tenuto:"Tenuto",staccatissimo:"Staccatissimo",'strong-accent':"Marcato"}
pub fn draw(event,events,beam_list,profile) array^ {
    let e=event
    let scale=e.visual_scale
    let s=stem(e,events,profile)
    let beam=[for (b in beam_list where contains(b.ids,e.anchor)) b][0]
    let tip=if (beam==null) s.tip else beam_y(beam,s.x)
    let has_stem=not e.rest and not e.chord and e.stem_direction!="none" and not contains(["long","breve","whole"],e.note_type)
    let ledgers=if (e.rest) [] else if (e.local_y<0) [for (y in 1 to int(floor(-e.local_y))) -float(y)]
        else if (e.local_y>4) [for (y in 5 to int(floor(e.local_y))) float(y)] else []
    let accidental=if (e.accidental_glyph==null) null else f.glyph(profile,e.accidental_glyph)^
    let flags=n.flag_count(e)
    let below=e.stem_direction=="up"
    let articulation_y=if (below) max(4.8,e.local_y+1.0) else min(-0.8,e.local_y-1.0)
    let lyric_y=max(7.5,e.local_y+3.0);
    if (not e.visible) [] else [
        *[for (y in ledgers) d.line(e.x-profile.rules.legerLineExtension,e.staff_y+y,
            e.x+e.glyph.advance*scale+profile.rules.legerLineExtension,e.staff_y+y,profile.rules.legerLineThickness)],
        d.glyph(profile,e.symbol,e.x,e.y,scale)^,
        *if (accidental!=null) [d.glyph(profile,e.accidental_glyph,e.x-e.dx-e.accidental_dx,e.y,scale)^] else [],
        *[for (i in 0 to (e.dots-1)) d.glyph(profile,"augmentationDot",e.x+e.glyph.advance*scale+0.4+float(i)*0.6,
            e.y-(if (e.local_y==floor(e.local_y)) 0.5 else 0.0),scale)^],
        *if (has_stem) [d.line(s.x,s.y,s.x,tip,profile.rules.stemThickness*scale)] else [],
        *if (has_stem and e.grace_slash) [d.line(s.x-0.5,tip+1.0,s.x+0.7,tip+0.3,0.12)] else [],
        *if (has_stem and beam==null and flags>0) [d.glyph(profile,"flag"++["8th","16th","32nd","64th"][flags-1]++
            (if (s.up) "Up" else "Down"),s.x,tip,scale)^] else [],
        *[for (i,a in e.notation.articulations where articulations[a]!=null)
            d.glyph(profile,"artic"++articulations[a]++(if (below) "Below" else "Above"),e.x+e.glyph.advance/2,
                e.staff_y+articulation_y+(if (below) 1.0 else -1.0)*float(i))^],
        *if (e.notation.fermata) [d.glyph(profile,"fermataAbove",e.x+e.glyph.advance/2,e.staff_y+min(-2.0,e.local_y-2.0))^] else [],
        *[for (i,l in e.lyrics) d.text(profile,l.text,e.x+e.glyph.advance/2,e.staff_y+lyric_y+float(i)*1.7,1.4,"middle")^],
        *[for (i,l in e.lyrics where contains(["begin","middle"],l.syllabic))
            d.text(profile,"-",e.x+e.glyph.advance/2+e.lyric_metrics[i].width/2+0.8,e.staff_y+lyric_y+float(i)*1.7,1.4,"middle")^]]
}
