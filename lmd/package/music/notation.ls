// Shared notation facts precede horizontal/vertical layout; time remains exact.
import c: .common
import t: .time
import f: .fonts

let head_names = {long: "noteheadDoubleWholeSquare", breve: "noteheadDoubleWhole", whole: "noteheadWhole", half: "noteheadHalf"}
let rest_names = {long: "Longa", breve: "DoubleWhole", whole: "Whole", half: "Half", quarter: "Quarter",
    eighth: "8th", '16th': "16th", '32nd': "32nd", '64th': "64th"}
let accidental_names = {sharp: "accidentalSharp", flat: "accidentalFlat", natural: "accidentalNatural",
    'double-sharp': "accidentalDoubleSharp", 'sharp-sharp': "accidentalDoubleSharp", 'flat-flat': "accidentalDoubleFlat"}
pub fn height(pitch, clef) {
    let steps = {C:0, D:1, E:2, F:3, G:4, A:5, B:6}
    let anchor = if (clef.sign == "G") 32 else if (clef.sign == "F") 24 else 28
    float(4 - (clef.line - 1)) - float(pitch.octave * 7 + steps[pitch.step] - anchor - 7 * clef.octave) / 2.0
}
pub fn head(event) => if (event.rest) "rest" ++ rest_names[event.note_type] else head_names[event.note_type] or "noteheadBlack"
pub fn flag_count(event) => (let i = index_of(["eighth", "16th", "32nd", "64th"], event.note_type), if (i < 0 or i == null) 0 else i + 1)
pub fn accidental_name(value) => accidental_names[value]
pub fn key_alter(step, fifths) => if (contains(slice(if (fifths > 0) ["F","C","G","D","A","E","B"]
    else ["B","E","A","D","G","C","F"], 0, abs(fifths)), step)) (if (fifths > 0) 1 else -1) else 0

fn accidental(event, previous) {
    if (event.rest) null else if (event.accidental != null) accidental_names[event.accidental]
    else {
        let prior = [for (p in previous where not p.rest and p.part == event.part and p.staff == event.staff and p.pitch.step == event.pitch.step
            and p.pitch.octave == event.pitch.octave and p.context.fifths == event.context.fifths) p]
        let expected = if (len(prior) == 0) key_alter(event.pitch.step, event.context.fifths) else prior[len(prior)-1].pitch.alter
        if (event.pitch.alter == expected or contains(event.notation.ties, "stop")) null
        else ["accidentalDoubleFlat","accidentalFlat","accidentalNatural","accidentalSharp","accidentalDoubleSharp"][event.pitch.alter + 2]
    }
}
pub fn ordered(events) => c.fold(events, [], (items, e) => [
    *[for (x in items where t.compare(x.onset,e.onset) <= 0) x], e,
    *[for (x in items where t.compare(x.onset,e.onset) > 0) x]])
fn prepare_event(e, events, previous, profile) map^ {
    let chord = [for (x in events where x.anchor == e.anchor) x]
    let voices = unique([for (x in events where x.part == e.part and x.staff == e.staff and not x.grace) x.voice])
    let ys = [for (x in chord where not x.rest) height(x.pitch,x.context.clef)]
    let stem = e.stem or (if (len(voices) > 1) (if (index_of(voices,e.voice) % 2 == 0) "up" else "down")
        else if (len(ys) > 0 and sum(ys)/len(ys) < 2.0) "down" else "up")
    let glyph_name = head(e)
    let metric = f.glyph(profile,glyph_name)^
    let a = accidental(e, previous)
    let a_metric = if (a == null) null else f.glyph(profile,a)^
    let y = if (e.rest) (if (len(voices) > 1) (if (stem == "up") 0.0 else 4.0)
        else if (e.note_type=="whole") 1.0 else 2.0) else height(e.pitch,e.context.clef)
    // Walk pitch order so a chain of seconds alternates, regardless of XML chord order.
    let pitches=sort([for (x in chord where not x.rest) {id:x.id,y:height(x.pitch,x.context.clef)}],(p)=>p.y)
    let offsets=c.fold(pitches,{y:null,shift:false,values:{}},(acc,p)=>(
        let shifted=acc.y!=null and p.y-acc.y==0.5 and not acc.shift,
        {y:p.y,shift:shifted,values:{*:acc.values,[p.id]:shifted}}))
    let dx = if (offsets.values[e.id]) (if (stem == "up") metric.advance else -metric.advance) else 0.0
    let prior_accidentals=[for (p in previous where p.part==e.part and p.staff==e.staff and p.onset==e.onset and p.accidental_glyph!=null) p]
    let accidental_dx=if (a_metric==null) 0.0 else a_metric.advance+0.4+
        sum([for (p in prior_accidentals) f.glyph(profile,p.accidental_glyph)^.advance+0.25])
    let lyrics = f.measure(profile,[for (x in e.lyrics) x.text],1.4)^;
    {*:e, symbol:glyph_name, glyph:metric, accidental_glyph:a, y:y, dx:dx, stem_direction:stem,
        visual_scale:if (e.grace) 0.65 else 1.0,accidental_dx:accidental_dx,
        left:max([0.0, -dx - metric.ink.left, accidental_dx+0.3,
            *[for (l in lyrics) l.width/2.0]]),
        right:max([metric.advance + dx + 0.5 + e.dots*0.6,
            if (flag_count(e) > 0 and len(e.beams)==0 and stem=="up") 2.5 else 0.0,
            *[for (l in lyrics) l.width/2.0]]),
        lyric_metrics:lyrics}
}
pub fn prepare(events, profile) array^ {
    let sorted = ordered(events)
    c.fold(sorted, [], (items,e) => [*items,prepare_event(e,sorted,items,profile)^])
}
pub fn beam_groups(events) array^ {
    let primary = [for (e in events where not e.chord and not e.rest and not e.grace) e]
    let result = c.fold([for (e in primary,b in e.beams) {e:e,beam:b}], {open:{},groups:[]}, (acc,row) => (
        let e=row.e,
        let beam=row.beam,
        let hook=contains(["forward hook","backward hook"],beam.kind),
        let key = e.part ++ "/" ++ string(e.staff) ++ "/" ++ e.voice++"/"++string(beam.level),
        let pending = acc.open[key] or [],
        let valid = c.require(beam.level<=flag_count(e) and e.stem_direction!="none" and
            (hook and beam.level>1 and len(pending)==0 or beam.kind=="begin" and len(pending)==0 or
            contains(["continue","end"],beam.kind) and len(pending)>0), e.id ++ ": unbalanced or invalid beam")^,
        if (hook) acc else (
            let members=[*pending,e.id],
            {*:acc,open:{*:acc.open,[key]:if (beam.kind=="end") [] else members},
                groups:if (beam.kind=="end" and beam.level==1) [*acc.groups,members] else acc.groups})))
    let complete = c.require(all([for (k,v in result.open) len(v)==0]), "unclosed or cross-measure beam")^
    result.groups
}
