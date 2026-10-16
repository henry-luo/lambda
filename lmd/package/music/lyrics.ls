// lyric identity and continuation are score data, independent of layout and audio (D7.2.5).
import c: .common
import t: .time

pub fn line_key(event,lyric) => event.part++"/"++string(event.staff)++"/"++event.voice++"/"++
    lyric.placement++"/"++(lyric.name or "")++"/"++lyric.number
pub fn parse(node,event_id) array^ {
    let rows=[for (i,x in c.named(node,"lyric")) (
        let syllables=[for (s in c.named(x,"syllabic")) trim(c.text(s))],
        let extend=c.child(x,"extend"),
        let action=if (extend==null) null else extend.type or "start",
        let passes=if (x['time-only']==null) [] else [for (p in split(x['time-only'],",")) float(trim(p))],
        let placement=x.placement or "below",
        let valid=c.require(all([for (s in syllables) contains(["single","begin","middle","end"],s)]) and
            (action==null or contains(["start","continue","stop"],action)) and
            contains(["above","below"],placement) and all([for (p in passes) c.is_integer(p) and p>=1 and p<=16]) and
            (x.number==null or len(x.number)>0),event_id++": invalid lyric syllabic, extension, verse or repeat pass")^,
        let fragments=[for (child in c.children(x) where contains(['text','elision'],name(child)))
            if (name(child)=='elision' and c.text(child)=="") "‿" else c.text(child)],
        let text=join(fragments,""),
        let content=c.require(len(fragments)>0 or action!=null,event_id++": lyric needs text or an extension")^,
        {id:event_id++"/lyric/"++string(i),text:text,number:x.number or x.name or "1",name:x.name,
         syllabic:syllables[len(syllables)-1] or "single",extend:extend!=null,extend_type:action,
         placement:placement,passes:[for (p in passes) int(p)],
         visible:if (x['print-object']!=null) x['print-object']!="no" else node['print-lyric']!="no"})]
    let unique_lines=c.require(len(unique([for (l in rows) l.placement++"/"++(l.name or "")++"/"++l.number]))==len(rows),
        event_id++": duplicate lyric verse")^;
    rows
}
pub fn warnings(node,event_id) array => [for (x in c.named(node,"lyric"))
    *c.names(x,["text","syllabic","elision","extend","end-line","end-paragraph","footnote","level"],event_id)]

pub fn lanes(parts) map {
    // S7.5.3v2: textual verse labels recover conversion failure before numeric ranking.
    let entries=[for (p in parts,m in p.measures,e in m.events,l in e.lyrics where l.visible)
        {key:line_key(e,l),staff:p.id++"/"++string(e.staff)++"/"++l.placement,
         number:l.number,order:(let ordinal=float(l.number) ^ { null },
             if (c.is_integer(ordinal)) ordinal else 1000000.0)}]
    let unique_entries=[for (key in unique([for (e in entries) e.key])) [for (e in entries where e.key==key) e][0]]
    let ordered=sort(unique_entries,(e)=>e.order);
    map([for (staff in unique([for (e in ordered) e.staff]),i,e in [for (x in ordered where x.staff==staff) x]) (e.key,i)])
}
pub fn underlays(score,event) array => [*event.lyrics,
    *[for (g in score.lyric_groups or [] where g.start!=event.id and contains(g.members,event.id))
        if (any([for (l in event.lyrics) l.id==g.lyric.id or line_key(event,l)==g.key])) *[]
        else {*:g.lyric,text:"",carry:true}]]

fn step(st,event,key) map^ {
    let lyric=[for (l in event.lyrics where line_key(event,l)==key) l][0]
    let has_text=lyric!=null and lyric.text!=""
    let pending=st.open
    let stop=event.rest or pending!=null and pending.mode=="tie" and not contains(event.notation.ties,"stop")
    let carried=if (stop) null else pending
    let finished=[*st.groups,*if (stop and pending!=null) [pending] else []]
    if (has_text) {
        let next={id:lyric.id,lyric:lyric,part:event.part,staff:event.staff,voice:event.voice,key:key,
            start:event.id,end:event.id,members:[event.id],mode:if (lyric.extend_type=="start") "extend" else "tie"}
        let continues=lyric.extend_type=="start" or contains(event.notation.ties,"start");
        {open:if (continues) next else null,groups:[*finished,*if (carried!=null) [carried] else [],
            *if (continues) [] else [next]]}
    } else {
        let valid=c.require(lyric==null or lyric.extend_type==null or carried!=null,
            event.id++": lyric extension has no starting syllable")^
        let next=if (carried==null) null else {*:carried,end:event.id,members:[*carried.members,event.id]}
        let ends=lyric.extend_type=="stop" or carried.mode=="tie" and not contains(event.notation.ties,"start");
        {open:if (ends) null else next,groups:[*finished,*if (ends and next!=null) [next] else []]}
    }
}
pub fn resolve(score) array^ {
    let keys=unique([for (e in score.events,l in e.lyrics) line_key(e,l)])
    let lines=[for (key in keys) (
        let events=sort([for (e in score.events where not e.chord or len(e.lyrics)>0) e],
            (e)=>[e.measure,t.value(e.onset),e.index]),
        let matching=[for (e in events where starts_with(key,e.part++"/"++string(e.staff)++"/"++e.voice++"/")) e],
        let result=c.fold(matching,{open:null,groups:[]},(st,e)=>step(st,e,key)^)^,
        [*result.groups,*if (result.open==null) [] else [result.open]])];
    [for (line in lines) *line]
}
