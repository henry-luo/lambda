# Lambda Archive Package — `lambda.archive` (Proposal)

> **Status:** proposal, drafted 2026-10-21; revised the same day with the user's decisions (§13, resolved items; superseded drafts in Appendix C). Nothing is implemented.
> **Scope:** a shipped package, `lambda.archive`, that downloads what a link points to (web page, video, image or gallery, file) under a configuration, preserves it in several formats (a zipped snapshot HTML by default; readable HTML, Markdown and PDF optionally), reports progress while it works, and keeps a searchable library; plus a demo, `lambda demo archive`, in the spirit of ArchiveBox: enter a URL to archive it, watch it download, then browse and search the library.
> **Spec linkage:** D7.2.4v3 and D7.2.6 (the `lambda.<package>` path and its resolution), S12.1.1v3 (the `fn`/`pn` effect bit that splits the pure core from the procedural shell, AR3), S12.1.3 (reactive templates and handlers in the demo), S1.4 (values never alias: the library is a value, not shared state), S13.1.1v3, S13.2.1–S13.2.3 and S13.3.1v2 (tasks, mailboxes and structured scope, which the worker design of AR12 builds on; S13.3.1v2, ratified 2026-10-21 in spec 71.1.0, lets a task be owned by the document, AR-E4a), S12.4.1v3 and S14.3.1v3 (ZIP snapshots and deferred member reads), S10.1.4 (`output` writes the snapshot ZIP), S7.4.2 (raised errors), S17.8.1 (option names checked at the call), D5.3.3 (native transfer threads never hold managed values), D7.5.3v2 (Lambda reaches Radiant only through the `radiant` module), D7.1.2v2 (all resource IO goes through `lambda-io`).
> **Authority:** package design. Decisions AR1–AR13, native enablers AR-E1–AR-E8, open items AR-O4–AR-O13 (AR-O1–AR-O3 and AR-O10 resolved). The document-ownership ruling (AR-O10) is ratified as S13.3.1v2 (design record K30g in `Lambda_Design_Concurrency.md`). AR-O11 and AR-O13 also touch S13 and need the user's ruling before work starts.
> **Related:** [Lambda_Pkg_WebScrape.md](Lambda_Pkg_WebScrape.md) (shares the fetch enablers), [Lambda_Pkg_Readability.md](Lambda_Pkg_Readability.md) (the readable format), [Lambda_IO_Zip.md](Lambda_IO_Zip.md) (the snapshot container), [Lambda_Demo_Finder.md](Lambda_Demo_Finder.md) (large-list browsing; ZIP member previews), the Reader View demo (`test/demo/readability/`), and the prior art in §2.

---

## 1. Purpose and goals

People keep what they read and watch: an article before it disappears behind a paywall, a talk before it is taken down, a gallery, a datasheet. Tools exist for each kind of content (yt-dlp for video, gallery-dl for galleries, SingleFile for pages), and two kinds of product bundle them. **Downloaders** such as Universal Content Intake take one link and drop the file into a folder. **Archivers** such as ArchiveBox keep every capture in a library with several renderings per page and a way to search it. `lambda.archive` is the second kind, built on what Lambda already has: a spec-grade HTML5 parser, the Readability port, a Markdown writer, ZIP output, Radiant's PDF and PNG export, process execution without a shell, cooperative tasks on the event loop, and ranked full-text search over files.

**Goals.**

1. **Configuration-driven acquisition.** One entry point takes a URL and decides, from configuration rules and then built-in heuristics, whether it is a page, a video, a single image, a gallery or a file. It then hands the URL to the matching engine with that rule's settings: quality, subtitles, formats, timeouts, cookies. The package's built-in defaults are complete, so configuration is optional.
2. **Preservation in several formats.** Every capture yields a snapshot of the page and its resources, stored as one ZIP. Readable HTML (through `lambda.doc.readability`), Markdown and PDF are optional, chosen per call or per rule. Every output can be re-derived later from the stored capture without touching the network.
3. **Observable long work.** Downloads run in Lambda worker tasks, write large files straight to disk, and report progress (bytes, total, rate, stage) from the transfer layer up to Lambda, so a script can log it and the demo can show it.
4. **A library you can use.** Captures are stored as folders with a JSON record each. The package lists, filters and full-text searches them, and the demo presents them: enter a URL to archive it, then browse, search and view the result in each format.
5. **Sharing later.** Phase 2 lets resources be shared across pages and sites, so a file already present from another capture is not downloaded again (AR13). Phase 1 records what Phase 2 needs.

