# Phase III TeX comparisons

These PDFs pin representative output from **TeX Live 2025/basic**, using
**pdfTeX 3.141592653-2.6-1.40.27** on 2026-10-06. Source and PDF hashes are in
[manifest.json](manifest.json). The Overleaf sources remain unchanged; their
authors and CC BY 4.0 attribution are in [the sample index](../../samples/README.md).
The caption source is a Lambda regression fixture.

| Reference | Checked behavior | Expected differences |
|---|---|---|
| [booktabs.pdf](booktabs.pdf) | Three tables, row/column content, full and partial rules | CSS rule spacing, font metrics, and page layout |
| [spacetime_diagrams.pdf](spacetime_diagrams.pdf) | Blue grid and rays, black axes, red rays, green accelerated trajectory, clipping and arrow direction | Font selection, label placement and diagram scale |
| [caption_profile.pdf](caption_profile.pdf) | Two subpanels, parent caption, caption outside a float, linked parent/subpanel/table references, unnumbered caption | CSS float layout and caption spacing |

## Reproduce

Run from the repository root, with the named TeX distribution and its required
packages installed. TeX output belongs under `temp/`:

```sh
mkdir -p temp/latex_phase3/reference
pdflatex -interaction=nonstopmode -halt-on-error -output-directory=temp/latex_phase3/reference test/latex/samples/booktabs.tex
pdflatex -interaction=nonstopmode -halt-on-error -output-directory=temp/latex_phase3/reference test/latex/samples/spacetime_diagrams.tex
pdflatex -interaction=nonstopmode -halt-on-error -output-directory=temp/latex_phase3/reference test/lambda/latex/fixtures/caption_profile.tex
pdflatex -interaction=nonstopmode -halt-on-error -output-directory=temp/latex_phase3/reference test/lambda/latex/fixtures/caption_profile.tex
python3 test/latex/samples/audit.py --target all --export
```

The spacetime source requires the `mathrsfs` package and RSFS font metrics.
The capture used the distribution's package and supplied RSFS `.tfm` files
through `TFMFONTS`; it did not change the source. PDF timestamps and object
identifiers can change across builds, so the manifest identifies the captured
files rather than requiring byte equality on regeneration.

The corpus audit records parse, serialization, diagnostics, resource paths,
visible text, exporter status, and artifact size. These records complement the
semantic regression fixtures and visual comparisons; zero unsupported elements
does not establish TeX typography or pagination fidelity (**S7.4.1–S7.4.4**).
See [the compatibility report](../../samples/COMPATIBILITY.md) for output limits.
