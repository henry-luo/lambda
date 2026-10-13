# beamer in `lambda.latex`: frames as `lambda.slide` decks

**Status:** Design proposal, 2026-10-13. The user chose this direction over keeping beamer out of scope ([Phase V plan](../impl/Lambda_Impl_Latex_Packages.md) §17.5). Nothing is implemented; until it is, `\documentclass{beamer}` reports `unknown-package` and renders with the article profile.

**Formal linkage:** **D7.2.7v2** (expansion only: `beamer.cls` never runs on the engine, and its semantics live in a class profile), **D7.2.1–D7.2.4** (script adapters in the static registry), **S1.8** (no document text becomes Lambda code), **S12.1.1v2** (the lowering is pure and deterministic), **D7.5.3** (the Lambda/Radiant boundary the slide player keeps), **S7.4.1–S7.4.4** (located diagnostics). Within the slide package: SLD1–SLD4 ([Lambda_Pkg_Slides.md](../Lambda_Pkg_Slides.md) §0).

**Scope:** a beamer document's frames, overlays, blocks, columns, notes, outline and themes, lowered onto the `<presentation>` deck that `lambda.slide` already plays. Out of scope: running beamer's templates as TeX, pixel-exact theme artwork, `beamerarticle`'s article mode beyond §6, and PPTX.

## 1. Proposal

A beamer document becomes a deck, not an article. The LaTeX package parses and analyses it as today (engine expansion, numbering, labels, bibliography), then a beamer lowering turns each frame into a `<slide>` whose content is the existing renderer's HTML for the frame body, placed in a `<content>` box. Overlays become `<cue>` groups over stable target IDs. The deck is ordinary Mark data under SLD1, so `lambda.slide` compiles, presents, snapshots and exports it with no new slide kind.

Why this shape:

- beamer's typesetting (headlines, sidebars, overlay page duplication) is TeX output; Lambda expands like TeX but never typesets like it (**D7.2.7v2**). The slide package already owns canvas fitting, playback and transitions, so beamer adds a source format, not a second player.
- The frame body is LaTeX the package already renders: math, tables, graphics, TikZ, theorems and bibliographies come for free.
- Overlay specifications have a direct reading as cue boundaries (§3), and the slide model's stable addresses `{slide, cue, time_ms}` match beamer's pages one for one, which gives a verification oracle (§8).

## 2. Pipeline

