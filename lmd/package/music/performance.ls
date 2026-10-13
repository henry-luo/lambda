// score semantics stay in Lambda; the native host consumes timed MIDI bytes (D7.2.5).
import c: .common
import t: .time
import tl: .timeline

let velocities={ppp:24,pp:36,p:48,mp:64,mf:80,f:96,ff:112,fff:124}
fn checked_int(value, fallback, low, high, label) int^ {
    let n=if (value==null) fallback else value
    let valid=c.require(c.is_integer(n) and n>=low and n<=high,"invalid "++label)^;
    int(n)
}
fn instrument_rows(score) array^ {
    let declared=[for (part in score.parts,ins in (if (len(part.midi_instruments or [])==0)
        [{id:part.instruments[0].id}] else part.midi_instruments)) {*:ins,part:part.id,label:part.label}]
    let explicit=[for (ins in declared where ins.channel!=null) checked_int(ins.channel,1,1,16,"MIDI channel")^-1]
    let valid=c.require(len(declared)<=15 and len(unique(explicit))==len(explicit) and not contains(explicit,9),
        "MIDI instrument channel collision, percussion, or capacity exceeded")^
    let free=[for (ch in 0 to 15 where ch!=9 and not contains(explicit,ch)) ch]
    let automatic=[for (i,ins in declared where ins.channel==null) i];
    [for (i,ins in declared) (
        let valid_bank=c.require((ins.bank==null or ins.bank==1) and ins.unpitched==null,"unsupported MIDI bank/percussion instrument")^,
        {*:ins,channel:if (ins.channel==null) free[index_of(automatic,i)] else int(ins.channel)-1,
         program:checked_int(ins.program,1,1,128,"MIDI program")^-1,
         volume:checked_int(if (ins.volume==null) null else round(ins.volume*127/100),100,0,127,"instrument volume")^,
         pan:checked_int(if (ins.pan==null) null else round((ins.pan+90)*127/180),64,0,127,"instrument pan")^})]
}
fn endings(measures) array^ {
    let result=c.fold(measures,{active:[],rows:[]},(st,m)=>(
        let begins=[for (b in m.barlines where b.ending.type=="start") b.ending],
        let stops=[for (b in m.barlines where b.ending.type=="stop" or b.ending.type=="discontinue") b.ending],
        let active=if (len(begins)==0) st.active else [for (s in split(begins[0].number,",")) checked_int(float(trim(s)),1,1,16,"ending number")^],
        {active:if (len(stops)>0) [] else active,rows:[*st.rows,active]}))^;
    result.rows
}
fn repeats(measures) array^ {
    let result=c.fold(measures,{open:null,pairs:[]},(st,m)=>(
        let forward=[for (b in m.barlines where b.repeat=="forward") b],
        let backward=[for (b in m.barlines where b.repeat=="backward") b],
        let begin=if (len(forward)>0) m.index else st.open,
        let valid=c.require(len(forward)<=1 and len(backward)<=1 and (len(forward)==0 or st.open==null),"nested or duplicate repeats are unsupported")^,
        {open:if (len(backward)>0) null else begin,pairs:[*st.pairs,*if (len(backward)==0) [] else
            [{first:begin or 0,last:m.index,times:checked_int(float(backward[0].times or "2"),2,2,16,"repeat count")^}]]}))^;
    let closed=c.require(result.open==null,"unclosed forward repeat")^;
    result.pairs
}
fn traverse(measures, endings_by_bar, pairs, bar = 0, pass = 1, at = {num:0,den:1}, result = []) array^ {
    let bounded=c.require(len(result)<=8192,"repeat expansion budget exceeded")^;
    if (bar>=len(measures)) result else {
        let starts=[for (p in pairs where p.first==bar) p]
        let ends=[for (p in pairs where p.last==bar) p]
        let allowed=len(endings_by_bar[bar])==0 or contains(endings_by_bar[bar],pass)
        let row={measure:bar,pass:pass,at:at,duration:measures[bar].span}
        let next_at=if (allowed) t.add(at,row.duration)^ else at
        let next_result=if (allowed) [*result,row] else result
        let again=len(ends)>0 and pass<ends[0].times
        let next_bar=if (again) ends[0].first else bar+1
        let next_pass=if (again) pass+1 else if (len([for (p in pairs where p.first==next_bar) p])>0) 1 else pass;
        traverse(measures,endings_by_bar,pairs,next_bar,next_pass,next_at,next_result)^
    }
}
fn quarter_bpm(entry) number^ {
    let units={whole:4.0,half:2.0,quarter:1.0,eighth:0.5,'16th':0.25,'32nd':0.125,'64th':0.0625}
    let beat=units[entry.unit]
    let valid=c.require(beat!=null and c.finite(entry.bpm) and entry.bpm>0 and entry.dots<=3,"unsupported metronome mark")^;
    entry.bpm*beat*(2.0-0.5**entry.dots)
}
fn bpm_tempo(at,bpm) map^ {
    let valid=c.require(c.finite(bpm) and bpm>0,"nonpositive tempo")^;
    tl.tempo(at,int(round(60000000.0/bpm)))
}
fn direction_rows(score,path) array => [for (visit in path,part in score.parts,dir in part.measures[visit.measure].directions)
    {*:dir,part:part.id,at:t.add(visit.at,dir.onset)^,visit:visit}]
