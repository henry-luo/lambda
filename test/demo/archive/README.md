# Archive

`./lambda.exe demo archive` opens a personal web archive in the spirit of
ArchiveBox, built on the [`lambda.archive`](../../../doc/Lambda_Packages.md)
package ([design](../../../vibe/Lambda_Pkg_Archive.md), §9). Paste a URL or a
local path and press **Archive**: a job row shows the stage and the bytes or
steps done while the window keeps painting, and the snapshot opens in the
viewer when it is filed. Everything runs in Lambda — downloads, media and
gallery engines, Readability, Markdown, ZIP, PDF and thumbnail rendering —
with no external tool.

| Area | What it does |
| --- | --- |
| Add bar | URL or path, a forced kind (Auto, Page, Video, Audio, Gallery, File), the formats to produce (HTML, Reader, Markdown, PDF) |
| Search | full-text search over every snapshot's `text.txt`, best first with snippets; a title or description match ranks first |
| Library rail | filter by kind and by site, with counts |
| List | newest first: thumbnail, title, site, kind, date, the formats present |
| Jobs | each queued or running job with its progress; **Cancel** stops it (its transfers and worker process too); a failure stays with its message |
| Viewer | **Snapshot** and **Reader** pages from the snapshot, the **Markdown** source, **Info** with every output's status and a **Re-run** for any that is not ok; **Re-run all** and **Delete** (with confirmation) below |

The library lives in `temp/archive/`; set `ARCHIVE_DEMO_ROOT` to use another
folder. A first run with an empty library archives five local sample pages:
three of the Reader View demo's articles, a page with a video and its subtitle
track, and a photo gallery (the archive tests' fixture site). The configuration
is `archive.yaml`: two jobs at a time, thumbnails on, and rules that classify a
few hosts.

## How it is wired

The view's `on load` handler opens the library and starts `archive.service`
with `{owner: 'document'}` (S13.3.1v2), so the service outlives the handler
and is cancelled, with its jobs, when the window closes. Handlers reach it
through the handle kept in the view's state. The service reports each job's
messages through a callback that calls `dom.dispatch` on the app element; they
arrive as `on job(evt)`, and `mod_model.ls` folds them into the job table and
the listing. Page renderings and PDFs are made by a child `lambda run` worker,
so the window never waits on Readability or layout.

| File | Role |
| --- | --- |
| `archive.ls` | the document: view, handlers, service wiring, the viewer's unpacked cache |
| `mod_model.ls` | state, actions, the job table, requests, labels (pure) |
| `mod_ui.ls` | layout |
| `archive.css`, `archive.yaml` | style and configuration |
| `tests/` | model golden and the native replay |

## Tests

The model golden covers planning under the demo's configuration, the pure
derivations over a fixture capture, the listing's filters and counts, requests,
the job table and labels. It runs in `test_lambda_gtest` (`tests_archive_model`):

```sh
./test/test_lambda_gtest.exe --gtest_filter='*tests_archive_model*'
```

The native replay archives the sample library, archives a page, searches for a
phrase from a video's transcript, switches tabs, re-runs a snapshot's outputs,
deletes one and closes the window (about 12 s). Captures go to `temp/archive_demo/`:

```sh
rm -rf temp/archive_demo/lib && ARCHIVE_DEMO_ROOT=temp/archive_demo/lib \
  ./lambda.exe demo archive --headless \
  --event-file test/demo/archive/tests/archive.json \
  --event-result temp/archive_demo/result.json
```

The package's own tests (`test/lambda/archive/`) cover pages, renderings,
search, the media and gallery engines and the service.

**Known issue.** While jobs report often, each re-render recreates the viewer's
frame and a load that completes after the next re-render is dropped; the
snapshot appears once the messages stop. Reading snapshots straight from the
ZIP (AR-E8) will replace the frame-over-cache approach.
