import music: lambda.music
import tl: lambda.music.timeline
let z={num:0,den:1}
let one={num:1,den:1}
let tracks=[{id:"p",label:"Piano",channels:[0]}]
let tiny=tl.finish([tl.message(z,144,60,80),tl.message({num:1,den:1000},128,60)],[],tracks,[],one)^
let score_text=input("test/music/playback.musicxml",'text')^
fn score(text) => music.parse(text)^;
[
 ((music.midi(tiny,{ppqn:1,quantize:true}) ^ { null }) or "invalid")=="invalid",
 ((tl.finish([tl.message(z,144,60,80),tl.message(z,128,60)],[],tracks,[],one) ^ { null }) or "invalid")=="invalid",
 ((tl.finish([tl.message(z,144,60,80),tl.message(z,144,60,70),tl.message(one,128,60)],[],tracks,[],one) ^ { null }) or "invalid")=="invalid",
 ((tl.finish([tl.message(z,144,60,80)],[],tracks,[],one) ^ { null }) or "invalid")=="invalid",
 ((tl.finish([tl.message(z,144,128,80),tl.message(one,128,128)],[],tracks,[],one) ^ { null }) or "invalid")=="invalid",
 ((tl.finish([tl.message(z,144,60,80),tl.message(one,128,60)],
    [tl.tempo(z,500000),tl.tempo(z,600000)],tracks,[],one) ^ { null }) or "invalid")=="invalid",
 ((music.performance(score(replace(score_text,"<midi-channel>2</midi-channel>","<midi-channel>1</midi-channel>"))) ^ { null }) or "invalid")=="invalid",
 ((music.performance(score(replace(score_text,"<instrument id=\"piano\"","<instrument id=\"missing\""))) ^ { null }) or "invalid")=="invalid",
 ((music.performance(score(replace(score_text,"tempo=\"120\"","tempo=\"120\" dacapo=\"yes\""))) ^ { null }) or "invalid")=="invalid",
 ((music.midi(tiny,{strict:true,unknown:true}) ^ { null }) or "invalid")=="invalid",
 ((music.performance(score(replace(score_text,"<tie type=\"stop\"/>",""))) ^ { null }) or "invalid")=="invalid",
 ((music.performance(score(replace(score_text,"<ending number=\"1\"", "<repeat direction=\"forward\"/><ending number=\"1\""))) ^ { null }) or "invalid")=="invalid",
 ((music.performance(music.load("test/music/study.musicxml")^,{strict:true}) ^ { null }) or "invalid")=="invalid",
 len(music.performance(music.load("test/music/study.musicxml")^)^.diagnostics)>0
]