**Non-goals.** Crawling and link-following (that is `lambda.scrape`'s job, AR1); installing or updating third-party engines; defeating DRM, paywalls, logins or bot protection; a multi-user server; scheduled monitoring of channels; and anything in Universal Content Intake's republishing pipeline (trend detection, translation, burned-in subtitles, publish copy).

---

## 2. Prior art

| Tool | Model | Adopt | Avoid |
|---|---|---|---|
| **Universal Content Intake** | One-link CLI router over yt-dlp, gallery-dl, gdown, trafilatura, SingleFile; files land in Downloads with a sidecar note | Classification order (known platform → extension → server response → article vs whole page); sane video defaults (1080p cap, no upscaling); a provenance note beside each file | No library, index or search; auto-updating engines in place; a personal-use-only licence; the republishing pipeline |
| **ArchiveBox** | Self-hosted archive: per-URL snapshot folder, a set of extractors (wget, SingleFile, readability, PDF, screenshot, media via yt-dlp), an index and a web UI | Snapshot folder per capture with a JSON record; several renderings per page; per-extractor status; re-running extractors on stored captures | A server, a database and a background queue as hard requirements; heavy dependency set (Chrome, Node, Python) |
| **SingleFile / monolith** | Save the live page as one HTML file with resources inlined as `data:` URIs | Rewriting every resource reference to a local copy; dropping scripts so the snapshot is static; one file per capture | Base64 inlining: a third larger, slow to parse, and no resource can be shared (AR5v2 keeps one file, but a ZIP) |
| **WARC / Wayback Machine** | Records of HTTP exchanges with headers; de-duplication by payload digest (`revisit` records) | Keeping response metadata per resource; digest-based de-duplication across captures (AR13) | The full WARC writer in Phase 1 (AR-O7) |
| **wget `-p -k` / HTTrack** | Fetch the page and its requisites, convert links to local paths | Relative links to local copies | Recursive mirroring (a crawler concern) |
| **yt-dlp / gallery-dl** | Site-specific extractors for video and image collections | Delegate to them (AR4); yt-dlp's `--progress-template` gives machine-readable progress | Re-implementing extractors in Lambda |
| **Wallabag / Readeck / Firecrawl** | Read-later and page-to-Markdown | Readable HTML and Markdown as first-class outputs with title, byline and date front matter | SaaS coupling, LLM dependencies |

The consensus among the archivers is the shape this proposal takes: **acquire once, keep the raw capture, derive renderings from it, record the status of each, index the text.** WARC adds the second idea Phase 2 needs: **identify payloads by digest, so a capture can point at bytes it did not fetch again.**

---

## 3. Feasibility

### 3.1 Present in the tree

| Need | What Lambda has | Where |
|---|---|---|
| Fetch a page | `io.fetch(url, options)` returns the body; `input(url, 'html')` fetches and parses | `lambda/runtime/lambda-proc.cpp:459` (`pn_fetch`) |
| Parse HTML | WHATWG HTML5 tree construction, entities, JSON-LD parsed in place | `lambda/input/html5/` |
| Resolve URLs | `url_resolve(base, relative)`, aligned with the WHATWG parser in 2026-10 | `lib/url_parser.c` |
| Readable article | `lambda.doc.readability`: `parse(source, {url})`, `readerable(source)`; 130/130 on Mozilla's corpus | `lmd/package/doc/readability.ls` |
| HTML → Markdown | `format(doc, 'markdown')` on a parsed HTML document (checked 2026-10-21: headings, emphasis, links, lists and images convert) | `lambda/format/` |
| HTML → PDF / PNG | `radiant.export_document(html, destination, {format: "pdf" \| "png", pagination, base_url, local_resources, width, height})`; embedded scripts are disabled | `lambda/module/radiant/radiant_module.cpp:3990`, `doc/dev/radiant/RAD_13_Render_Walk_Painters.md` §8.2 |
| ZIP container | `output(tree, path, 'zip')` writes a constructed file tree atomically, Stored or DEFLATE, deterministic on request; an unmodified input tree keeps its original member bytes; `input(zip)` + `content(node)` + `input(member, format)` read members lazily with CRC checks | `lambda/io/zip_archive.hpp`, [Lambda_IO_Zip.md](Lambda_IO_Zip.md) §3.2, §7 |
| Run yt-dlp / gallery-dl | `io.exec(program, args, {cwd, env, timeout_ms, max_output_bytes})`: no shell, argument array, exact binary capture, timeout kills the process group, cancellable. It runs the process on the libuv thread pool and parks the calling task, so other tasks and the UI keep running | `lambda/runtime/concurrency.cpp:1672` (`pn_io_exec3`) |
| Worker tasks | `start`, `wait`, `send`, `receive`, `select`, `cancel`; each task on its own stack; all tasks run cooperatively on the one libuv loop | `lambda/runtime/concurrency.cpp`, `doc/Lambda_Concurrency.md` |
| Loop shared with the UI | Radiant's window loop runs `uv_run(…, UV_RUN_NOWAIT)` every turn, so a parked task resumes between frames | `radiant/window.cpp:95`, `:152`, `:2066` |
| Thumbnails of images | `load(path)`, `resize(img, h, w)`, `save(img, path)` | `doc/Lambda_Sys_Func.md` ("Also Available") |
| File management | `io.mkdir`, `io.move`, `io.delete`, `io.rename`, `exists`, `output(data, path)` | `doc/Lambda_Sys_Func.md` |
| Search | `io.text_search(source, query, options)`: BM25 over files with web-search query syntax, phrase, prefix and negation, CJK words, snippets, no index to build | `lambda/runtime/io_text_search.cpp`, `vibe/Lambda_IO_Fulltext_Search.md` |
| UI | `view` templates with `state`, `on` handlers, `<input>`, `<iframe src/srcdoc>`, `<video>` (macOS, local files), `dom.request_frame(owner, event)` | `doc/Reactive_UI.md`, `doc/HTML_CSS_SVG_Support.md` |

### 3.2 Gaps

**Does `io.fetch` report progress?** No. `pn_fetch` calls `http_fetch` (`lambda/input/input_http.cpp:580`), which runs `curl_easy_perform` on the calling thread (`:673`) and appends the body to a memory buffer through its write callback (`:610`). It installs no progress callback. Because the transfer runs on the loop thread, a fetch from a worker task also stalls every other task and the window for its whole duration. Radiant's own resource loader does install curl progress callbacks (`lambda/network/network_downloader.cpp:92`, `lambda/network/curl_multi_backend.cpp:90`), but both use them only to abort cancelled transfers and discard the byte counts.

| # | Gap | Effect on the archiver | Enabler |
|---|---|---|---|
| G1 | `io.fetch` returns only the body: `fetch_response_to_item` drops status, headers and the final URL (`lambda-proc.cpp:442`). The body is a string even when the content is binary. | A 404 page would be archived as a success; `Content-Type` cannot drive classification or charset; redirects lose the canonical URL; ETag and Last-Modified are lost, which Phase 2 needs. | AR-E1 (= WebScrape E1) |
| G2 | `io.fetch` blocks the loop thread for the whole transfer (above). | A worker cannot download without freezing the UI. | AR-E1 |
| G3 | Request headers in `io.fetch` options are a no-op (`lambda-proc.cpp:562`); the User-Agent is hard-coded. | No configurable User-Agent, `Referer`, conditional requests (`If-None-Match`) or cookies. | AR-E2 (= WebScrape E2) |
| G4 | No transfer reports progress, and `io.copy` from a URL buffers the whole body in a heap string before writing (`pn_io_copy`, `lambda-proc.cpp:840`). | Large files are held in memory; nothing can be shown while a download runs. | AR-E3 |
| G5 | Today a task started in a handler is owned by the handler's block and joined at its end; S13.3.1v2's document owner is ruled but not implemented. Nothing delivers a task's message to a view. | A view cannot start a long-lived worker or receive its progress. | AR-E4 |
| G6 | `io.exec` returns output only when the process exits. | No progress from yt-dlp or gallery-dl. | AR-E5 |
| G7 | Pages rendered by JavaScript: the fetch path returns the server HTML, not the hydrated DOM. | Single-page apps archive as empty shells. | AR-E6 (= WebScrape E6) |
| G8 | No hash function among the system functions. | Phase 2 identifies shared resources by digest. | AR-E7 (= WebScrape E7) |
| G9 | Radiant cannot load an HTML document from a ZIP member with its relative resources resolved against sibling members. The Finder records the same gap ([Lambda_Demo_Finder.md](Lambda_Demo_Finder.md) §6), and the IO-Zip design rules out a `zip:` path scheme ([Lambda_IO_Zip.md](Lambda_IO_Zip.md) §3.2). | The demo cannot display a zipped snapshot in place. | AR-E8 |

None needs a language change; AR-E4 needs a ruling on task ownership (AR-O10). G1–G3 are also the WebScrape package's first blockers; doing them once serves both packages.

---

## 4. Design decisions

**AR1 — An archiver, not a crawler.** One URL in, one snapshot out. The page engine fetches the page's own resources (stylesheets, images, fonts, icons) but never follows `<a>` links. A gallery or a playlist is one snapshot, because the source presents it as one thing. Link-following, frontier management and politeness across a site belong to `lambda.scrape`; a later bulk import (§10) feeds URLs one at a time. *Why:* the two problems have different failure modes and different user expectations.

**AR2v2 — Configuration is data owned by the caller; the package's defaults are complete (USER, 2026-10-21).** The package carries a complete set of defaults, so `open(root)` with no configuration archives sensibly. A caller may supply a configuration: a Lambda data value read with `input()` from any supported format (YAML, TOML, JSON, Mark), merged over the defaults, and checked against the exported `Config` type before use. The demo keeps its configuration in its own directory (`test/demo/archive/archive.yaml`), not in the library. An ordered rule list maps URL predicates to a kind and settings, and the first matching rule wins. When no rule matches, classification falls back to the built-in platform table, then the path extension, then the response `Content-Type` (AR-E1), then sniffing the body. Each snapshot records the effective settings it was made with, so the library stays self-describing per snapshot. *Why:* configuration belongs to the application that archives, and one library can be filled by several callers with different rules.

**AR3 — Pure core, procedural shell.** Classification and planning are `fn`: `plan(url, config)` returns a `Plan` value that names the kind, the engine, its arguments and the outputs to derive. Acquisition is `pn`: it executes the plan and returns a `Capture` record (files written, final URL, response metadata, engine version, log). Deriving outputs from a capture is again `fn` (rewrite, readable, Markdown, text), except PDF and PNG, which go through Radiant's procedural export (D7.5.3v2). Packing and writing are `pn`. *Why:* S12.1.1v3 makes the split checkable; plans can be golden-tested without a network; a stored capture can be re-derived at any time (`refresh`), so improvements to Readability or the Markdown writer reach old snapshots without re-downloading.

**AR4 — Delegate media to maintained engines, through `io.exec`, user-installed.** Video and audio go to yt-dlp; galleries go to gallery-dl; both run with an argument array and no shell, from paths set in configuration. The package never installs or updates them. `doctor()` reports which engines are present and their versions, and every capture records the engine and version it used. Pages, files and single images use the native transfer path (AR-E1, AR-E3). *Why:* yt-dlp supports well over a thousand sites and changes weekly as sites change; re-implementing that in Lambda would be permanently stale. Silently replacing an executable is a supply-chain decision the user should make, and a recorded version makes failures diagnosable.

**AR5v2 — The default HTML is a ZIP of the page and its resources (USER, 2026-10-21).** `snapshot.zip` holds `index.html` with every resource reference rewritten to a relative path, the resources under `assets/`, and `manifest.json` (AR10). `<script>`, `on*` handler attributes and `javascript:` URLs are removed, so the snapshot is static and safe to open; the raw response is kept in the ZIP as `source.html` for provenance and re-derivation. The renderings that use the same resources go in the same ZIP: `readable.html` and `article.md` point at the same `assets/` members. Outside the ZIP stay the files other tools read directly: `snapshot.json`, `text.txt` (searched by `io.text_search`), `page.pdf`, `thumb.png`, and `media/` (video and audio stay loose files because `<video>` plays local files and media is already compressed). The ZIP is written with `output(tree, path, 'zip')`; `refresh` reads it back, replaces the derived members, and writes it again, which keeps the original bytes of unchanged members. *Why:* one file per capture is portable and easy to copy, verify (CRCs) and delete; a ZIP avoids SingleFile's base64 growth; and its members can later be shared rather than duplicated (AR13). Viewing in place needs AR-E8.

**AR6 — Every snapshot has the same face, whatever its kind.** Each snapshot has `snapshot.json`, `snapshot.zip` with an `index.html`, and `text.txt`. For a page, `index.html` is the snapshot (AR5v2). For a video it is a generated media page: player, title, uploader, date, description and the transcript from subtitles, with the player pointing at `../media/`. For a gallery it is a generated grid; for a file, a card with name, type, size and a link. *Why:* the viewer, the search, the PDF export and later sharing treat every kind the same way, and kind-specific logic stays in the engines.

**AR7 — The snapshot record is the truth; the index is a cache.** `snapshot.json` in each folder is authoritative. `index.json` at the root is a cache rebuilt by scanning `snapshots/*/snapshot.json` whenever it is missing or older than a folder. A capture is assembled as loose files under `.staging/<id>/`, where the PDF and thumbnail are rendered from those files; it is then packed into `snapshot.zip`, and the folder moves into `snapshots/<id>/` only when its record is written. A crash leaves no half-snapshot in the library. *Why:* deleting or copying a folder by hand stays correct, sync tools see whole folders, and there is no database to repair.

**AR8 — Search without an index.** `search` runs `io.text_search` over `snapshots/*/text.txt` (BM25, phrases, prefixes, negation, snippets) and filters by the metadata in the index cache (kind, host, tag, date). `text.txt` holds the title, byline, description and body text: Readability's `text` for pages, the description and subtitle transcript for video, captions for galleries. Title and URL matches are found in the index and ranked above body-only matches. *Why:* `io.text_search` reads files on every call, so results are always current and nothing can fall out of sync; a video becomes searchable by what is said in it.

**AR9 — Outputs succeed or fail independently.** `snapshot.json` carries a status per output (`ok`, `failed` with the message, `skipped` with the reason). A failed PDF does not lose the snapshot, and `refresh(id, ['pdf'])` retries it. Acquisition failure is the only thing that fails `add`.

**AR10 — Resources go through one manifest.** The page engine records every resource in `manifest.json`: its absolute URL, final URL, local path, content type, byte count, ETag, Last-Modified, the `integrity` value the page declared for it if any, and its digest once AR-E7 exists. One rewrite function applies the manifest to the snapshot, to the readable article (Readability resolves the article's links against the original URL, so its image sources are absolute and map back to members), and to Markdown image links. Hyperlinks stay absolute to the live web. *Why:* three formats, one rule (AGENTS.md rule 13); and the manifest is exactly what Phase 2 needs to find a resource without fetching it (AR13).

**AR11 — Offline-first demo and tests.** The demo ships a sample library and archives local pages over `file:` URLs, reusing the Reader View demo's fictional articles (`test/demo/readability/articles/`) as its "web". Network archiving is optional. Engine adapters are tested with stub executables named in a test configuration, and progress is tested with a local slow-file source, so CI needs neither the network nor yt-dlp.

**AR12 — Long work runs in a Lambda worker, and progress flows up as messages (USER, 2026-10-21).** An archive job is a `pn` run as a task. Every transfer it makes reports progress to a handle the caller names, as messages in the shape `{type: 'progress', tag, stage, received, total, rate}`, where `tag` identifies the job and `stage` is `fetch`, `assets`, `media`, `render` or `pack`. The source of each update is the lowest layer that knows it: curl's transfer callback for native downloads (AR-E3), yt-dlp's `--progress-template` lines for video (AR-E5), the asset count for pages. The job sends its own stage changes the same way. Progress is a level, not an event: updates are rate-limited (default every 250 ms) and the newest replaces an undelivered older one, so a fast download cannot fill a mailbox (AR-O11). Large files go straight to disk through a temporary file (AR-E3), never through a Lambda string. In the demo, one archive service task, owned by the document (USER, 2026-10-21), receives job requests and runs up to N jobs as its own children; the view keeps the service's handle in its state to send it requests, and progress reaches the view as events (AR-E4). *Why:* the user requires downloads to run in Lambda workers with progress observable end to end. Tasks are the worker model Lambda has today: IO parks the task off the loop thread, so the window stays responsive while bytes arrive. CPU-heavy steps are the exception: Readability on a 1 MB page takes about two seconds and a PDF export is synchronous native work, and both would pause the window inside a cooperative task. They run in a child `lambda.exe` process through `io.exec` until thread workers (`mode: 'thread'`, E501 today) exist (AR-O12).

**AR13 — Phase 2 shares resources across pages and sites (USER, 2026-10-21).** A library-wide resource store keeps each payload once, by digest, and a URL index records what was fetched where and when. Before a resource is downloaded, the page engine checks, in order: a fresh URL hit (reuse it); a stale URL hit with validators (send `If-None-Match` / `If-Modified-Since`, and reuse it on `304`); an `integrity` attribute whose digest is already in the store (reuse it with no request at all, since the page itself names the bytes it expects); otherwise download, digest, and store the payload once even when another URL already brought the same bytes. A snapshot ZIP then holds the page-specific members and references shared ones through `manifest.json`, as WARC's `revisit` records do. `export(id)` materializes a standalone ZIP when a snapshot leaves the library, and `gc()` drops payloads no manifest references. *Why:* fonts, scripts, logos and CDN libraries repeat across pages of a site and across sites; the integrity attribute turns some of that repetition into zero requests. Phase 1 keeps the manifest from the first capture, so Phase 2 can backfill the store from existing snapshots.

---

## 5. Architecture

```
  test/demo/archive  (lambda demo archive)                   user scripts
   view ── on job(evt) ◀── dom.address ◀── service task ◀────┐   │
        └─ send(state.service, {op:'add', url}) ─▶ (AR-E4)     │   │
  ┌──────────────────────────────────────────────────────────┴───┴──────┐
  │ archive.ls  facade: open · add · job · service · refresh · remove · │
  │             list · search · doctor · plan · export · gc              │
  ├──────────────────────────────────────────────────────────────────────┤
  │ L3 LIBRARY   store.ls   layout, ids, staging, pack, snapshot.json,  │
  │                         index cache, duplicates                      │
  │              search.ls  io.text_search + metadata filters           │
  │              shared.ls  Phase 2: resource store, URL index (AR13)   │
  ├──────────────────────────────────────────────────────────────────────┤
  │ L2 PRESERVE  rewrite.ls  manifest → snapshot / reader / md          │
  │  (fn, PDF    readable.ls lambda.doc.readability → reader page       │
  │   and PNG    markdown.ls front matter + format(…, 'markdown')       │
  │   pn)        pages.ls    media page, gallery page, file card        │
  │              text.ls     text.txt per kind                          │
  │              render.ls   radiant.export_document → pdf, png         │
  ├──────────────────────────────────────────────────────────────────────┤
  │ L1 ACQUIRE   plan.ls     classify (rules → platforms → ext →        │
  │  (pn)                    content type → sniff) → Plan (fn)           │
  │              page.ls     page fetch, resource collection, download  │
  │              file.ls     direct download (files, single images)     │
  │              video.ls    yt-dlp adapter                              │
  │              gallery.ls  gallery-dl adapter                          │
  │              transfer.ls io.fetch / io.download with progress        │
  │              progress.ls message shapes, stage reports               │
  ├──────────────────────────────────────────────────────────────────────┤
  │ L0 CONFIG    config.ls   defaults, merge, Config type, rules        │
  │              platforms.ls built-in platform table (data)            │
  └──────────────────────────────────────────────────────────────────────┘
        │                │                 │                  │
  io.fetch/io.download  io.exec         lambda.doc.readability  radiant.export_document
  (curl off the loop;   (yt-dlp,        format(…,'markdown')    output(…,'zip')
   progress, AR-E3)      gallery-dl)                            io.text_search
```

**Flow of one job.**

```
url ─▶ normalize ─▶ duplicate check (index) ─▶ plan(url, config)              [fn]
     ─▶ acquire(plan) into .staging/<id>/ ─────────── progress ──▶ notify      [pn, parks]
     ─▶ derive: index.html, readable.html, article.md, text.txt              [fn]
     ─▶ render page.pdf, thumb.png from the staged files ── stage ──▶ notify [pn]
     ─▶ pack index.html, source.html, readable.html, article.md, assets/,
        manifest.json ─▶ snapshot.zip                                         [pn]
     ─▶ write snapshot.json, move .staging/<id> ─▶ snapshots/<id>            [pn]
     ─▶ Snapshot record (returned to the job's owner)
```

**The page engine.** Fetch the URL (status, final URL and content type from AR-E1), decode by the declared charset, and parse. Collect resources: `<link rel=stylesheet|icon>`, `<img src|srcset>`, `<picture><source srcset>`, `<video poster>`, inline `style` and `<style>` `url(…)`, and, inside each downloaded stylesheet, `url(…)` and `@import` (recursively, bounded). Resolve each against the final URL or `<base href>` with `url_resolve`, download each once, and record it in the manifest (AR10). Downloads run as child tasks of the job, a few at a time per host, each writing straight to its staged file. Stylesheet text is rewritten with string patterns, since there is no CSS writer and none is needed. Limits come from configuration: total bytes, per-asset bytes, count and timeout; an asset over a limit stays a remote link and is listed in the manifest. `srcset` parsing reuses the Readability package's candidate pattern, promoted to a shared module rather than copied (rule 13).

**The video engine.** One `io.exec("yt-dlp", args, {timeout_ms, cwd: staging, progress: notify, tag})` with arguments built from the rule: the format selector (default `bv*[height<=1080]+ba/b[height<=1080]`, never upscaled), `--no-playlist` unless the rule allows playlists, `--write-info-json`, `--write-thumbnail`, subtitles (`--write-subs`, optionally `--write-auto-subs`, `--sub-langs`, converted to VTT), `--cookies <file>` when configured, an output template inside the staging folder, `--print after_move:filepath` for the final names, and `--newline --progress-template "download:%(progress)j"` so each progress line is one JSON object that AR-E5 delivers and the adapter turns into the AR12 shape. The adapter reads `*.info.json` for title, uploader, date, duration and description, and turns the VTT cues into the transcript for `text.txt` and the media page.

**The gallery engine.** `gallery-dl --directory <staging>/media --write-metadata <url>`; each output line names the file just saved, which AR-E5 delivers as progress (file name and elapsed time). Metadata JSON gives captions, authors and dates; thumbnails come from `load`/`resize`/`save`.

**Transfer layer.** `transfer.ls` wraps `io.fetch` (pages and stylesheets, which are parsed) and `io.download` (everything else, written to disk) with the configured User-Agent, timeout, per-host delay and the job's progress target. When `lambda.scrape` exists, its session (cookies, retry) slots in here (AR-O6).

---

## 6. Configuration

The package's defaults (shown below as the values they take) apply when no configuration is given. The demo reads `test/demo/archive/archive.yaml` and passes it to `open`; any `input()` format works. Per-call options override the matched rule, which overrides the caller's `defaults`, which override the package defaults.

```yaml
defaults:
  formats: [html, readable, markdown]   # html (the snapshot ZIP) is always produced
  user_agent: "LambdaArchive/0.1 (+personal archive)"
  timeout_s: 60
  per_host_delay_ms: 500
  on_duplicate: skip                     # skip | new | replace
  progress_ms: 250
  jobs: 2                                # jobs the demo's service runs at once
  page:
    max_assets: 400
    max_asset_mb: 25
    max_total_mb: 200
    assets_per_host: 4
  pdf:
    source: readable                     # readable | snapshot
    pagination: true
  thumbnail: true

engines:
  yt-dlp:
    path: yt-dlp
    format: "bv*[height<=1080]+ba/b[height<=1080]"
    subtitles: [en]
    auto_subtitles: true
    cookies: null                        # a cookies.txt the user exported
    timeout_s: 3600
  gallery-dl:
    path: gallery-dl
    timeout_s: 1800

rules:                                   # first match wins
  - match: { host: ["youtube.com", "*.youtube.com", "youtu.be"] }
    kind: video
    engine: { subtitles: [en, zh-Hans] }
  - match: { host: "*.bandcamp.com" }
    kind: audio
  - match: { host: ["*.flickr.com", "www.pixiv.net"] }
    kind: gallery
  - match: { host: "*.substack.com" }
    formats: [html, readable, markdown, pdf]
  - match: { path: ["*.pdf", "*.epub", "*.zip"] }
    kind: file
```

**Matching.** `host` and `path` are glob lists (`*` within a label or segment; a leading `*.` matches the domain and its subdomains); `scheme` and `query` are optional. All the fields in one `match` must hold. A rule without `kind` only changes settings, and the kind is still classified (AR2v2).

**Kinds and engines.**

| Kind | Engine | Primary files | Default outputs |
|---|---|---|---|
| `page` | native (`page.ls`) | `snapshot.zip`: `index.html`, `source.html`, `assets/`, `manifest.json` | html, readable, markdown; pdf optional |
| `video`, `audio` | yt-dlp | `media/<title>.<ext>`, `.info.json`, thumbnail, subtitles | media page, transcript text; readable and markdown from description and transcript |
| `image` | native (`file.ls`) | `media/<name>`, thumbnail | image page |
| `gallery` | gallery-dl | `media/*`, metadata JSON | gallery page |
| `file` | native (`file.ls`) | `media/<name>` | file card; a PDF file is also viewable directly |

**Validation.** The package exports `Config` and `Rule` types. A configuration that fails them is rejected with every path that failed, before anything is fetched. An unknown option name is an error, in the spirit of S17.8.1.

---

## 7. Library layout and records

```
<root>/
  index.json                   cache of all records (AR7)
  .staging/<id>/               captures in progress (loose files)
  snapshots/
    20261021T143012Z-shade-by-design/
      snapshot.json            the record (truth)
      snapshot.zip             index.html, source.html, readable.html, article.md,
                               assets/, manifest.json   (AR5v2, AR10)
      text.txt                 search text (AR8)
      page.pdf                 PDF
      thumb.png                list thumbnail
      media/                   video, audio, images, files and engine metadata
  resources/                   Phase 2: shared payloads by digest + URL index (AR13)
```

**Identifiers** are the UTC capture time plus a slug of the title or host: sortable, readable in a file manager, and unique with a numeric suffix on collision.

**`snapshot.json`.**

```json
{
  "format": 1,
  "id": "20261021T143012Z-shade-by-design",
  "url": "https://ledger.example/news/shade-by-design",
  "final_url": "https://ledger.example/news/shade-by-design/",
  "kind": "page",
  "title": "Shade by design",
  "byline": "A. Writer",
  "site": "The Meridian Ledger",
  "published": "2026-09-30T08:00:00Z",
  "archived": "2026-10-21T14:30:12Z",
  "status": 200,
  "content_type": "text/html; charset=utf-8",
  "tags": [],
  "engine": { "name": "native", "version": "<lambda version>" },
  "settings": { "formats": ["html", "readable", "markdown", "pdf"], "...": "..." },
  "outputs": {
    "html":     { "status": "ok", "file": "snapshot.zip", "member": "index.html", "bytes": 48213 },
    "readable": { "status": "ok", "file": "snapshot.zip", "member": "readable.html" },
    "markdown": { "status": "ok", "file": "snapshot.zip", "member": "article.md" },
    "pdf":      { "status": "failed", "message": "…" },
    "thumb":    { "status": "ok", "file": "thumb.png" }
  },
  "assets": { "count": 23, "bytes": 1840221, "skipped": [] },
  "log": ["…"]
}
```

**`manifest.json`** (inside the ZIP) lists one entry per resource: `{url, final_url, path, content_type, bytes, etag, last_modified, integrity, digest}`, with `digest` absent until AR-E7 exists. In Phase 2 an entry may carry `shared: true` and no member, the payload living in `resources/` under its digest (AR13).

**Duplicates.** URLs are normalized (lowercase scheme and host, default port removed, fragment dropped, tracking parameters such as `utm_*` dropped by a configurable list). `on_duplicate: skip` returns the existing snapshot; `new` adds a dated version, and versions of one URL group together in the library; `replace` archives anew and removes the old folder after the new one is in place.

---

## 8. Package API

```lambda no-run
import archive: lambda.archive

pn log_progress() {
    var running = true
    while (running) {
        let m = receive()^
        match m {
            case {type: 'progress'} { print(m.stage, " ", m.received, "/", m.total, "\n") }
            case {type: 'stop'} { running = false }
            default { }
        }
    }
}

pn main() {
    let lib = archive.open("temp/archive", {config: input("archive.yaml")^})^
    let logger = start(log_progress)
    let job = start(archive.job, [lib, "https://ledger.example/news/shade-by-design",
                                  {formats: ['html', 'readable', 'markdown', 'pdf'],
                                   progress: logger, tag: "job-1"}])
    let snap = wait(job)^
    send(logger, {type: 'stop'})^
    print(snap.id, " ", snap.outputs.pdf.status, "\n")

    for (h in archive.search(lib, "\"street trees\" -opinion", {kind: 'page', limit: 20})^)
        print(h.snapshot.title, " — ", h.snippet, "\n")
}
```

| Function | Effect | Result | Notes |
|---|---|---|---|
| `open(root, options?)` | `pn` | `Library^` | `options.config` is a configuration value; without it the package defaults apply. Reads the index cache; creates the layout when missing |
| `plan(url, config, options?)` | `fn` | `Plan^` | classification and engine arguments, no IO (AR3) |
| `job(lib, url, options?)` | `pn` | `Snapshot^` | one archive job: acquire, derive, pack, write. `options.progress` is the handle that receives AR12 messages, `options.tag` labels them. Meant to run as a task; raises only when acquisition fails (AR9) |
| `add(lib, url, options?)` | `pn` | `Snapshot^` | `job` run in the caller's task, for scripts that do not need concurrency |
| `service(lib, notify)` | `pn` | `null^` | a long-running task: receives `{op: 'add', url, options}` and `{op: 'cancel', tag}`, runs up to `jobs` jobs as children, and reports `progress`, `done` and `failed` messages to `notify` |
| `refresh(lib, id, formats?)` | `pn` | `Snapshot^` | re-derive outputs from the stored capture, no network; rewrites the ZIP |
| `remove(lib, id)` | `pn` | `bool^` | deletes the folder, updates the index |
| `list(lib, filter?)` | `fn` | `Snapshot[]` | over the library value: kind, host, tag, date range, sort |
| `search(lib, query, options?)` | `pn` | `Hit[]^` | AR8; `Hit` = `{snapshot, score, snippet}` |
| `doctor(lib)` | `pn` | `map` | engines found, versions, configuration errors |
| `export(lib, id, path)`, `gc(lib)` | `pn` | | Phase 2: standalone ZIP; drop unreferenced shared payloads |
| `readable(capture)`, `markdown(capture)`, `snapshot_html(capture)` | `fn` | `string^` | the pure derivations, usable on their own |

`Library` is a value: the root, the merged configuration and the records read at `open` (S1.4). Procedures that change the library return the new record, and a caller that holds a `Library` folds the record in; the demo keeps the listing in view state and replaces it on each `done` event.

---

## 9. Demo: `lambda demo archive`

**Location.** `test/demo/archive/`, registered in the demo catalog. Entry `archive.ls`, with `mod_model.ls` (state, actions, filters, job table), `mod_ui.ls` (layout), `archive.yaml` (the demo's configuration, AR2v2), `archive.css`, `sample/` (pre-archived snapshots for the sample library) and `tests/`. The default library root is `temp/archive/`, seeded from `sample/` on first run; a settings field points it elsewhere.

**Layout.**

```
┌──────────────────────────────────────────────────────────────────────────┐
│ [ https://…                         ] (Auto ▾) [HTML][Reader][MD][PDF] ⏎ │
│ [ Search the archive…                                              ]     │
├────────────┬─────────────────────────────┬───────────────────────────────┤
│ Library    │ ▣ Shade by design            │ Snapshot | Reader | Markdown │
│  All   42  │   ledger.example · page ·    │ | PDF | Media | Info          │
│  Pages 30  │   21 Oct · HTML R MD PDF     │ ┌───────────────────────────┐ │
│  Video  6  │ ▣ Overnight focaccia         │ │  (index.html or readable  │ │
│  Images 4  │   crumbandhearth.example …   │ │   .html from snapshot.zip; │ │
│  Files  2  │ ▣ How reader mode works      │ │   PDF; <video>; metadata)  │ │
│ Sites      │   …                          │ └───────────────────────────┘ │
│  ledger 12 ├─────────────────────────────┤ Open original · Re-run PDF ·  │
│            │ Jobs                         │ Reveal folder · Delete        │
│            │ ▶ talk.mp4  ███████░░ 71%    │                               │
│            │   412/580 MB · 9.6 MB/s      │                               │
└────────────┴─────────────────────────────┴───────────────────────────────┘
```

**Archive a URL.** Type or paste a URL, optionally force a kind and choose formats, press Enter. The handler sends `{op: 'add', url, options}` to the archive service and returns at once. A job row appears with its stage and, while bytes arrive, a progress bar with received and total size and the rate; **Cancel** sends `{op: 'cancel', tag}`. When the job finishes, the snapshot appears at the top of the list and opens in the viewer; a failure stays in the jobs list with its message.

**The worker wiring (AR12, AR-E4).** The view's `on load` handler starts `archive.service(lib, notify)` owned by the document, so the service outlives the handler, and assigns its handle to the view's state; later handlers reach the service only through that handle (USER, 2026-10-21). The Archive action sends `{op: 'add', url, options}` to it and **Cancel** sends `{op: 'cancel', tag}`. `notify` is the view's event address (AR-E4b): each message the service sends to it arrives as `on job(evt)`, with the message as `evt.detail`, and the model folds a `progress` message into the job row, a `done` message into the listing, a `failed` message into the row's error. Closing the window discards the document, which cancels the service; the service's own scope then cancels its jobs and their transfers. Downloads and engines park their tasks off the loop thread, so the window keeps painting; Readability and PDF rendering run in a child `lambda.exe` through `io.exec` (AR12).

```lambda no-run
view <archive_app> state st: model.initial(), service: null {
    <body ui.page(st)>
}
on load(evt) {
    // owned by the document, so it outlives this handler; the handle in state reaches it later
    service = start(archive.service, [LIB, dom.address(evt.target, "job")], {owner: 'document'})
}
on click(evt) {
    let button = dom.closest(evt.target, "[data-action]")
    if (button == null or service == null) { return 'pass' }
    let request = model.request(st, dom.get_attribute(button, "data-action"), dom.get_attribute(button, "data-value"))
    if (request != null) {
        send(service, request) ^ { st = model.note_error(st, ^.message) }
    }
    'handled'
}
on job(evt) { st = model.apply_job_message(st, evt.detail) }
```

**Browse.** The left rail filters by kind and by site, with counts; the list shows a thumbnail, title, site, date, kind and the formats present, newest first, and is windowed for large libraries as in the Finder demo.

**View.** Tabs show the snapshot and reader pages from `snapshot.zip` (AR-E8; until it lands, the selected snapshot's ZIP is unpacked into a view cache under `temp/`), the Markdown (rendered, with a source toggle), the PDF (pages drawn by `lambda.doc.pdf`), media (`<video>` on macOS; the gallery grid), and Info (the record, the manifest, per-output status, files and sizes, log). Actions: open the original URL, re-run a failed output (`refresh`), reveal the folder, delete with confirmation.

**Search.** Typing in the search field replaces the list with hits, best first, each with its snippet; the kind and site filters still apply.

**Checks.** A model golden in `test_lambda_gtest` (`tests_archive_model`): planning against the demo configuration, outputs derived from fixture captures, ZIP packing and reading back, the index rebuild, filters, and search over the sample library. A progress test that downloads a large local fixture through a throttled source and checks that progress messages arrive in order, are rate-limited, never fill the mailbox, and end at the file size. A native replay (`tests/archive.json`) that archives a sample page, watches its job row complete, searches it, switches tabs, re-runs an output and deletes a snapshot. Engine adapters are checked against stub `yt-dlp` and `gallery-dl` scripts that print scripted progress lines (AR11).

---

## 10. Phases and milestones

**Phase 1 — archiving with progress.**

| Milestone | Delivers | Acceptance |
|---|---|---|
| **M0 Enablers** | AR-E1 (full response, off the loop thread), AR-E2 (request options), AR-E3 (`io.download` to disk with progress messages), shared with WebScrape | A task reads `status`, `headers` and the final URL; a 2 GB local-server download keeps resident memory flat; progress messages arrive rate-limited while a second task keeps running |
| **M1 Pages** | configuration and defaults, `plan`, page engine, manifest, snapshot ZIP, readable HTML, Markdown, `text.txt`, store, `open`/`add`/`job`/`list`/`remove` | The Reader View demo's four pages archive over `file:` URLs; goldens for `plan`, ZIP members and `manifest.json`; the snapshot opens from the ZIP with no network |
| **M2 Renderings and search** | PDF and thumbnail from staged files, `refresh` (ZIP rewrite), `search` | PDF and PNG for the sample pages; search finds a phrase, a prefix and a negation with snippets; a forced PDF failure leaves the snapshot `ok` with `pdf: failed` |
| **M3 Media engines** | AR-E5 (streamed process lines), yt-dlp and gallery-dl adapters with progress, media and gallery pages, transcripts, `doctor` | Stub-engine goldens including progress; one recorded manual run against a real public-domain video and gallery |
| **M4 Service and demo** | AR-E4 (document-owned tasks per S13.3.1v2; task-to-view events after AR-O13), `service`, `lambda demo archive` with job rows, browse, filter, search, view, re-run, delete | Archiving a large local file keeps the window responsive and the bar moving; model golden, progress test and native replay pass; catalog entry |
| **M5 In-place viewing** | AR-E8, replacing the view cache | Snapshot and reader pages, images and stylesheets load from `snapshot.zip` members |

**Phase 2 — shared resources (AR13).** AR-E7 (digest), the resource store and URL index, conditional revalidation, `integrity` reuse, manifest references to shared payloads, `export` and `gc`, and a backfill pass that moves existing snapshots' resources into the store. *Acceptance:* archiving a second page of the same site downloads only its new resources (request log); a page whose scripts carry `integrity` values already in the store makes no request for them; `export` produces a standalone ZIP that opens with the library removed; `gc` after `remove` frees the payloads only that snapshot used.

**Later.** JavaScript-rendered pages (AR-E6), bulk import (URL lists, browser bookmark HTML), WARC output (AR-O7).

---

## 11. Native enablers

**AR-E1 — Full fetch response, off the loop thread.** `fetch_response_to_item` (`lambda/runtime/lambda-proc.cpp:442`) returns `{status, ok, url, headers, body}`, with `body` a string for text content types and a binary otherwise (WebScrape E1). The transfer moves off the loop thread: either onto the libuv thread pool, parking the calling task as `io.exec` does (`concurrency.cpp:1689`), or onto the curl-multi thread Radiant already runs (`curl_multi_backend.cpp:401`). Either way the transfer thread holds no managed values (D5.3.3); the loop thread builds the result.

**AR-E2 — Request options.** Apply `headers` (now a no-op at `lambda-proc.cpp:562`), `user_agent`, `follow_redirects` and `verify_tls` on the script path (WebScrape E2). Conditional headers (`If-None-Match`, `If-Modified-Since`) need nothing more, and a `304` comes back as an ordinary status.

**AR-E3 — `io.download(url, path, options)`: to disk, with progress.** A procedure that streams the response body into `path.part` and renames it to `path` on success, parking the calling task while it runs. It returns the AR-E1 response map without a body, plus `bytes`. Options: the AR-E2 request options, `max_bytes`, `resume` (a `Range` request continuing an existing `.part`), `progress` (a handle), `tag`, and `progress_ms` (default 250). The curl transfer callback (`CURLOPT_XFERINFOFUNCTION`) updates atomic counters on the transfer thread and wakes the loop; the loop thread turns the latest counters into `{type: 'progress', tag, stage: 'download', url, received, total, rate}` and delivers it, at most once per `progress_ms`, with a newer update replacing an undelivered older one (AR-O11). Cancelling the task aborts the transfer through the same callback, which is all the existing callbacks do today, and removes the `.part` file unless `resume` is set. `io.copy` from a URL becomes a call to the same path, so it stops buffering in memory.

**AR-E4 — Document-owned tasks, and messages to a view.**

*(a) Document ownership (USER, 2026-10-21).* A task can be, and should be, owned by the document, so that it outlives the handler that starts it. To communicate with the task, the handler keeps its handle in state that lives longer than the handler: a view's `state` (S12.1.3). Storing the handle grants the capability to send to it, wait on it and cancel it, not ownership. The spelling is an explicit start option (USER, 2026-10-21), `start(target, args, {owner: 'document'})`, accepted while a document runs Lambda code (its handlers and templates) and an error elsewhere. The handler's block exit neither joins nor cancels such a task; the document cancels and then joins it when the document is discarded (the window closes or navigates away). A task started without the option keeps today's scope, so a library procedure that a handler calls (`archive.add` downloading assets in child tasks) still joins its children at the end of its own block. Ratified 2026-10-21 (spec 71.1.0; design record K30g in [Lambda_Design_Concurrency.md](Lambda_Design_Concurrency.md) §10.8) as:

> **S13.3.1v2** A started handle is a scoped resource owned by the nearest lexical block: normal exit **joins**; error exit **cancels then joins**; ownership escapes only by `return` (visible in the type), or at the start site by `owner: 'document'`, which makes the running document the owner: the document cancels then joins the task when it is discarded. Storing or sending a handle grants capability, not ownership; a handler that must reach a document-owned task later keeps its handle in state that outlives the handler (S12.1.3).

Runtime work: each document gets a task scope that the reactive host joins at teardown, and `start` with the option registers the task there instead of in the current block's scope. The loop needs nothing new: Radiant's window loop already runs the libuv loop every turn (`radiant/window.cpp:95`), so tasks advance between frames.

*(b) Messages to a view (open, AR-O13).* A view is not a task and has no mailbox, and Lambda has no way to look at a task without parking, so the service needs an address to report to. Proposed, mirroring `dom.request_frame(owner, event)`: `dom.address(owner, event)` returns a value that `send` accepts; each message sent to it is dispatched as `on <event>(evt)` to the owner, with the message as `evt.detail`, in send order (S13.2.3). It extends S13.2.1, under which only task handles are addresses.

**AR-E5 — Streamed process output.** An `io.exec` option `progress: handle` (with `tag` and `progress_ms`) that delivers the most recent complete stdout line as `{type: 'line', tag, text}` under the same rate limit and replacement rule as AR-E3, while the full output is still captured in the result. yt-dlp's JSON progress template and gallery-dl's per-file lines both fit it.

**AR-E6 — Rendered DOM readback.** WebScrape E6: load a page with scripts on, settle, and return the live DOM, for single-page apps.

**AR-E7 — Digest.** WebScrape E7's `hash`, with SHA-256 for the store and SHA-384/SHA-512 for comparing `integrity` values. Required in Phase 2; in Phase 1 the manifest leaves `digest` empty.

**AR-E8 — Documents loaded from ZIP members.** Radiant loads an HTML document from a captured ZIP member node and resolves its relative resource URLs against sibling members, without a `zip:` path scheme (Lambda_IO_Zip §3.2); in Phase 2 the resolver also consults the library's resource store for shared entries. The Finder's member previews need the same resolution (Lambda_Demo_Finder §6). PDF and thumbnail rendering do not depend on it, because they render from the staged loose files before packing (AR7).

---

## 12. Risks

| Risk | Mitigation |
|---|---|
| Archived pages are hostile input | Scripts, `on*` attributes and `javascript:` URLs are removed from the snapshot (AR5v2); Radiant's export disables scripts; `source.html` is never opened by the viewer |
| File and member names from titles or servers escape their folder | Every name is slugged and joined under the snapshot; `..`, separators and control characters never survive; ZIP member paths are checked by the ZIP writer; engine output templates point inside staging |
| Copyright and terms of service | Personal archiving is the use case; the package has no republishing features; the README states that the user is responsible for what they archive; cookies are used only from a file the user names |
| Engines break when sites change | Failures carry the engine's stderr and version; `doctor` tells the user to update; no silent in-place updates (AR4) |
| CPU-heavy steps pause the window | Readability and PDF run in a child process (AR12) until thread workers exist (AR-O12) |
| Progress floods a mailbox | Rate limit plus replacement of undelivered updates (AR-E3, AR-O11) |
| Rewriting a large ZIP on `refresh` | Derived members are small; the media stays outside the ZIP; unchanged members keep their original bytes |
| Non-UTF-8 pages decode wrongly | AR-E1 exposes the charset; transcode before parsing; record the charset |
| Radiant renders a snapshot differently from Chrome | The ZIP keeps the original markup and CSS, so it unpacks into a folder any browser opens |
| Library growth | Asset limits, `on_duplicate: skip` by default, sizes in the record; Phase 2 de-duplication |

---

## 13. Open items

**Resolved (USER, 2026-10-21).**

- **AR-O1 — Name.** Keep `lambda.archive`. ZIP and other archive-format support is named `lambda.io.zip`, so the two do not collide.
- **AR-O2 — Default HTML form.** A ZIP of the page and its resources (AR5v2).
- **AR-O3 — Background jobs.** Long downloads run in Lambda workers, with progress reported from the low-level IO up to Lambda (AR12).
- **AR-O10 — Ownership of a task started from a view.** A task can be, and should be, owned by the document, so that it outlives the handler that starts it; to communicate with it, its handle is kept in state that lives longer than the handler (AR-E4a). The spelling is the explicit start option `owner: 'document'`, not implicit ownership of every task a handler starts; ratified as S13.3.1v2 (K30g).

**Open.**

- **AR-O4 — Configuration language.** Data file (AR2v2, proposed) or a `.ls` module, which would allow real string patterns in `match` and computed settings.
- **AR-O5 — Duplicate policy default.** `skip` (proposed) or `new` (ArchiveBox keeps every capture).
- **AR-O6 — Relation to `lambda.scrape`.** Proposed: a thin `transfer.ls` now, with the scrape session slotting in when that package lands; the native enablers are built once for both.
- **AR-O7 — WARC.** Writing WARC files would make captures readable by Wayback tooling and ArchiveBox; the manifest already holds what a response record needs except the raw headers. Deferred.
- **AR-O8 — Robots and politeness for single URLs.** Proposed: a URL the user submits is archived regardless of `robots.txt` (as ArchiveBox does), with a per-host delay for its assets; bulk import obeys `robots.txt`.
- **AR-O9 — Default library root for scripts.** The demo uses `temp/archive/`; `open` takes an explicit root. Whether a user-level default should exist is open.
- **AR-O11 — Replacing undelivered progress (needs a ruling).** S13.2.2 says `send` never drops. Progress messages produced by the runtime (AR-E3, AR-E5) replace an undelivered older update rather than queueing behind it. Proposed: this is a property of runtime-produced progress, not of `send`; the alternative is a separate progress cell the receiver reads, which avoids the question but needs its own builtin.
- **AR-O12 — CPU-heavy steps.** Until `mode: 'thread'` workers exist (E501 today), Readability and PDF rendering run in a child `lambda.exe` through `io.exec` (AR12). Whether to wait for thread workers instead is open.
- **AR-O13 — How a task's messages reach a view (needs a ruling).** The handle in state carries messages from the view to the service; the reverse direction needs an address the service can send to. Proposed: `dom.address(owner, event)` (AR-E4b), which extends S13.2.1. Alternative: the view polls by frame, which needs a new non-parking builtin to read a task's progress, since `wait`, `select` and `receive` all park.

---

## Appendix A. Source map

| File | Relevance |
|---|---|
| `lambda/runtime/lambda-proc.cpp` (`fetch_response_to_item` :442, `pn_fetch` :459, headers :562, `http_fetch` call :587, `pn_io_copy` :840) | AR-E1, AR-E2, AR-E3 |
| `lambda/input/input_http.cpp` (`http_fetch` :580, write callback :610, `curl_easy_perform` :673) | the blocking, buffered transfer AR-E1/AR-E3 replace |
| `lambda/network/curl_multi_backend.cpp` (progress callback :90, network thread :401), `lambda/network/network_downloader.cpp` (:92) | existing curl progress callbacks, used only for cancellation |
| `lambda/runtime/concurrency.cpp` (`pn_io_exec3` :1672, `uv_queue_work` + park) | the off-loop pattern for AR-E1/AR-E3; AR-E5 |
| `radiant/window.cpp` (:95, :152, :2066) | the window loop runs the libuv loop each turn (AR-E4) |
| `lambda/io/zip_archive.hpp`, `vibe/Lambda_IO_Zip.md` | snapshot ZIP writing and reading; AR-E8 |
| `lambda/runtime/io_text_search.cpp`, `vibe/Lambda_IO_Fulltext_Search.md` | AR8 |
| `lmd/package/doc/readability.ls`, `lmd/package/doc/readability/clean.ls` (srcset pattern) | readable format; the shared srcset parser |
| `lambda/module/radiant/radiant_module.cpp` (`fn_radiant_export_document` :3990) | PDF and thumbnails |
| `test/demo/readability/` | sample pages; browser chrome and frame patterns |
| `test/demo/finder/`, `vibe/Lambda_Demo_Finder.md` | windowed lists, ZIP member previews |
| `test/demo/photo-album/` | `dom.request_frame` events, thumbnails |
| `lmd/package/map.ls` + `lmd/package/map/` | facade-plus-modules package layout |

## Appendix B. Package module map (planned)

`lmd/package/archive.ls` is the facade; modules live under `lmd/package/archive/` and are imported as `lambda.archive.<module>` (D7.2.4v3, D7.2.6).

| Module | Role | Effect |
|---|---|---|
| `config.ls` | `Config` and `Rule` types, defaults, merge, validation | `fn` |
| `platforms.ls` | built-in host → kind table | data |
| `plan.ls` | URL normalization, rule matching, classification, `Plan` | `fn` |
| `progress.ls` | progress and stage message shapes; engine-line parsers | `fn` |
| `transfer.ls` | User-Agent, timeout, per-host delay, progress target over `io.fetch` / `io.download` | `pn` |
| `page.ls` | resource collection and download, manifest | `pn` |
| `file.ls` | files and single images | `pn` |
| `video.ls`, `gallery.ls` | yt-dlp and gallery-dl argument building (`fn`) and execution (`pn`), metadata and subtitle parsing (`fn`) | both |
| `rewrite.ls` | manifest rewrite, script stripping, CSS `url()` rewrite | `fn` |
| `readable.ls`, `markdown.ls`, `pages.ls`, `text.ls` | derived outputs | `fn` |
| `render.ls` | PDF and thumbnail export | `pn` |
| `store.ls` | ids, staging, packing, records, index cache, duplicates | `pn` (record building `fn`) |
| `service.ls` | the job service: queue, concurrency limit, cancellation, reports | `pn` |
| `search.ls` | text search and metadata filters | `pn` |
| `shared.ls` | Phase 2 resource store, URL index, revalidation, `export`, `gc` | `pn` |

## Appendix C. Superseded drafts (2026-10-21)

- ~~**AR2** — the configuration lives at `<root>/archive.yaml`, so a library describes itself.~~ Replaced by **AR2v2**: the caller owns the configuration (the demo's lives in its directory), the package's defaults are complete, and each snapshot records its effective settings (USER).
- ~~**AR5** — the default HTML is a folder snapshot (`index.html` + `assets/`); a single file with `data:` URIs is an option.~~ Replaced by **AR5v2**: one `snapshot.zip` holds the page, its resources and the renderings that share them (USER).
- ~~**AR-E3** (first draft) — streaming `io.copy` to disk, progress deferred until the view question settles; **AR-E5** optional.~~ Replaced by **AR-E3** `io.download` with progress messages and a required **AR-E5**, because progress must reach Lambda from the transfer layer (USER).
- ~~**AR-O3** — run demo jobs synchronously or in a separate polled worker process.~~ Resolved by **AR12**: Lambda worker tasks (USER); the remaining ownership question is **AR-O10**.
- ~~**AR-E4** (second draft) — `dom.start(owner, target, args)` starts a task owned by the owner element and cancelled when the element is removed.~~ Replaced by **AR-E4a**: the task is owned by the document, and its handle lives in view state (USER). The task-to-view address stays open as **AR-E4b** / **AR-O13**.
