import music: lambda.music
let plain=music.load("test/music/study.musicxml")^
let zipped=music.load("test/music/study.mxl")^;
[
 zipped.events==plain.events,
 zipped.title=="Morning Study",
 ((music.load("test/music/ambiguous.mxl") ^ { null }) or "rejected")=="rejected",
 music.load("test/music/ambiguous.mxl",{rootfile:"second.musicxml"})^.title=="Second score",
 ((music.load("test/music/study.mxl",{rootfile:"missing.musicxml"}) ^ { null }) or "rejected")=="rejected",
 ((music.load("test/music/study.mxl",{rootfile:"../scores/main.musicxml"}) ^ { null }) or "rejected")=="rejected",
 ((music.load("test/music/study.musicxml",{format:'unknown'}) ^ { null }) or "rejected")=="rejected"
]
