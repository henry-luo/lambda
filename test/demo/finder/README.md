# Finder

A native Lambda/Radiant file browser inspired by macOS Finder.

From the repository root:

```sh
make build
./lambda.exe demo finder
# Or open the entry point directly:
./lambda.exe view test/demo/finder/finder.ls
```

The demo starts in its bundled `files/` directory. Sidebar shortcuts open real
Home, Desktop, Documents, Downloads, Pictures, project and root folders.
Missing or inaccessible folders report an error while keeping the current folder.

Double-click a folder to open it. Back, Forward and the breadcrumb bar navigate
between folders. **Go to Folder** accepts an absolute POSIX path, a path relative
to the current folder, or `~` / `~/…` for home. Search filters names in the current
folder, ignoring case. Sorting supports name, kind, size and modification time,
ascending or descending, with folders first. Hidden files can be shown in either view.

Image tiles load thumbnails progressively in icon and list views. PNG, JPEG,
GIF, WebP, AVIF, BMP, TIFF (`.tif` / `.tiff`) and ICO use the shared image decoder;
SVG stays on the vector rendering path. Thumbnails preserve aspect ratio and
normalize orientation. GIFs use the first display frame and TIFFs the first page.
Transparent images use a checkerboard. Corrupt or unsupported images retain
their file icon; hover the graphic for the decoder diagnostic.

Click an item to select it; Cmd/Ctrl-click toggles it and Shift-click selects a
range. The preview pane shows metadata, images and text. Space opens Quick Look,
including when the preview pane is hidden or the window is narrow. Text previews
admit files up to 128 KiB and show at most 6,000 characters. Other types show their
metadata. Files are read as explicit text or image resources; scripts are not executed.
Browsing and previews never write, rename or delete files. Search is local to one
folder; recursive indexing, file operations and OS application launching are outside
this demo's scope.

| Shortcut | Action |
| --- | --- |
| Enter / Cmd/Ctrl + Down | Open the selected folder or Quick Look a file |
| Space / Escape | Toggle / close Quick Look; Escape also clears selection |
| Arrow keys / Shift + arrows | Move / extend the selection |
| Home / End | Select the first / last visible item |
| Cmd/Ctrl + A | Select all visible items |
| Cmd/Ctrl + 1 / 2 | Icon / list view |
| Cmd/Ctrl + F | Focus search |
| Cmd/Ctrl + Shift + G | Go to Folder |
| Cmd/Ctrl + Up / Alt + Up | Parent folder |
| Cmd/Ctrl + [ / ] or Alt + Left / Right | Back / Forward |
| Cmd/Ctrl + Shift + . | Toggle hidden files |
| Cmd/Ctrl + R | Refresh the current folder |

Native text controls retain their editing shortcuts. Dialogs trap Tab navigation
and restore focus when closed. In compact windows the sidebar reduces to icons
and Quick Look provides the full preview.

`mod_model.ls` owns immutable session transitions (**S1.4**), path normalization,
ordering and selection. `mod_io.ls` captures directory metadata and preview text
at read time (**S12.4.1v2**). `mod_ui.ls` and `finder.css` present the interface;
`finder.ls` handles events and updates retained regions, keeping native controls
and row identities stable (**S12.1.3**, **D4.5.1v4**). Procedure errors are captured
in statement handlers (**S7.6.7v4**). No language or runtime ruling changes.

`mod_thumbnails.ls` measures the visible scrollport plus one surrounding row,
prioritizes Quick Look and the selected preview, and submits one raster decode
at a time through `radiant.image_job`'s thumbnail channel. Native jobs copy paths
and own their decoded buffers (**D4.5.2**); source reads use `lambda-io`
(**D7.1.2v2**). Canvas dimensions are derived from CSS bounds and device scale.
Offscreen canvases release their pixels, and retired regions are cleared before
replacement. Folder generations and native job tokens reject stale completions;
window closure cancels and joins workers.

The document owns a 128-entry LRU cache, including negative results. Its
32 MiB retained-pixel budget reserves the app's canvas pixels and the pending
upload before admitting cached pixels.
Keys include canonical path, file size, modification time with subsecond precision,
file identity and physical output bounds. The decoder rechecks the revision after
reading. Refresh cancels work and invalidates the cache, including files whose
timestamps were deliberately preserved. Source admission and scratch limits remain
those of the shared image library; a small thumbnail does not bypass them.

Verification:

```sh
make -C build/premake config=debug_native test_lambda_gtest
./test/test_lambda_gtest.exe --gtest_filter='*tests_finder_*'
LAMBDA_EXEC_BACKEND=interp ./test/test_lambda_gtest.exe --gtest_filter='*tests_finder_*'
LAMBDA_EXEC_BACKEND=jit ./test/test_lambda_gtest.exe --gtest_filter='*tests_finder_*'
./test/test_ui_automation_gtest.exe --suite finder --jobs 1
python3 test/demo/finder/tests/run_thumbnails.py
```

The two golden scripts and their expected `.txt` files are in `tests/`. The
registered native UI fixtures are in `test/ui/finder/`. They cover the original
navigation, dialogs and compact layouts, eight raster formats, list/grid cache
reuse and tile geometry. The thumbnail-job fixture runs under forced collection
and poisoning, and covers file replacement, invalidation, errors, cancellation
and closure. The Python replay creates a real scrolling folder under `temp/`
and checks offscreen release, Unicode/fragment-like filenames, EXIF rotation and
refresh. Captures are written under `temp/finder/`.
