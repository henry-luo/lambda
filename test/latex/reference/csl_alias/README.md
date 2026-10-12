# CSL style alias references

These PDFs pin what **biblatex itself** prints for the four documents in [`test/lambda/latex/fixtures/csl_alias/`](../../../lambda/latex/fixtures/csl_alias/). They are the admission fixtures required by [Lambda_Design_BibTeX_CSL.md](../../../../vibe/latex/Lambda_Design_BibTeX_CSL.md) §7.4 for the aliases that format `style=apa`, `style=ieee` and `biblatex-chicago` documents with bundled CSL styles. Versions, source hashes and PDF hashes are in [manifest.json](manifest.json).

| Reference | biblatex style | Lambda CSL style |
|---|---|---|
| [apa.pdf](apa.pdf) | biblatex-apa 9.20 | `apa` (APA 7th edition) |
| [ieee.pdf](ieee.pdf) | biblatex-ieee 1.4f | `ieee` (IEEE Reference Guide 2023) |
| [chicago_authordate.pdf](chicago_authordate.pdf) | biblatex-chicago 3.18b, `authordate` | `chicago-author-date` (Chicago 18th edition) |
| [chicago_notes.pdf](chicago_notes.pdf) | biblatex-chicago 3.18b, `notes` | `chicago-fullnote-bibliography` (Chicago 17th edition) |

[`test_latex_csl_alias_reference.ls`](../../../lambda/latex/test_latex_csl_alias_reference.ls) compares every citation sentence, bibliography entry and note with the extracted text. Its golden prints `same` or both texts, so it is the exact record of the differences below.

## Differences

All citation sentences match except one Chicago author–date case below. The notes match apart from title capitalization. Each remaining difference has a known cause:

| Difference | Styles | Cause |
|---|---|---|
| Article and book titles keep their stored capitalization (`Measuring Citation Styles`), where biblatex prints sentence case (`Measuring citation styles`) | APA, IEEE | biblatex-apa and biblatex-ieee convert titles to sentence case. CSL 1.0.2 has no reliable sentence-case conversion and expects titles stored in sentence case. |
| `A Book About Bibliographies` against `A Book about Bibliographies` | Chicago, both | The CSL title-case rule lowercases only a fixed list of short words, and *about* is not on it. biblatex-chicago keeps the stored text. |
| `(Smith et al. 2019)` against `(Smith, Jones, and Lee 2019)`, and no place of publication in the author–date bibliography | Chicago author–date | The bundled CSL style follows the Chicago **18th** edition, which uses *et al.* for three or more authors and drops the place of publication. biblatex-chicago 3.18b follows the 17th edition. |
| `Review of Samples 4: 20–31` against `4:20–31` | Chicago author–date | The CSL style delimits volume and pages with `": "`; its source notes that CSL cannot tell when a volume has no issue. |
| `…, 2020, doi: 10.1234/jex.2020.003.` against `…, 2020. doi: 10.1234/jex.2020.003` | IEEE | The CSL style follows the 2023 IEEE Reference Guide; biblatex-ieee 1.4f punctuates the DOI the older way. |
| `12(3)` against `12 (3)` | APA | Not a real difference: the PDF text extractor inserts a space after the italic volume number. |

Lambda places a note-style `\autocite` mark after the following punctuation (`First.¹`), as biblatex's default `autopunct` does; this was fixed while capturing these references.

## Reproduce

The capture used a private TeX Live user tree under `temp/`, so the system TeX installation was not changed. Run from the repository root:

```sh
mkdir -p temp/texmf
tlmgr --usermode --usertree "$PWD/temp/texmf" init-usertree
tlmgr --usermode --usertree "$PWD/temp/texmf" --repository https://ftp.math.utah.edu/pub/tex/historic/systems/texlive/2025/tlnet-final install biblatex biblatex-apa biblatex-ieee biblatex-chicago logreq xstring
```

Biber is a binary package, which user mode cannot install. Download `archive/biber.universal-darwin.tar.xz` from the same repository, check its SHA-512 against the `containerchecksum` in that repository's `tlpkg/texlive.tlpdb`, and extract it. For each document, copy the `.tex` file and `refs.bib` into a directory under `temp/` and run, with `TEXMFHOME` pointing at the user tree:

```sh
pdflatex -interaction=nonstopmode -halt-on-error apa.tex
biber apa
pdflatex -interaction=nonstopmode -halt-on-error apa.tex
pdflatex -interaction=nonstopmode -halt-on-error apa.tex
pdftotext -enc UTF-8 apa.pdf apa.pdftotext.txt
```

PDF timestamps and object identifiers change between builds, so the manifest identifies the captured files rather than requiring byte equality.
