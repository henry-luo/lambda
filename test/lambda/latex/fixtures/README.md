# Native TeX oracle

`tex_native_oracle.json` was captured on 2026-11-25 from the native TeX and
LaTeX input provider, before its retirement, using
`utils/capture_latex_native_oracle.ls`. Each case stores the source, input mode
and JSON-formatted native result. The Script facade test reads these snapshots
without invoking the native provider. This small initial sample does not qualify
the full engine or replace the existing native comparison corpus (S1.8,
D10.2.2).
