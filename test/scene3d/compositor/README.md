# GPU compositor parity pages

Live-window fixtures for the GPU compositor's steps 3 to 5
([design](../../../vibe/radiant/Radiant_Design_GPU_Compositor.md)). Each page animates its scenes
for 1.5 s; the event file only waits. Live windows composite by default (`RADIANT_GPU_COMPOSITOR=off`
turns it off). Run one with the present check on:

```sh
RADIANT_PRESENT_CHECK=1 LAMBDA_LOG_FILE=temp/compositor.log \
  ./lambda.exe view test/scene3d/compositor/interleaved.html \
  --event-file test/scene3d/compositor/interleaved.json
grep -E '\[COMPOSITOR\]|\[PRESENT_CHECK\]' temp/compositor.log
```

`[PRESENT_CHECK]` lines must report no pixel beyond tolerance. The expected outcome per page:

| Page | Expected `[COMPOSITOR] window frames:` |
| --- | --- |
| `interleaved.html` | two of two viewports composited: scenes between text, under translucent panels, one at a fractional offset |
| `opacity.html` | two of three: a scene alone in an opacity group and one with its own opacity; the third shares its group with text, so it stays on the CPU |
| `transform.html` | two of two: a rotated scene and a translated, scaled-up one; the scaled-down scene is never cut (minified) |
| `clips.html` | four of four: a scene in a rounded box, one clipped on whole pixels, one under `clip-path: circle()`, and one in two nested rounded boxes; the fractional clip stays on the CPU |
| `density.html` | two of two: scenes at `max-pixel-ratio` 1 and 0.5, drawn from their capped-density targets; the transparent half-density scene at a fractional offset stays on the CPU |
| `webgl.html` | four of four WebGL canvases: premultiplied and magnified under a translucent panel, straight alpha at a fractional offset, opaque in a rounded box, and a preserved drawing buffer, rotated; the `image-rendering: pixelated` canvas stays on the CPU |

The pages run headless as well, where they exercise only the CPU path. They need a display and
native graphics, so no gtest runs them; rerun them after changing the cut, segment replay or the
presenter.
