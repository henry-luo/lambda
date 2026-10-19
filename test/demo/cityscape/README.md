# The Rings of a City · 一座城的年轮

A native `lambda.scene3d` reproduction of Eian's short film
[《一座城的年轮》](https://x.com/EianLu/status/2111662397278847478). The film shows
Chengdu inside the Third Ring Road as a 1:10,000 white model, from the Jinsha Sun Bird
(c. 1250 BC) to 2022. This demo rebuilds the model from OpenStreetMap and plays the
story as one 150-second animation clip with timed bilingual captions. It is a live
scene, not a video file.

```sh
python3 -I test/demo/cityscape/_generate.py --fetch --osm temp/chengdu-osm
./lambda.exe demo cityscape
```

The first command downloads about 37 MB of OSM extracts through the Overpass API into
`temp/chengdu-osm/`; later runs reuse them. The command then writes
`assets/city.glb` (about 50 MB) and `assets/city.json`. Run both commands from
the repository root. For quick look-development, `--thin 12` keeps one building in
twelve (about 7 MB).

## What is generated

`_generate.py` uses only the Python standard library. It projects OSM data to
kilometres around Tianfu Square: x points east, y up, z south. It clips everything to
a square 319 km² slab, the area quoted in the film.

| Layer | Source | Notes |
| --- | --- | --- |
| Buildings | `building=*` ways and multipolygons | Footprints are simplified (0.6 m), roofs ear-clipped and walls extruded with flat normals. Heights come from OSM `height` or `building:levels` where tagged (about 6%). Everything else uses `estimate_height`, a fixed rule set based on building type, footprint area and shape, and ring zone. |
| Ring zones | 一环路 / 二环路 / 三环路 by name | Rays cast from Tianfu Square trace each ring as a polar outline. The data gives the Third Ring 50.5 km and 192.5 km² (officially 51 km), and the Second Ring 27.8 km. |
| Old city | 府河 · 南河 · 锦江 · 西郊河 | The moat loop of the two rivers bounds the Tang Luo City and the Ming–Qing Great City. |
| Water, roads | `natural=water`, waterways, major highways | Flat layers lifted a few metres above the slab, so the depth buffer keeps them apart. |

Buildings are split into 15 growth bands, three per zone: old city, First, Second and
Third Ring, and outside. Each zone has its own material. Water is cut into eight
slices along the north-west → south-east axis, so it can "flow in" for Dujiangyan.
glTF node and material names carry these roles, and `cityscape.ls` finds the prefixed
ids by name after `lambda.scene3d.load()`.

Historical outlines are **schematic**. The Qin "Turtle City" is a shrunken moat loop.
The Imperial City follows the old 御河 streets and Tianfu Square. The Lesser City is the
Great City west of 东城根街. The Jinsha and Brocade Office markers are placed by
location, not surveyed.

## The film

`cityscape.ls` builds everything else procedurally:
- the slab, coloured by five ring-zone washes;
- walls that rise and sink (a vertical strip plus a cap);
- ring-road glows and tree-ring lines interpolated between the old city and the Third Ring;
- the Sun Bird: a rim, twelve swirling rays and four birds;
- the Shudu Mansion beam and the Dujiangyan arrow.

One autoplaying `film` clip drives about 80 tracks:

- **Camera.** A rig group turns about its target while the camera rides its +z axis.
  Shots are eased in Lambda (smootherstep) and sampled every 0.25 s.
- **Looks.** Each chapter cut crossfades the slab, five building zones, water, roads
  and ring roads to a palette: night, Qin sand, Dujiangyan blue, Brocade coral, Luo
  City, Ming–Qing sepia, 1958 grey, the dark growth map and the tree-ring finale.
- **Growth.** Band groups scale in y. The city sinks for Jinsha, returns for "today",
  and later regrows band by band from the centre while the ring roads light up.

| Time | Chapter |
| --- | --- |
| 0–7.5 s | Title |
| 16 s | 前1250 · Jinsha, the Sun Bird |
| 28.5 s | 2022 · Chengdu today |
| 38.5 s | 前311 · Qin walls (Turtle City) |
| 50.5 s | 前256 · Dujiangyan |
| 62.5 s | 汉 · the Brocade City |
| 74.5 s | 876 · Tang Luo City |
| 84.5 s | 明清 · Great, Imperial and Lesser Cities |
| 96.5 s | 1958 / 1968 · walls and the Imperial City demolished |
| 106.5 s | ring roads · Third Ring 2002 |
| 124.5 s | 1991 · Shudu Mansion |
| 134.5 s | tree rings, then the closing title |

Captions are HTML over the scene, faded by CSS animations on the same document
timeline as the clip. The top timeline is chronological; the "today" and 1991
chapters jump ahead as flash-forwards. Caption facts are limited to what could be
checked: Third Ring opening (2002, 51 km), Shudu Mansion (about 1991, 34 storeys), wall
and Imperial City demolition (around 1958, 1968). No years are given for the First or
Second Ring.

## Soundtrack

[`_score.ls`](_score.ls) composes an original 150-second score as code. It runs at
60 BPM, so every cue is a plain film second. Melodies use D major pentatonic, and the
1958 chapter moves to its B (yu) mode. The score uses twelve General MIDI parts:
pad, strings, koto, flute, harp, celesta, timpani, contrabass, piano, horn, choir and
the drum kit. These play through the music package's bundled TimGM6mb SoundFont with
`dom.midi_open`.

Cues follow the film:
- a timpani roll and crash as the city returns for "today";
- a koto run that climbs with each growth band;
- a war-drum pulse for Qin;
- celesta droplets timed to each water slice for Dujiangyan;
- a koto song for the Brocade City and a flute line for the Luo City;
- solo piano for 1958;
- a climbing ostinato with strikes as each ring road lights;
- a rising horn for Shudu Mansion;
- one bell per tree ring;
- the opening motif again at the close.

The browser rule applies here too: audio starts only on a click (S12.1.1v2/D7.4.6).
D7.5.4 would let a local file start sound on its own; this demo still waits for the viewer.
Press **♪ 声音 · Sound** at the top right to play the film from the start with music.
The Lambda sound control starts the score at 0 and bumps `data-epoch`.
[`cityscape.js`](cityscape.js) sees the change and does two things:
- it rewinds the native clip through `RadiantAnimationHost`;
- it toggles `.replay`, which swaps every caption's `animation-name` to a twin keyframe
  set and so restarts the captions.

Press the button again while the music plays to mute it. Press it after the music has
ended to replay the film.

Press **旁白 · Narration** below it while the music plays to hear the remaining Chinese
subtitles spoken by the system voice. The sound control saves `dom.audio_clock(owner)`
when it starts the score. Each line is then queued with
`dom.speak(owner, line.zh, {lang: "zh-CN", at: clock0 + line.t})`, so speech and music
share one clock on the document's audio mix
([Radiant_Design_Speech §3.4](../../../vibe/radiant/Radiant_Design_Speech.md)).
Press it again to stop the narration. Restarting or muting the music stops it too.

The narrator is the most natural **male** Mandarin voice installed, spoken at rate 0.9.
`dom.speech_voices()` lists voices best first, and the page takes the first one with
`lang: "zh-CN"` and `gender: "male"`; it shows which in the button's `data-voice`.
A stock Mac has none. Its Mandarin voices are the female Tingting and the Eloquence set,
which reports no gender, so narration then falls back to Tingting. For a natural male
voice, download **Han** (Premium), **Li-mu** (Premium, about 175 MB) or **Binbin**
(Enhanced) once from System Settings → Accessibility → Spoken Content → System voice →
Manage Voices… → Chinese (China mainland). The demo picks the best of them up without
changes. With Han installed, it narrates as `Han (Premium)`.

## Checking frames

Headless captures advance the shared document clock, so the 3D clip and the captions
move together:

```sh
LAMBDA_HEADLESS_GLFW_WINDOW=1 LAMBDA_SCENE3D_CAPTURE_DIR=temp/cityscape-frames \
  ./lambda.exe view test/demo/cityscape/cityscape.ls --headless \
  --event-file <events.json> --event-result temp/cityscape-frames/result.json
```

Each `advance_time` event followed by a `capture_surface` (`"source": "page"`) event
writes one frame. The capture directory must be relative and under `temp/`.

## Limitations

- The full model opens in about a second in a release build. Debug builds with the
  default `log.conf` are much slower, because each closed element's subtree is
  written to `log.txt`.
- The film autoplays silently on load. Sound restarts it from 0. There is no pause or seek.
- Heights are estimates for most buildings, so the skyline is plausible, not surveyed.

Map data © OpenStreetMap contributors, ODbL 1.0. Film concept and title after Eian
([@EianLu](https://x.com/EianLu)).
