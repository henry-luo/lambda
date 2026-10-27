# Greetings

`./lambda.exe demo greetings` is a greeting card studio in the spirit of Greeting Card Studio and Hallmark Card Studio. Pick a design for an occasion, change its words, art, photos and paper on every page of the folded card, preview it opening, then print a sheet that folds into the card.

## Using it

| Area | What it does |
| --- | --- |
| Pages | **Front**, **Inside** and **Back** in the top bar. Inside shows the spread: the inside left and inside right pages side by side |
| Designs | Thirty-two designs for ten occasions: birthday, thank you, holidays, love, wedding, new baby, congratulations, get well, sympathy and just because. Film, sound and open-book badges identify animation, music and pop-ups. **Pop-up cards** filters the gallery to the four layered designs. Each design brings its front, an inside spread with a verse and a signature, and a back. Starting from a design is one undo step |
| Text | Headings, subheadings, body text and signatures in sixteen fonts, and four original verses for each occasion. A verse replaces the selected text, or goes on the inside right page |
| Art | Thirty-two pieces of clip art drawn in Lambda (balloons, cake, rings, evergreen, teddy and more). Each is drawn in one ink plus its tints, so recolouring keeps its shading |
| Photos | Two sample photographs, or any image path, in a plain, rounded, circle, arch or snapshot frame. A photo click with a photo selected replaces it |
| Backdrop | Seventeen papers, eight patterns and six borders for the current page, each with its own colour; **Use this backdrop on every page** copies it to the others |
| Effects | Buttons beside the current page heading open **Animation**, **Music** or **Pop-up** in the right inspector. Coloured icons show existing effects; a plus badge adds one to a plain card. Choose from six animation styles, five original tunes and four pop-up scenes. **Still**, **No music** and **No pop-up** remove their respective effects. Changes support undo and redo. The **Effects** rail button also opens this inspector |
| Pop-up | A birthday cake, hearts, a secret garden or a winter scene on the inside left. Three paper planes rise from their hinges as the preview opens. The right inspector changes the design, its primary colour and Low / Classic / High depth. The editor shows the flat illustration beside the editable message on the right |
| Card | The card's name, its shape (portrait 5 × 7 in, landscape 7 × 5 in or square 6 × 6 in), its occasion, and a blank start. A new shape keeps every item's centre in place |
| Inspector | For the selected item: words, font, style, size, spacing, line height and colour for text; colour, size and mirror for art; frame and zoom for photos; rotation, opacity, stacking, copy and delete for all. Clicking a card effect button opens its settings here; selecting an item returns to its item settings |
| Preview | The folded card on a table, with its animation and softly looping music. **Open the card** turns the complete two-sided cover on its hinge, with moving shading, using the photo-album demo's presentation technique and 0.55-second timing. **Close the card** reverses the turn; clicking it during opening folds back from the current position. Pop-up layers rise with the opening, and **Half open** holds a partial fold. The entire spread fits the viewport throughout the turn. **Pause animation** and **Mute music** work independently without resetting the turn. **Turn it over** shows the back. Leaving preview or closing the window stops music and cancels the turn |
| Print & share | Page images (four PNGs at 300 dpi), print sheets (two PNGs to print back to back and fold in half), a print-ready two-page PDF the size of the unfolded card, or a `.mark` card file that **Open** reads back |

On the canvas, drag an item to move it; its centre snaps to the page's middle lines. The round corner handle resizes (text scales with its font; Shift keeps a photo's proportions), the square side handle of a text box changes its width, and the dot above rotates it (Shift for 15° steps). Every gesture is one undo step.

| Key | Action |
| --- | --- |
| ⌘Z / ⇧⌘Z / ⌘Y | Undo / redo |
| ⌘D | Duplicate the selected item |
| ⌘S | Save the card file |
| Delete, Backspace | Delete the selected item |
| Arrow keys | Nudge 1 unit (Shift: 10) |
| `[` `]` | Send backward / bring forward |
| Escape | Deselect, or close the preview or a dialog |

Files are written to `temp/greetings/` unless you choose another folder, and are named after the card.

Fourteen designs include animation and thirteen include music. The animated designs include
Birthday parade, Winter wishes, Heart song, Moonlight lullaby, Golden moment,
Blooming gratitude, Pocket sunshine and Snow day (a quiet animated card). Any
template can use any effect through the buttons above the card or **Effects**.
The right inspector changes the current card's animation or song while the
template gallery stays available on the left. Gallery thumbnails and the edit
canvas stay still; effects play in **Preview**. PNGs and PDFs are still print
exports; `.mark` files preserve effect choices for reopening in Greetings.
Older card files without effects open still and silent. Reduced-motion settings
disable card animation.

