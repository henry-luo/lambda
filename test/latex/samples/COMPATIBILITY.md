# Phase III sample compatibility

Checked on 2026-10-07 against the unchanged 31 originals. All 93 target records parse and serialize; all 62 native SVG/PDF exports succeed. The 30 non-Lua samples have no unsupported-output placeholders. Lua source parses safely and receives located unsupported-lua diagnostics (**S1.8, S7.4.1–S7.4.4**).

**Supported** means the used bounded constructs render in that target. **Partial** means the used output or a declared package has a diagnosed limit/substitution. **Unsupported** identifies the Lua-generated content. Native corpus exports use the continuous/fixed-page path: these statuses do not promise TeX page breaks, float placement, exact font metrics or typography. Shared paged output is checked separately with the page-style fixture. Static source adapters follow **D7.2.1–D7.2.4**.

| Sample | HTML | SVG | PDF | Checked constructs | Limits |
|---|---|---|---|---|---|
| [algebra.tex](algebra.tex) | Partial | Partial | Partial | Spanish headings, theorem/proof counters, math, lists, page-style declarations | Unknown rare declarations; substituted script font; continuous page counters unresolved; PDF running content needs paged admission and flattens mixed fonts. |
| [arithmetic_lualatex.tex](arithmetic_lualatex.tex) | Unsupported | Unsupported | Unsupported | Opaque directlua/luacode bodies; static surrounding document | Lua is never executed. Generated arithmetic is unsupported; unicode-math/luacode declarations remain diagnosed. |
| [biblatex_biber.tex](biblatex_biber.tex) | Supported | Supported | Supported | Inline filecontents bibliography, author-year citations, bibliography and links | Bounded CSS/math/drawing layout; exact TeX typography unclaimed. |
| [booktabs.tex](booktabs.tex) | Supported | Supported | Supported | Three tables, full/partial rules, column content and spacing | Bounded CSS/math/drawing layout; exact TeX typography unclaimed. |
| [bungee_model.tex](bungee_model.tex) | Partial | Partial | Partial | TikZ nodes, computed positions, decorated coils and arrows | Computer Modern Bright is substituted. |
| [complex_numbers.tex](complex_numbers.tex) | Supported | Supported | Supported | Math exercises, numbered lists and text declarations | Bounded CSS/math/drawing layout; exact TeX typography unclaimed. |
| [diagonal_table_cells.tex](diagonal_table_cells.tex) | Supported | Supported | Supported | Literal text, diagonal headers, tables and labels | Bounded CSS/math/drawing layout; exact TeX typography unclaimed. |
| [dragon_curve.tex](dragon_curve.tex) | Supported | Supported | Supported | TikZ L-system generation and reusable path output | Bounded CSS/math/drawing layout; exact TeX typography unclaimed. |
| [filter_overview.tex](filter_overview.tex) | Supported | Supported | Supported | Verbatim code, raw comments, sections and literal commands | Bounded CSS/math/drawing layout; exact TeX typography unclaimed. |
| [foray_into_latex.tex](foray_into_latex.tex) | Supported | Supported | Supported | Text, lists, spacing and ordinary inline/display mathematics | Bounded CSS/math/drawing layout; exact TeX typography unclaimed. |
| [greek_russian.tex](greek_russian.tex) | Partial | Partial | Partial | Mixed Greek/Cyrillic text, local language spans and polytonic LGR conversion | GFS Porson is substituted; per-page style override is diagnosed. |
| [longtable.tex](longtable.tex) | Partial | Partial | Partial | Shared table cells, first/continued heads/feet and captions | Continuous first head/last foot only; PDF repeated-page table semantics are unsupported. |
| [math_proof.tex](math_proof.tex) | Partial | Partial | Partial | AMS math, theorem/proof structure, bold/calligraphic math requests | Euler calligraphic substitution and CSS bold-math approximation. |
| [math_test_flight.tex](math_test_flight.tex) | Supported | Supported | Partial | Math, lists, macros and fancyhdr running text | Continuous PDF reports unavailable running headers and flattened inline margin fonts; paged blocks remain host-limited. |
| [nursing_care_plan.tex](nursing_care_plan.tex) | Partial | Partial | Partial | Landscape geometry, custom fixed columns, multiline cells and row color | Continuous longtable; PDF row fragmentation and repeated headers remain unsupported. |
| [ofdm_spectrum.tex](ofdm_spectrum.tex) | Partial | Partial | Partial | PGF programs, bounded accumulators, sampled spectrum, Spanish text | Helvetica is substituted; bounded PGF expression/program profile. |
| [plotting_lesson.tex](plotting_lesson.tex) | Supported | Supported | Supported | PGFPlots axes, arbitrary sampling variable, smooth curves and domain bounds | Bounded CSS/math/drawing layout; exact TeX typography unclaimed. |
| [pulley_systems.tex](pulley_systems.tex) | Partial | Partial | Partial | TikZ nodes, arcs, fills, arrows, labels and common decorations | Computer Modern Bright is substituted. |
| [russian_article.tex](russian_article.tex) | Supported | Supported | Partial | Russian text, headings, linked references and font/input encoding declarations | PDF cmap/searchable extraction request is unsupported; visual Unicode glyphs are preserved. |
| [scientific_writing.tex](scientific_writing.tex) | Supported | Supported | Supported | Theorem/proof, AMS row tags, line breaks and bibitem citations | Bounded CSS/math/drawing layout; exact TeX typography unclaimed. |
| [semantic_inference_rules.tex](semantic_inference_rules.tex) | Supported | Supported | Supported | Reusable semantic inference diagrams | Bounded CSS/math/drawing layout; exact TeX typography unclaimed. |
| [sequent_calculus.tex](sequent_calculus.tex) | Supported | Supported | Supported | Bussproofs trees, inference labels and local proof macros | Bounded CSS/math/drawing layout; exact TeX typography unclaimed. |
| [slashbox.tex](slashbox.tex) | Supported | Supported | Supported | Diagonal cell headers and ordinary table content | Bounded CSS/math/drawing layout; exact TeX typography unclaimed. |
| [spacetime_diagrams.tex](spacetime_diagrams.tex) | Partial | Partial | Partial | Declared PGF functions, computed coordinates, grid, clip, rays/curves/arrows | RSFS is substituted; clipping and PGF program remain within the bounded profile. |
| [thai_polyglossia.tex](thai_polyglossia.tex) | Partial | Partial | Partial | Thai/English spans, font families and Thai numeral formatting | Requested fonts are substituted; native SVG/PDF lacks complex-script shaping and dictionary line breaks. |
| [tikzpeople.tex](tikzpeople.tex) | Partial | Partial | Partial | 29 named reusable shapes, pins, mirror/colors/accessories and gallery | Simplified vector artwork is diagnosed; CTAN artwork detail is not reproduced. |
| [timeline.tex](timeline.tex) | Partial | Partial | Partial | TikZ timeline nodes/paths/colors and standalone/preview declarations | Per-page style changes and requested preview cropping are diagnosed. |
| [ubc_math_220.tex](ubc_math_220.tex) | Supported | Supported | Supported | AMS theorem styles, shared counters, proofs and math | Bounded CSS/math/drawing layout; exact TeX typography unclaimed. |
| [unit_circle.tex](unit_circle.tex) | Supported | Supported | Supported | Geometry and reusable TikZ circle/arc/nodes/labels | Bounded CSS/math/drawing layout; exact TeX typography unclaimed. |
| [vietnamese_document.tex](vietnamese_document.tex) | Partial | Partial | Partial | Vietnamese text, nested macros, tcolorbox, nested split mathematics and numbered exercises | Unknown rare packages/legacy arrows library remain diagnosed; font/bold approximations and continuous page counters remain. |
| [xltabular.tex](xltabular.tex) | Partial | Partial | Partial | Flexible X columns, heads/feet, captions and table references | Continuous first head/last foot only; PDF repeated-page table semantics are unsupported. |

