import music: lambda.music.musicxml
import t: lambda.music.time
let score = music.load("test/music/study.musicxml")^
let notes = score.parts[0].measures[0].events;
[
 score.title == "Morning Study",
 len(score.parts) == 1,
 len(score.events) == 12,
 score.duration == {num:8, den:1},
 notes[4].onset == notes[3].onset,
 notes[6].onset == t.ZERO,
 notes[6].duration == {num:4, den:1},
 notes[6].context.clef.sign == "F",
 score.parts[0].measures[1].start == {num:4, den:1},
 t.divide(t.from_decimal("0.5")^, t.from_decimal("1.5")^)^ == {num:1,den:3},
 len(score.diagnostics) == 0
]