The four pop-up templates are **Birthday surprise**, **Hearts in bloom**,
**Secret garden** and **Winter wonderland**. Any existing card can acquire a
pop-up through the open-book button above the page. The illustration appears
on the inside left; the message on the inside right stays editable. Pop-ups
can be combined with any animation or song. Their paper layers fold down when
the preview closes and disappear when showing the back.

Pop-ups are digital illustrations with a layered opening preview. Page images,
print sheets and PDFs include the flat illustration; they do not provide
cutting patterns or assembly instructions for a physical pop-up mechanism.

## How it is built

| File | Role |
| --- | --- |
| `greetings.ls` | The studio's state, its commands, the canvas gestures and the file exports |
| `mod_templates.ls` | Formats, pages, occasions, verses and the thirty-two designs; `assemble` adds the inside spread and the back to a design's front |
| `mod_effects.ls`, `mod_music.ls` | Effect catalogs, legacy defaults, and document-owned native music playback and cleanup (D7.5.4) |
| `mod_popup.ls` | Pop-up scene catalogs, validated saved settings and a shared three-plane illustration for flat and live views |
| `mod_preview.ls` | Persistent two-sided leaf, frame-driven presentation transforms and shading, interrupted turns and cleanup |
| `_generate_music.py`, `assets/music/` | Reproducible original music-box miniatures; no downloaded music or runtime network requests |
| `mod_art.ls` | Clip art, papers, patterns, borders, fonts and colour arithmetic |
| `mod_render.ls` | Draws a page as HTML at any scale. The editor, the gallery thumbnails, the preview and every export share it |
| `mod_model.ls` | Card edits, reshaping and undo history, as pure functions |
| `mod_ui.ls` | The top bar, tool rail, panels, inspector, preview and dialogs |
| `card.css`, `greetings.css` | Card rendering (shared with the exports) and the studio chrome |

A card is plain Lambda data: `{title, template, occasion, format, effects: {animation, music}, popup: {design, color, depth}, pages: {front, left, right, back}}`, and each page is a paper, a pattern, a border and a list of text, art and photo items in card units (hundredths of an inch). The studio's `view` state holds the card, so every command re-renders it. A drag is the exception: pointer moves write only a presentation `transform` to the dragged element, which Radiant commits without layout, and the card changes once on release. Animation runs on separate wrappers so it preserves saved art rotations and mirrors. A stable audio owner outside the studio survives redraws; its frame callback checks for the end of a tune and replays the same decoded voice. It schedules frames only while music is playing.

The pop-up's normalized settings are optional for old card files. A single
descriptor table drives all three canvas buttons and inspector choice lists.
Each scene uses existing Lambda-drawn art on three planes: backdrop, main motif
and foreground banner. Flat views composite those planes at their saved page
position; live preview gives each a depth scaled to the card and rotates it
around its paper hinge as the book changes between closed, half-open and open.

The preview mounts in a stable host outside the studio's generated subtree.
Commands commit their state before a frame publishes changed preview controls;
turn frames update the existing paper surfaces instead of rerendering the card.
As in the photo-album demo, presentation transforms rotate the complete cover
and its back, with separate opacity-only shading planes that preserve retained
page rasters. The turn uses cubic ease-out over 550 milliseconds of document
time, so delayed frames do not prolong the animation. The same progress centres
the book and unfolds its pop-up planes. Viewport fitting samples the whole turn,
including perspective between resting poses, and the stage reserves space for
both leaves. Reversing starts at the displayed progress; music and artwork
controls keep those surfaces mounted.
Frame callbacks belong to this document and are cancelled on exit, following
**D5.4.4**'s "one event loop per context" ruling. Reduced-motion preferences
apply the selected open/closed pose immediately.

Fonts are macOS families (Snell Roundhand, Savoye, Bradley Hand, Didot and others) with portable fallbacks. The sample photographs are the Photo Studio demo's, under the Unsplash License; see [ATTRIBUTION.md](../photo/assets/ATTRIBUTION.md).

## Tests

```sh
./lambda.exe test/demo/greetings/tests/greetings_model.ls
./test/test_ui_automation_gtest.exe --suite greetings
```

The first is the golden test of the catalog, card edits, reshaping, history, colour arithmetic, saved effects, pop-up validation, legacy defaults and flat print rendering; it runs in `make test-lambda-baseline`. The UI suite replays `studio.json` (the editing workflow), `effects.json` (animated designs, music, pause, mute and saved effects), `inspector.json` (canvas controls and right-panel editing), `popup.json` (pop-up designs, spread geometry, colour/depth, reshaping and save/open), `page_turn.json` (pixel changes during opening/closing, stable resting frames, interrupted turns, mounted-node identity, pop-up/music synchronization and callback cancellation), and `page_turn_fit.json` (whole-card bounds during the turn, narrow and landscape viewports, completion timing and resizing mid-turn). It writes captures under `temp/greetings/`. Native audio checks need access to the operating system's audio services, including in headless mode.
