import music: lambda.music
import spans: lambda.music.spans
import n: lambda.music.notation
let score=music.load("test/music/features.musicxml")^
let sheet=music.layout(score,{width:620.0,mode:'pages',page_height:850.0})^
let continuous=music.layout(score,{width:1000.0,respect_breaks:false,parts:["F"]})^
let first=sheet.systems[0].events
let xml=format(music.svg(sheet)^,'xml')^;
[
 score.title=="Reflow & Rhythm",
 score.events[1].onset=={num:1,den:3},
 score.events[2].onset=={num:2,den:3},
 len(sheet.systems)==3,
 len(sheet.pages)==2,
 len(sheet.events)==len(score.events),
 all([for (id,e in sheet.events) e.bounds.right>=e.bounds.left and e.bounds.bottom>=e.bounds.top and e.bounds.left>=0.0]),
 first[0].accidental_glyph==null,
 [for (e in first where e.pitch.step=="F" and e.pitch.alter==0) e.accidental_glyph][0]=="accidentalNatural",
 [for (e in first where e.grace) e.x][0]<[for (e in first where not e.grace and e.onset.num==1 and e.onset.den==1) e.x][0],
 len(spans.resolve(score)^)==5,
 len(continuous.parts)==1,
 len(continuous.events)==len(score.parts[0].measures[0].events)+len(score.parts[0].measures[1].events)+len(score.parts[0].measures[2].events),
 contains(xml,"data-event-id"),
 contains(xml,"@font-face"),
 contains(xml,"Reflow &amp; Rhythm"),
 name(music.render(score,{parts:["V"]})^)=='div',
 n.height({step:"C",octave:4},{sign:"C",line:3,octave:0})==2.0,
 n.height({step:"G",octave:4},{sign:"G",line:2,octave:0})==3.0,
 n.height({step:"F",octave:3},{sign:"F",line:4,octave:0})==1.0,
 len(sheet.diagnostics)==0,
 (music.svg(sheet,2) or "rejected")=="rejected",
 (music.layout(score,{width:0.0}) or "rejected")=="rejected",
 (music.layout(score,{parts:["missing"]}) or "rejected")=="rejected",
 (music.layout(score,{strict:true,unknown:1}) or "rejected")=="rejected"
]
