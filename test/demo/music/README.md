# Music

`./lambda.exe demo music` is a score player. The library on the left lists five pieces. The chosen one is engraved on A4 pages by the [`lambda.music`](../../../lmd/package/music.ls) package and played through its SoundFont player.

| Area | What it does |
| --- | --- |
| Library | Each score's title, composer, instrumentation, bar count and length. A click opens it; a playing score stops first |
| Transport | **Play**, **Pause** and **Stop**, a position slider, speed from 0.5× to 2×, and volume |
| Tracks | **Mute** and **Solo** for each instrument |
| Sheet | The sounding notes and chords, and the lyric syllable being sung, light up in the chosen highlight colour (red, blue or yellow). The sheet scrolls to follow the music |

The player is `music.player(score, {layout})`; this demo adds the library around it. The page layout is the A4 portrait one of the package's own samples (`test/music/_sample.ls`).

## Scores

All five are in `scores/` as MusicXML.

| Score | File | Notes |
| --- | --- | --- |
| Ode to Joy | `ode-to-joy.musicxml` | Beethoven's theme from the Ninth Symphony (1824, public domain), arranged for piano by Lambda Music: melody over block chords |
| Twinkle, Twinkle, Little Star | `twinkle.musicxml` | Traditional French melody with Jane Taylor's words (1806, public domain), arranged for piano by Lambda Music, with lyrics |
| Morning Song | `lyrics.musicxml` | Two verses, a repeat and a composite time signature |
| Morning Study | `a4-study.musicxml` | Twenty bars on five piano systems |
| Two Passes | `playback.musicxml` | Two instruments with repeats, endings, ties and tempo changes |

The last three are copies of original fixtures from `test/music/`; see [test/music/README.md](../../music/README.md). The demo keeps its own copies because a release ships `test/demo` but not `test/music`.

## Layout note

The player sizes itself to 100% of its box. Its scrolling sheet area renders only when that box is the full window height (`100vh`). A shorter box, whether a fixed pixel height or `calc()`, leaves the sheet unpainted in Radiant, so the library is a fixed column beside a full-height player.
