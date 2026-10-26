# Soundflow

`./lambda.exe demo soundflow` is a visual instrument after Agoston Nagy's [SoundBow](https://medium.com/swlh/soundbow-a-personal-journey-5789fc2caba8). Draw across a dark field of strings: each stroke replays in a loop at the pace you drew it, and wherever its tip crosses a string, the string sounds. The drawing, the timing and the three instruments are all Lambda code.

| Area | What it does |
| --- | --- |
| Field | 1280×800 units, letterboxed to the window. A drag on the empty field draws a stroke; a tap on a string plucks it |
| Strokes | A broad-nib ribbon, wide or hairline by direction. While you draw, every string the pen crosses sounds at once. When the pen lifts, the stroke rests for 450 ms, then its tip retraces the path with a short lit trail. Up to twelve strokes loop, each with its own period, so their rhythms drift against each other. **H** (or the stroke button) hides the wide ribbons; the tips and their trails, now in the voice's colour, keep playing |
| Strings | A string's pitch comes from its length on a log scale: longer is lower. A string never changes direction. Drag an end to stretch or shrink it along its own line, between 60 and 640 units; that retunes it. Drag its middle to slide the whole string sideways, at right angles to itself; that keeps its pitch and changes when the loops reach it. Strings stay inside the field, and letting go plucks the string. A sounding string swings and lights in the voice's colour |
| Voices | Harp (teal), Kalimba (coral) and Glass (ivory), chosen at the top right or with **1 2 3**. A stroke keeps the voice it was drawn with. A faster bow plays louder |
| Menu (•••) | Four string layouts (Harp, Loom, Sun, Constellation), four scales (Pentatonic, Dub minor, Hirajoshi, Whole tone), playback speed, and a sample piece |
| Speed | 0.5×, 0.75×, 1×, 1.5× or 2×. The loops replay faster or slower; pitch does not change. The speed button cycles through them, and **−** / **+** step down and up. Drawing always sounds at the pen's own pace |
| Bottom bar | Pause (**Space**), Speed, Undo (**Z**), Sound on/off (**M**); Show/hide wide strokes (**H**), Clear, and Save (**E**) |

**Save** writes `temp/soundflow/soundflow-N.wav`, a recording of the loops as they stand, at the current speed (8 to 24 seconds, long enough for the slowest loop to come round twice), and `soundflow-N.svg`, a picture of the drawing. N is the first number not already used.

## How it works

| File | Role |
| --- | --- |
| `mod_model.ls` | Pure state: strings, layouts and pitch, strokes with their timed points, crossings, the loop clock, undo |
| `mod_synth.ls` | The sound engine. A Karplus–Strong plucked string for the harp; decaying sine partials (two-term recurrence, no per-sample `sin`) for the kalimba and the glass; 16-bit WAV encoding; the mixdown for Save |
| `mod_sound.ls` | Live playback through `dom.audio_open`/`dom.audio_play`. Tones are synthesized on first use and cached; idle frames warm the cache a tone at a time |
| `mod_scene.ls` | The SVG field and the HTML chrome, rendered from state |
| `soundflow.ls` | The view, its pointer, key and frame handlers, and Save |

Each stroke keeps its points with their draw times. A crossing is the point where a segment meets a string, at a time interpolated along that segment. On every frame the clock advances and each stroke plays the crossings its tip passed since the last frame. The engine is separate from the picture, as SoundBow's Pure Data engine was. Save renders each loop once as a circular buffer, so tails wrap into the next pass, and then tiles it from the loop's current phase. The recording starts in the loops' steady state.

## Tests

```sh
./lambda.exe test/demo/soundflow/tests/soundflow_model.ls
./lambda.exe view test/demo/soundflow/soundflow.ls --headless --event-file test/ui/soundflow/soundflow.json
```

The model test checks pitch from length, live and looped crossings, pausing, taps, constrained stretching and sliding, playback speed and its effect on saving, the stroke toggle, layouts, scales, undo and the stroke cap against `soundflow_model.txt`. The UI test draws, loops, pauses, retunes, slides a string, pulls an end off its line (the string stays straight) and undoes, changes speed by key and button, hides and shows the wide strokes, then switches voice, scale and layout, clears, saves, plays the sample piece and mutes.
