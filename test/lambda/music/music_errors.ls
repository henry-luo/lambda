import music: lambda.music
import t: lambda.music.time
fn document(body) => "<score-partwise version='4.0'><part-list><score-part id='P'><part-name>Test</part-name></score-part></part-list><part id='P'><measure number='1'><attributes><divisions>12</divisions></attributes>"++body++"</measure></part></score-partwise>"
let quarter="<note><pitch><step>C</step><octave>4</octave></pitch><duration>12</duration><type>quarter</type></note>"
fn rejected(source) => ((music.parse(source) ^ { null }) or "rejected")=="rejected"
fn bad(body) => rejected(document(body))
fn bad_layout(body) => ((music.layout(music.parse(document(body))^) ^ { null }) or "rejected")=="rejected";
[
 rejected("<score-partwise><broken></score-partwise>"),
 rejected(document(quarter)++"<extra/>"),
 rejected(replace(document(quarter),"<part id='P'>","<part id='P' id='Q'>")),
 rejected(replace(document(quarter),"<part id='P'>","<part id='P' xmlns='urn:foreign'>")),
 rejected("<!DOCTYPE score-partwise [<!ENTITY x 'text'>]>"++document(quarter)),
 rejected(document(quarter)++"<!DOCTYPE score-partwise>"),
 rejected("<!DOCTYPE score-partwise><!DOCTYPE score-partwise>"++document(quarter)),
 music.parse("<!DOCTYPE score-partwise SYSTEM 'https://invalid.example/no-network.dtd'>"++document(quarter))^.kind=="music-score",
 bad("<backup><duration>12</duration></backup>"++quarter),
 bad(replace(quarter,"<duration>12</duration>","<duration>60</duration>")),
 bad(replace(quarter,"<duration>12</duration>","<duration>0</duration>")),
 bad(replace(quarter,"<note>","<note><chord/>")),
 bad(replace(quarter,"<note>","<note><staff>2</staff>")),
 bad(replace(quarter,"<octave>4</octave>","<octave>4</octave><alter>0.5</alter>")),
 bad("<attributes><staff-details><staff-lines>6</staff-lines></staff-details></attributes>"++quarter),
 bad(replace(quarter,"<note>","<note><cue/>")),
 bad(quarter++"<backup><duration>12</duration></backup>"++quarter),
 bad_layout(replace(quarter,"</note>","<beam number='1'>begin</beam></note>")),
 bad_layout(replace(quarter,"</note>","<notations><slur type='stop'/></notations></note>")),
 bad(replace(quarter,"<pitch><step>C</step><octave>4</octave></pitch>","<rest/>")++replace(quarter,"<note>","<note><chord/>")),
 bad_layout(replace(quarter,"</note>","<tie type='start'/></note>")++quarter++
     replace(quarter,"</note>","<tie type='stop'/></note>")),
 ((music.parse(document(quarter++"<harmony/>"),{strict:true}) ^ { null }) or "rejected")=="rejected",
 len(music.parse(document(quarter++"<harmony/>"))^.diagnostics)==1,
 ((t.ratio(1,0) ^ { null }) or "rejected")=="rejected",
 ((t.from_decimal("1e2") ^ { null }) or "rejected")=="rejected",
 t.from_decimal("0.125")^=={num:1,den:8},
 t.add(t.ratio(1,3)^,t.ratio(1,6)^)^=={num:1,den:2}
]
