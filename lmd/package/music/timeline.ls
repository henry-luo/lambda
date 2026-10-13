// one exact performance representation feeds SMF, native playback and score selection.
import c: .common
import t: .time

// 34-digit decimal keys distinguish every bounded rational without float rounding.
pub fn ordered(items) => sort(items,(e)=>decimal(e.at.num)/decimal(e.at.den))
pub fn message(at, status, a, b = 0) => {at:at,status:status,a:a,b:b}
pub fn tempo(at, microseconds) => {at:at,microseconds:microseconds}
fn sounding_step(st,e) map^ {
    let kind=e.status div 16
    let key=string(e.status%16)++":"++string(e.a)
    let before=st[key]
    let onset=kind==9 and e.b>0
    let valid=c.require(not onset or before==null,"overlapping MIDI notes on one channel/key")^
    let positive=c.require((kind!=8 and not (kind==9 and e.b==0)) or before==null or t.compare(e.at,before)>0,
        "sounding MIDI note has no positive duration")^;
    if (onset) {*:st,[key]:e.at} else if (kind==8 or kind==9) {*:st,[key]:null}
    else if (kind==11 and (e.a==120 or e.a==123)) map([for (k,v in st)
        (k,if (starts_with(k,string(e.status%16)++":")) null else v)]) else st
}
pub fn check_notes(events) bool^ {
    let valid=c.fold(events,{},sounding_step)^
    let closed=c.require(all([for (key,onset in valid) onset==null]),"unterminated MIDI note")^;
    true
}
pub fn seconds(tempos, at) {
    let index=max([for (i,x in tempos where t.compare(x.at,at)<=0) i])
    let segment=tempos[index];
    segment.seconds+t.value(t.sub(at,segment.at)^)*segment.microseconds/1000000.0
}
fn tempo_step(rows,event) array^ {
    let previous=rows[len(rows)-1]
    let valid=c.require(c.is_integer(event.microseconds) and event.microseconds>0 and event.microseconds<=16777215,
        "invalid MIDI tempo")^
    let same=t.compare(event.at,previous.at)==0
    let conflict=c.require(not same or previous.default==true or previous.microseconds==event.microseconds,
        "conflicting simultaneous tempo instructions")^
    let row={at:event.at,microseconds:event.microseconds,seconds:previous.seconds+
        t.value(t.sub(event.at,previous.at)^)*previous.microseconds/1000000.0};
    if (same) [*slice(rows,0,len(rows)-1),row] else [*rows,row]
}
pub fn finish(events, tempos, tracks, occurrences, duration, diagnostics = []) map^ {
    let checked=c.require(len(events)>0 and len(events)<=200000 and t.compare(duration,t.ZERO)>0,"empty or excessive performance")^
    let fields=c.require(all([for (e in events) c.is_integer(e.status) and contains([8,9,11,12,14],e.status div 16) and
        c.is_integer(e.a) and e.a>=0 and e.a<128 and c.is_integer(e.b) and e.b>=0 and e.b<128 and
        e.at.num>=0 and e.at.den>0 and t.compare(e.at,duration)<=0]),"invalid MIDI event or event outside performance")^
    let timeline=ordered(events)
    let note_check=check_notes(timeline)^
    let tempo_map=c.fold(ordered(tempos),[{at:t.ZERO,microseconds:500000,seconds:0.0,default:true}],tempo_step)^
    let end=seconds(tempo_map,duration)
    let bounded=c.require(end<=86400,"performance exceeds 24 hours")^;
    {kind:"music-performance",version:1,events:[for (e in timeline) {*:e,seconds:seconds(tempo_map,e.at)}],
     tempos:tempo_map,tracks:tracks,occurrences:[for (o in occurrences) {*:o,seconds:seconds(tempo_map,o.at),
        end_seconds:seconds(tempo_map,t.add(o.at,o.duration)^)}],duration:duration,seconds:end,diagnostics:diagnostics}
}
