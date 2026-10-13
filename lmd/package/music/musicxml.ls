// D7.2.4/S12.4.1v2: normalize immutable XML/ZIP snapshots into ordinary score values.
import c: .common
import t: .time

let note_types = ["long", "breve", "whole", "half", "quarter", "eighth", "16th", "32nd", "64th"]
let note_lengths = [16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625]
let default_staff = {clef: {sign: "G", line: 2, octave: 0}, fifths: 0,
    meter: {beats: "4", unit: 4, span: {num: 4, den: 1}}}
fn location(part, bar) => part ++ "/measure/" ++ string(bar)
fn duration(node, divisions) map^ {
    let checked = c.require(divisions != null, "duration requires divisions")^
    let raw = t.from_decimal(c.field(node, "duration", "0"))^
    let positive = c.require(raw.num > 0, "duration must be positive")^
    t.divide(raw, divisions)^
}
fn clef(node, previous) map^ {
    if (node == null) previous else {
        let sign = c.field(node, "sign", "G")
        let line = c.field_number(node, "line", if (sign == "F") 4 else if (sign == "C") 3 else 2)
        let octave = c.field_number(node, "clef-octave-change", 0)
        let checked = c.require(contains(["G", "F", "C"], sign) and c.is_integer(line) and line >= 1 and line <= 5
            and c.is_integer(octave) and abs(octave) <= 2, "unsupported clef")^;
        {sign: sign, line: int(line), octave: int(octave)}
    }
}
fn meter(node, previous) map^ {
    if (node == null) previous else {
        let beats = c.field(node, "beats", "")
        let values = [for (x in split(beats, "+")) float(x)]
        let unit = c.field_number(node, "beat-type")
        let checked = c.require(len(c.named(node, "beats")) == 1 and len(values) > 0 and
            all([for (x in values) c.is_integer(x) and x > 0 and x <= 32]) and
            contains([1, 2, 4, 8, 16, 32, 64], unit), "unsupported meter")^;
        {beats: beats, unit: int(unit), symbol: node.symbol, span: t.ratio(int(sum(values)) * 4, int(unit))^}
    }
}
fn staff_attribute(node, tag, staff) => [for (x in c.named(node, tag)
    where x.number == null or int(x.number) == staff) x][0]
fn attributes(node, previous) map^ {
    let supported=c.only(node,["divisions","key","time","staves","clef","transpose","staff-details"],"attributes")^
    let conventional_staff=c.require(all([for (details in c.named(node,"staff-details"))
        c.only(details,["staff-lines"],"staff-details")^ and c.field_number(details,"staff-lines",5)==5]),
        "only conventional five-line staves are supported")^
    let count = c.field_number(node, "staves", len(previous.staffs))
    let checked = c.require(c.is_integer(count) and count >= 1 and count <= 8, "unsupported staff count")^
    let divisions = if (c.present(node, "divisions")) t.from_decimal(c.field(node, "divisions"))^ else previous.divisions
    let valid_divisions = c.require(divisions == null or divisions.num > 0, "divisions must be positive")^
    let trans = c.child(node, "transpose")
    let supported_transpose=c.require(trans.number==null and not c.present(trans,"double"),"staff-specific or doubled transposition is unsupported")^
    let shift = if (trans == null) previous.transpose else
        c.field_number(trans, "chromatic", 0) + 12 * c.field_number(trans, "octave-change", 0)
    let valid_transpose = c.require(c.is_integer(shift) and abs(shift) <= 48, "unsupported transposition")^
    let scoped=c.require(all([for (x in c.children(node) where x.number!=null) c.is_integer(float(x.number)) and
        float(x.number)>=1 and float(x.number)<=count]),"attribute references unknown staff")^;
    {divisions: divisions, transpose: shift, staffs: [for (i in 1 to int(count)) (
        let before = previous.staffs[i - 1] or default_staff,
        let key = staff_attribute(node, "key", i),
        let fifths = if (key == null) before.fifths else c.field_number(key, "fifths"),
        let valid_key = c.require(c.is_integer(fifths) and abs(fifths) <= 7, "unsupported key signature")^,
        {clef: clef(staff_attribute(node, "clef", i), before.clef)^,
         fifths: int(fifths), meter: meter(staff_attribute(node, "time", i), before.meter)^})]}
}