fn validate_direction(dir) bool^ {
    let navigation=["dacapo","dalsegno","tocoda","fine","segno","coda"]
    let jumps=c.require(all([for (key in navigation) dir.sound[key]==null]),"unsupported playback jump or navigation")^
    let sound_children=c.require(len(dir.sound.children or [])==0,"unsupported nested sound instruction")^
    let sound_attributes=c.require(all([for (key,value in dir.sound)
        contains(["tempo","dynamics","damper-pedal","children"],string(key))]),"unsupported sound attribute")^
    let supported=c.require(all([for (e in dir.entries) not contains(["segno","coda","pedal"],e.kind)]),
        "unsupported playback direction; pedal must use sound damper-pedal")^;
    true
}
fn velocity(directions,part,at) int^ {
    let prior=[for (d in directions where d.part==part and t.compare(d.at,at)<=0 and
        (d.sound.dynamics!=null or len([for (e in d.entries where e.kind=="dynamics") e])>0)) d]
    let dir=prior[len(prior)-1]
    let mark=[for (e in dir.entries where e.kind=="dynamics") e.value][0]
    let raw=if (dir.sound.dynamics!=null) round(float(dir.sound.dynamics)*90/100) else if (mark==null) 80 else velocities[mark]
    let known=c.require(raw!=null,"unsupported expressive dynamic")^;
    checked_int(raw,80,1,127,"note velocity")^
}
fn note_rows(score,path,instruments,directions) array^ {
    [for (visit in path,part in score.parts,e in part.measures[visit.measure].events) (
        let supported=c.require(not e.grace and not e.notation.fermata,"grace notes and fermatas require an explicit timing policy")^,
        let matches=[for (ins in instruments where ins.part==part.id and (e.instrument==null or ins.id==e.instrument)) ins],
        let valid=c.require(e.rest or len(matches)>0 and (e.instrument!=null or len(matches)==1),"missing or ambiguous note instrument")^,
        let ins=if (e.rest) [for (ins in instruments where ins.part==part.id) ins][0] else matches[0],
        let pitch=if (e.rest) 0 else ({C:0,D:2,E:4,F:5,G:7,A:9,B:11}[e.pitch.step]+12*(e.pitch.octave+1)+e.pitch.alter+e.transpose),
        let key=checked_int(pitch,0,0,127,"sounding MIDI pitch")^,
        let at=t.add(visit.at,e.onset)^,
        {id:e.id,pass:visit.pass,part:part.id,voice:e.voice,staff:e.staff,channel:ins.channel,key:key,rest:e.rest,
         at:at,duration:e.duration,velocity:velocity(directions,part.id,at)^,ties:e.notation.ties})]
}
fn tie_key(e) => e.part++":"++e.voice++":"++string(e.staff)++":"++string(e.channel)++":"++string(e.key)
fn expression_warnings(score) array => [
    *[for (e in score.events where len(e.notation.articulations)>0 or len(e.notation.slurs)>0)
        c.diagnostic("playback-expression","articulation and slur expression is not synthesized",e.id)],
    *[for (part in score.parts,m in part.measures,dir in m.directions,e in dir.entries where
        not contains(["dynamics","metronome","rehearsal"],e.kind))
        c.diagnostic("playback-expression","direction does not alter playback: "++e.kind,part.id)]]
