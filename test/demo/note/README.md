# Notes

A native Lambda/Radiant demo inspired by Mac Notes, following the
[Finder](../finder/README.md) and [Word](../word/README.md) demos.

From the repository root:

```sh
make build
./lambda.exe demo note
# Or open the entry point directly:
./lambda.exe view test/demo/note/note.ls
```

The three-pane workspace includes folders, pinned notes, previews, full-note
search, alphabetical/date sorting, and a rich-text editor. Create notes and
folders, move notes between folders through the **…** menu, pin/unpin, and
delete/restore notes through **Recently Deleted**. Permanent deletion asks for
confirmation. Search applies to the selected folder; **All Notes** searches
every live note. Arrow keys navigate the focused note list; Enter opens its
editor. Escape clears search and dismisses a popover.

**Aa** offers titles, headings, body text, bold, italic, underline and bullets.
The checklist button converts the current paragraph/list to tasks; click a
task's circle to toggle it. Checkbox presses preserve the current text caret
or range; text clicks place the caret. Tables, native typing,
selection, clipboard and composition use the shared `lambda.editor` controller.
Repeated spaces remain visible; Enter creates paragraphs, including empty ones,
and Shift+Enter inserts line breaks within a paragraph.
Typing `- ` or `1. ` starts a list. Enter continues the list; Enter in an empty
item returns to body text.
Each note keeps its own undo/redo history while the notebook is open.

| Shortcut | Action |
| --- | --- |
| Cmd/Ctrl + N | New note |
| Cmd/Ctrl + F | Search notes |
| Cmd/Ctrl + B / I / U | Bold / italic / underline |
| Cmd/Ctrl + Shift + L | Checklist |
| Cmd/Ctrl + Enter | Toggle the current task |
| Cmd/Ctrl + Z / Shift + Z, Ctrl + Y | Undo / redo |
| Cmd/Ctrl + S / Shift + S | Save notebook / save as |
| Cmd/Ctrl + O | Open notebook |
| Delete / Backspace in note list | Move selected note to Recently Deleted |

Save/Open use local version-1 `.note.json` notebooks, defaulting to
`temp/note/Notebook.note.json`. Saving is explicit; the sample notebook is loaded
at startup. The JSON envelope contains folders, note metadata, and Mark-encoded
editor documents to preserve symbols, exact whitespace and formatting. Reads
parse data without executing scripts. Atomic writes, lossless round-trip checks,
overwrite prompts, external-change detection, and unsaved Open/Close Notebook guards
reuse the shared edit-session file lifecycle. Files are limited to 4 MB; other
save destinations need an existing parent directory. Undo history is session
state and is not written to disk.

Use **… → Close Notebook** for the save/discard prompt. The viewer host currently
honors native window-close guards only for `lambda edit` sessions, so closing
a demo with the OS window control does not prompt; save explicitly first.

Cloud sync, collaboration, attachments, locked notes, tags, scanning, drawing,
OS sharing, and timed trash expiry are outside this demo. Narrow windows hide
the folder sidebar and retain the list/editor panes.

The model follows **S1.4** and **S12.1.3**; native editing uses the shipped
Lambda policy boundary in **D7.2.5**, and the launcher follows **D7.2.4v3**.
No language/runtime ruling changes.

Verification:

```sh
make -C build/premake config=debug_native test_lambda_gtest
./test/test_lambda_gtest.exe --gtest_filter='*tests_note_*'
LAMBDA_EXEC_BACKEND=interp ./test/test_lambda_gtest.exe --gtest_filter='*tests_note_*'
LAMBDA_EXEC_BACKEND=jit ./test/test_lambda_gtest.exe --gtest_filter='*tests_note_*'
./test/test_ui_automation_gtest.exe --suite note --jobs 1
```

Model and notebook-format goldens live in `tests/`; registered native UI replays
live in `test/ui/note/`. Replays cover navigation, filtering, task hit areas, editing,
history, formatting, folders, trash and local persistence. The selection replay
uses mouse drags, Shift-arrow ranges and physical typing, checking that native
and DOM selections survive reactive redraws and formatting-menu changes
(**D7.2.5**). The checkbox replay checks that activation leaves the caret in
editable text, preserves ranges and supports subsequent typing and undo.
The whitespace replay uses physical typing to verify repeated spaces, consecutive
empty paragraphs and consecutive soft line breaks.
The list-caret replay checks bullet/numbered marker autoformat, Enter continuation
in inline and paragraph-wrapped items, empty-item exit, and checklist continuation.
Captures and results are written under `temp/note/`.
