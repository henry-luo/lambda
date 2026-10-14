import music: lambda.music
let header="<part-list><score-part id='P'><part-name>Voice</part-name></score-part></part-list>"
let attrs="<attributes><divisions>6</divisions><time><beats>2</beats><beat-type>4</beat-type><beats>3</beats><beat-type>8</beat-type></time></attributes>"
let notes="<note><pitch><step>C</step><octave>4</octave></pitch><duration>9</duration><dot/></note>"++
    "<note><pitch><step>D</step><octave>4</octave></pitch><duration>4</duration>"++
    "<time-modification><actual-notes>3</actual-notes><normal-notes>2</normal-notes></time-modification></note>"
fn partwise(body) => "<score-partwise>"++header++"<part id='P'><measure number='1'>"++body++"</measure></part></score-partwise>"
fn timewise(body) => "<score-timewise>"++header++"<measure number='1'><part id='P'>"++body++"</part></measure></score-timewise>"
fn rejected(xml) => (music.parse(xml) or "rejected")=="rejected"
let score=music.parse(partwise(attrs++notes),{strict:true})^
let timed=music.parse(timewise(attrs++notes),{strict:true})^
let sheet=music.layout(timed,{strict:true})^
let played=music.performance(timed,{tempo:120,strict:true})^;
[
 score.events==timed.events,
 score.duration=={num:7,den:2},
 score.events[0].note_type=="quarter" and score.events[0].dots==1,
 score.events[1].note_type=="quarter" and score.events[1].duration=={num:2,den:3},
 len(score.events[0].context.meter.groups)==2,
 score.events[0].context.meter.groups[1].unit==8,
 len(sheet.diagnostics)==0,
 len(played.occurrences)==2 and played.seconds==1.75,
 rejected(replace(timewise(attrs++notes),"<part id='P'>","<part id='other'>")),
 rejected(replace(timewise(attrs++notes),"</part></measure>","</part><part id='P'/></measure>")),
 rejected(partwise(replace(attrs,"<beat-type>8</beat-type>","" )++notes)),
 rejected(partwise(replace(notes,"<duration>9</duration>","<duration>5</duration>"))),
 rejected(partwise(replace(notes,"<normal-notes>2</normal-notes>","<normal-notes>0</normal-notes>"))),
 rejected(replace(timewise(attrs++notes),"<score-timewise>","<score-timewise xmlns='urn:foreign'>")),
 music.load("test/music/lyrics-timewise.mxl")^.events==music.load("test/music/lyrics.musicxml")^.events
]
