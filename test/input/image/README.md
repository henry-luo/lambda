# Image format gallery samples

Original geometric artwork created for this repository. The gallery is
[`test/html/image_formats_gallery.html`](../../html/image_formats_gallery.html).
All gallery samples are 256 × 192; `sample.svg` is the vector reference.

The seven writable raster formats are encoded through Lambda's common image
library. GIF is a single-frame, 255-color example with binary transparency.
JPEG and BMP flatten alpha onto RGB `(248, 245, 236)`; other samples preserve
the supported alpha representation. The five dots illustrate color and opacity.

Regenerate the samples and gallery from the repository root with a built
`lambda-cli.exe` or `lambda.exe` and Python Pillow installed:

```sh
python3 test/input/image/generate.py
```

The generator shares the artwork geometry between SVG and the raster source,
uses Pillow for rasterization and GIF palette output, and invokes Lambda for
PNG/JPEG/WebP/AVIF/BMP/TIFF/ICO encoding. Temporary sources and logs stay in
`temp/image-formats-gallery/`. `gallery.template.html` is the editable page
template; file sizes and image cards are generated from one format list.

## Layered PSD viewer fixture

`layered.psd` is a Lambda-generated 96 × 64 RGB8 document with a red
background and a foreground group containing a blue layer at `[48,0]`.
The blue layer has a 24 × 64 white mask with default coverage zero, so the
rendered image is red at x=0–47, blue at x=48–71, and red at x=72–95.

The file's raw merged preview was deliberately replaced with solid green.
Viewer checks must display the editable layers and mask through `lambda.image`,
not mistake that stale preview for the current composition. The fixture uses
raw compression, normal blending and no ICC profile or Photoshop effects.