1. **Class profile.** `beamer` joins `registry.CLASSES` with base `article`, its options (`aspectratio`, `handout`, `t`/`c`/`b`, `10pt`–`20pt`, `xcolor=…`, `hyperref=…`) and its commands, and `implies` the packages beamer loads (graphicx, xcolor, hyperref, amsmath, amsthm). Its profile names a **deck** output, so `render_result` returns `{deck, diagnostics, …}` beside or instead of `elements`.
2. **Engine.** Overlay specifications `<…>` after a command or `\begin{env}` must reach the adapter. The engine gains an overlay argument kind for a registry list of overlay-aware commands (`\item`, `\only`, `\uncover`, `\visible`, `\invisible`, `\onslide`, `\alt`, `\temporal`, `\alert`, `\textbf`, `\textit`, `\color`, `\includegraphics`, `\pause[n]`, `frame`, `block` and the list environments' `[<+->]`), recorded in `argument_groups` with kind `overlay`. `\pause` becomes a constructor. This is the only engine work.
3. **Analysis.** Frames are counted and labelled (`label=`), sections and subsections form the outline, `\AtBeginSection`/`\AtBeginSubsection` bodies are recorded raw, and each overlay-carrying element gets a deterministic ID.
4. **Lowering** (`lmd/package/latex/beamer/`): `deck.ls` builds `<presentation>` and the slides, `overlay.ls` parses and resolves specifications, `themes.ls` maps themes and `\setbeamer…` settings.
5. **Viewing.** `lambda view talk.tex` presents the deck through `lambda.slide.present` (SLD2) when the result has one; `convert` writes the deck as Mark or, through `slide.handout`, static slides.

## 3. Overlays

beamer gives every frame an overlay count `n`, the largest step any specification names. A specification `<a-b>`, `<a->`, `<-b>`, `<a,c>` or the incremental `<+->`/`<.->` (with offsets such as `<+(1)->`) resolves, in source order, to the set of steps an element shows on.

Two lowerings, chosen per frame:

- **Cues.** When every overlay only shows, hides or emphasises content (`\pause`, `\uncover`, `\visible`, `\invisible`, `\onslide`, `\item<…>`, `[<+->]` lists, `\alert<…>`, `\textbf<…>`, `\color<…>`), the frame is one slide. Step 1 is its initial state; step `k` is cue `k-1` with `start: 'click'`. An element entering at `a` gets an entrance effect in cue `a-1`; one leaving after `b` an exit in cue `b`; an emphasis (`\alert<2>`) a set-and-reset pair. Hidden content keeps its space, which is beamer's `\uncover` semantics and the slide model's (Pkg_Slides §7.2).
- **Step expansion.** When an overlay changes layout (`\only`, `\alt`, `\temporal`, `\onslide+`/`\onslide*`, an overlay on `\includegraphics` or a column), the frame becomes `n` slides with `transition: 'none'`, each rendered at its step, as beamer's own PDF does. It reports `beamer-overlay-expanded`, since the steps are slides, not cues.

Malformed or unresolvable specifications report `unsupported-overlay-spec` and show the element on every step. `handout` mode (or `\mode<handout>`) lowers every frame at its final step, and `\mode<article>` content is dropped from the deck.

## 4. Frames and content

| beamer | Deck |
|---|---|
| `frame` environment, `\frame{…}`, `\begin{frame}{title}{subtitle}`, `\frametitle`, `\framesubtitle` | A `<slide>` with a title `<text role: 'title'>` and a flow `<content>` box for the body |
| `[fragile]` | Accepted: the engine copies raw environments inside any frame |
| `[plain]`, `[t]`/`[c]`/`[b]`, `[noframenumbering]`, `[label=x]`, `[standout]` | No head or foot strip; vertical alignment of the content box; skipped in the frame count; the slide ID; the theme's emphasis background |
| `[allowframebreaks]`, `\framebreak` | Explicit `\framebreak`s split slides; overflow is not measured, so automatic breaks report `frame-breaks-approximation` and the body stays one scrollable slide |
| `\titlepage`, `\maketitle` in a frame, `\titlegraphic`, `\institute`, `\subtitle` | The title slide from the title fields, as the class profiles' title block (plan §14) |
| `\section`, `\subsection`, `\tableofcontents[currentsection,…]`, `\AtBeginSection` | Outline entries; a contents slide with the current section highlighted; the recorded hook body lowered as an extra frame at each section |
| `block`, `alertblock`, `exampleblock`; `theorem`, `definition`, `example`, `lemma`, `corollary`, `fact`, `proof` | Themed boxes with a title bar; the class's built-in theorems as llncs's are |
| `columns`, `column{0.4\textwidth}`, `[T]`/`[c]` | A flex row with the stated widths and alignment |
| `\note{…}`, `\note[item]{…}` | `<notes>` on the slide |
| `\transfade`, `\transpush`, `\transwipe`, `\transdissolve`, `\transduration` | The slide's `transition` and `transition_duration` where the slide package has the effect; others report `unsupported-transition` |
| `\againframe{label}` | The labelled frame's slides again, with new IDs |
| `\hyperlink{label}{\beamerbutton{…}}`, `\beamergotobutton` | A link to the labelled slide |
| `\logo{…}` | An image in the theme's logo corner |

## 5. Themes and templates

Themes are presentation, so they map rather than run. `themes.ls` maps a small set of presentation themes (default, Madrid, Berlin, Warsaw, CambridgeUS, Boadilla, metropolis) to a palette, a title-bar style and a footline (author, short title, frame number), and color, font, inner and outer themes to the matching tokens. Any other theme renders as the default and reports `beamer-theme-approximation`. `\setbeamercolor{name}{fg=…,bg=…}` and `\setbeamerfont{name}{size=…,series=…}` set tokens for the named elements in a bounded subset (`frametitle`, `title`, `structure`, `alerted text`, `block title`, `block body`, `normal text`); `\setbeamertemplate{navigation symbols}{}` is accepted, since a deck has none, and other templates report `unsupported-beamer-template`. `aspectratio=169` sets a 1280 × 720 canvas, `43` (the default) 1024 × 768, and the others their ratio at a height of 720.

## 6. What stays out

- beamer's own `\usebeamertemplate` machinery, headline and sidebar navigation, mini-frames and `pgfpages` layouts: themes are mapped, not executed.
- `beamerarticle` (`\usepackage{beamerarticle}` in an article): its frames could render inline as article sections; this is left until a corpus document needs it.
- Measured frame breaking and shrinking (`[shrink]`, `[squeeze]`): they need typeset heights, which a deck does not have; both report `frame-breaks-approximation`.

## 7. Milestones

- **B0. Deck output.** The class profile, frames as slides without overlays, the title slide, sections and the outline slide, `render_result.deck`, `lambda view`. Overlay specifications are parsed by the engine and ignored, with one `unsupported-overlay-spec` per frame.
- **B1. Overlays.** Specification resolution, cue lowering and step expansion, `handout` and `\mode`.
- **B2. Content.** Blocks and theorems, columns, notes, transitions, `\againframe`, buttons, `allowframebreaks`.
- **B3. Themes.** The theme map, `\setbeamercolor`, `\setbeamerfont`, the logo and footline.

## 8. Verification

- Golden decks: each fixture's deck as Mark, plus `slide.snapshot` HTML at chosen addresses.
- Page oracle: a pinned TeX Live beamer PDF has one page per `{slide, cue}` address of the cue lowering (or per slide under step expansion); the page count and each page's visible text are compared with the snapshot at the matching address, as `test/latex/reference/phase3/` compares articles.
- Diagnostics: every reported construct has a failing fixture with its located diagnostic.

## 9. Decisions for the user

1. **Deck output.** Should `render_result` return the deck beside an article fallback (the frames as sections), or the deck only? The proposal returns both, so existing `convert` callers still get HTML.
2. **Layout-changing overlays.** Step expansion (one slide per step, matching beamer's PDF) or a cue approximation that keeps one slide and reports it? The proposal expands.
3. **Theme coverage.** Is the seven-theme map enough for a first release, or should the corpus decide (§17.6 of the plan asked no downloads)?