## Reproduce and inspect

```sh
python3 test/latex/samples/audit.py --target all --export
python3 test/latex/samples/audit.py --sample spacetime_diagrams --target all --export
```

Artifacts, JSON records and stderr logs are written under `temp/latex_phase3/audits/`. Full audits write `summary_all.json`; selected audits write `summary_all_selected.json`. A nonzero CLI/export status, parse failure, serialization error or empty output/artifact fails the runner. Each record includes activated packages, located diagnostics, image/bibliography resolution, selected visible text and artifact size. Resource availability checks local existence; remote resources are not fetched by this check (**D7.1.2v2, S12.4.1**).

The captured [phase3_audit.json](phase3_audit.json) records all three targets. The [proposal completion record](../../../vibe/Lambda_Pkg_Latex3.md#85-checked-implementation--2026-10-07) describes adapters, host changes and broader validation limits. [TeX Live reference PDFs and comparisons](../reference/phase3/README.md) cover table geometry, spacetime paths and subcaptions; zero diagnostics alone is not visual acceptance.

Additional `.tex` fixtures under `test/lambda/latex/fixtures/` cover natbib/inline resources through script-generated source, caption/subcaption, listings, English cleveref, nested macros, paged headers/counters and hierarchical PDF bookmarks. Matching Phase III `.ls`/`.txt` tests assert semantic content, link targets, geometry, diagnostics and serialization.
