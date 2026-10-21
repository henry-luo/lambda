# Photo album

A photo library written in Lambda and rendered by Radiant: a date timeline in a
justified grid, albums, search, favorites and a full-screen viewer, over 1000
featured photographs from Wikimedia Commons. The design and the later features
(collage, memories film, album book, slideshow) are in
[the proposal](../../../vibe/Lambda_Demo_Photo_Album.md).

## Run it

The photos are not stored in git. Download them once, from the repository root:

```sh
python3 -I test/demo/photo-album/_fetch.py
./lambda.exe run test/demo/photo-album/_thumbs.ls
```

The first command downloads the 1000 photos listed in `photos.json`, about
317 MB, into `photos/`. The second builds 512 px grid thumbnails in `thumbs/`.
Both skip finished work when run again. Then open the demo:

```sh
./lambda.exe demo photo-album
```

Without the download, the demo still opens. Tiles show each photo's average
colour, and a banner repeats the commands above.

## Using it

| Action | How |
| --- | --- |
| Browse | Scroll the timeline. Drag or click the year rail on the right to jump. |
| Tile size | The slider, the `−`/`+` buttons, the `-`/`=` keys, or Ctrl/⌘ + wheel over a photo. The photo under the pointer stays in place. |
| Select | Click; ⌘/Ctrl-click toggles; Shift-click selects a range; drag on the background draws a selection band; ⌘/Ctrl+A selects all; Escape clears. |
| Move focus | Arrow keys, Home and End; Space toggles selection of the focused photo. |
| Favorite | `F`, or the selection bar's **Add to Favorites**. |
| Search | ⌘/Ctrl+F. Every word must match a title, description, keyword, album, camera, year or month. |
| Albums | The sidebar's album list, or **Albums** for cover cards. |
| Slideshow | **Slideshow** in the toolbar (the photos shown), the selection bar (the selection) or the viewer (from that photo). Choose a name, theme, transition and pace, then **Present** or **Save as .slides**. Decks are saved in `slideshows/` and open with `lambda view`. In the show, the player's keys apply; Escape returns to the album. |
| Viewer | Double-click a photo or press Enter. ←/→ step, Home/End jump, `I` shows info, `F` favorites, `=`/`-`/`0` zoom, drag pans a zoomed photo, Escape closes. |

## Files

| File | Role |
| --- | --- |
| `photo-album.ls` | Entry point: session, publishing, grid mounting and event handlers |
| `mod_library.ls` | The immutable library: ordering, albums, scopes and search |
| `mod_grid.ls` | Justified row packing, the mounted row window, hit testing and zoom anchoring |
| `mod_ui.ls` | Markup for the shell, grid rows, album cards, viewer and slideshow sheet |
| `mod_deck.ls` | Slideshow decks for `lambda.slide`: slide plan, layouts, notes and credits |
| `photo-album.css` | Styles |
| `mod_corpus.ls` | Thumbnail and colour building shared by `_thumbs.ls` and `_fixture.ls` |
| `_fetch.py` | Selects and downloads the Commons corpus; `--refresh`, `--verify`, `--no-download` |
| `photos.json` | The pinned selection, with metadata and attribution |
| `fixture/` | A committed synthetic corpus of 24 gradient images, made by `_fixture.ls` |
| `benchmark.py` | Times an instrumented copy over the full corpus with a release host |

The page also imports `../mod_publish.ls`, the write-only-on-change DOM helpers
it shares with Finder.

`PHOTO_ALBUM_DIR` points the demo, `_thumbs.ls` and the tests at another corpus
directory, relative to the current directory.

## Tests

```sh
./lambda.exe test/demo/photo-album/tests/album_library.ls
./lambda.exe test/demo/photo-album/tests/album_grid.ls
./lambda.exe test/demo/photo-album/tests/album_deck.ls
./test/test_ui_automation_gtest.exe --suite photo_album
```

The `.ls` tests compare against their `.txt` goldens in `test_lambda_gtest`.
The UI replays in `test/ui/photo_album/` run headless against `fixture/`, or
against `tests/no_photos/` for the not-downloaded state. `saved_deck.json`
opens `tests/fixture_slideshow.slides`, a deck the album saved, on its own.

## Credits

Every photo comes from Wikimedia Commons featured pictures under CC BY-SA,
CC BY, CC0 or public domain terms. The author, license and source page of each
photo are in `photos.json` and in the viewer's info panel.
