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
ascending or descending, with folders first. Hidden files can be shown in all three views.

Choose **Column view** in the toolbar or press **Cmd/Ctrl + 3** to browse a folder
branch side by side. A single click on a folder opens its contents in the next
column; click another folder in an earlier column to switch branches. Parent
columns keep their rows and vertical scroll positions. Each column scrolls
vertically, and the browser scrolls horizontally to reveal the current folder.
Left returns to the previous column and selects the folder you just left;
Right opens the selected folder. Search filters the current column, while sorting
and hidden-file settings apply across columns. Sidebar and Go to Folder destinations
outside the displayed branch start a new column root. Switching views keeps the
current folder and file selection.

Image tiles load thumbnails progressively in icon, list and column views. PNG, JPEG,
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

Right-click a file, folder or folder background to open the context menu. The
**File actions** toolbar button opens the same menu for the current selection.
Use **New File**, **New Folder**, **Rename**, **Move to…**, or **Delete…** there;
the folder-plus toolbar button also creates a folder. New files start empty.
Names must be single path components. Existing destinations are rejected,
including names that resolve to an existing item on case-insensitive filesystems.

Drag files or folders onto a folder row, a column background, a breadcrumb or
a sidebar destination to move them. Dragging a selected item moves the current
selection together; dragging an unselected item moves just that item. A drop
target highlights while dragging. **Move to…** provides the same operation
through a destination-path dialog. Escape cancels a drag before the drop.
Moves use atomic filesystem renames and
currently require source and destination to be on the same filesystem.
Moving an open ancestor updates the displayed branch and navigation paths.

Deletion requires confirmation and is permanent, including folder contents;
it does not use the OS Trash. Cancelling a dialog leaves the filesystem unchanged.
The whole selection is checked for missing sources and destination conflicts
before a batch starts. If a later filesystem operation fails, completed changes
remain, the browser refreshes and the error is reported. There is no batch undo
or coordination with other programs changing the same paths concurrently.
Search is local to one folder; recursive indexing and OS application launching
remain outside this demo's scope.

| Shortcut | Action |
| --- | --- |
| Enter / Cmd/Ctrl + Down | Open the selected folder or Quick Look a file |
| Space / Escape | Toggle / close Quick Look; Escape also clears selection |
| Arrow keys / Shift + arrows | Move / extend the selection |
| Home / End | Select the first / last visible item |
| Cmd/Ctrl + A | Select all visible items |
| Cmd/Ctrl + 1 / 2 | Icon / list view |
| Cmd/Ctrl + 3 | Column view |
| Left / Right in column view | Previous column / open selected folder |
| Cmd/Ctrl + F | Focus search |
| Cmd/Ctrl + Shift + G | Go to Folder |
| Cmd/Ctrl + Up / Alt + Up | Parent folder |
| Cmd/Ctrl + [ / ] or Alt + Left / Right | Back / Forward |
| Cmd/Ctrl + Shift + . | Toggle hidden files |
| Cmd/Ctrl + R | Refresh the current folder |
| Cmd/Ctrl + E | Rename the selected item |
| Cmd/Ctrl + Shift + N | New folder |
| Cmd/Ctrl + Alt + N | New empty file |
| Cmd/Ctrl + Shift + V | Move the selected items to a folder |
| Delete / Cmd/Ctrl + Backspace | Confirm permanent deletion |
| Cmd/Ctrl + Shift + M | Open the context menu |

Native text controls retain their editing shortcuts. Dialogs trap Tab navigation
and restore focus when closed. In compact windows the sidebar reduces to icons
and Quick Look provides the full preview.

`mod_model.ls` owns immutable session transitions (**S1.4**), path normalization,
ordering and selection. `mod_io.ls` captures directory metadata and preview text
at read time (**S12.4.1v3**), validates operation plans and performs filesystem
changes in procedures (**S12.1.1v3**). `mod_ui.ls` and `finder.css` present the interface;
`finder.ls` handles events and updates retained regions, keeping native controls
and row identities stable (**S12.1.3**, **D4.5.1v4**). Procedure errors are captured
in statement handlers (**S7.6.7v4**). No language or runtime ruling changes.
`finder.js` activates the session after the first layout and captures non-bubbling
scroll events from retained columns, relaying them to the Lambda viewport handler.
It also relays native drag-and-drop gestures. Navigation, operation validation,
filesystem effects and thumbnail scheduling remain in the Lambda session.

`mod_thumbnails.ls` measures the visible scrollport plus one surrounding row,
prioritizes Quick Look and the selected preview, and submits one raster decode
at a time through `radiant.image_job`'s thumbnail channel. Native jobs copy paths
and own their decoded buffers (**D4.5.2**); source reads use `lambda-io`
(**D7.1.2v2**). Canvas dimensions are derived from CSS bounds and device scale.
Offscreen canvases release their pixels, and retired regions are cleared before
replacement. Folder generations and native job tokens reject stale completions;
window closure cancels and joins workers.

Large listings use CSS size/layout containment on the flex-sized scrollport, so
ancestor sizing does not repeatedly measure every file. New markup includes
selection/focus attributes; later selection and dialog updates reuse the published
listing (**S12.1.3**). Thumbnail polling reuses measured slot geometry until
publication, scrolling or resizing invalidates it. The full folder remains
searchable and accessible through keyboard navigation.

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
python3 test/demo/finder/tests/run_operations.py
LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 python3 test/demo/finder/tests/run_operations.py
```

The model, column-navigation, action-validation and I/O golden scripts and their expected `.txt` files are in `tests/`. The
registered native UI fixtures are in `test/ui/finder/`. They cover the original
navigation, context menus, operation dialogs and compact layouts, retained column branches and keyboard
navigation, eight raster formats, view-switch cache reuse and tile geometry. The thumbnail-job fixture runs under forced collection
and poisoning, and covers file replacement, invalidation, errors, cancellation
and closure. The Python replay creates a real scrolling folder under `temp/`
and checks offscreen release, Unicode/fragment-like filenames, EXIF rotation and
refresh. Captures are written under `temp/finder/`.

The operation replay creates a unique disposable directory under
`temp/finder/operations/`, then uses real native mouse drags and keyboard input
to create, rename, move and delete files and nested folders in all three views.
It checks collisions, invalid destinations, cancellation, Unicode names, moves
of open ancestors and multi-selection. Filesystem checks independently verify
the final directory tree and preserved file contents. `--log` retains native
diagnostics beside the replay. Bundled demo files are never mutated by these tests.
The native drag transport retains its target and event across callback allocation
(**D5.3.3**); the forced-GC drag fixture checks cancellation and drop delivery
without changing bundled files.

`large_directory.json` opens `./test`, checks that navigation avoids per-row
attribute writes, and exercises retained selection, sorting, filtering,
Home/End scrolling and resizing. Column replays also check single-click opening,
ancestor selection, history, filtering and horizontal scrolling in compact windows.
The thumbnail UI also runs under forced GC and freed-memory poisoning.

Measure directory navigation through the next animation-frame callback with a
release host, optionally comparing the previous implementation:

```sh
make build-release-compile
python3 test/demo/finder/benchmark.py --reference 476e9075f --repetitions 3
```

The benchmark rejects debug hosts, alternates reference/current runs, and writes
its generated scripts, logs and JSON results under `temp/finder/performance/benchmark/`.
On macOS with 463 visible entries in `./test`, median opening latency fell from
2,944 ms to 558 ms (5.3×); selection fell from 1,579 ms to 722 ms. These are three
release runs per version on this checkout, including script and layout work;
viewer startup is excluded and timings vary with host load.
