# Greetings

`./lambda.exe demo greetings` is a greeting card studio in the spirit of Greeting Card Studio and Hallmark Card Studio. Pick a design for an occasion, change its words, art, photos and paper on every page of the folded card, preview it opening, then print a sheet that folds into the card.

## Using it

| Area | What it does |
| --- | --- |
| Pages | **Front**, **Inside** and **Back** in the top bar. Inside shows the spread: the inside left and inside right pages side by side |
| Designs | Twenty designs for ten occasions: birthday, thank you, holidays, love, wedding, new baby, congratulations, get well, sympathy and just because. Each design brings its front, an inside spread with a verse and a signature, and a back. Starting from a design is one undo step |
| Text | Headings, subheadings, body text and signatures in sixteen fonts, and four original verses for each occasion. A verse replaces the selected text, or goes on the inside right page |
| Art | Thirty-two pieces of clip art drawn in Lambda (balloons, cake, rings, evergreen, teddy and more). Each is drawn in one ink plus its tints, so recolouring keeps its shading |
| Photos | Two sample photographs, or any image path, in a plain, rounded, circle, arch or snapshot frame. A photo click with a photo selected replaces it |
| Backdrop | Seventeen papers, eight patterns and six borders for the current page, each with its own colour; **Use this backdrop on every page** copies it to the others |
| Card | The card's name, its shape (portrait 5 × 7 in, landscape 7 × 5 in or square 6 × 6 in), its occasion, and a blank start. A new shape keeps every item's centre in place |
| Inspector | For the selected item: words, font, style, size, spacing, line height and colour for text; colour, size and mirror for art; frame and zoom for photos; rotation, opacity, stacking, copy and delete for all |
| Preview | The folded card on a table. **Open the card** swings the front open on its fold to show the inside; **Turn it over** shows the back |
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

## How it is built

| File | Role |
| --- | --- |
| `greetings.ls` | The studio's state, its commands, the canvas gestures and the file exports |
| `mod_templates.ls` | Formats, pages, occasions, verses and the twenty designs; `assemble` adds the inside spread and the back to a design's front |
| `mod_art.ls` | Clip art, papers, patterns, borders, fonts and colour arithmetic |
| `mod_render.ls` | Draws a page as HTML at any scale. The editor, the gallery thumbnails, the preview and every export share it |
| `mod_model.ls` | Card edits, reshaping and undo history, as pure functions |
| `mod_ui.ls` | The top bar, tool rail, panels, inspector, preview and dialogs |
| `card.css`, `greetings.css` | Card rendering (shared with the exports) and the studio chrome |

A card is plain Lambda data: `{title, template, occasion, format, pages: {front, left, right, back}}`, and each page is a paper, a pattern, a border and a list of text, art and photo items in card units (hundredths of an inch). The studio is one `view` template whose state holds the card, so every command re-renders it. A drag is the exception: pointer moves write only a presentation `transform` to the dragged element, which Radiant commits without layout, and the card changes once on release.

Fonts are macOS families (Snell Roundhand, Savoye, Bradley Hand, Didot and others) with portable fallbacks. The sample photographs are the Photo Studio demo's, under the Unsplash License; see [ATTRIBUTION.md](../photo/assets/ATTRIBUTION.md).

## Tests

```sh
./lambda.exe test/demo/greetings/tests/greetings_model.ls
./test/test_ui_automation_gtest.exe --suite greetings
```

The first is the golden test of the catalog, the card edits, reshaping, history and colour arithmetic; it runs in `make test-lambda-baseline`. The second replays `test/ui/greetings/studio.json`: designs, a verse on the inside spread, text edits with undo and redo, an art drag, the backdrop, the preview, and a card saved and opened again.
