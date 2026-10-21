# Reader View

A small browser window, written in Lambda, that shows what [`lambda.doc.readability`](../../../doc/Lambda_Packages.md#21-docreadability--article-extraction) does. It opens three cluttered article pages and a newspaper front page. The page icon at the right of the address bar swaps the current tab for the clean article the package extracts, and swaps it back.

From the repository root:

```sh
./lambda.exe demo readability
# Or open the entry point directly:
./lambda.exe view test/demo/readability/readability.ls
```

## Using it

Each tab shows its page as the site serves it, in a frame. Click the reader icon (a page with lines of text) to enter Reader View; it turns blue, and the address gains Firefox's `about:reader?url=` prefix. Click it again, press **Escape**, or use **View original page** at the end of the article to return. Reader View belongs to the tab: each tab remembers its own mode while you switch between them.

Reader View shows the site name, title, byline, publication date and an estimated reading time above the article. The rail on the left changes the text size (**A−** / **A+**, five steps) and the theme (light, sepia or dark).

The icon is greyed out on **The Meridian Ledger** front page. Like Firefox, the demo offers Reader View only for pages that pass the package's `readerable` check (Readability.js `isProbablyReaderable`), and a page of headlines and short teasers does not.

| Shortcut | Action |
| --- | --- |
| Cmd/Ctrl + Shift + R | Enter or leave Reader View |
| Escape | Leave Reader View |
| Cmd/Ctrl + 1–4 | Switch to that tab |

## The pages

The sites and articles are fictional and were written for this demo; their addresses use the reserved `.example` domain. Each page carries the clutter its kind of site really has, and the extracted article keeps none of it.

| Tab | Page | Clutter removed | Kept |
| --- | --- | --- | --- |
| Shade by design | News article, *The Meridian Ledger* | Ticker, section menu, banner and box ads, share buttons, inline related links, most-read sidebar, newsletter form, comments, footer, cookie banner | Standfirst, sections, pull quote, chart with caption |
| Overnight focaccia | Recipe blog, *Crumb & Hearth* | Promo bar, breadcrumbs, jump/pin/print buttons, in-text ads, rating box, related recipes, reviews, author sidebar, newsletter pop-up | Story, photo, ingredients list, numbered method, notes |
| How reader mode works | Engineering blog, *Fieldnotes* | Navigation, banner, on-this-page contents, tags, author card, subscribe box, previous/next links, discussion | Prose, code block, table, diagram |
| The Meridian Ledger | Newspaper front page | — | Not offered: the page fails `readerable` |

Metadata comes from where real sites put it: `<meta>` author and `article:published_time` tags on the Meridian article and the Fieldnotes post, and a schema.org `BlogPosting` JSON-LD block on the recipe blog. For these pages the package's article HTML is byte-for-byte identical to the output of Readability.js 0.6.0 run through jsdom.

## How it works

The entry script loads every page once at start-up. For each page it runs `readability.readerable(doc)` and, when that passes, `readability.parse(doc, {url})`. The page's own file URL is passed as `url`, so relative image sources in the article resolve to the local files. The view then only re-renders state: the active tab, the tabs in Reader View, the theme and the text size. The model returns new state values (**S1.4**); the view builds its document from them, and the event handlers assign them (**S12.1.3**). The view renders `<body>` itself so that keyboard shortcuts still arrive after focus falls back to the body.

| File | Purpose |
| --- | --- |
| `readability.ls` | Entry point: loads the pages, the reactive view and its click and key handlers |
| `mod_model.ls` | Page catalog, loading through the package, per-tab reader state, settings, shortcuts, date and reading-time labels |
| `mod_ui.ls` | Tab strip, address bar and reader toggle, the original-page frame, and the reader view |
| `readability.css` | Browser chrome and the reader themes |
| `articles/` | The four sample pages and their SVG illustrations |
| `tests/` | Model golden and the native replay |

## Focused checks

The model golden covers loading, reader availability, the extracted titles, bylines, sites and dates, per-tab modes, settings bounds and shortcuts. It runs in `test_lambda_gtest` (`tests_readability_model`):

```sh
./test/test_lambda_gtest.exe --gtest_filter='*tests_readability_model*'
```

The native replay drives the window: it toggles Reader View on three tabs, checks what the article keeps and drops, changes theme and text size, uses the shortcuts and **View original page**, and checks that the front page cannot enter Reader View. Captures go to `temp/readability/`:

```sh
./lambda.exe demo readability --headless \
  --event-file test/demo/readability/tests/reader.json \
  --event-result temp/readability/reader-result.json
```

`RadiantViewTest.DemoCommandLaunchesCatalogAndPreservesViewerOptions` also launches the demo from the `lambda demo` catalog.
