# Slides editor and presentation demos

Run these commands from the repository root.

```bash
./lambda.exe demo slides
```

The demo opens **Slides**, a desktop editor with a ribbon, slide thumbnails,
canvas, format panel and speaker notes. Its entry is [slides.ls](slides.ls).
Start with the editable two-slide presentation, choose **New**, or open one of
the decks in **Samples**. Northstar is a sample rather than the application entry.

## Create and edit

- Select objects on the canvas or in the Format selection list, including
  objects inside groups. Drag to move; drag the corner handle to resize.
  The inspector edits position, size, rotation, opacity, colors and typography.
- Double-click plain text to edit it on the slide. Click outside or press
  Ctrl/Cmd+Enter to finish; Escape cancels. The Text field also edits plain text.
  Rich text and boxed HTML/SVG retain their structure and can be edited as Mark
  in **Content (Mark)**.
- **Insert** adds text boxes, rectangles, ellipses, lines and pictures. Set a
  picture's path or URL in Format. **Home** provides copy/paste, duplication,
  object deletion, text alignment, bold/italic, centering and stacking order.
- Add, duplicate, delete and reorder slides with Home and the thumbnail pane's
  Move up/down buttons. Every deck keeps at least one slide.
- **Design** selects a theme or layout and toggles the grid. Authored object
  colors and coordinates retain their overrides. **Animations** adds entrance
  effects on entry or click, clears a slide's builds, and edits its transition.
- Speaker notes are editable below the canvas. Undo/redo includes text changes,
  gestures, slide operations, notes, formatting and opening another deck.

Keyboard shortcuts: Ctrl/Cmd+Z to undo, Shift+Ctrl/Cmd+Z or Ctrl/Cmd+Y to redo,
Ctrl/Cmd+C/V to copy/paste an object, Ctrl/Cmd+D to duplicate,
Ctrl/Cmd+M to add a slide, and Delete/Backspace to remove a selected object.
Arrow keys nudge an object; Shift increases the step to ten logical units.
Native text fields keep their own editing shortcuts.

**Save** writes an editable `.slides` file; **Open** reads that same format.
The file dialog starts under `temp/slides/` and accepts another local path.
Images authored with `<image>` resolve against the loaded deck's folder; Save
records that resource base so Save As keeps those images working. **Export**
writes an HTML handout containing final builds and speaker notes. Save explicitly
before closing; edits are held in memory until saved. PPTX import/export,
multi-object selection, master editing and an animation timeline are not implemented.
Editing an object on a master-based slide detaches that slide's composed content
from its master; other slides and master definitions remain intact.

## Present

Choose **Present** or F5 to start at slide one; Shift+F5 starts at the selected
slide. The presentation uses the current edited deck, including unsaved changes.
The existing player supplies its builds, transitions, autoplay, speed, notes,
overview, pointer and full-screen controls. **Back to editor**, or Escape after
player overlays have been cleared, returns to authoring.

## Sample decks

**Northstar / Strategy 2027** is an 18-slide fictional leadership briefing with
a consistent navy, teal and warm-white palette, generous spacing, numbered
sections and speaker notes. It includes KPI cards, a market-size donut, revenue
charts, a customer journey, a product architecture, an operating flywheel, a
quarterly roadmap, comparison and scorecard tables, a funnel, a risk matrix and
an investment allocation. All business data is illustrative.

When opened directly as a `.slides` file, the opening plays automatically.
**Play** runs the whole deck, with a three-second
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
helpers generated it; open it from **Samples** to adapt it in the editor, or edit
the Mark directly. A script reads a deck
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

The presenter and authoring example demonstrates a shared master, a named layout
and per-slide themes:

```bash
./lambda.exe view test/demo/slides/presenter_tools.slides
```

Press **P** for notes, the next-slide preview and an independent timer in a docked
console. **O** opens the title overview; the slide picker accepts a number or ID.
**H**, or the **⌄** arrow at the toolbar's bottom-right, hides the toolbar; **H** or moving
the mouse into the bottom-right corner brings it back. **B** blacks out the canvas, and **L** enables the
laser pointer, and **F** (or the **Full screen** button) toggles full screen, which hides the
toolbar until you reach for the bottom-right corner. **Escape**
clears blackout, pointer and overview first; once none is active it leaves full screen,
or closes the viewer. These tools also
work on the other decks. The console shares the viewer with the audience canvas.

## Verification

The editor has two baseline UI replays and three discovered script goldens under
`test/lambda/slide/test_slide_editor*`. Run the UI replays with:

```bash
./test/test_ui_automation_gtest.exe --suite baseline --test 'slide_editor*'
```

The Northstar UI replay checks the opening slides, a staged reveal, chart pixels
and nested group bounds:

```bash
./lambda.exe view test/demo/slides/northstar.slides --headless \
  --event-file test/ui/slide_northstar.json \
  --event-result temp/northstar-result.json
```
