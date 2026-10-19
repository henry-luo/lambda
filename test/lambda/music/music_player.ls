import music: lambda.music
import engraving: lambda.music.engrave
let score=music.load("test/music/study.musicxml")^
let player=music.player(score,{layout:{width:760.0}})^
let midi_player=music.player(music.performance(score)^)^
let html=format(player,'html')^
let midi_html=format(midi_player,'html')^;
let links=engraving.highlight_bindings(music.layout(score)^)
let beam=links[score.events[1].id][len(links[score.events[1].id])-1];
[
 name(player)=='section',
 contains(html,"data-state=\"ready\""),
 contains(html,"Playback position"),
 contains(html,"data-event-id"),
 contains(html,"data-control=\"mute\""),
 contains(midi_html,"Playback speed"),
 not contains(midi_html,"<svg"),
 ((music.player({}) ^ { null }) or "invalid")=="invalid",
 contains(html,"Note highlight color"),
 all([for (color in ["red","blue","yellow"]) contains(html,"data-color=\""++color++"\"")]),
 not contains(midi_html,"Note highlight color"),
 starts_with(beam,"beam/") and contains(links[score.events[2].id],beam),
 contains(links[score.events[4].id],score.events[3].id)
]