fn pitch(node) map^ {
    let step = c.field(node, "step")
    let octave = c.field_number(node, "octave")
    let alter = c.field_number(node, "alter", 0)
    let checked = c.require(contains(["C", "D", "E", "F", "G", "A", "B"], step) and
        c.is_integer(octave) and octave >= 0 and octave <= 9 and c.is_integer(alter) and abs(alter) <= 2,
        "unsupported or invalid pitch")^;
    {step: step, octave: int(octave), alter: int(alter)}
}
fn notation(node) map^ {
    let n = c.child(node, "notations")
    let tuplets = c.named(n, "tuplet")
    let checked = c.require(len([for (x in tuplets where x.number != null and x.number != "1") x]) == 0,
        "nested tuplets are not supported")^;
    {ties: [for (x in c.named(node, "tie")) x.type],
     tied: [for (x in c.named(n, "tied")) x.type],
     slurs: [for (x in c.named(n, "slur")) {kind: x.type, number: x.number or "1", placement: x.placement}],
     tuplets: [for (x in tuplets) {kind: x.type, number: x.number or "1", bracket: x.bracket}],
     articulations: [for (x in c.children(c.child(n, "articulations"))) string(name(x))],
     fermata: c.present(n, "fermata")}
}
fn note(node, st, part, bar, index) map^ {
    let id = location(part, bar) ++ "/note/" ++ string(index)
    let staff = c.field_number(node, "staff", 1)
    let voice = c.field(node, "voice", "1")
    let rest = c.present(node, "rest")
    let grace = c.present(node, "grace")
    let chord = c.present(node, "chord")
    let valid_kind=c.require(not c.present(node,"cue") and not c.present(node,"unpitched") and
        not c.present(c.child(node,"rest"),"display-step"),id++": unsupported cue, percussion, or positioned rest")^
    let checked_staff = c.require(c.is_integer(staff) and staff >= 1 and staff <= len(st.context.staffs), id ++ ": invalid staff")^
    let checked_chord = c.require(not chord or st.anchor != null and st.anchor.voice == voice and
        st.anchor.staff == staff and not rest and not st.anchor.rest and st.anchor.grace == grace, id ++ ": invalid or cross-staff chord")^
    let dur = if (grace) t.ZERO else duration(node, st.context.divisions)^
    let onset = if (chord) st.anchor.onset else st.cursor
    let checked_duration = c.require(not chord or t.compare(dur, st.anchor.duration) <= 0, id ++ ": chord duration exceeds anchor")^
    let nonoverlap=c.require(chord or grace or all([for (e in st.events where not e.chord and not e.grace and e.staff==staff and e.voice==voice)
        t.compare(onset,t.add(e.onset,e.duration)^)>=0 or t.compare(t.add(onset,dur)^,e.onset)<=0]),id++": overlapping events in one voice")^
    let type_name = c.field(node, "type", if (rest and c.child(node, "rest").measure == "yes") "whole"
        else note_types[index_of(note_lengths, t.value(dur))])
    let checked_type = c.require(contains(note_types, type_name), id ++ ": unsupported note type")^
    let dots = len(c.named(node, "dot"))
    let checked_dots = c.require(dots <= 3, id ++ ": too many augmentation dots")^
    let p = if (rest) null else pitch(c.child(node, "pitch"))^
    let beams = [for (x in c.named(node, "beam")) {level: int(x.number or "1"), kind: trim(c.text(x))}]
    let valid_beams = c.require(all([for (b in beams) b.level >= 1 and b.level <= 4 and
        contains(["begin", "continue", "end", "forward hook", "backward hook"], b.kind)]) and
        len(unique([for (b in beams) b.level]))==len(beams) and
        (len(beams)==0 or not rest and not grace and contains([for (b in beams) b.level],1)), id ++ ": invalid or unsupported beam")^
    let stem = c.field(node, "stem")
    let valid_stem = c.require(stem == null or contains(["up", "down", "none"], stem), id ++ ": invalid stem")^
    let head = c.field(node, "notehead", "normal")
    let valid_head = c.require(head == "normal", id ++ ": unsupported notehead")^
    let alter = c.field(node, "accidental")
    let valid_alter = c.require(alter == null or contains(["sharp", "flat", "natural", "double-sharp", "sharp-sharp", "flat-flat"], alter),
        id ++ ": unsupported accidental")^
    let marks = notation(node)^
    let modification=c.child(node,"time-modification")
    let actual=c.field_number(modification,"actual-notes")
    let normal=c.field_number(modification,"normal-notes")
    let valid_tuplet=c.require(modification==null and len(marks.tuplets)==0 or
        c.is_integer(actual) and actual>0 and actual<=64 and c.is_integer(normal) and normal>0 and normal<=64,
        id++": invalid tuplet ratio")^
    let e = {id: id, source_id: node.id, part: part, measure: bar, index: index, staff: int(staff), voice: voice,
        onset: onset, duration: dur, rest: rest, grace: grace, grace_slash:c.child(node,"grace").slash=="yes", chord: chord,
        anchor: if (chord) st.anchor.id else id, pitch: p, note_type: type_name, dots: dots,
        context: st.context.staffs[int(staff) - 1], transpose: st.context.transpose,
        stem: stem, beams: beams, accidental: alter, notation: marks,
        time_modification: {actual:actual,normal:normal},
        lyrics: [for (x in c.named(node, "lyric")) {text: c.field(x, "text", ""),
            number: x.number or "1", syllabic: c.field(x, "syllabic"), extend: c.present(x, "extend")}],
        visible: node['print-object'] != "no"}
    {*:st, cursor: if (chord or grace) st.cursor else t.add(onset, dur)^,
        extent: t.later(st.extent, t.add(onset, dur)^), anchor: if (chord) st.anchor else e,
        events: [*st.events, e], warnings: [*st.warnings,
            *c.names(node, ["pitch", "rest", "duration", "voice", "type", "dot", "accidental", "time-modification",
                "stem", "staff", "beam", "notations", "lyric", "tie", "chord", "grace", "notehead", "instrument"], id),
            *c.names(c.child(node, "notations"), ["tied", "slur", "tuplet", "articulations", "fermata"], id)]}
}

