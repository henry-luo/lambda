# Slides editor and presentation demos

Run these commands from the repository root.

```bash
./lambda.exe demo slides
```

The demo opens **Slides**, a desktop editor with a ribbon, slide thumbnails,
canvas, format panel and speaker notes. Its entry is [slides.ls](slides.ls).
It opens **Northstar / Strategy 2027** by default. Choose **New** for an editable
two-slide presentation, or choose another deck in **Samples**.

Drag the right edge of the **Slides** panel to resize it. Thumbnails scale with
the panel and the editing canvas refits automatically. With the divider focused,
use Left/Right to adjust its width or Home/End for its limits; Escape cancels a
drag. The width stays a view preference while editing, independently of undo or
opening another deck, and clamps to the space available when the window changes.

## Create and edit

The **Home** ribbon groups its commands like a desktop presentation editor:

| Group | Commands |
|---|---|
| Undo | Undo/Redo above Save/Print, using Word's shared two-row icon group. Print saves one final slide per PDF page; Ctrl/Cmd+P opens the print dialog. |
| Clipboard | Cut, Copy, Paste with source formatting, Paste Text Only, and Format Painter. Click a matching text/shape object to apply the captured style; Escape cancels. |
| Slides | New Slide; Layout; Reset placeholder bounds and typography; duplicate/delete from Slide actions. |
| Font | Installed font families, custom font size, grow/shrink, bold, italic, underline, strike, subscript/superscript, case, character spacing, highlight, text color and clear formatting. |
| Paragraph | Bullets/numbering and style libraries, indent/outdent, four horizontal alignments, line spacing and vertical text alignment. |
| Drawing | Picture, Shapes and Text Box insertion; stacking order, six object alignments, rotation, selection list, shared quick styles, fill, outline and stroke weight. |

Home formatting applies to the selected text box or shape. Disabled controls
follow the selected object kind; pressed buttons show the current formatting,
and color commands preview the current color. Menus support arrow keys and
Escape. The native font list keeps an imported deck's uninstalled family as an
option. Lists remain editable as text, including when vertically aligned.
These edits participate in undo/redo and the existing `.slides` save/present path.

- Select objects on the canvas or in the Format selection list, including
  objects inside groups. Drag to move; drag the corner handle to resize.
  Standalone SVG graphics resize with their box during the drag, keeping their
  `viewBox` and aspect-ratio settings. Thumbnails and presentation use the same sizing.
  The inspector edits position, size, rotation, opacity, colors and typography.
- Double-click text, click an already selected text box again, or press F2/Enter
  with the canvas focused to edit in place. This also edits rich text, HTML cards
  and table cells while keeping their styles. Pointer entry places the caret at
  the clicked text, including empty space beside a text row; F2/Enter starts at
  the end. Finishing an unchanged edit or canceling restores the original card.
  Click outside, press Tab or Ctrl/Cmd+Enter to finish; Escape cancels.
  Save/Open/Print shortcuts finish the current edit first. Completed edits
  participate in Undo/Redo. The inspector also exposes text and **Content (Mark)**
  for source editing.
- Right-click a thumbnail, or press Shift+F10 with it focused, for New Slide,
  Duplicate Slide, Delete Slide, Move Up/Down and Present from Here. Unavailable
  actions are disabled. Escape closes the menu and returns focus to the slide.
- Drag thumbnails up or down to reorder slides. The insertion line marks the
  drop position; hold near the panel's top or bottom to scroll. Escape or dropping
  outside the panel cancels. Each move is one undo step, and the moved slide stays
  selected. Home and the thumbnail pane's Move up/down buttons also change the
  order. Every deck keeps at least one slide.
- **Design** selects a theme or layout and toggles the grid. Authored object
  colors and coordinates retain their overrides. **Transitions** controls how
  slides change; **Animations** controls effects on objects.
