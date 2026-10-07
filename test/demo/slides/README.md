# Slide presentation demos

Run these commands from the repository root.

```bash
./lambda.exe view test/demo/slides/northstar.slides
```

**Northstar / Strategy 2027** is an 18-slide fictional leadership briefing with
a consistent navy, teal and warm-white palette, generous spacing, numbered
sections and speaker notes. It includes KPI cards, a market-size donut, revenue
charts, a customer journey, a product architecture, an operating flywheel, a
quarterly roadmap, comparison and scorecard tables, a funnel, a risk matrix and
an investment allocation. All business data is illustrative.

The opening plays automatically. **Play** runs the whole deck, with a three-second
pause before each build or slide; **Pause** freezes playback, and **Play** or
**Resume** continues it. **Restart** returns to slide 1. Use **Next** for manual
builds and slides (manual navigation stops automatic playback).
slides 2, 6, 10, 11 and 15 have an additional staged reveal. The deck combines
fade, fly, zoom and wipe effects with fade and push transitions. Previous,
Pause/Resume and Restart are available in the controls. Arrow keys and Space
also navigate when the player has focus. Resize the window to fit the 1280 × 720
logical canvas.

The toolbar's **Speed** slider runs from 0.5× to 4× in 0.25× steps. It scales
animations and the time between automatic builds/slides, preserving current
progress when adjusted. Changing speed while paused keeps playback paused;
Restart retains the chosen speed. The readout shows the active multiplier.

Charts animate automatically when their slide enters: the market donut fills
clockwise, the revenue line draws from left to right, vertical bars rise from
their baseline, and horizontal bars fill in sequence. Axes and legends remain
steady; value labels follow the data. Pause/Resume freezes and continues these
reveals. The reducer's `restart-slide` command replays the current chart, and
reduced-motion playback shows its final state immediately. The shared player toolbar uses rounded dark buttons, a mint
Next action and a separate slide-count badge.

The example has no network dependencies. Typography uses Arial with the host's
sans-serif fallback; charts and diagrams are authored SVG. Each deck is a
`.slides` file — Mark data with a `<presentation>` root — that `lambda view`
presents through `lambda.slide.present`. A slide renders the first time it is
shown and stays mounted; navigation afterwards only changes which layer is
displayed and its animation state. [northstar.slides](northstar.slides) was
converted from the earlier Lambda deck script, whose layout, card and chart
helpers generated it; edit the Mark directly to adapt it. A script reads a deck
with `input("test/demo/slides/northstar.slides", 'slides')^`, and
`slide.snapshot`, `slide.slides` and `slide.handout` accept that value for static
views and notes.

| Slides | Content |
|---|---|
| 1–3 | Opening, strategic thesis, market context |
| 4–6 | Market opportunity, traction, customer journey |
| 7–9 | Product system, operating model, unit economics |
| 10–12 | Financial plan, roadmap, positioning |
| 13–15 | Go-to-market funnel, scorecard, risk matrix |
| 16–18 | Resource allocation, execution cadence, closing |

Two smaller examples remain available:

```bash
./lambda.exe view test/demo/slides/slide_presentation.slides
./lambda.exe view test/demo/slides/slide_package_content.slides
```

The first introduces basic builds and slide transitions. The second carries a
`lambda.chart` visualization and paragraph builds, rendered once when the deck
was converted. See the
[slide package reference](../../../doc/Lambda_Slide.md) for the authoring API.

The Northstar UI replay visits every slide, checks five staged reveals, pauses
charts to inspect intermediate pixels, and verifies nested group bounds:

```bash
./lambda.exe view test/demo/slides/northstar.slides --headless \
  --event-file test/ui/slide_northstar.json \
  --event-result temp/northstar-result.json
```