fn direction(node, st) map^ {
    let supported=c.require(all([for (group in c.named(node,"direction-type")) not c.present(group,"octave-shift")]),
        "octave-shift directions are not supported")^
    let offset = if (c.present(node, "offset")) t.divide(t.from_decimal(c.field(node, "offset"))^, st.context.divisions)^ else t.ZERO
    let at = t.add(st.cursor, offset)^
    let staff = c.field_number(node, "staff", 1)
    let checked = c.require(at.num >= 0 and c.is_integer(staff) and staff >= 1 and staff <= len(st.context.staffs), "invalid direction position")^
    let sound = c.child(node, "sound")
    let entries = [for (group in c.named(node, "direction-type"), x in c.children(group))
        {kind: string(name(x)), text: trim(c.text(x)),
         value: if (name(x) == 'dynamics') string(name(c.children(x)[0])) else x.type,
         number: x.number or "1", metronome: if (name(x) == 'metronome')
             {unit: c.field(x, "beat-unit"), dots: len(c.named(x, "beat-unit-dot")), bpm: c.field_number(x, "per-minute")} else null}]
    {*:st, directions: [*st.directions, {onset: at, staff: int(staff), placement: node.placement or "above",
        entries: entries, sound: if (sound == null) {} else map([for (k,v in sound where k is symbol or k is string) (string(k),v)])}]}
}
fn measure_child(st, row, part, bar) map^ {
    let node = row.node
    let tag = string(name(node))
    if (tag == "attributes") {
        let context = attributes(node, st.context)^;
        {*:st, context: context, changes: [*st.changes, {onset: st.cursor, context: context}]}
    } else if (tag == "note") note(node, st, part, bar, row.index)^
    else if (tag == "backup" or tag == "forward") {
        let dur = duration(node, st.context.divisions)^
        let at = if (tag == "backup") t.sub(st.cursor, dur)^ else t.add(st.cursor, dur)^
        let checked = c.require(at.num >= 0, location(part, bar) ++ ": backup before measure start")^;
        {*:st, cursor: at, extent: t.later(st.extent, at), anchor: null}
    } else if (tag == "direction") direction(node, st)^
    else if (tag == "barline") {*:st, barlines: [*st.barlines, {location: node.location or "right",
        style: c.field(node, "bar-style", "regular"), repeat: c.child(node, "repeat").direction,
        times: c.child(node, "repeat").times, ending: c.child(node, "ending")} ]}
    else if (tag == "print") {*:st, new_system: node['new-system'] == "yes", new_page: node['new-page'] == "yes"}
    else if (tag == "sound") {*:st, directions: [*st.directions, {onset: st.cursor, staff: 1,
        placement: "above", entries: [], sound: map([for (k,v in node where k is symbol or k is string) (string(k),v)])}]}
    else {*:st, warnings: [*st.warnings, c.diagnostic("unsupported-decoration", "unsupported " ++ tag, location(part, bar))]}
}
fn read_measure(previous, row, part) map^ {
    let initial = {context: previous.context, cursor: t.ZERO, extent: t.ZERO, anchor: null,
        events: [], changes: [], directions: [], barlines: [], warnings: [], new_system: false, new_page: false}
    let nodes = [for (i,x in c.children(row.node)) {index: i, node: x}]
    let body = c.fold(nodes, initial, (st, x) => measure_child(st, x, part, row.index)^)
    let checked = c.require(body is map and len(body.events)<=1024, location(part, row.index) ++ ": invalid or excessive measure")^
    let initial_changes=[for (change in body.changes where change.onset.num==0) change]
    let start_context = if (len(initial_changes)>0) initial_changes[len(initial_changes)-1].context else previous.context
    let nominal = start_context.staffs[0].meter.span
    let implicit = row.node.implicit == "yes"
    let span = if (implicit) body.extent else nominal
    let valid_span = c.require(span.num > 0 and (implicit or t.compare(body.extent, nominal) <= 0), location(part, row.index) ++ ": measure duration overflow or empty pickup")^
    let bounded_directions=c.require(all([for (dir in body.directions) t.compare(dir.onset,span)<=0]),"direction offset exceeds measure")^
    let matched_meters = c.require(all([for (s in start_context.staffs) s.meter.span == nominal]), "independent staff meters are not supported")^
    let measure = {index: row.index, label: string(row.node.number or string(row.index + 1)), implicit: implicit,
        start: previous.end, span: span, context: start_context, events: body.events, changes: body.changes,
        directions: body.directions, barlines: body.barlines, new_system: body.new_system, new_page: body.new_page}
    {context: body.context, end: t.add(previous.end, span)^, measures: [*previous.measures, measure],
        warnings: [*previous.warnings, *body.warnings]}
}
fn read_part(node, declaration) map^ {
    let id = string(node.id)
    let bars = c.named(node, "measure")
    let checked = c.require(len(bars) > 0 and len(bars) <= 2048, id ++ ": measure count exceeds profile")^
    let initial = {context: {divisions: null, transpose: 0, staffs: [default_staff]}, end: t.ZERO, measures: [], warnings: []}
    let result = c.fold([for (i,bar in bars) {index:i, node:bar}], initial, (st, row) => read_measure(st, row, id)^)
    let midi = c.child(declaration, "midi-instrument")
    {id: id, label: c.field(declaration, "part-name", id), abbreviation: c.field(declaration, "part-abbreviation"),
        instruments: c.named(declaration, "score-instrument"),
        midi: {channel: c.field_number(midi, "midi-channel"), program: c.field_number(midi, "midi-program"),
            volume: c.field_number(midi, "volume"), pan: c.field_number(midi, "pan")},
        measures: result.measures, duration: result.end, diagnostics: result.warnings}
}

