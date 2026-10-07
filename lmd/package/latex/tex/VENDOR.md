# Vendored TeX programming packages

These are unmodified copies of upstream LaTeX programming packages. Lambda's
TeX expansion engine loads them as *bundled resources* when a document or a
beside-document package requests them (`vibe/Lambda_Pkg_Latex3.md` §9.4,
§9.8). They are never looked up in a TeX installation. Do not edit them in
place: a change belongs upstream, or in the engine, or in the Lambda-owned
kernel (`lambda/input/input-tex-kernel.cpp`). See CLAUDE.md rule 16.

| | |
|---|---|
| Source | TeX Live 2025 (`/usr/local/texlive/2025basic/texmf-dist`, resolved with `kpsewhich`) |
| Copied | 2026-10-07 |
| Licence | LaTeX Project Public License 1.3c, as stated in each file's header |
| Local patches | none |

| File | Upstream version | SHA-256 (prefix) |
|---|---|---|
| `calc.sty` | 2023/07/08 v4.3 | `5af5f4960d4390f4` |
| `etex.sty` | 1997/08/12 v0.1 | `eba429a3179645fc` |
| `etexcmds.sty` | 2019/12/15 v1.7 | `a7db2834e02bd11b` |
| `expl3.ltx` | 2025-01-18 (l3kernel) | `5fe990d648915d27` |
| `expl3.sty` | 2025-01-18 (l3kernel) | `1841b4d9b33cfd28` |
| `expl3-code.tex` | 2025-01-18 (l3kernel) | `7e765c50730451dd` |
| `etoolbox.sty` | 2025/02/11 v2.5l | `23e5277f81dc9639` |
| `iftex.sty` | 2024/12/12 v1.0g | `0dc0cefe2131f823` |
| `ifthen.sty` | 2024/03/16 v1.1e | `699c509ebe73440e` |
| `infwarerr.sty` | 2019/12/03 v1.5 | `8c3e23b8bb7d940f` |
| `keyval.sty` | 2022/05/29 v1.15 | `011ba643109d799f` |
| `keyval.tex` | (xkeyval 2.9 bundle, generic keyval) | `7a8dbfe90926a910` |
| `kvdefinekeys.sty` | 2019-12-19 v1.6 | `c42f0c2628dc5fbd` |
| `kvoptions.sty` | 2022-06-15 v3.15 | `70595629ec990ba8` |
| `kvsetkeys.sty` | 2022-10-05 v1.19 | `e3fcf7f3372a27bb` |
| `ltxcmds.sty` | 2023-12-04 v1.26 | `8fea89995d37ba4c` |
| `pdftexcmds.sty` | 2020-06-27 v0.33 | `2df7e0bd148c99d8` |
| `xkeyval.sty` | 2022/06/16 v2.9 | `6904c396f2a69929` |
| `xkeyval.tex` | 2014/12/03 v2.7a | `29c2b60c6973c374` |
| `xkvtxhdr.tex` | (xkeyval 2.9 bundle) | `cc6150c71e73d1dd` |
| `xkvutils.tex` | (xkeyval 2.9 bundle) | `5e82c31b1b4492c8` |

## Unicode data

expl3 reads these while it is preloaded into the engine's expl3 format
(`lambda/input/input-tex-format.cpp`). They come from TeX Live's
`tex/generic/unicode-data`, unmodified copies of the Unicode Character Database.

| | |
|---|---|
| Version | Unicode 16.0.0 |
| Licence | Unicode License v3; see https://www.unicode.org/terms_of_use.html (stated in each file's header) |
| Local patches | none |

| File | SHA-256 (prefix) |
|---|---|
| `UnicodeData.txt` | `ff58e5823bd09516` |
| `CaseFolding.txt` | `6f1f9c588eb4a5c7` |
| `SpecialCasing.txt` | `8d5de354eef79f23` |
| `GraphemeBreakProperty.txt` | `c29360bd6f713281` |

To re-sync, copy the files again with `kpsewhich`, update the tables, and run
`test/lambda/latex/test_latex_phase4_bundled.ls` and
`test/lambda/latex/test_latex_phase4_expl3.ls`.
