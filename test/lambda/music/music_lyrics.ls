import music: lambda.music
import lyrics: lambda.music.lyrics
import engraving: lambda.music.engrave
import position: lambda.music.spans
let score=music.load("test/music/lyrics.musicxml",{strict:true})^
let sheet=music.layout(score,{width:760.0,strict:true})^
let hidden=music.layout(score,{width:760.0,show_lyrics:false,strict:true})^
let groups=[for (page in sheet.pages,g in page.shapes where g.kind=="lyric") g]
let text=[for (g in groups where g.lyric_kind=="text") g]
let shine=[for (g in score.lyric_groups where g.lyric.text=="shine") g][0]
let carried=shine.members[1]
let verse1=score.parts[0].measures[0].events[0].lyrics[1]
let verse2=score.parts[0].measures[0].events[0].lyrics[0]
let a=position.positioned(sheet,score.events[0].id)
let b=position.positioned(sheet,score.events[1].id)
let xml=format(music.svg(sheet)^,'xml')^
let links=engraving.highlight_bindings(sheet)
let note_xml="<score-partwise><part-list><score-part id='P'><part-name>Voice</part-name></score-part></part-list>"++
    "<part id='P'><measure number='1'><attributes><divisions>1</divisions></attributes>"++
    "<note><pitch><step>C</step><octave>4</octave></pitch><duration>1</duration><type>quarter</type>";
fn one(extra) map^ => music.parse(note_xml++extra++"</note></measure></part></score-partwise>")^
fn rejected(extra) => ((one(extra) ^ { null }) or "rejected")=="rejected";
[
 len(text)>40,
 verse1.passes==[1] and verse2.passes==[2],
 a.lyrics[1].lane==0 and a.lyrics[0].lane==1,
 b.lyrics[1].lane==a.lyrics[1].lane,
 len([for (g in groups where g.lyric_kind=="extend" and g.lyric_id==shine.id) g])==2,
 contains(links[carried],shine.id),
 len(shine.members)==4,
 len([for (g in score.lyric_groups where g.lyric.text=="hold" and len(g.members)==2) g])==1,
 contains(xml,"Let‿our"),
 contains(xml,"data-lyric-id") and contains(xml,"data-verse=\"1\""),
 len([for (g in groups where g.lyric_kind=="hyphen") g])>2,
 len([for (page in hidden.pages,g in page.shapes where g.kind=="lyric") g])==0,
 not contains(format(music.svg(hidden)^,'xml')^,"data-lyric-id"),
 one("<lyric><text>two </text><text>words</text></lyric>")^.events[0].lyrics[0].text=="two words",
 one("<lyric><text>one</text><elision/><text>two</text></lyric>")^.events[0].lyrics[0].text=="one‿two",
 not one("<lyric print-object='no'><text>hidden</text></lyric>")^.events[0].lyrics[0].visible,
 rejected("<lyric><syllabic>wrong</syllabic><text>x</text></lyric>"),
 rejected("<lyric><extend type='continue'/></lyric>"),
 rejected("<lyric><text>x</text></lyric><lyric><text>duplicate</text></lyric>"),
 rejected("<lyric time-only='0'><text>x</text></lyric>"),
 len(one("<lyric name='verse'><text>one</text></lyric><lyric name='chorus'><text>two</text></lyric>")^.lyric_groups)==2
]