pub fn from_xml(document, options = {}) map^ {
    let option_issues=c.option_warnings(options,["strict","format","rootfile"])^
    let roots = if (name(document) == 'document') c.children(document) else [document]
    let root = roots[0]
    let checked = c.require(len(roots) == 1 and name(root) == 'score-partwise', "expected one score-partwise MusicXML root")^
    let valid_namespace = c.namespace_check(root)^
    let valid_version = c.require(root.version == null or contains(["1.0", "1.1", "2.0", "3.0", "3.1", "4.0"], root.version), "unsupported MusicXML version")^
    let declarations = c.named(c.child(root, "part-list"), "score-part")
    let raw_parts = c.named(root, "part")
    let ids = [for (p in raw_parts) p.id]
    let declared_ids = [for (p in declarations) p.id]
    let valid_parts = c.require(len(raw_parts) > 0 and len(raw_parts) <= 32 and len(unique(ids)) == len(ids) and
        len(unique(declared_ids)) == len(declared_ids) and all([for (id in ids) id != null and contains(declared_ids, id)]) and
        len(ids) == len(declared_ids), "invalid or duplicate part declarations")^
    let parts = [for (d in declarations) read_part([for (p in raw_parts where p.id == d.id) p][0], d)^]
    let first = parts[0]
    let aligned = c.require(all([for (p in parts) len(p.measures) == len(first.measures) and
        all([for (i,m in p.measures) m.span == first.measures[i].span])]), "parts have inconsistent measure spans")^
    let events = [for (p in parts, m in p.measures, e in m.events) e]
    let valid_events = c.require(len(events) <= 50000, "score event budget exceeded")^
    let warnings = c.warnings([*option_issues,*[for (p in parts, w in p.diagnostics) w],
        *c.names(root, ["work", "movement-number", "movement-title", "identification", "defaults", "credit", "part-list", "part"], "score")], options)^;
    {kind: "music-score", version: 1, title: c.field(root, "movement-title", c.field(c.child(root, "work"), "work-title", "Untitled score")),
        composer: join([for (x in c.named(c.child(root, "identification"), "creator") where x.type == "composer") c.text(x)], ", "),
        parts: parts, measures: first.measures, events: events, duration: first.duration, diagnostics: warnings}
}

