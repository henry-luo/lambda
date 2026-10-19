import music: lambda.music
import t: lambda.music.time
let score=music.load("test/music/playback.musicxml")^
let performance=music.performance(score)^
let notes=[for (e in performance.events where e.status div 16==9) e]
let offs=[for (e in performance.events where e.status div 16==8) e]
let fast=music.performance(score,{tempo:240})^;
[
 performance.kind=="music-performance",
 performance.duration=={num:16,den:1},
 performance.seconds==10.0,
 len(performance.occurrences)==8,
 [for (o in performance.occurrences) o.pass]==[1,1,1,1,2,2,2,2],
 len(notes)==6,
 [for (e in notes where e.status==144) e.a]==[62,62],
 [for (e in offs where e.status==128) e.seconds]==[6.0,10.0],
 [for (e in notes where e.status==145) e.a]==[66,69,66,71],
 all([for (e in notes) e.b==90]),
 [for (e in performance.events where e.status==193) e.a]==[40],
 performance.tracks[0].channels==[0,1],
 fast.seconds==4.0,
 performance.occurrences[0].id==performance.occurrences[4].id,
 performance.occurrences[0].seconds!=performance.occurrences[4].seconds,
 music.read_midi(music.midi(performance)^)^.seconds==performance.seconds,
 ((music.performance(music.load("test/music/features.musicxml")^) ^ { null }) or "unsupported")=="unsupported",
 ((music.performance(score,{tempo:0}) ^ { null }) or "invalid")=="invalid"
]
