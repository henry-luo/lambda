# Books

An ebook library and reader in the spirit of macOS Books, written in Lambda. The library shelves seven public-domain classics; a book opens in the reader of the [`lambda.doc.ebook`](../../../doc/Lambda_Packages.md#19-docebook--epub-reader) package, and the place you reach is waiting the next time you open it.

From the repository root:

```sh
./lambda.exe demo book
# Or open the entry point directly:
./lambda.exe view test/demo/book/book.ls
```

## Using it

| Area | What it does |
| --- | --- |
| Home | **Continue Reading** shows up to three books you have opened, most recent first, with how far you have read and the chapter you are in. **Up Next** lists the books you have not opened, and **Collections** opens a genre shelf |
| Library | **All Books**, **In Progress** and **Unread** shelves, with a count beside each. A shelf sorts by **Recent**, **Title** (past a leading "The") or **Author** |
| Collections | Fantasy, Science Fiction, Gothic, Mystery, Adventure and Children's Classics. A book can sit in more than one |
| Search | The field at the top of the sidebar matches titles, subtitles, authors and genres as you type. Cmd/Ctrl + F moves to it |
| Reading | A click on a cover or card opens the book. The reader has contents, history, bookmarks, whole-book search, text size, fonts, spacing, margins, three themes, and scrolling or paged layout. **Library**, or Escape, returns to the shelves |
| Pages | Three toolbar icons switch the layout (Scroll, Fit to window, or Two pages side by side); **Aa** has the same choices, the paper (Plain, Antique with its ruled frame and corner flourishes, Parchment, Linen, Newsprint, Ivory) and the page turn (None, Slide left, Slide up, Turn, Flip, Dissolve). Turns play in the two paged layouts, also across chapters |
| Flip | The page folds from its corner like paper: the leaf lifts along a fold, shades as it bends, shows the next page on its back and casts a shadow on the page it uncovers. Hovering a page corner curls it; dragging a corner turns the page under the pointer, and letting go past the spine finishes the turn while letting go before it lays the page back. The fold follows [StPageFlip](https://github.com/Nodlik/StPageFlip)'s soft pages |
| Search | The magnifier opens a search of the book. Three toggles beside the field match letter case, match whole words only, and keep the search to the current chapter; changing one re-runs the search. Book-wide results start with their chapter number ("Ch. 5", its title on hover) and highlight the match; a click opens the passage |
| Highlights | Drag across a passage and a bar of five colors opens above it; a color highlights the passage. A highlight that overlaps others absorbs them. Click a highlight to recolor or **Remove** it. The sidebar lists every highlight with its chapter; a click there opens it |

The covers are the publications' own cover images. Progress counts the chapters before your place by their packaged size and the current chapter by the text you have passed in it, so long and short chapters weigh what they should.

## Where your place is kept

The reader saves its position, bookmarks, highlights and settings through the ebook package's state store, keyed by each book's SHA-256, so `lambda view test/demo/book/books/dracula.epub` resumes at the same page. The demo adds one file beside them, `books-library.json`, which lists the books you have opened, most recent first. The directory is `$LAMBDA_EBOOK_STATE_DIR` when set, else `$XDG_STATE_HOME/lambda/ebook`, `%LOCALAPPDATA%/lambda/ebook`, or `~/.local/state/lambda/ebook`. Delete the files there to start over.

## The books

`books/` holds EPUB 3 editions with images from [Project Gutenberg](https://www.gutenberg.org/). The works are in the public domain in the United States; each file keeps Project Gutenberg's license text at the end of the book.

| Book | Author | First published | File | Gutenberg |
| --- | --- | --- | --- | --- |
| Alice's Adventures in Wonderland | Lewis Carroll | 1865 | `alice-in-wonderland.epub` | [#11](https://www.gutenberg.org/ebooks/11) |
| The Adventures of Sherlock Holmes | Arthur Conan Doyle | 1892 | `sherlock-holmes.epub` | [#1661](https://www.gutenberg.org/ebooks/1661) |
| Frankenstein; or, The Modern Prometheus | Mary Wollstonecraft Shelley | 1818 | `frankenstein.epub` | [#84](https://www.gutenberg.org/ebooks/84) |
| The Time Machine | H. G. Wells | 1895 | `the-time-machine.epub` | [#35](https://www.gutenberg.org/ebooks/35) |
| Moby Dick; Or, The Whale | Herman Melville | 1851 | `moby-dick.epub` | [#2701](https://www.gutenberg.org/ebooks/2701) |
| Peter Pan | J. M. Barrie | 1911 | `peter-pan.epub` | [#16](https://www.gutenberg.org/ebooks/16) |
| Dracula | Bram Stoker | 1897 | `dracula.epub` | [#345](https://www.gutenberg.org/ebooks/345) |

The catalog in `mod_library.ls` names the files and their collections; any other EPUB 2 or 3 book added there joins the shelves. A book that cannot be read stays on the shelf, disabled, with the reason.

## Files

| File | Role |
| --- | --- |
| `book.ls` | Entry point |
| `mod_app.ls` | The window's state and event handlers, and the document the demo and its UI fixture open |
| `mod_library.ls` | The catalog, publication loading, covers, progress, and the last-opened record |
| `mod_ui.ls` | Sidebar, Home shelves, cover grids and cards, and the reader screen |
| `book.css` | The library's styles; the reader brings `lambda.doc.ebook.reader.stylesheet` |

`test/ui/book/library.json` checks the shelves, collections, sorting, search, and opening and closing a book; `test/ui/book/highlight.json` drags selections in the chapter, then highlights, merges, recolors, removes and jumps to them; `test/ui/book/search.json` covers the search toggles and results; `test/ui/book/page_options.json` switches layouts and papers and turns pages within and across chapters. `test/ui/book/page_flip.json` plays timed flips in both layouts and across a chapter, curls a hovered corner, and drags corners to turn a page or lay it back. Their entry, `test/ui/book/_library.ls`, opens the same document without reading or saving any reading state, so the run does not depend on, or change, your own library.