- Speaker notes are editable below the canvas. Undo/redo includes text changes,
  gestures, slide operations, notes, formatting and opening another deck.

The **Insert** ribbon supplies these editing tools:

| Group | Functions |
|---|---|
| Slides / Tables | New Slide; styled tables with row/column counts, header rows and tab-separated cell data. |
| Images | Pictures from a file or URL; an existing screenshot or a rendered capture of the current slide. |
| Illustrations | Native basic shapes plus Word's shape gallery, SmartArt layouts and editable chart data. |
| Links | Slide Zoom with a rendered thumbnail, external links, internal slide links and navigation action buttons. |
| Comments | Add, edit and delete slide comments independently of speaker notes. Comments stay off the audience canvas. |
| Text | Text boxes, header/footer fields, WordArt, date/time text, slide numbers and embedded HTML/SVG/Mark objects. |
| Symbols | LaTeX equations with examples and a shared symbol palette. |
| Media | Video and audio from a file or URL, with native controls and optional looping. Leaving a slide pauses its media. |

Insert dialogs validate input before changing the deck. Select an inserted object
and choose **Edit object…** to revise its retained settings. Double-click a table
to edit its cells directly; use **Edit object…** for its dimensions and style.
Other inserted graphics open their settings on double-click.
Escape cancels; Enter submits a single-line field; Ctrl/Cmd+Enter also submits
from a multi-line field. Tab stays inside the dialog. Edits keep object placement
and participate in undo/redo. Home's shape paints also update inserted SVG shapes.
Header/footer and numbering can apply to the current slide or all existing slides;
numbering follows slide reordering. Date/time is inserted as editable text.
Links, actions and media activate during presentation; canvas clicks select them.
Screenshot captures use the slide renderer or an image file; operating-system
screen capture and OLE attachments are not implemented.

Keyboard shortcuts: Ctrl/Cmd+Z to undo, Shift+Ctrl/Cmd+Z or Ctrl/Cmd+Y to redo,
Ctrl/Cmd+X/C/V to cut/copy/paste an object, Ctrl/Cmd+D to duplicate,
Ctrl/Cmd+M to add a slide, and Delete/Backspace to remove a selected object.
In the Slides navigation panel, Delete/Backspace removes the focused slide and
keeps the next thumbnail focused. Undo restores it; the last slide is retained.
Arrow keys nudge an object; Shift increases the step to ten logical units.
Native text fields keep their own editing shortcuts.

**Save** writes an editable `.slides` file; **Open** reads that same format.
The file dialog starts under `temp/slides/` and accepts another local path.
Images authored with `<image>` resolve against the loaded deck's folder; Save
records that resource base so Save As keeps those images working. **Export**
writes an HTML handout containing final builds and speaker notes. Save explicitly
before closing; edits are held in memory until saved. PPTX import/export,
multi-object selection, master editing and a graphical animation time ruler are not implemented.
Editing an object on a master-based slide detaches that slide's composed content
from its master; other slides and master definitions remain intact.

## Animations

Select an object and choose an entrance or emphasis effect. **More** opens each
family's complete gallery; **Exit Effects** and **Path Animation** supply exits
and motion. A gallery choice replaces the selected animation. **Add Animation**
appends an effect, allowing one object to have several builds.

**Animation Pane** lists all effects on the slide. Select a row to select its
object, edit its options, remove it, or move its cue Earlier/Later. Imported
parallel and sequence groups retain their structure. Start, Trigger and order
apply to the containing cue group; duration, delay, repeat, easing and effect
options apply to the individual effect. Entry groups precede click groups.

**Start** offers On Click, With Previous, After Previous and On Slide Entry.
The two automatic choices share the preceding build's navigation step; they
require a preceding build. Duration and delay are seconds in the editor and
milliseconds in the saved source. Effect Options exposes direction, scale,
angle, distance, color or opacity as appropriate. Motion paths include lines,
an arc and a loop, with optional orientation along the path. Existing custom
paths and easing curves stay intact until replaced.

