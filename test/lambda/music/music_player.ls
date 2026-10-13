import music: lambda.music
let score=music.load("test/music/study.musicxml")^
let player=music.player(score,{layout:{width:760.0}})^
let midi_player=music.player(music.performance(score)^)^
let html=format(player,'html')^
let midi_html=format(midi_player,'html')^;
[
 name(player)=='section',
 contains(html,"data-state=\"ready\""),
 contains(html,"Playback position"),
 contains(html,"data-event-id"),
 contains(html,"data-control=\"mute\""),
 contains(midi_html,"Playback speed"),
 not contains(midi_html,"<svg"),
 (music.player({}) or "invalid")=="invalid"
]
