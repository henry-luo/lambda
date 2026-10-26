# Lambda demos

Run `./lambda.exe demo <name>` from the repository root, or `./lambda demo <name>`
from an unpacked release. Each named demo uses the same viewer as `lambda view`.
Run `lambda demo --list` for the catalog.

With no name, `lambda demo` opens the demo index (`index.ls`): one animated tile per
demo. A click starts that demo as its own `lambda demo <name>` process, so it opens
in a window of its own; closing the index also closes the demos it started. The
tile catalog is in `index.ls` and its art in `mod_demo_art.ls`; a new named demo
needs an entry there as well as in `LAMBDA_DEMOS` (`lambda/main.cpp`). The document
viewer's startup splash is still at `lmd/package/doc/doc_viewer.html`.

| Name | Entry point | Guide |
| --- | --- | --- |
| `tetris` | `tetris/tetris.ls` | [Tetris](tetris/TETRIS.md) |
| `marble-drop` | `marble-drop/marble-drop.ls` | [Marble Drop](marble-drop/README.md) |
| `sentence-game` | `sentence-game/sentence-game.ls` | [Build Sentence](sentence-game/README.md) |
| `superlambda` | `superlambda/superlambda.ls` | [Superlambda](superlambda/README.md) |
| `doom` | `doom/doom.ls` | [DOOM](doom/README.md) |
| `photo` | `photo/photo.ls` | [Photo Studio](photo/README.md) |
| `photos` | `photos/photos.ls` | [Layered Photos editor](photos/README.md) |
| `draw` | `draw/draw.ls` | [Draw](draw/README.md) |
| `word` | `word/word.ls` | [Word](word/README.md) |
| `finder` | `finder/finder.ls` | [Finder](finder/README.md) |
| `note` | `note/note.ls` | [Notes](note/README.md) |
| `scene3d` | `scene3d/index.html` | [Scene3D collection](scene3d/README.md) |
| `chart` | `chart/index.html` | [Chart collection](chart/README.md) |
| `cityscape` | `cityscape/cityscape.ls` | [The Rings of a City](cityscape/README.md) |
| `photo-album` | `photo-album/photo-album.ls` | [Photo album](photo-album/README.md) |
| `slides` | `slides/slides.ls` | [Slides](slides/README.md) |
| `readability` | `readability/readability.ls` | [Reader View](readability/README.md) |
| `archive` | `archive/archive.ls` | [Archive](archive/README.md) |
| `mail` | `mail/mail.ls` | [Mail](mail/README.md) |
| `music` | `music/music.ls` | [Music](music/README.md) |
| `soundflow` | `soundflow/soundflow.ls` | [Soundflow](soundflow/README.md) |
| `greetings` | `greetings/greetings.ls` | [Greetings](greetings/README.md) |

Paths in the table are relative to `test/demo/`. Viewer options follow the name:

```sh
./lambda.exe demo tetris
./lambda.exe demo scene3d
./lambda.exe demo tetris --headless --event-file test/ui/tetris_autoplay.json \
  --event-result temp/tetris-demo.json
```

The games and Ringworld include an **Auto Play** button. Direct `lambda view`
commands and individual test/replay commands remain available in each guide.

The [physics experiments](scene3d/physics/README.md) include four interactive native
Scene3D experiments: a domino cascade, compound bodies in zero gravity, a
marble sorter and a bounce comparison with trajectory charts. Open `./lambda.exe demo scene3d` and choose any of the four experiment tiles directly. The same index
also contains CellScope, Ringworld, Crystal garden, Observatory, and Shared Animation.

[The Rings of a City](cityscape/README.md) rebuilds Chengdu inside the Third Ring
Road from OpenStreetMap, then plays its history from 1250 BC to 2022 as one
150-second native Scene3D clip. **♪ Sound** replays it with an original score composed in
Lambda and played through the music package's SoundFont. Generate the model with
`python3 -I test/demo/cityscape/_generate.py --fetch`, then open it with
`./lambda.exe demo cityscape`.

## Reader View

`./lambda.exe demo readability` opens a small browser window with three cluttered articles and a newspaper front page. The page icon at the right of the address bar switches a tab to the article that [`lambda.doc.readability`](../../doc/Lambda_Packages.md#21-docreadability--article-extraction) extracts, a port of Mozilla's Readability.js. The icon is disabled on the front page, which fails the package's `readerable` check. See [Reader View](readability/README.md).

## Archive

`./lambda.exe demo archive` is a personal web archive built on [`lambda.archive`](../../doc/Lambda_Packages.md): paste a URL or a local path, watch the job's progress, then browse, filter and search the library and view each snapshot, its readable page, Markdown and record. Pages, video and audio (direct files, HLS streams, subtitle tracks), galleries and files are archived by Lambda code alone. A first run archives a small local sample. See [Archive](archive/README.md).

## Mail

`./lambda.exe demo mail` is a mail client in the spirit of macOS Mail. It reads IMAP accounts (Gmail, iCloud, Fastmail and Yahoo with app passwords, or Proton Mail Bridge) through [`lambda.io.mail`](../../doc/Lambda_Sys_Func.md#lambdaiomail) and keeps passwords in the Keychain. The window has a sidebar of mailboxes, a message list that mounts only the rows in view, and a viewer that shows HTML mail in a sandboxed, script-free frame with remote content blocked until you load it. See [Mail](mail/README.md).

`./lambda.exe demo music` plays sheet music. A library of five scores sits on the left; the chosen one is engraved on A4 pages by [`lambda.music`](../../doc/Lambda_Packages.md) and played through its SoundFont player, with the sounding notes, chords and lyric syllables highlighted as they play. See [Music](music/README.md).

## Soundflow

`./lambda.exe demo soundflow` is a visual instrument after Agoston Nagy's SoundBow. Draw across a field of strings: each stroke replays in a loop, and its tip sounds every string it crosses. Drag a string's end to retune it, since longer strings sound lower, or its middle to slide it sideways. The loops can play from half to double speed, and the wide strokes can be hidden to leave only the moving tips. Harp, kalimba and glass voices are synthesized in Lambda. **Save** writes the loops as a WAV recording and the drawing as SVG. See [Soundflow](soundflow/README.md).

## Greetings

`./lambda.exe demo greetings` is a greeting card studio. Twenty designs for ten occasions give a front, an inside spread with a verse and a back; text in sixteen fonts, clip art drawn in Lambda, framed photos, papers, patterns and borders change any page. The preview swings the folded card open, and **Print & share** writes page images, print sheets that fold into the card, a print-ready PDF or a card file. See [Greetings](greetings/README.md).

## Interactive Tetris

Run `./lambda.exe demo tetris` for a native, interactive Tetris
game written in Lambda. See [TETRIS.md](tetris/TETRIS.md) for controls and test commands.