pub fn parse(source, options = {}) map^ {
    let checked = c.require(source is string and len(source) <= 16000000, "expected XML text within the 16 MB profile")^
    from_xml(lambda.sys.parse(source, {type: 'xml', flavor: 'strict'})^, options)^
}
fn files(node) => [for (x in content(node)^) if (x.is_dir) *files(x) else x]
fn member(entries, path) element^ {
    let matches = [for (x in entries where x.entry_path == path) x]
    let checked = c.require(len(matches) == 1, "MXL member not found: " ++ string(path))^
    matches[0]
}
pub fn load(source, options = {}) map^ {
    let checked_options=c.require((options.format==null or contains(['xml','mxl'],options.format)) and
        (options.rootfile==null or options.rootfile is string),"invalid input format or rootfile option")^
    let filename = if (source is element) string(source.entry_path) else string(source)
    let archive = options.format == 'mxl' or ends_with(lower(split(filename, "?")[0]), ".mxl")
    if (archive) {
        let entries = files(input(source, 'zip')^)
        let container = lambda.sys.parse(input(member(entries, "META-INF/container.xml")^, 'text')^, {type: 'xml', flavor: 'strict'})^
        let root=c.children(container)[0]
        let valid_container=c.require(name(root)=='container' and (root.xmlns==null or root.xmlns=="") and
            len(c.named(root,"rootfiles"))==1,"invalid MXL container")^
        let roots = c.named(c.child(root, "rootfiles"), "rootfile")
        let choices = [for (r in roots where (r['media-type']==null or r['media-type']=="application/vnd.recordare.musicxml+xml") and
            (options.rootfile == null or r['full-path'] == options.rootfile)) r]
        let checked = c.require(len(choices) == 1, "MXL requires exactly one selected rootfile")^
        parse(input(member(entries, choices[0]['full-path'])^, 'text')^, options)^
    } else parse(input(source, 'text')^, options)^
}
