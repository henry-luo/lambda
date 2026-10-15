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

Verification:

```sh
make -C build/premake config=debug_native test_lambda_gtest
./test/test_lambda_gtest.exe --gtest_filter='*tests_finder_*'
LAMBDA_EXEC_BACKEND=interp ./test/test_lambda_gtest.exe --gtest_filter='*tests_finder_*'
LAMBDA_EXEC_BACKEND=jit ./test/test_lambda_gtest.exe --gtest_filter='*tests_finder_*'
./test/test_ui_automation_gtest.exe --suite finder --jobs 1
```

The two golden scripts and their expected `.txt` files are in `tests/`. The
registered native UI fixtures are in `test/ui/finder/`: `smoke.json`,
`navigation.json` and `compact.json`. Captures are written under `temp/finder/`.
