import c: .common
import f: .fonts
import d: .draw
import l: .layout

let dynamics={p:"dynamicPiano",pp:"dynamicPP",ppp:"dynamicPPP",mp:"dynamicMP",mf:"dynamicMF",f:"dynamicForte",
    ff:"dynamicFF",fff:"dynamicFFF",sfz:"dynamicSforzato",sffz:"dynamicSforzatoFF",fp:"dynamicFortePiano"}
pub fn direction(dir,profile) array^ => [for (i,e in dir.entries) *(
    let y=dir.staff_y+(if (dir.placement=="below") 6.5+float(i)*1.8 else -2.0-float(i)*1.8),
    if (e.kind=="dynamics" and dynamics[e.value]!=null) [d.glyph(profile,dynamics[e.value],dir.x,y)^]
    else if (e.kind=="words" or e.kind=="rehearsal") [d.text(profile,e.text,dir.x,y)^]
    else if (e.kind=="metronome") [d.text(profile,(e.metronome.unit or "quarter")++
        (if (e.metronome.dots>0) "." else "")++" = "++string(e.metronome.bpm),dir.x,y)^]
    else if (contains(["segno","coda"],e.kind)) [d.glyph(profile,e.kind,dir.x,y)^]
    else [])]
pub fn barline(bar,x,y,profile) array^ {
    let style=bar.style or "regular"
    let heavy=profile.rules.thickBarlineThickness
    let thin=profile.rules.thinBarlineThickness
    let gap=profile.rules.barlineSeparation
    let backwards=bar.repeat=="backward"
    let forward=bar.repeat=="forward"
    let thick_left=forward or style=="heavy-light" or style=="heavy-heavy"
    let thick_right=backwards or style=="light-heavy" or style=="heavy" or style=="heavy-heavy"
    let double=forward or backwards or contains(["light-light","light-heavy","heavy-light","heavy-heavy"],style)
    let x1=x-(if (double) gap+heavy/2 else 0.0);
    if (style=="none" and not forward and not backwards) [] else [
        d.line(x1,y,x1,y+4.0,if (thick_left) heavy else if (not double and thick_right) heavy else thin),
        *if (double) [d.line(x,y,x,y+4.0,if (thick_right) heavy else thin)] else [],
        *if (forward or backwards) [for (dy in [1.5,2.5])
            d.glyph(profile,"augmentationDot",if (forward) x+0.4 else x1-0.7,y+dy)^] else []]
}
pub fn system(system,parts,profile) array^ {
    let grouped=[for (p in parts) [for (st in system.staffs where st.part==p.id) st]];
    [*[for (st in system.staffs,i in 0 to 4) d.line(system.left,st.y+float(i),system.left+system.width,st.y+float(i),profile.rules.staffLineThickness)],
     d.line(system.left,system.staffs[0].y,system.left,system.staffs[len(system.staffs)-1].y+4.0,profile.rules.thinBarlineThickness),
     *[for (i,p in parts) d.text(profile,p.label,system.left-2.0,(grouped[i][0].y+grouped[i][len(grouped[i])-1].y+4.0)/2,1.4,"end")^],
     *[for (staffs in grouped where len(staffs)>1) (
        let top=staffs[0].y,
        let bottom=staffs[len(staffs)-1].y+4.0,
        let metric=f.glyph(profile,"brace")^,
        let sx=0.9/(metric.ink.right-metric.ink.left),
        let sy=(bottom-top)/(metric.ink.bottom-metric.ink.top),
        d.glyph(profile,"brace",system.left-1.5-metric.ink.left*sx,top-metric.ink.top*sy,sx,sy)^)],
     *[for (m in system.measures) *[
        d.text(profile,m.label,m.x,system.staffs[0].y-1.0,1.0)^,
        *[for (header in m.headers) *d.move_all(header.shapes,m.x,l.staff_y(system.staffs,header.part,header.staff))],
        *[for (change in m.changes) *d.move_all(change.shapes,
            [for (col in m.columns where col.onset==change.onset) col.x-col.left-col.prefix][0],
            l.staff_y(system.staffs,change.part,change.staff))],
        *[for (raw in m.raw,st in system.staffs where st.part==raw.part) *(
            let right=[for (bar in raw.barlines where bar.location=="right") bar][0] or {style:"regular"},
            barline(right,m.x+m.width,st.y,profile)^)],
        *[for (raw in m.raw,bar in raw.barlines,st in system.staffs where st.part==raw.part and bar.location=="left")
            *barline(bar,if (bar.repeat=="forward") m.x+m.placed_prefix-1.1 else m.x,st.y,profile)^]]],
     *[for (dir in system.directions) *direction(dir,profile)^]]
}
pub fn warnings(score) array => [
    *[for (e in score.events,a in e.notation.articulations where not contains(["accent","staccato","tenuto","staccatissimo","strong-accent"],a))
        c.diagnostic("unsupported-decoration","unsupported articulation "++a,e.id)],
    *[for (p in score.parts,m in p.measures,dir in m.directions,e in dir.entries where
        not contains(["words","rehearsal","metronome","dynamics","wedge","segno","coda"],e.kind) or e.kind=="dynamics" and dynamics[e.value]==null)
        c.diagnostic("unsupported-decoration","unsupported direction "++e.kind,p.id++"/"++m.label)],
    *[for (p in score.parts,m in p.measures,b in m.barlines where b.location=="middle" or
        not contains(["regular","none","light-light","light-heavy","heavy-light","heavy-heavy","heavy"],b.style))
        c.diagnostic("unsupported-decoration","unsupported barline style or middle placement",p.id++"/"++m.label)]]
