# Lambda demos

Run `./lambda.exe demo <name>` from the repository root, or `./lambda demo <name>`
from an unpacked release. Each named demo uses the same viewer as `lambda view`.
Run `lambda demo --list` for the catalog. With no name, `lambda demo` opens the
bundled document viewer with its startup splash (D7.2.4).

| Name | Entry point | Guide |
| --- | --- | --- |
| `tetris` | `tetris/tetris.ls` | [Tetris](tetris/TETRIS.md) |
| `sentence-game` | `sentence-game/sentence-game.ls` | [Sentence Match](sentence-game/README.md) |
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
| `slides` | `slides/northstar.slides` | [Slides](slides/README.md) |
| `readability` | `readability/readability.ls` | [Reader View](readability/README.md) |
| `archive` | `archive/archive.ls` | [Archive](archive/README.md) |
| `wordcloud` | `wordcloud.ls` | [Charts](CHARTS.md) |

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

## Interactive Tetris

Run `./lambda.exe demo tetris` for a native, interactive Tetris
game written in Lambda. See [TETRIS.md](tetris/TETRIS.md) for controls and test commands.