fn tie_step(st,e) map^ {
    if (e.rest) st else {
        let key=tie_key(e)
        let previous=st.pending[key]
        let begins=contains(e.ties,"start")
        let ends=contains(e.ties,"stop")
        let valid=c.require((ends and previous!=null and t.compare(t.add(previous.at,previous.duration)^,e.at)==0) or
            not ends and previous==null,"unmatched or noncontiguous performed tie")^
        let merged=if (ends) {*:previous,duration:t.add(previous.duration,e.duration)^} else e;
        {pending:{*:st.pending,[key]:if (begins) merged else null},notes:if (begins) st.notes else [*st.notes,merged]}
    }
}
pub fn build(score,options = {}) map^ {
    let opts=c.option_warnings(options,["strict","tempo"])^
    let valid=c.require(score.kind=="music-score","expected a music score")^
    let instruments=instrument_rows(score)^
    let navigation=c.require(all([for (part in score.parts,m in part.measures,i in [m.index])
        all([for (b in m.barlines where b.repeat!=null or b.ending!=null)
            len([for (canonical in score.measures[i].barlines where canonical.repeat==b.repeat and canonical.times==b.times and
                canonical.ending==b.ending and canonical.location==b.location) canonical])>0])]),"conflicting repeat navigation between parts")^
    let path=traverse(score.measures,endings(score.measures)^,repeats(score.measures)^)^
    let dirs=tl.ordered(direction_rows(score,path))
    let checked=[for (dir in dirs) validate_direction(dir)^]
    let notes=tl.ordered(note_rows(score,path,instruments,dirs)^)
    let tied=c.fold(notes,{pending:{},notes:[]},tie_step)^
    let complete=c.require(all([for (key,n in tied.pending) n==null]),"unclosed performed tie")^
    let collisions=c.require(all([for (i,a in tied.notes) all([for (b in slice(tied.notes,i+1,len(tied.notes)) where
        a.channel==b.channel and a.key==b.key) t.compare(t.add(a.at,a.duration)^,b.at)<=0 or t.compare(t.add(b.at,b.duration)^,a.at)<=0])]),
        "same-key overlaps require independent MIDI channels")^
    let changes=if (options.tempo!=null) [bpm_tempo(t.ZERO,options.tempo)^] else [for (dir in dirs)
        *(if (dir.sound.tempo!=null) [bpm_tempo(dir.at,float(dir.sound.tempo))^]
        else [for (e in dir.entries where e.kind=="metronome") bpm_tempo(dir.at,quarter_bpm(e.metronome)^)^])]
    let setups=[for (ins in instruments) *[tl.message(t.ZERO,192+ins.channel,ins.program),
        tl.message(t.ZERO,176+ins.channel,7,ins.volume),tl.message(t.ZERO,176+ins.channel,10,ins.pan)]]
    let controllers=[for (dir in dirs,ins in instruments where dir.sound['damper-pedal']!=null and ins.part==dir.part)
        tl.message(dir.at,176+ins.channel,64,if (dir.sound['damper-pedal']=="yes") 127 else if (dir.sound['damper-pedal']=="no") 0
            else checked_int(round(float(dir.sound['damper-pedal'])*127/100),0,0,127,"damper pedal")^)]
    // note-offs precede setups/controllers/onsets at equal positions; stable sorting retains that order.
    let events=[*[for (n in tied.notes) tl.message(t.add(n.at,n.duration)^,128+n.channel,n.key)],*setups,*controllers,
        *[for (n in tied.notes) tl.message(n.at,144+n.channel,n.key,n.velocity)]]
    let final_visit=path[len(path)-1]
    let diagnostics=c.warnings([*opts,*score.diagnostics,*expression_warnings(score),*if (len(changes)==0)
        [c.diagnostic("default-tempo","no tempo specified; using 120 quarter notes per minute")] else []],options)^;
    tl.finish(events,changes,[for (p in score.parts) {id:p.id,label:p.label,
        channels:[for (ins in instruments where ins.part==p.id) ins.channel]}],notes,t.add(final_visit.at,final_visit.duration)^,diagnostics)^
}
