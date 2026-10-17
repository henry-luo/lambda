import music: lambda.music
import tl: lambda.music.timeline
let bytes=b'\x4D546864000000060000000100604D54726B0000001C00FF510307A12000C00000903C64603C0000FF51030F424060FF2F00'
let p=music.read_midi(bytes)^
let roundtrip=music.read_midi(music.midi(p)^)^
let third=tl.finish([tl.message({num:0,den:1},144,60,80),tl.message({num:1,den:3},128,60)],[],
    [{id:"p",label:"Piano",channels:[0]}],[],{num:1,den:1})^
let precise=music.read_midi(music.midi(third)^)^;
[
 p.source_format==0,
 p.source_ppqn==96,
 p.seconds==1.5,
 len(p.events)==3,
 p.events[2].status==144 and p.events[2].b==0,
 p.events[2].at=={num:1,den:1},
 roundtrip.source_format==1,
 roundtrip.seconds==1.5,
 [for (e in roundtrip.events) [e.status,e.a,e.b,e.at]]==[for (e in p.events) [e.status,e.a,e.b,e.at]],
 precise.events[1].at=={num:1,den:3},
 ((music.midi(third,{ppqn:100}) ^ { null }) or "invalid")=="invalid",
 music.read_midi(music.midi(third,{ppqn:100,quantize:true})^)^.events[1].at=={num:33,den:100},
 ((music.read_midi(slice(bytes,0,len(bytes)-1)) ^ { null }) or "invalid")=="invalid",
 ((music.read_midi(b'\x4D546864000000060002000100604D54726B0000000400FF2F00') ^ { null }) or "invalid")=="invalid",
 ((music.read_midi(b'\x4D5468640000000600000001E7284D54726B0000000400FF2F00') ^ { null }) or "invalid")=="invalid",
 ((music.read_midi(b'\x4D546864000000060000000100604D54726B0000000480808080') ^ { null }) or "invalid")=="invalid",
 ((music.read_midi(b'\x4D546864000000060000000100604D54726B00000008003C4000FF2F0000') ^ { null }) or "invalid")=="invalid"
]
