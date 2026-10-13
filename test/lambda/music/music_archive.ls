import music: lambda.music
let plain=music.load("test/music/study.musicxml")^
let zipped=music.load("test/music/study.mxl")^;
[
 zipped.events==plain.events,
 zipped.title=="Morning Study",
 (music.load("test/music/ambiguous.mxl") or "rejected")=="rejected",
 music.load("test/music/ambiguous.mxl",{rootfile:"second.musicxml"})^.title=="Second score",
 (music.load("test/music/study.mxl",{rootfile:"missing.musicxml"}) or "rejected")=="rejected",
 (music.load("test/music/study.mxl",{rootfile:"../scores/main.musicxml"}) or "rejected")=="rejected",
 (music.load("test/music/study.musicxml",{format:'unknown'}) or "rejected")=="rejected"
]
