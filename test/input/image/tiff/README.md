# TIFF decoder fixtures

`jpeg12-gray.tiff` is a 32 by 24, single-channel, 12-bit JPEG-compressed TIFF.
It was generated on 2026-10-16 with the original libtiff 4.7.1 JPEG12 adapter
and libjpeg-turbo 3.1.2, before disabling JPEG12 in Lambda's build. It uses
eight rows per strip and JPEG quality 95. Input sample `(x, y)` is
`(97*x + 43*y) & 4095`, packed as two 12-bit samples per three bytes.

The original backend successfully read its first scanline and rejected its
RGBA convenience-reader conversion. The stored fixture tests successful
metadata probing and an explicit `unsupported_feature` result for pixel
decoding. It avoids requiring the excluded JPEG12 adapter to generate test
input. The image suite separately generates and decodes ordinary 8-bit RGB
JPEG-in-TIFF with both strips and tiles.

12-bit JPEG-in-TIFF has specialist uses, including
[GDAL GeoTIFF workflows](https://gdal.org/en/stable/drivers/raster/gtiff.html).
Lambda currently renders TIFF through an RGBA8 pipeline whose fallback reader
does not accept 12-bit samples. This build choice preserves ordinary 8-bit
JPEG-in-TIFF and other enabled TIFF compression methods.
