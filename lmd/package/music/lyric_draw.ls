// lyric text and connectors use stable groups so playback can color whole syllables.
import d: .draw
import spans: .spans
import lyrics: .lyrics

fn local_lyric(event,id) => [for (l in event.lyrics where l.id==id) l][0]
fn metric(event,id) => event.lyric_metrics[index_of([for (l in event.lyrics) l.id],id)]
fn baseline(system,event,lyric) => (
    let staff=[for (s in system.staffs where s.part==event.part and s.staff==event.staff) s][0],
    staff.y+system.y+(if (lyric.placement=="above") staff.lyric_above-float(lyric.lane)*1.7
        else staff.lyric_below+float(lyric.lane)*1.7))
fn group(owner,id,kind,system,shapes) => {kind:"lyric",id:id,lyric_id:owner.id,
    lyric_kind:kind,verse:owner.lyric.number,passes:owner.lyric.passes,description:owner.lyric.text,
    page:system.page,system:system.index,members:owner.members,shapes:shapes,
    bounds:d.union([for (s in shapes) s.bounds])}
fn text(owner,event,layout) map^ {
    let system=layout.systems[event.system]
    let lyric=local_lyric(event,owner.id)
    let shapes=[d.text(layout.profile,lyric.text,event.x+event.glyph.advance/2,
        baseline(system,event,lyric),1.4,"middle")^];
    group(owner,owner.id,"text",system,shapes)
}
fn connector(owner,target,kind,layout) array^ {
    let a=spans.positioned(layout,owner.start)
    let b=spans.positioned(layout,target)
    let lyric=local_lyric(a,owner.id)
    let next_lyric=if (kind=="hyphen") [for (l in b.lyrics where lyrics.line_key(b,l)==owner.key) l][0] else null
    let segments=if (a==null or b==null or lyric==null) [] else
    [for (system in layout.systems where system.index>=a.system and system.index<=b.system) (
        let first=system.index==a.system,
        let end_segment=system.index==b.system,
        let x1=if (first) a.x+a.glyph.advance/2+metric(a,owner.id).width/2+0.3 else system.left+0.5,
        let x2=if (not end_segment) system.left+system.width-0.5 else if (kind=="extend") b.x+b.glyph.advance
            else b.x+b.glyph.advance/2-metric(b,next_lyric.id).width/2-0.3,
        let y=baseline(system,a,lyric),
        let shapes=if (x2<=x1) [] else if (kind=="extend") [d.line(x1,y+0.25,x2,y+0.25,0.1)]
            else [for (i in 0 to max(0,int(floor((x2-x1)/8.0))-1)) (
                let count=max(1,int(floor((x2-x1)/8.0))),
                let x=x1+(x2-x1)*(float(i)+0.5)/float(count),
                d.line(x-0.3,y-0.4,x+0.3,y-0.4,0.1))],
        group(owner,owner.id++"/"++kind++"/"++string(system.index),kind,system,shapes))];
    [for (g in segments where len(g.shapes)>0) g]
}
pub fn build(layout) array^ {
    let owners=[for (g in layout.score.lyric_groups or [] where g.lyric.visible) (
        let event=spans.positioned(layout,g.start),
        if (event!=null and local_lyric(event,g.id)!=null) g else null)]
    let visible=[for (g in owners where g!=null) g]
    let rows=[for (i,g in visible) (
        let next=[for (x in slice(visible,i+1,len(visible)) where x.key==g.key) x][0],
        [text(g,spans.positioned(layout,g.start),layout)^,
         *if (g.mode=="extend") connector(g,g.end,"extend",layout)^ else [],
         *if (next!=null and contains(["begin","middle"],g.lyric.syllabic)) connector(g,next.start,"hyphen",layout)^ else []])];
    [for (row in rows) *row]
}
