# DOOM demo attribution

The Lambda Script gameplay and CSS scene port derive from Niels Leenheer's
[cssDOOM](https://github.com/NielsLeenheer/cssDOOM), pinned to commit
`438d2e17fb9f75fa3dbc54e1d21a3bc088ad009d`. Its package metadata declares
`GPL-2.0`; the exact upstream license is preserved in `LICENSE.cssDOOM.txt`.
Translated gameplay, visual constants and scene code retain that attribution.

Upstream credits game design, code and samples to id Software (1993), and its
CSS 3D rendering and JavaScript reimplementation to Niels Leenheer. The maps,
textures, sprites, HUD images, weapon images and sounds here are exact copies
of that revision's `public/maps` and `public/assets`. They are recorded separately
as `upstream-bundled-doom-resource` in `data/resources.json`; the source-code
license declaration is not an assertion that all DOOM artwork or audio is GPL.
No additional asset-license grant is inferred by this port.

`data/resources.json` records original and destination paths, byte sizes and
SHA-256 hashes. `tools/import_upstream.py` reproduces the import from a clean
checkout at the pinned revision. Its offline constant extraction runs Node;
the playable application has no JavaScript runtime dependency.
