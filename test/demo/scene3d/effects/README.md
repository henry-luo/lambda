# Make waves

```sh
./lambda.exe view test/demo/scene3d/effects/effects.ls
```

Five animated GLSL materials share one source: channel split, liquid distortion,
rolling distortion, sliding doors and flies. Switch between typography, SVG
artwork and a raster landscape; move Intensity, pause/play, or restart all five.
The original stays alongside the effects. The layout becomes one column on
mobile, and breadcrumbs lead back to the Scene3D collection.

Construction and control edits use Lambda view state (**S12.1.1v3**,
**D7.5.3v2**). Radiant owns shader programs, texture uploads and timeline playback
(**D4.5.1v4**, **D4.5.2**). There is no per-frame Lambda or JavaScript callback.

See the [package guide](../../../../doc/Lambda_Scene3d_Effects.md) for API,
uniforms, file and generated-SVG sources, and custom GLSL. These are independent
effects inspired by [Blotter](https://blotter.js.org/#/materials), without a
Blotter or Three.js dependency.

`assets/text.svg` and `assets/graphic.svg` are original SVG artwork.
`assets/landscape.png` is original raster artwork generated with Python's standard
library: `python3 test/demo/scene3d/effects/assets/generate.py`. All three sources
are bundled and need no network. Artwork is CC0; the font for the text SVG comes
from the host's normal Arial/sans-serif lookup.
