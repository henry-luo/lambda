# Photo Album Demo — Proposal

**Status:** Proposal, 2026-10-20. All milestones are implemented: M0 (corpus
preparation), M1 (browsing and viewer), M2 (slideshow), M3 (collage), M4 (album
book) and M5 (film and places); see the [M0](#m0-checkpoint--2026-10-20),
[M1](#m1-checkpoint--2026-10-20), [M2](#m2-checkpoint--2026-10-21),
[M3](#m3-checkpoint--2026-10-21), [M4](#m4-checkpoint--2026-10-21) and
[M5](#m5-checkpoint--2026-10-21) checkpoints. This
document proposes a new demo application. It introduces no language or runtime
rulings.

**Scope:** a new demo under `test/demo/photo-album/`, registered as
`lambda demo photo-album`; the preparation script that downloads its 1000-photo
corpus; and the small Lambda/Radiant gaps the demo may expose. The existing
`photo` (Photo Studio) and `photos` (layered editor) demos are editors and stay
separate; the album is a viewer and storyteller.

**Specification linkage:** [Formal Semantics](../doc/Lambda_Formal_Semantics.md)
S1.4 (values never alias; session state is explicit), S12.1.1v3 and S12.1.3
(pure `fn` templates, mutation only in `pn` handlers), S12.4.1v3 (resource
acquisition is `pn`-only). [Formal Design](../doc/Lambda_Formal_Design.md)
D7.2.4v3 (shipped `lambda.*` packages the demo imports), D7.5.3v2 (the
Lambda↔Radiant boundary), D7.1.2v2 (all file IO through `lambda-io`), D4.5.1v4
and D4.5.2 (Radiant owns native pixels and never retains GC pointers),
D5.3.3 (precise rooting in any native helper), D7.5.4 (audible output is gated
by document origin; it governs the film soundtrack). The slide-package rulings
SLD1–SLD7 in [Lambda_Pkg_Slides.md](Lambda_Pkg_Slides.md) govern the slideshow
and film playback.

**Related records:** [Finder demo](Lambda_Demo_Finder.md) (viewport-windowed
rows and the thumbnail scheduler this demo reuses),
[slide package](Lambda_Pkg_Slides.md) and its [public reference](../doc/Lambda_Slide.md),
[cityscape README](../test/demo/cityscape/README.md) (fetch-script and
soundtrack precedent), and the image API in
[Lambda_Sys_Func.md](../doc/Lambda_Sys_Func.md) (`load`, `save`,
`image_process`, `radiant.image_job`).

---

## 1. Purpose and product direction

The demo shows that Lambda and Radiant can carry a polished, media-heavy
consumer application, built mostly from Lambda modules and HTML/CSS:

1. **Library browsing**: a date timeline, albums, search, and a full-screen
   viewer over 1000 real photos.
2. **Collage**: arrange selected photos into a templated or free-form collage
   and export it as an image.
3. **Memories film**: a video-like story with pan-and-zoom motion, crossfades,
   captions and a soundtrack, generated from an album, a date range or a
   selection.
4. **Classic album**: a bound photo book with mounted prints, captions and a
   3D page turn.
5. **Slide presentation**: turn a selection into a `.slides` deck with themes
   and animated transitions, and present it in the slide player.

The reference products are Apple Photos and Immich for browsing, and the
"Memories" features of Apple and Google Photos for the film. The demo does not
attempt photo editing, face recognition, cloud sync or import from devices.

Two principles shape the design:

- **One library model, five presentations.** All five features read the same
  immutable library value and the same selection. Each presentation is a pure
  function from library, selection and its own small state to markup (S12.1.3).
- **Reuse before building.** The grid reuses Finder's row windowing and
  thumbnail scheduler; the slideshow and the film reuse the slide package's
  track sampler, effects and transitions. Where a Finder helper fits, it is
  promoted to a shared module, never copied (AGENTS rule 13).

## 2. Photo corpus preparation

### 2.1 Source choice: Wikimedia Commons

The corpus comes from **Wikimedia Commons**, through its public MediaWiki API.

| Need | How Commons meets it |
|---|---|
| Free to download and redistribute | Every file has a free license: CC0, public domain, CC BY or CC BY-SA. Attribution and share-alike are satisfied by showing credits. |
| High quality | Featured and quality pictures are reviewed photographs at high resolution. |
| Real metadata | The API returns capture date, camera, exposure, GPS coordinates, author, license, description and categories. |
| Sized downloads | The API returns scaled thumbnail URLs, so no full-size originals are needed. |
| No account | No API key is required; the API asks for a descriptive User-Agent and serial requests. |

Unsplash Lite was considered first and is rejected for this purpose. Its
dataset terms forbid publishing or redistributing any portion of the data
without written permission. A demo that ships in a public repository and its
releases should not depend on that. Openverse, Pexels and Pixabay were also
considered; their metadata is thinner, and Pexels and Pixabay need keys and
discourage bulk mirroring.

### 2.2 Selection policy

The fetch script draws the 1000 photos from eleven topic albums with fixed
quotas, so albums are meaningful without clustering heuristics. Each album reads
one or more Commons "Featured pictures of …" categories. Depth 1 also reads the
category's direct subcategories.

| Album | Quota | Featured-picture categories (depth) |
|---|---|---|
| Landscapes | 120 | landscapes, forests, lakes, waterfalls, volcanoes |
| Mountains | 80 | mountains (1) |
| Coasts and sea | 100 | coasts (1), beaches, lighthouses (1), boats, islands |
| Cities | 120 | cityscapes (1), skylines, trams |
| Architecture | 100 | buildings (1), palaces, castles (1), bridges (1), churches (1) |
| Animals | 120 | birds (1), mammals (1), animals (1), cats, dogs, reptiles (1) |
| Plants and flowers | 80 | flowers (1), trees, fungi, plants |
| Night and sky | 60 | sunsets, astronomy (1), the Milky Way, clouds, lightning, storms |
| Food | 40 | food (1) |
| Fog, snow and ice | 60 | fog, snow, ice, Christmas |
| People and events | 120 | people (1), children, concerts (1), sports (1), dance (1), musicians (1) |

A file listed under several albums belongs to the first album in this order.
If an album has fewer usable candidates than its quota, the shortfall moves to
the albums with the largest surplus, so the total is always 1000. Every
candidate must pass these filters:

- The MIME type is JPEG.
- The capture date (`DateTimeOriginal`) is present and plausible. This drives
  the timeline.
- The camera make is present. This excludes illustrations, maps and scans that
  appear in featured-picture categories.
- The license is one of CC0, PD, CC BY or CC BY-SA, in any version.
- The original is at least 1600 px on its short edge, and no wider than 3:1.
- The original's content hash is not a duplicate.
- **Content.** The title and categories do not match a blocklist of sensitive
  subjects, such as nudity, injury, poverty, protest and weapons. The demo
  remixes photos into collages and films, so photos carrying Commons'
  personality-rights warning are also excluded; those identifiable people have
  not consented to such uses.

Within a quota the script prefers files with GPS coordinates, then spreads
capture years, so the timeline and the places view both have material.
Selection is deterministic: candidates are sorted by page ID and filtered in
order, so a rerun with the same configuration picks the same files.

### 2.3 Download and layout

The preparation command follows the cityscape precedent. It is a Python script
that uses only the standard library and runs isolated:

```sh
python3 -I test/demo/photo-album/_fetch.py             # download the pinned selection
python3 -I test/demo/photo-album/_fetch.py --refresh   # re-resolve the selection first
python3 -I test/demo/photo-album/_fetch.py --verify    # check local files only
./lambda.exe run test/demo/photo-album/_thumbs.ls      # grid thumbnails and colours
```

`--refresh` caches every API response under `temp/photo-album/api/`, so a
repeated refresh with the same configuration runs offline in seconds.

Resulting layout:

```text
test/demo/photo-album/
  .gitignore            # photos/ and thumbs/
  photos.json           # pinned manifest, committed
  photos/               # 1000 JPEGs, 1280 px wide, not committed
  thumbs/               # 512 px grid thumbnails, not committed
```

- **Download size.** The script requests the 1280 px-wide scaled image, a
  standard Commons thumbnail step. The originals are not fetched. The measured
  total is in the M0 checkpoint.
- **Robustness.** Requests are serial with a descriptive User-Agent and
  back-off on HTTP 429 and 5xx responses, honouring `Retry-After`. API calls
  send `maxlag`. Downloads go to a `.part` file and are renamed on completion.
  A rerun skips files whose size and SHA-1 match the manifest, so an
  interrupted run resumes.
- **Naming.** Files are named by the zero-padded manifest index plus a short
  slug, for example `0042-matterhorn-sunrise.jpg`. The manifest is the only
  link between a file and its metadata.
- **Thumbnails.** `_thumbs.ls` uses Lambda's own `load({data, thumbnail:512})`
  and `save(image, {path, format:'jpeg', quality:82})`. It dogfoods the image API and
  removes full-image decodes from the grid's hot path. Whether the grid needs
  these precomputed thumbnails or can rely on the native thumbnail channel is
  measured at M1 (§5, G1). If the channel is fast enough, the step is dropped.
- **Git.** `test/demo/photo-album/.gitignore` ignores `photos/` and `thumbs/`.
  `git status` stays clean after a full fetch.

### 2.4 The manifest

`photos.json` is committed. It pins the selection so every checkout downloads
the same photos, and it carries the attribution the licenses require even when
the photos are absent. One entry:

```json
{
  "id": 42,
  "file": "0042-matterhorn-sunrise.jpg",
  "title": "Matterhorn at sunrise",
  "description": "The Matterhorn seen from Riffelsee at sunrise.",
  "album": "mountains",
  "taken": "2019-07-14T05:48:12",
  "width": 1280, "height": 853,
  "color": "#8a6f52",
  "camera": {"make": "Nikon", "model": "D850", "lens": "24-70mm f/2.8",
             "focal_mm": 32, "aperture": 8.0, "exposure": "1/125", "iso": 64},
  "gps": {"lat": 45.9822, "lon": 7.7599},
  "keywords": ["mountain", "sunrise", "reflection", "Switzerland"],
  "author": "Jane Doe",
  "license": "CC BY-SA 4.0",
  "license_url": "https://creativecommons.org/licenses/by-sa/4.0/",
  "source_url": "https://commons.wikimedia.org/wiki/File:...",
  "url": "https://thumb.wikimedia.org/wikipedia/commons/thumb/.../1280px-....jpg",
  "pageid": 12345678,
  "source_sha1": "…",
  "sha1": "…",
  "bytes": 318204
}
```

The top level also carries the source, the download width and the album list
with titles and counts. `url` is the scaled download, so a download needs no API
call. `source_sha1` is the Commons original's hash, used for de-duplication.
`sha1` and `bytes` describe the downloaded copy. If Commons re-renders a
thumbnail after the manifest is pinned, a fresh download no longer matches; the
script keeps the new bytes, updates the hash and reports the file as
re-rendered. Public-domain entries have no `license_url`.

`color` is the photo's average colour, computed by `_thumbs.ls` and written back
to the manifest; placeholder tiles use it. Descriptions are stripped of HTML at
fetch time. Keywords are up to ten of the file's categories, after removing
Commons housekeeping categories such as uploader, assessment, license and
camera categories. The demo reads the manifest
with `input(path, 'json')` at startup. It never parses EXIF from the scaled
JPEGs, because Commons thumbnails do not reliably keep EXIF.

### 2.5 Running without the corpus

The demo must open cleanly with no photos downloaded. It shows the library
from the manifest with placeholder tiles, tinted from a dominant colour stored
in the manifest, plus a banner with the fetch command. Partially downloaded
corpora show real tiles where files exist. Tests never depend on the download
(§7).

## 3. Features

### 3.1 Library browsing

**Views.** A sidebar switches among:

- **Library**: all photos in a justified grid, grouped by date, with a sticky
  label for the section under the top edge. Sections are months when a scope
  averages at least eight photos per month, and years otherwise; the Commons
  corpus averages under four per month, so it groups by year.
- **Albums**: album cards with a cover photo, count and date span. Opening one
  shows its grid.
- **Places**: photos with GPS coordinates on a map, clustered by zoom (§3.1.1).
- **Favorites**: photos the user hearts in this session.
- **Search**: incremental search across title, description, keywords, album,
  camera and year.

**Justified grid.** Rows keep each photo's aspect ratio and fill the width,
like Flickr and Google Photos. The row-packing function is pure: given aspect
ratios, a target row height and a width, it returns rows. Only the rows in the
viewport plus one screen of margin are mounted, keyed by row index, so a scroll
frame only adds and removes the rows that crossed the margin. This follows
Finder's retained-window approach ([Finder §3](Lambda_Demo_Finder.md)), with its
own variable-height window calculation (§4.3).

**Zoom levels.** Pinch, Ctrl-scroll or a slider changes the target row height
in steps, from a dense overview at about 80 px to large tiles at about 320 px.
The grid keeps the photo under the pointer anchored across zoom changes.

**Scrubber.** A thin year/month rail on the right edge jumps through 1000
photos without scrolling through every row.

**Selection.** Click selects, Shift extends, Cmd or Ctrl toggles, and a
rubber-band drag selects a region, matching the Finder interaction policy.
A selection bar floats over the bottom of the grid, so showing it never moves
the photos under the pointer. M1 offers **Add to favorites**, **Select all** and
**Clear**; **Collage**, **Film**, **Album book** and **Slideshow** join it with
their milestones. All entry points accept a selection, an album or the current
search result.

#### 3.1.1 Places

Places uses the `lambda.map` package with clustered pins sized by photo count.
Clicking a cluster zooms in; clicking a pin opens its photos. Whether the map
package can draw a basemap offline is checked at M5 (§5, G6). If it cannot, the
view falls back to pins over a coarse bundled world outline.

*As built (M5):* `lambda.map` has neither clustering nor tile sources, and its
camera moves only by user input. Clusters are computed in Lambda for each
integer zoom and shipped in one GeoJSON source; a layer filter on the map's zoom
shows one band, so double-click, wheel or **+** splits pins. A click on a pin, or
on a place in the side list, lists its photos; a photo opens in the viewer. The
basemap is the bundled outline (G6).

### 3.2 Photo viewer

- **Open and close.** Opening a tile animates the photo from its tile rectangle
  to fit the window. The animation is frame-driven from Lambda, because the
  start box differs per photo. Closing is immediate in M1.
- **Navigation.** Arrow keys, swipe and on-screen chevrons move through the
  current scope. The viewer prefetches the next and previous photos.
- **Zoom.** Double-click, scroll or pinch zooms up to 1:1 on the 1280 px file,
  with drag to pan.
- **Info panel.** Press `i` for title, description, date, camera and exposure,
  keywords, a small map when GPS exists, and the author, license and source
  link.
- **Filmstrip.** A strip of neighbouring thumbnails along the bottom can be
  hidden.
- **Actions.** Favorite, add to selection, and start a slideshow from this
  photo.

Full-size display uses the ordinary `<img>` path. Measurement shows the image
cache does not bound memory across many photos (§5, G2); the fix belongs in
Radiant's image cache, not in the demo.

### 3.3 Collage

The collage editor composes 2 to 30 photos.

**Templates.**

| Template | Behaviour |
|---|---|
| Grid | Equal cells in N×M, with a configurable gutter. |
| Mosaic | Recursive split of the canvas that follows the photos' aspect ratios, so little is cropped. |
| Magazine | A large hero cell plus smaller cells, with a title band. |
| Polaroid scrapbook | Free-form prints with a white border, caption strip, slight random rotation and drop shadow on a textured background. |
| Heart, circle and letter masks | Cells arranged inside a shape, using `clip-path`. |

**Editing.**

- Drag photos from a side tray into cells. Drag a cell onto another to swap.
- Pan and zoom within a cell; the crop is stored as normalized offsets.
- In scrapbook mode, move, rotate and resize prints with handles.
- Set gutter, corner radius, border, background colour or texture, and caption
  text.
- Undo and redo work over the collage model's history.

**Model.** A collage is plain data: canvas aspect, template, slots with photo id,
focal point and zoom (and a scrapbook print box once moved), styling, caption,
credits flag and a seed. Layout is a pure function from the model to rectangles
in export pixels. The model is saved as `<corpus>/collages/<name>.mark` and
reopened from the editor.

**Export.** Export produces a PNG or JPEG at a chosen size, from 1080 px up to
4096 px on the long edge. An optional credits line is added at the bottom, and
a sidecar text file lists every author and license. CC BY and CC BY-SA require
the attribution if the collage is shared. Export renders the collage as a
standalone HTML document with `radiant.export_document(html, path, {format:"png",
width, height, local_resources:true})`, the same in-app path the Photo Studio
demo uses for layered exports (`test/demo/photo/mod_io.ls`), with `format`
`"png"` or `"jpeg"`. The export document is the editor's canvas markup at export
scale with full-size photos, so it matches the screen by construction.

### 3.4 Memories film

A film is a generated, autoplaying story of 20 to 60 photos, about 1 to 3
minutes long.

**Story generation** is a pure function from a photo set to a film model:

- Group the set into chapters by album, day or place.
- Open with a title card: the album or place name and the date span, set over
  a blurred hero photo.
- Start each chapter with a short caption card or a lower-third.
- Pick a shot for each photo. Landscape photos get a slow pan, portraits a
  vertical drift, and a strong subject gets a zoom toward the frame centre.
- Pair two portrait photos side by side in a split-screen shot.
- Every few chapters, insert a quick-cut sequence of 4 to 6 photos on the beat.
- Close with end credits that list each photo's author and license.

**Motion.** Pan and zoom, the Ken Burns effect, uses the slide package's
`keyframes` channels `tx`, `ty`, `sx` and `sy` in parallel on one photo
wrapper, with ease-in-out easing. Transitions between shots use `fade`, `push`
and `slide`. A chapter heading can use `morph` to grow a tile into the next
full-frame shot.

**Playback.** The film compiles to an in-memory slide deck with entry cues only
and plays in the slide player with its chrome hidden. That gives play, pause,
speed control, reduced motion and seeking for free, and SLD3/SLD4 keep
rendering on demand. A thin film bar adds a scrubber with chapter ticks. If the
player's seek API cannot drive a continuous scrubber, the gap is fixed in the
slide package, not worked around in the demo (§5, G4).

**Soundtrack.** An optional score is composed in Lambda and played through the
`lambda.music` package's SoundFont, as the cityscape film does. The tempo is
fixed, so shot boundaries align to beats. Audible output follows D7.5.4. Tests
run with the offline audio output.

*As built (M5):* motion uses `keyframes` on each photo; a chapter card's tile
`morph`s into the chapter's first shot, and quick-cut runs use `cut`. The film
plays in an embedded player with `controls: false`, driven by `slide_command`
events; the film bar reads the player's published deck clock (G4). The score is
`mod_score.ls`, composed from the film plan at 120 BPM so every shot starts on a
beat; the MIDI event helpers it shares with cityscape live in
`test/demo/mod_midi.ls`.

**Video files.** Exporting an MP4 or WebM file is out of scope; the slide
package also excludes video export. A deterministic frame sequence through the
slide `snapshot` at fixed time addresses is a possible later step that an
external encoder could assemble.

### 3.5 Classic album book

The book presents an album or selection as a bound photo album.

**Look.**

- A cloth or leather cover with an embossed title, a spine and visible page
  edges.
- Cream or black paper pages with a subtle texture.
- Prints with a white border and photo corners, or tucked into slits.
- Handwritten-style captions with the date and place under each print.

**Pagination** is a pure function from photos to spreads. It chooses one to
four prints per page by aspect ratio, from templates such as one landscape
print, two portraits, or one landscape plus two small prints. It keeps chapter
starts on a right-hand page and varies the templates, so consecutive pages do
not repeat.

**Page turn.**

- The turning leaf is a two-faced element: front and back pages with
  `backface-visibility: hidden`, inside a `transform-style: preserve-3d`
  container with `perspective`.
- It rotates about the spine from 0° to −180° on the Y axis. A gradient overlay
  darkens the leaf as it approaches 90° and lightens it after, which reads as
  curvature.
- Radiant has no depth sorting for intersecting planes. The leaf never
  intersects the page stack; the turning leaf stays on top for the whole turn
  (as built: no swap at 90° is needed, because each face hides its back).
- The turn is sampled by the album's own frame callback, not a CSS transition.
  CSS transitions do not yet cover `transform` (see the transitions row in
  [HTML_CSS_SVG_Support.md](../doc/HTML_CSS_SVG_Support.md)). Progress runs on
  frame timestamps, so headless replays are deterministic.

**Interaction.**

- Arrow keys, clicking a page edge, or dragging a page corner turn the leaf.
  During a drag, the angle follows the pointer, and release completes or
  reverts the turn.
- A thumbnail bar of spreads jumps to a page; the jump flips several leaves
  quickly.
- Double-clicking a print opens it in the viewer.
- Spreads are rendered on demand and stay mounted, the same policy as SLD3/SLD4.

### 3.6 Slide presentation

The slideshow turns a selection into a real deck for the slide package.

- **Generation.** A pure function builds a `<presentation>` from at most 120
  photos: a title slide over the first photo, then full-bleed landscapes,
  captioned portraits, paired portraits, and every sixth slide a grid of four
  followed by a morph zoom into its first photo, and a closing credits slide.
  Speaker notes carry date, place, camera and the photo's credit.
- **Themes.** Dark gallery uses the slide package's `midnight` theme, light
  gallery its `paper` theme, and postcard adds a paper frame and stamp. The
  frame is appended to each slide rather than placed in a master, because a
  master's objects compose beneath the slide's own and a full-bleed photo would
  cover them.
- **Animation.** A gallery transition preset maps to `fade`, `push`, `slide` or
  `morph`. A per-slide build reveals the caption with `fly-in` or `wipe-in`.
  Grid slides use `morph` to zoom into one photo on the next slide.
- **Present.** The deck plays in an embedded `lambda.slide` player inside the
  demo window, with the presenter console, overview, autoplay with dwell and
  speed control. A small view creates the player from its state when the album
  asks it to load the saved file, and removes it on close: a live player
  carries its own handlers, so it cannot be inserted later as HTML text.
- **Save.** **Save as .slides** and **Present** both write
  `<corpus>/slideshows/<name>.slides` with `output(deck, path, 'mark')` (SLD1),
  so `lambda view` replays exactly what was presented. Photo paths are relative
  to the deck file. The folder is git-ignored.

## 4. Architecture

### 4.1 Data model

```text
Library (immutable, loaded once)
  photos: [Photo]                 manifest entries plus derived fields
  by_date, by_album, by_keyword   sorted index arrays of photo ids
  places: [Cluster]               precomputed for a few zoom levels

Session (owned by the entry procedure, mutated only in pn handlers)
  view: 'library' | 'albums' | 'places' | 'favorites' | 'search'
  scope: current photo ids
  selection, favorites            sets of photo ids
  viewer: {open, index, zoom, pan, info}
  grid: {row_height, scroll_anchor, window}
  collage | film | book | deck    the active presentation model, or null
```

The library is a single immutable value (S1.4). Each feature keeps its own
model in the session and renders through a pure template (S12.1.3). Handlers
are `pn` procedures that compute a new session and publish it. File reads use
`input()` and the thumbnail channel, which sit in `lambda-io` (D7.1.2v2), and
acquisition stays in `pn` code (S12.4.1v3).

### 4.2 Modules

| Module | Responsibility |
|---|---|
| `photo-album.ls` | Entry point and session owner; routing, grid mounting, viewer state and animation, command dispatch |
| `mod_library.ls` | Validate the manifest; ordering, albums, scopes, search and labels (pure) |
| `mod_grid.ls` | Justified row packing, zoom anchoring, the mounted row window, hit testing and arrow navigation (pure) |
| `mod_corpus.ls` | Thumbnail and colour building shared by `_thumbs.ls` and `_fixture.ls` |
| `mod_collage.ls` | Collage templates, mosaic split, editing commands and history, export |
| `mod_film.ls` | Story generation and lowering of a film to a slide deck |
| `mod_film_app.ls` | Film playback: the embedded player, film bar, scrubber, chapters and soundtrack sync |
| `mod_places.ls` | Per-zoom clusters of located photos as GeoJSON for `lambda.map` (pure) |
| `mod_book.ls` | Spread pagination, page-turn tracks and drag mapping |
| `mod_ui.ls` | Markup: sidebar, toolbar, grid rows, album cards, selection bar, viewer, info, filmstrip and the slideshow sheet |
| `mod_deck.ls` | Slideshow plan and deck generation, the postcard frame, source resolution for an embedded player, and deck file names (pure) |
| `photo-album.css` | Visual design, the book's paper and cover textures, collage frames |
| `_fetch.py`, `_thumbs.ls` | Corpus preparation (§2) |
| `mod_score.ls` | Film soundtrack composed from the film plan; event helpers from `test/demo/mod_midi.ls`, shared with cityscape |
| `world-outline.json` | The coarse hand-drawn land outline Places draws offline (G6) |

The demo is registered in the demo table in `lambda/main.cpp` and listed in
`test/demo/README.md`.

### 4.3 Reuse and promotion

- **Row window and thumbnail scheduler: not shared (M1 finding).** Finder's
  window assumes a uniform grid of equal rows and fixed columns; the album's
  justified rows vary in height and interleave date headers, so it is a
  different calculation, not a copy. Finder's thumbnail scheduler is wired to
  Finder's element ids and slot roles, and the album does not need it: tile
  cost is in painting, not decoding, so plain `<img>` tiles on the precomputed
  thumbnails are the right fit (§5, G1).
- **DOM publishing helpers: shared (M1).** The write-only-on-change helpers
  both demos used (`attr`, `text`, `style`, `show` and a plain region update)
  now live in `test/demo/mod_publish.ls` beside `mod_resources.ls`. Finder
  imports it, and its `mod_dom.ls` is gone. This answers Q3.
- **Slide sampler, effects and transitions.** Used through the slide package's
  public API only. The demo adds no animation engine of its own.
- **Image API.** `load`, `save` and `image_process` for thumbnails and export;
  `radiant.image_job` with the thumbnail channel for asynchronous decoding.

## 5. Engine gaps to verify

None of these is assumed. Each is checked by measurement or a focused test at
the milestone that first needs it, and fixed at its root if real.

| ID | Question | First needed | If it is a real gap |
|---|---|---|---|
| G1 | Can a 1000-photo grid scroll and zoom smoothly with the thumbnail channel's 128-entry / 32 MiB cache, or with plain `<img>` thumbnails and the image cache? | M1 | **Measured, partly real.** Lambda work is about 1 ms per step; Radiant paint of the image tiles dominates. Precomputed 512 px thumbnails stay. See the M1 checkpoint. |
| G2 | Does Radiant bound image-cache memory when the viewer pages through hundreds of 1280 px photos? | M1 | **Measured, real.** The window's image cache has no budget or eviction. Needs an ownership-aware eviction design in Radiant. See the M1 checkpoint. |
| G3 | Does `radiant.export_document` export a 4096 px collage of 30 local photos within its limits and in acceptable time? | M3 | **Resolved: no gap.** See the M3 checkpoint. |
| G4 | Can the slide player seek continuously for a scrubber, and play a deck with its chrome hidden? | M5 | **Real, fixed in `lambda.slide`.** It had no deck clock, no host seek and no way to hide its controls. It gained `timeline`, `position`, the `seek_time` command, `controls: false` and `slide_command` host events. See the M5 checkpoint. |
| G5 | Does the 3D page turn render, hit-test and update damage bounds correctly while the leaf animates? | M4 | **Real for text, fixed.** Glyphs on a projected plane were bounded before the perspective divide and mostly dropped; fixed in `radiant/glyph_sampling.hpp`. Hit testing verified mid-turn. See the M4 checkpoint. |
| G6 | Can `lambda.map` draw an offline basemap for the Places view? | M5 | **Real; fallback used.** No tile or URL sources, and no clustering. Places draws a bundled hand-drawn outline and clusters in Lambda. See the M5 checkpoint. |

## 6. Milestones and acceptance

Milestones are ordered by user impact and dependency. Each one ships a usable
increment.

| Milestone | Delivers | Acceptance |
|---|---|---|
| **M0 Corpus** | `_fetch.py`, the pinned manifest, `.gitignore`, `_thumbs.ls` | 1000 files download with matching SHA-1; a rerun downloads nothing; an interrupted run resumes; `git status` is clean; every entry has author, license and source. |
| **M1 Browse** | Library, albums, search, favorites, justified grid, scrubber, selection, viewer with info panel | Opening the demo shows the timeline; zoom keeps the anchor photo in place; search for "snow" returns the expected ids; viewer navigation, zoom and info work by keyboard; G1 and G2 measured. |
| **M2 Slideshow** | Deck generation, themes, transitions, in-app presentation, Save as `.slides` | A 20-photo selection becomes a deck; `lambda view` of the saved file plays the same slides; the presenter notes show credits. |
| **M3 Collage** | Templates, tray, cell editing, scrapbook mode, history, export with credits | Each template lays out 2, 7 and 30 photos without gaps or overlaps; swap, crop and undo work; exported PNG matches the on-screen layout; G3 resolved. |
| **M4 Album book** | Pagination, cover, spreads, page turn by key, click and drag, spread bar | A 100-photo album paginates with no repeated adjacent templates; drag-turn follows the pointer and completes or reverts; G5 verified. |
| **M5 Film and places** | Story generation, film playback with scrubber, soundtrack, end credits, Places view | A 40-photo album becomes a film of about 90 seconds with chapters; snapshots at fixed times are deterministic; the soundtrack plays offline in tests; Places clusters and opens photos; G4 and G6 resolved. |

## 7. Verification budget

- **Fixture corpus.** Tests never download. A `_fixture.ls` script generates
  about 24 small synthetic JPEGs with Lambda's image API into `./temp/`, with a
  matching small manifest. Varied aspect ratios, dates, GPS and albums
  exercise the same code paths.
- **Pure-function tests.** Row packing, mosaic split, spread pagination, story
  generation and deck generation are pure functions. They get `.ls` tests with
  golden `.txt` results (AGENTS rule 8).
- **UI tests.** Each milestone adds `--event-file` replays under
  `test/ui/photo_album/`, run headless against the fixture corpus, in the same
  form as the Finder and slide UI tests.
- **Performance.** Scrolling, viewer paging, page turn and film frame times are
  measured in a release build (AGENTS rule 10) over the full 1000-photo corpus.
  Initial targets:

  | Measure | Target |
  |---|---|
  | Cold open to first painted grid | under 500 ms |
  | Grid scroll or zoom frame | under 16 ms |
  | Viewer next photo, prefetched | under 100 ms |
  | Page-turn and film frames | under 16 ms |

- **Engine gates.** If a milestone changes Radiant or the Lambda runtime, run
  `make test-radiant-baseline` or `make test-lambda-baseline` at the milestone
  boundary. Changes confined to the demo's `.ls` and CSS files need only the
  demo's own tests.

## 8. Open questions

| ID | Question | Recommendation |
|---|---|---|
| Q1 | Wikimedia Commons or Unsplash Lite as the source? | Commons, for the redistribution reason in §2.1. Unsplash Lite would be acceptable only for a private local corpus. |
| Q2 | Commit `photos.json` (about 0.5 MB), or regenerate it on every fetch? | Commit it, so the corpus is reproducible and credits are available offline. |
| Q3 | Where should modules shared with Finder live? | Answered at M1: `test/demo/`, beside `mod_resources.ls`. Only the DOM publishing helpers turned out to be shared (§4.3). |
| Q4 | Should the demo ship in release packages without photos, with a fetch prompt on first open? | Yes; the empty-library state in §2.5 already covers it. |
| Q5 | Is a 1280 px corpus enough, or should the viewer fetch 1920 px for large displays? | Start with 1280 px; revisit if 1:1 zoom looks soft on Retina displays. |

## M0 checkpoint — 2026-10-20

M0 is implemented in `test/demo/photo-album/`: `.gitignore`, `_fetch.py`, the
pinned `photos.json` and `_thumbs.ls`. Nothing is committed yet.

| Measure | Result |
|---|---|
| Photos downloaded | 1000, 1280 px wide, 124 of them portrait |
| Photo download size | 317 MB |
| Thumbnails | 1000 at 512 px, 45 MB, built in about 60 s |
| Photos with GPS | 836 |
| Capture years | 2004–2026; 44–59 photos in each year from 2009 to 2025 |
| Licenses | 821 CC BY-SA, 108 CC BY, 41 CC0, 30 public domain |
| Committed manifest | 1.5 MB |

- **Selection.** All eleven albums met their quotas; no redistribution was
  needed. People and events had the tightest pool, 200 candidates after the
  content and personality-rights filters. Every entry has an author and a
  source URL.
- **Reproducibility.** A `--refresh` from the cached API responses reproduces
  the committed manifest byte for byte. A rerun downloads nothing, `--verify`
  passes, a second `_thumbs.ls` run changes nothing, and `git status` shows only
  the four source files.
- **JSON layout.** `_fetch.py` writes the manifest in the layout of Lambda's JSON
  output, including whole-number floats written as integers, so the Python and
  Lambda writers produce identical bytes.
- **Runtime defect found.** `sum` and `avg` along an axis of a ubyte image
  wrap modulo 256 ([LR05-16](Lambda_Issue_Ledger.md#lr05-16)). `_thumbs.ls`
  converts with `as_float` first, which is the documented conversion.
- **Observation.** Files written by Lambda's atomic `output` and by `save` get
  mode 0600 rather than the usual 0644. It does not affect the demo, but it is
  worth checking in the IO layer.

## M1 checkpoint — 2026-10-20

M1 is implemented: `photo-album.ls`, `mod_library.ls`, `mod_grid.ls`,
`mod_ui.ls`, `mod_corpus.ls` and `photo-album.css`, run with
`lambda demo photo-album`. Nothing is committed yet.

**Delivered.** The library timeline with date sections, a sticky section label
and a year rail; albums as sidebar entries and cover cards; favorites; search
over every word; tile zoom by slider, buttons, keys and Ctrl/⌘ + wheel, anchored
on the photo under the pointer; click, toggle, range, rubber-band, select-all
and keyboard selection; a floating selection bar; the viewer with the open
animation, stepping, Home/End, zoom with clamped pan, info panel, filmstrip,
favorite and prefetch; and the not-downloaded state with colour tiles and a
banner. Closing the viewer is immediate; a closing animation is not done.

**Tests.** The pure modules have goldens (`tests/album_library`,
`tests/album_grid`), and both match on the MIR JIT too. Four headless replays in
`test/ui/photo_album/` (91 assertions) run in the Radiant baseline's UI suite
against the committed `fixture/` corpus and `tests/no_photos/`. The M1
acceptance row passes: the timeline opens; zoom keeps the anchor photo in
place; "snow" returns the six expected photos; viewer navigation, zoom and info
work by keyboard.

**Measurements.** `benchmark.py` instruments a copy of the demo and replays a
1440 × 900 headless session over the 1000-photo corpus. The host was
`release/lambda`, built 2026-10-19; the one-minute load was 3–4 during the runs,
so treat the numbers as indicative. Medians of two runs:

| Step | Lambda handler | Whole step | Without image files |
|---|---|---|---|
| Startup layout and first mount | 9–11 ms | — | 9 ms |
| Scroll 700 px | 1.1 ms | 77–90 ms | 43 ms |
| Tile zoom step | 7.4 ms | 84–86 ms | 60 ms |
| Grid arrow key | under 1 ms | 57 ms | 24 ms |
| Viewer next photo | 1.3 ms | 72–75 ms | 30 ms |
| No-op key (replay baseline) | — | 23 ms | 5 ms |

- **G1 — partly real.** Lambda work stays near 1 ms per step; the rest is
  Radiant layout and paint, and the headless replay repaints the whole frame on
  every event. About 50 visible image tiles add some 18 ms to every frame, even
  a no-op one. Halving the thumbnails to 256 px changed nothing (scroll 80–84 ms),
  so the cost is per-image paint, not decoding. Tiles that fall back to the
  1280 px photos are clearly worse (scroll 136 ms, 560 MB peak), so the
  precomputed 512 px thumbnails stay. Frame cost on large jumps exceeds a 16 ms
  budget; cheaper image paint, for example cached tile rasters, belongs in Radiant.
- **G2 — real.** Peak memory grows by about 5.3 MB for every photo the viewer
  shows, one decoded 1280 px image each, with no plateau:

  | Viewer photos shown | Peak memory |
  |---|---|
  | 0 | 457 MB |
  | 100 | 1,026 MB |
  | 300 | 2,080 MB |

  The window's image cache (`UiContext::image_cache`, which owns every
  `ImageSurface` under ruling O2) has no byte budget, entry cap or eviction.
  Surfaces are freed only at teardown; changing an `<img>` source only drops the
  element's borrowed pointer. Elements borrow surfaces, so eviction needs an
  ownership-aware design rather than a demo workaround.

**Findings outside the demo.**

- `format(element, 'html')` wraps a fragment in a whole document; regions use
  `format([element], 'html')`.
- A bounding box read in the same handler as a display change reports the old
  layout, so the grid measures its always-displayed scroll port instead.
- A custom property set from script was not picked up by `var()` inside
  `@keyframes` in a quick probe, so the open animation is frame-driven. The root
  cause was not investigated.
- Showing the selection bar above the grid moved every photo 51 px and broke
  double-clicks at small tile sizes; the bar now floats over the grid.

## M2 checkpoint — 2026-10-21

M2 is implemented: `mod_deck.ls`, the slideshow sheet and show layer in
`mod_ui.ls`, and the save, present and close commands with the `album_show`
player view in `photo-album.ls`. Nothing is committed yet.

**Delivered.** **Slideshow** opens from the selection bar (the selection in
timeline order), the toolbar (the current scope) and the viewer (the scope from
the current photo). The sheet takes a name, theme, transition and seconds per
slide, and offers **Save as .slides** and **Present**. Escape closes the show
once the player has no overview, blackout or pointer of its own to dismiss.

**Acceptance.** The M2 row passes. A 20-photo selection becomes an 18-slide
deck (`test/ui/photo_album/slideshow.json`). `tests/fixture_slideshow.slides`
is that deck as saved beside the fixture; the deck golden asserts it equals
fresh output, and `test/ui/photo_album/saved_deck.json` opens it as a
standalone `.slides` document and plays the same slides. The presenter notes
show each photo's author and license. On the real corpus, the 80-photo
Mountains album becomes a 62-slide show.

**Tests.** `tests/album_deck` (golden, interpreter and MIR JIT agree) covers the
slide plan, layouts, transitions, morph pairs, notes, credits, missing-photo
placeholders, file names, compilation, the Mark round trip and source
resolution. The `photo_album` UI suite now has six replays.

**Findings outside the demo.**

- `viewport_size` returns null inside an embedded document by design
  (RAD_16, implementation footnote in `Lambda_Impl_Slide_Presentation.md`), so a
  `.slides` page in an iframe never fits its canvas. Embedding the player
  directly avoids it; a frame-aware viewport would need a design change.
- An embedded player resolves image sources against the host page, not the
  deck's `base_uri`, and autostarts only from a standalone page's load event.
  The demo resolves sources itself (`D.resolve_sources`) and requests
  `slide_activate` after creating the player.
- A master's objects compose beneath the slide's own objects.
- Iterating an element with `for` yields its attribute values as well as its
  children; `content(el)` gives only the children.

## M3 checkpoint — 2026-10-21

M3 is implemented: `mod_collage.ls` (model, layouts, edits, history, credits),
`mod_collage_ui.ls` (canvas and editor markup) and `mod_collage_app.ls` (editor
procedures and export). **Collage** opens from the toolbar (the first 30 photos
shown) and the selection bar.

**Delivered.** Six templates: grid, mosaic, magazine, scrapbook, heart and
circle (clip-path masks over a mosaic). Four canvas shapes, spacing, corners,
white border, four backgrounds, a caption band and a credits line. A drag inside
a cell pans its crop, the wheel zooms it, and dragging out onto another cell or
from the tray swaps photos. Scrapbook prints move, rotate and resize from
handles, and **Shuffle prints** re-deals them. Undo and redo (⌘Z, ⇧⌘Z) record a
slider or caption edit once, when it ends. **Save** and **Open** keep collages
as `.mark` files; **Export image** writes PNG or JPEG at 1080, 2048 or 4096 px
with a `-credits.txt` file beside it. Both folders are git-ignored.

**Acceptance.** The M3 row passes. `tests/album_collage` checks that grid,
mosaic, magazine, heart and circle cover their area exactly with no overlaps for
2, 7 and 30 photos, that scrapbook prints stay on the canvas, and swap, crop,
history and settings. `tests/proc/collage_export` exports a collage through
Radiant and checks pixels: gutters show the background and each cell centre
matches its own photo within 3 levels. `test/ui/photo_album/collage.json`
(30 assertions) covers the editor end to end.

**G3 — resolved.** A 4096 × 2731 export of 30 real photos took 2.9–3.1 s for
the mosaic and 1.6 s for the scrapbook, with a 570 MB peak, on the 2026-10-19
release host under a load average of 20.

**Slide player theming (M2 follow-up).** The slideshow now matches the album:
`lambda.slide` gained `--slide-ui-*` chrome tokens and a validated `ui` option
and deck attribute ([Player chrome](../doc/Lambda_Slide.md#player-chrome)).
Album decks carry the viewer palette, so saved decks keep it under `lambda view`.
Default decks are unchanged pixel for pixel.

**Findings outside the demo.**

- In a live, inserted region, a flex container whose single nowrap text
  overflows with `overflow: hidden` paints nothing; the same markup renders in a
  static export. The credits line uses a plain block instead.
- `dom.set_state(el, "value", …)` applies to text controls only: selects use
  `set_selected_index` and ranges `set_property` and `range_value`.
- Events dispatched from script (`dispatchEvent(new Event('input'))`) do not
  reach Lambda `on input` handlers; replays use real key input on sliders.
- A keyboard event targeted at `<body>` does not reach a view rooted below it,
  so the editor layer takes focus when it opens.
- Unsigned-byte pixel values wrap on subtraction (`226 - 227` is 255); compare
  them as `int`s.

## M4 checkpoint — 2026-10-21

M4 is implemented: `mod_book.ls` (pagination, leaves, spreads and turn maths as
pure values), `mod_book_ui.ls` (pages, leaves, spread bar and shell markup) and
`mod_book_app.ls` (mounting, frame-driven turns, drags and jumps). **Album
book** opens from the toolbar (the photos shown, up to 240) and the selection
bar. Nothing is committed yet.

**Delivered.**

- Four cloth or leather covers with an embossed title, cream or black paper,
  page-edge stacks that grow with the pages on each side.
- Eight templates of one to four prints. Prints have white borders, photo
  corners, a small hand-placed tilt and a handwritten date and album caption.
- One chapter per year, each opening on a right-hand page; a title page, page
  numbers and an end page that pads the count to whole leaves.
- Page turn by arrow keys, Page Up/Down, Space, Home/End, the Previous and Next
  buttons, a click on either page, or a drag. A drag follows the pointer and
  release completes the turn past the vertical or falls back.
- A spread bar of thumbnails; a jump flips the leaves in between quickly.
- Double-clicking a print opens the viewer over the book, paging through the
  book's photos; closing it returns to the book.
- Leaves mount near the open spread and stay mounted; far ones are hidden.

**Acceptance.** The M4 row passes. `tests/album_book` paginates 100 fixture
photos (cycled) into 54 pages on 29 leaves and checks that no two adjacent
pages share a template, chapters start on right-hand pages, and all 100
entries print once, inside their pages. `test/ui/photo_album/book.json`
(30 assertions) covers the cover, key and click turns, a mid-turn angle and hit
test, spread jumps, mount limits, a drag that reverts and one that completes, a
left turn, the viewer round trip, paper and cover changes and closing.

**G5 — real for text, fixed at the root.** Glyphs drawn on a plane under
`perspective` were bounded with the affine part of the matrix only, so most of a
turning page's text was clipped away. `glyph_draw_transformed_coverage_bitmap`
now bounds the glyph after the homogeneous divide, as its inverse sampling
already did, and samples the clip when a glyph crosses w = 0.
`RenderOutputParity.TextInPerspectivePlaneKeepsItsGlyphs` guards it. Image and
box painting on the leaf were already correct, and hit testing mid-turn picks
the turning leaf. Damage bounds were checked only through headless renders, not
in a live window.

**Findings.**

- `mod_publish`'s `updates.markup` skips a region whose markup is unchanged, so
  children mounted into it later survive a rebuild. The book clears its leaf
  host on every rebuild.
- Animations sampled with `clock()` follow wall time, which headless replays do
  not advance with the document timeline. The book and the viewer's open
  animation now use the frame event's `time_stamp`.
- A select's choice can arrive as an `input` event; the book handles both
  `input` and `change`.
- Leaf transforms read back lowercased (`rotatey(...)`) from the style
  attribute.
- The new toolbar button narrowed the heading enough to wrap the status line;
  the status now stays on one line and the search box shrinks first.

**Page-turn performance (2026-10-21).** A page turn felt late to start. It was
measured with a release host on the full corpus at 1440 × 900, using
`benchmark.py --book`.

| Cause | Change | Effect |
|---|---|---|
| Each frame wrote the leaf's `transform` and shade `opacity` as inline style, so Radiant relaid out the document on every frame (26 ms). | Frames write through the presentation channel (`dom.presentation_style_set_property`, RAD_16); each leaf starts with `rotateY(0deg)`, so a turn replaces a transform. | Frame commits drop from 27 ms to 0.6 ms (paint-only). |
| Every page's gutter shade was a blurred inset `box-shadow`, re-blurred at page size on every repaint (~35 ms a frame). | Use a 28 px gradient band instead. The spine shade on the back cover becomes a gradient over the cloth. | |
| Near-invisible 1.8 %-alpha paper stripes cost ~25 ms a frame. | Removed. | |
| Turns eased in from rest, and their clock started at the first frame, so the leaf barely moved for the first two or three frames. | Turns ease out (cubic), and their clock starts at the input's time stamp. | The leaf is at −112° 120 ms after the key. |
| In a live Retina window, starting a turn mounted the next leaf and wrote inline `z-index`, so the first frames were full relayouts and repaints. Together with the click, they used up the 550 ms clock, and a turn showed 4–8 frames. | Mount one leaf ahead. Each stack's top leaf rests at its lifted `z-index`. The clock advances at most 50 ms per frame. Radiant tracks presentation-styled elements before their first write (Radiant_Design_Animation_Repaint, stage A). | First frame 3 ms after the click; 14–17 frames per turn, 47 ms apart. |
| The library grid under the full-window book repainted every frame (about 14 thumbnails per frame, behind an opaque backdrop). | While the book is open, the sidebar and workspace are `visibility: hidden`. | Live 2× window: 77 → 42 ms; with Radiant's later layer fixes 33.5 ms (about 30 fps), 16–22 frames per turn. |
| Each leaf's shades sat inside its faces, so their per-frame opacity would invalidate a face's retained raster (Radiant stage B paint layers). | Shades are sibling planes of the faces (`backface-visibility: hidden`; the back shade turned 180°). | Faces paint from layers; headless 1× frames 72–75 → 45 ms; live 2× window about 5% faster (Radiant_Design_Animation_Repaint, stage B checkpoint). |

The frame gap within a turn fell from a 141 ms median to 55–61 ms.

Changes that did not help:
- Thumbnails instead of photos for prints: 88 → 85 ms. Prints keep the
  1280 px photos.
- Removing the photo drop shadows, the photo corners or the radial backdrop:
  within noise.

**Not met: the 16 ms frame target (§7).** What remains is Radiant
re-rasterizing the whole window on each paint-only frame: the turning leaf's
projected images, plus the static spread, bars and backdrop. The paint-only
commit marks a full repaint. Two engine options:
- damage-limited repaint for paint-only commits;
- retained raster layers for the static spread.

Either is a design decision for Radiant, not a demo change. A Retina window
rasterizes four times the pixels, so it is slower than these 1× numbers.

## M5 checkpoint — 2026-10-21

M5 is implemented:
- **Film:** `mod_film.ls` (story and deck lowering), `mod_film_app.ls`
  (playback) and `mod_score.ls` (soundtrack).
- **Places:** `mod_places.ls` and `world-outline.json`.

**Film** opens from the film icon in the toolbar (the photos shown, up to 60) or
from the selection bar. **Places** is a sidebar view. Nothing is committed yet.

**Delivered.**

- **Story.** A film groups photos into chronological chapters by year, or by
  month within one year, and lays them out at 120 BPM:
  - a blurred-hero title card;
  - a card per chapter, whose tile morphs into the chapter's first shot;
  - landscapes that pan or zoom, portraits that drift over a blurred copy of
    themselves, and paired portraits side by side;
  - a quick-cut run every third chapter;
  - rolling credits with each photo's author and license.
- **Film bar.** Play or pause (Space, or a click on the picture), skip ±5 s,
  Home and End, a draggable scrubber, chapter marks that jump, and a sound
  toggle.
- **Soundtrack.** It is composed from the film plan, so the chords change on
  the chapter cards. It follows play, pause and seek, and stops at the end.
- **Places.** Photos with GPS are drawn as pins sized by count over the bundled
  outline. Pins split as the map zooms. A pin or a place in the side list shows
  its photos, and a photo opens the viewer. Closing a layer returns focus to
  the map.
- **Toolbar.** Film, Album book and Collage are now icon buttons, so the
  heading keeps its room.

**Acceptance.** The M5 row passes.
- `tests/album_film` makes a film of 40 photos from one album:
  - it runs 97 s in four chapters;
  - every photo appears once, in date order;
  - quick runs cut, and chapter openers morph;
  - the slide deck's own clock puts every shot on its beat, with the same total
    length;
  - a frame snapshot at a fixed film time is identical across renders, and
    differs a second later;
  - the score lasts exactly as long as the film, its events are time-ordered,
    and a chord lands on each chapter card.
- `tests/album_places` checks the pins at every zoom band, for the fixture and
  for all 836 located photos:
  - every band partitions the photos;
  - zooming in only splits pins;
  - pins keep library order;
  - the outline's rings are closed.
- `test/ui/photo_album/film.json` (56 assertions) covers playback end to end,
  with the soundtrack on the offline audio output:
  - opening, and the player's hidden controls;
  - pause and resume, chapter jumps, and skips;
  - two seeks to the same time showing the same pose;
  - scrubbing that pauses and then resumes;
  - click-to-pause and sound off;
  - the end and replay;
  - closing, and a film made from a selection.
- `test/ui/photo_album/places.json` (31 assertions) covers the map:
  - the fitted camera's 11 pins;
  - list and map picks;
  - zoom splitting the pins into 12;
  - photos opening in the viewer;
  - returning from the viewer and the library;
  - a film made from Places.

**G4: real, fixed in `lambda.slide`.** The embedded player had no deck-wide
clock, no seek from its host, and no way to hide its controls. The slide
package gained:
- `clock.ls` (`timeline`, `position`, and a seek to deck time), with the
  reducer's `seek_time` command;
- `controls: false`, which hides the bar, drops the Toolbar button and leaves
  stage clicks to the host;
- a `slide_command` event that a host sends with `dom.dispatch`;
- `data-slide-position` and `data-slide-position-at`, published at each command
  or build boundary, so a host extrapolates the position on frame timestamps.

`test_slide_clock` checks the clock against the reducer: positions equal
elapsed time while autoplaying, and seeks round-trip. The
`test/ui/slide_host_control` replay drives a player without controls. These
are documented in [Lambda_Slide.md](../doc/Lambda_Slide.md#deck-clock-and-host-control).

**G6: real; fallback used.** `lambda.map` has no tile or URL sources, so there
is no offline basemap. It also has no clustering, and its camera moves only by
user input.
- Places draws `world-outline.json`, a coarse hand-drawn outline (29 land
  masses, 838 vertices, CC0).
- Clusters are computed in Lambda for each integer zoom and shown through a
  layer filter on the zoom.
- "Clicking a cluster zooms in" became "double-click, scroll or **+** zooms in";
  a click lists the photos.

**Findings outside the demo.**

- **Radiant: writes made after a nested view re-render were never restyled.**
  The bug, now fixed:
  - A handler dispatched to another view and then called `set_inner_html` in
    the same turn.
  - The template splice in `rebuild_lambda_doc_incremental` cascaded only the
    re-rendered template, then cleared the mutation ledger.
  - The inserted nodes kept UA styles and stale inherited values.
  - The fix commits the pending mutations first
    (`radiant/cmd_layout.cpp`).
  - Regression replay: `test/ui/restyle_after_nested_dispatch`.
- **Handler binding.** A view's `on` handlers bind to the nearest preceding
  `view`. Placing another view between a view and its handlers silently moves
  them, and nothing reports it.
- **Text under a fractional `translate`** is resampled bilinearly, so its edges
  soften slightly while it moves. At integer offsets it is crisp.
- **`lambda.chart` ring checks** treat a vertex touching another edge as an
  intersection, so authored outlines must keep vertices off other edges.
- **The selection bar** is centred with `left: 50%`. At seven buttons it is
  wide enough to cover a tile near the bottom of the grid; the interactions
  replay now aims at the tile's upper part.
- **Not measured:** release-build film and map frame times. The film runs a
  single bar update per frame and the player's own patching.