**Trigger** assigns the selected cue to a click on an explicitly named object.
Triggered cues run independently of ordinary Next/Previous builds. Repeated
clicks replay that trigger; pause, resume, speed and Stop affect it. Triggers
writing the same channel overlay in firing order. Navigation and seeking reset
them. A triggered build suspends automatic advancement while it runs.

**Animation Painter** captures the selected object's animations. Click another
object to append copies with fresh effect IDs; Escape cancels. Copies retain
effect settings and grouping, and join ordinary playback without the source's
trigger binding. Undo restores the whole operation. **Preview** plays the
selected cue in the live player; deselect objects to preview every build. It
returns to editing without changing the deck or undo history. **Clear slide
animations** removes all regular and triggered builds.

Fill Color animates native shape fill or text color. Line Color requires a
native shape with a visible outline. These choices are disabled for embedded
HTML/SVG graphics; movement, opacity, clipping, growth and rotation work on
those objects. The package catalog and sampler supply the gallery and playback;
shared Word ribbon controls provide the chrome (D7.2.5, S12.1.1v3/S12.1.3).

## Transitions

The **Transitions** tab provides None, Fade, Push, Wipe, Split, Reveal, Cut,
Random Bars, Shape, Uncover, Cover and Morph. Select a tile to apply it to the
current slide. **Effect Options** selects direction, inward/outward Split,
circle/diamond/box Shape, or through-black Fade/Cut. None and normal Cut switch
immediately; their duration field is disabled. Other durations are in seconds.
Morph uses the existing authored `morph_id` matching rules.

**Preview** plays the selected transition from the preceding slide (or a blank
slide for the first slide), then returns to editing. It leaves the deck and undo
history unchanged. Escape or **Back to editor** cancels it. **Apply To All** copies
the current transition, effect options, duration, sound and advancement settings
to every slide in one undo step, preserving objects, notes, masters and builds.

**On Mouse Click** controls stage clicks; explicit controls and keyboard navigation
remain available. **After** sets a delay in seconds after the final build completes.
It advances without Play and supports pause/resume and playback speed changes.
The last slide stays on screen. Click builds still require manual navigation or Play.

**Sound** offers No Sound, Chime, Click, Whoosh and Other Audio. The three original
PCM cues are embedded in saved decks; their generator lives under
`assets/sounds/`. Other Audio exposes a file/URL field in Format, including when
an object is selected. A cue starts with slide entry, pauses with playback and
closes on navigation or leaving the show. Missing files, unsupported media and
unavailable audio output are reported in the player's status.

The native audio replay requires working audio output:

```bash
./lambda.exe view test/demo/slides/slides.ls --event-file test/demo/slides/tests/transitions_audio.json
```

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
reduced-motion playback shows its final state immediately. The shared player toolbar
uses one row of icon controls, a mint Next action and a slide-count badge. Previous,
Play, Pause, Resume and Restart keep their keyboard and click behavior. Hover an
icon for its name and shortcut; every icon has an accessible label. Narrow windows
scroll the controls horizontally while the hide arrow stays visible on the right.

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

Editor and player goldens live under `test/lambda/slide/`, including
`test_slide_animations.ls`. Run all slide UI replays or the focused animation
checks with:

```bash
./test/test_ui_automation_gtest.exe --suite baseline --test 'slide_*'
./test/test_ui_automation_gtest.exe --suite baseline --test 'slide_animation*'
LAMBDA_EXEC_BACKEND=jit ./test/test_ui_automation_gtest.exe --suite baseline --test 'slide_animation*'
```

The Northstar UI replay checks the opening slides, a staged reveal, chart pixels
and nested group bounds:

```bash
./lambda.exe view test/demo/slides/northstar.slides --headless \
  --event-file test/ui/slide_northstar.json \
  --event-result temp/northstar-result.json
```
