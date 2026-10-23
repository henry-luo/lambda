# Lambda Archive Package — `lambda.archive` (Proposal)

> **Status:** in implementation. Drafted 2026-10-21; revised the same day with the user's decisions (§13, resolved items; superseded drafts in Appendix C); revised 2026-10-22 when the user ruled that transfer progress is a callback on `io.fetch` and `io.exec` (AR-E3v2, AR-E5v2, AR12v2; AR-O11 and AR-O12 resolved); revised again 2026-10-22 when the user ruled that **the archiver uses no external tools or libraries** — every engine is Lambda script, with a small C system function only where script lacks a primitive (AR4v2, AR-E9; AR-O14 opened). Phase 1 implementation started 2026-10-22: M0–M4 done (§10, implementation notes); AR-O13 resolved to `dom.dispatch` by its own test; AR-O14 still open (implemented as proposed). Revised 2026-10-23 when the user asked for **Phase 2: batches of URLs and site crawls** (AR1v2, AR14–AR20, M6–M9), which moves the shared-resource work (AR13) to Phase 3, and for the never-built `lambda.scrape` proposal (`Lambda_Pkg_WebScrape.md`) to be absorbed here, this package being the one implemented (Appendix D; AR-O6 resolved). Phase 2 is a proposal: not started.
> **Scope:** a shipped package, `lambda.archive`, implemented entirely in Lambda (no external tools or libraries, AR4v2), that downloads what a link points to (web page, video, image or gallery, file) under a configuration, preserves it in several formats (a zipped snapshot HTML by default; readable HTML, Markdown and PDF optionally), reports progress while it works, and keeps a searchable library; in Phase 2 it also archives a batch of URLs, or a site crawled from an entry page within limits on depth, page count, download size and time; plus a demo, `lambda demo archive`, in the spirit of ArchiveBox: enter a URL to archive it, watch it download, then browse and search the library.
> **Spec linkage:** D7.2.4v3 and D7.2.6 (the `lambda.<package>` path and its resolution), S12.1.1v3 (the `fn`/`pn` effect bit that splits the pure core from the procedural shell, AR3), S12.1.3 (reactive templates and handlers in the demo), S1.4 (values never alias: the library is a value, not shared state), S13.1.1v3, S13.2.1–S13.2.3 and S13.3.1v2 (tasks, mailboxes and structured scope, which the worker design of AR12 builds on; S13.3.1v2, ratified 2026-10-21 in spec 71.1.0, lets a task be owned by the document, AR-E4a), S12.4.1v3 and S14.3.1v3 (ZIP snapshots and deferred member reads), S10.1.4 (`output` writes the snapshot ZIP), S7.4.2 (raised errors), S17.8.1 (option names checked at the call), D5.3.3 (native transfer threads never hold managed values), D7.5.3v2 (Lambda reaches Radiant only through the `radiant` module), D7.1.2v2 (all resource IO goes through `lambda-io`).
> **Authority:** package design. Decisions AR1v2–AR20, native enablers AR-E1–AR-E11, open items AR-O4–AR-O19 (AR-O1–AR-O3, AR-O6 and AR-O10–AR-O13 resolved). The document-ownership ruling (AR-O10) is ratified as S13.3.1v2 (design record K30g in `Lambda_Design_Concurrency.md`). This document is also the record of the former web-scrape proposal (Appendix D).
> **Related:** [Lambda_Pkg_Readability.md](Lambda_Pkg_Readability.md) (the readable format), [Lambda_IO_Zip.md](Lambda_IO_Zip.md) (the snapshot container), [Lambda_Demo_Finder.md](Lambda_Demo_Finder.md) (large-list browsing; ZIP member previews), the Reader View demo (`test/demo/readability/`), and the prior art in §2.

---

## 1. Purpose and goals

People keep what they read and watch: an article before it disappears behind a paywall, a talk before it is taken down, a gallery, a datasheet. Tools exist for each kind of content (yt-dlp for video, gallery-dl for galleries, SingleFile for pages), and two kinds of product bundle them. **Downloaders** such as Universal Content Intake take one link and drop the file into a folder. **Archivers** such as ArchiveBox keep every capture in a library with several renderings per page and a way to search it. `lambda.archive` is the second kind, built on what Lambda already has: a spec-grade HTML5 parser, the Readability port, a Markdown writer, ZIP output, Radiant's PDF and PNG export, cooperative tasks on the event loop, and ranked full-text search over files — and nothing outside Lambda (AR4v2).

**Goals.**

1. **Configuration-driven acquisition.** One entry point takes a URL and decides, from configuration rules and then built-in heuristics, whether it is a page, a video, a single image, a gallery or a file. It then hands the URL to the matching engine with that rule's settings: quality cap, subtitles, formats, timeouts. Every engine is Lambda code (AR4v2). The package's built-in defaults are complete, so configuration is optional.
2. **Preservation in several formats.** Every capture yields a snapshot of the page and its resources, stored as one ZIP. Readable HTML (through `lambda.doc.readability`), Markdown and PDF are optional, chosen per call or per rule. Every output can be re-derived later from the stored capture without touching the network.
3. **Observable long work.** Downloads run in Lambda worker tasks, write large files straight to disk, and report progress (bytes, total, rate, stage) from the transfer layer up to Lambda, so a script can log it and the demo can show it.
4. **A library you can use.** Captures are stored as folders with a JSON record each. The package lists, filters and full-text searches them, and the demo presents them: enter a URL to archive it, then browse, search and view the result in each format.
5. **Batches and crawls (Phase 2).** One request archives a list of URLs — typed, read from a text file, a browser's bookmark export or a sitemap — or a site: from one entry page, the pages it links to, within a scope and limits on depth, page count, download size and time, politely (robots.txt, per-host pacing, retry). Every page taken is an ordinary snapshot; the crawl keeps its own record of what it took, skipped and left, and can be stopped and resumed (AR1v2, AR14–AR20).
6. **Sharing later (Phase 3).** Resources are shared across pages and sites, so a file already present from another capture is not downloaded or stored again (AR13). Phase 1 records what Phase 3 needs; Phase 2's crawls are what make it pay (AR20).

**Non-goals.** Structured data extraction — CSS-selected fields, item pipelines, schema-validated datasets — which the former web-scrape proposal planned and which is not archiving (Appendix D); a distributed or multi-machine crawler; running, installing or updating third-party tools — none are used (AR4v2); per-site extractors for streaming platforms (AR-O14); defeating DRM, paywalls, logins or bot protection, proxy rotation and stealth fingerprinting; a multi-user server; scheduled monitoring of channels or re-crawls on a timer; and anything in Universal Content Intake's republishing pipeline (trend detection, translation, burned-in subtitles, publish copy).

---

## 2. Prior art

| Tool | Model | Adopt | Avoid |
|---|---|---|---|
| **Universal Content Intake** | One-link CLI router over yt-dlp, gallery-dl, gdown, trafilatura, SingleFile; files land in Downloads with a sidecar note | Classification order (known platform → extension → server response → article vs whole page); sane video defaults (1080p cap, no upscaling); a provenance note beside each file | No library, index or search; auto-updating engines in place; a personal-use-only licence; the republishing pipeline |
| **ArchiveBox** | Self-hosted archive: per-URL snapshot folder, a set of extractors (wget, SingleFile, readability, PDF, screenshot, media via yt-dlp), an index and a web UI | Snapshot folder per capture with a JSON record; several renderings per page; per-extractor status; re-running extractors on stored captures; `add --depth=0\|1` (a list of URLs, or each one plus the pages it links to, every page its own snapshot); importing URL lists and bookmark exports | A server, a database and a background queue as hard requirements; heavy dependency set (Chrome, Node, Python) |
| **Browsertrix Crawler** (Webrecorder) | Crawls a site from seeds into one WACZ collection; scope types `page`, `prefix`, `host`, `domain`, `any`, with include and exclude patterns; limits on pages, size and time; sitemap seeds; crawls that stop and resume | The scope types and the `prefix` default; limits by depth, pages, size and time; one crawl record grouping its pages; a saved frontier for resume | A browser per worker (rendering is AR-E6, Later); WACZ and WARC (AR-O7) |
| **Scrapy / Crawlee / Colly** | Crawl frameworks: a request frontier with de-duplication, a worker pool, per-host throttling, retry, robots obeyance; Crawlee persists its request queue so a crawl resumes | The frontier as the coordinator's own state; de-duplication by normalized URL; per-host concurrency and delay; retry with backoff that honours `Retry-After`; a resumable frontier | Per-element callbacks, routers, item pipelines and dataset exports (structured extraction, not archiving); session pools, proxy rotation, autoscaling from CPU and memory probes |
| **SingleFile / monolith** | Save the live page as one HTML file with resources inlined as `data:` URIs | Rewriting every resource reference to a local copy; dropping scripts so the snapshot is static; one file per capture | Base64 inlining: a third larger, slow to parse, and no resource can be shared (AR5v2 keeps one file, but a ZIP) |
| **WARC / Wayback Machine** | Records of HTTP exchanges with headers; de-duplication by payload digest (`revisit` records) | Keeping response metadata per resource; digest-based de-duplication across captures (AR13) | The full WARC writer in Phase 1 (AR-O7) |
| **wget `-p -k -r` / HTTrack** | Fetch the page and its requisites, convert links to local paths; recurse with a depth limit and `--no-parent` | Relative links to local copies; the depth limit; `--no-parent` as the `prefix` scope | Rewriting the links between mirrored pages inside the stored files (a snapshot stays independent of its crawl; AR-O17) |
| **yt-dlp / gallery-dl** | Site-specific extractors for video and image collections | Their output conventions as the shape of a media record: an info record beside the file, subtitle sidecars, per-file metadata, a 1080p cap and no upscaling | Depending on them (AR4v2: no external tools); their per-site extractor corpus, which is out of scope (AR-O14) |
| **Wallabag / Readeck / Firecrawl** | Read-later and page-to-Markdown | Readable HTML and Markdown as first-class outputs with title, byline and date front matter | SaaS coupling, LLM dependencies |

The consensus among the archivers is the shape this proposal takes: **acquire once, keep the raw capture, derive renderings from it, record the status of each, index the text.** The crawlers add the second idea, Phase 2's: **a crawl is a frontier over ordinary captures** — scope, limits and politeness decide what enters it, and each page it takes is archived like any other. WARC adds the third, Phase 3's: **identify payloads by digest, so a capture can point at bytes it did not fetch again.**

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
| Download a file with progress | `io.fetch(url, {to, progress})` streams the body to disk off the loop thread and calls back with the level (AR-E3v2, landed in M0) | `lambda/runtime/lambda-proc.cpp` (`pn_fetch`), `concurrency.cpp` (`lambda_task_offload`) |
| Media on a page | `<video>`, `<audio>`, `<source>`, `<track>`, `og:video`/`og:audio`/`og:image`, `<link rel=enclosure>`, JSON-LD `VideoObject`/`ImageObject`: all plain markup the HTML5 parser already exposes | `lambda/input/html5/`, `lmd/package/doc/readability/metadata.ls` (JSON-LD reading) |
| HLS playlists | `.m3u8` is a line format a Lambda module parses; segments download with `io.fetch {to}` and append to one file with `output(bytes, path, {mode: 'append'})` | `doc/Lambda_Sys_Func.md` (output modes) |
| Images and thumbnails | `load`, `resize`, `save`, `image_process`, with pixel dimensions for the gallery size threshold | `doc/Lambda_Sys_Func.md` ("Image snapshots") |
| Worker tasks | `start`, `wait`, `send`, `receive`, `select`, `cancel`; each task on its own stack; all tasks run cooperatively on the one libuv loop | `lambda/runtime/concurrency.cpp`, `doc/Lambda_Concurrency.md` |
| Loop shared with the UI | Radiant's window loop runs `uv_run(…, UV_RUN_NOWAIT)` every turn, so a parked task resumes between frames | `radiant/window.cpp:95`, `:152`, `:2066` |
| Thumbnails of images | `load(path)`, `resize(img, h, w)`, `save(img, path)` | `doc/Lambda_Sys_Func.md` ("Also Available") |
| File management | `io.mkdir`, `io.move`, `io.delete`, `io.rename`, `exists`, `output(data, path)` | `doc/Lambda_Sys_Func.md` |
| Search | `io.text_search(source, query, options)`: BM25 over files with web-search query syntax, phrase, prefix and negation, CJK words, snippets, no index to build | `lambda/runtime/io_text_search.cpp`, `vibe/Lambda_IO_Fulltext_Search.md` |
| UI | `view` templates with `state`, `on` handlers, `<input>`, `<iframe src/srcdoc>`, `<video>` (macOS, local files), `dom.request_frame(owner, event)` | `doc/Reactive_UI.md`, `doc/HTML_CSS_SVG_Support.md` |
| A page's hyperlinks (Phase 2) | The page engine already walks every element of the parsed page (`all_elements`, `attr` from `lambda.doc.readability.node`) and resolves references against `<base href>` | `lmd/package/archive/page.ls` (`refs`, `base_of`) |
| Sitemaps and bookmark exports (Phase 2) | `input(source, 'xml')` reads `<urlset>` and `<sitemapindex>`; the Netscape bookmark file every browser exports is HTML for `lambda.sys.parse(…, 'html')` | `lambda/input/input-xml.cpp`, `lambda/input/html5/` |
| Pacing and retry (Phase 2) | `sleep(ms)`, `clock()`, `select` with a timeout; the job service's worker pool and cancellation | `lambda/runtime/concurrency.cpp`, `lmd/package/archive/service.ls` |
| Append-only crawl logs (Phase 2) | `output(text, path, {mode: 'append'})` | `doc/Lambda_Sys_Func.md` (output modes) |
| Cache keys (Phase 2) | `hash(value, 'sha256')`, `url_encode` (added 2026-10-22 for the mail demo) | `lambda/runtime/sys_crypto.cpp`, `test/lambda/hash_url_encode.ls` |

### 3.2 Gaps

**Does `io.fetch` report progress?** No. `pn_fetch` calls `http_fetch` (`lambda/input/input_http.cpp:580`), which runs `curl_easy_perform` on the calling thread (`:673`) and appends the body to a memory buffer through its write callback (`:610`). It installs no progress callback. Because the transfer runs on the loop thread, a fetch from a worker task also stalls every other task and the window for its whole duration. Radiant's own resource loader does install curl progress callbacks (`lambda/network/network_downloader.cpp:92`, `lambda/network/curl_multi_backend.cpp:90`), but both use them only to abort cancelled transfers and discard the byte counts.

| # | Gap | Effect on the archiver | Enabler |
|---|---|---|---|
| G1 | `io.fetch` returns only the body: `fetch_response_to_item` drops status, headers and the final URL (`lambda-proc.cpp:442`). The body is a string even when the content is binary. | A 404 page would be archived as a success; `Content-Type` cannot drive classification or charset; redirects lose the canonical URL; ETag and Last-Modified are lost, which Phase 3 needs. | AR-E1 (was WebScrape E1) |
| G2 | `io.fetch` blocks the loop thread for the whole transfer (above). | A worker cannot download without freezing the UI. | AR-E1 |
| G3 | Request headers in `io.fetch` options are a no-op (`lambda-proc.cpp:562`); the User-Agent is hard-coded. | No configurable User-Agent, `Referer`, conditional requests (`If-None-Match`) or cookies. | AR-E2 (was WebScrape E2) |
| G4 | No transfer reports progress, and `io.copy` from a URL buffers the whole body in a heap string before writing (`pn_io_copy`, `lambda-proc.cpp:840`). | Large files are held in memory; nothing can be shown while a download runs. | AR-E3v2 |
| G5 | Today a task started in a handler is owned by the handler's block and joined at its end; S13.3.1v2's document owner is ruled but not implemented. Nothing delivers a task's message to a view. | A view cannot start a long-lived worker or receive its progress. | AR-E4 |
| G6 | ~~`io.exec` returns output only when the process exits.~~ | No longer an archiver gap: no process is run (AR4v2). `io.exec {progress}` landed in M0 as a general runtime feature. | — |
| G7 | Pages rendered by JavaScript: the fetch path returns the server HTML, not the hydrated DOM. | Single-page apps archive as empty shells, and a crawl misses links that only scripts insert. | AR-E6 (was WebScrape E6) |
| G8 | ~~No hash function among the system functions.~~ `hash` exists since 2026-10-22. | Phase 3 identifies shared resources by digest; Phase 2 keys its crawl cache by one. | AR-E7 (was WebScrape E7) |
| G10 | No AES-128-CBC primitive: an HLS stream whose segments are encrypted (`#EXT-X-KEY METHOD=AES-128`, not DRM) cannot be decrypted by script. | Such streams are skipped, with the playlist recorded as a remote link, until a C system function exists. | AR-E9 |
| G9 | Radiant cannot load an HTML document from a ZIP member with its relative resources resolved against sibling members. The Finder records the same gap ([Lambda_Demo_Finder.md](Lambda_Demo_Finder.md) §6), and the IO-Zip design rules out a `zip:` path scheme ([Lambda_IO_Zip.md](Lambda_IO_Zip.md) §3.2). | The demo cannot display a zipped snapshot in place. | AR-E8 |
| G11 | No cookies on the script fetch path: Radiant's RFC 6265 jar (`lambda/network/cookie_jar.cpp`) serves only its own resource loader (was WebScrape G4). | A site that sets a consent or session cookie before serving content archives its interstitial. Logins are a non-goal; a cookie file the user names is Later. | AR-E10 |
| G12 | No CSS selector over a parsed HTML tree outside a Radiant document (was WebScrape G3). | A crawl cannot limit link-following to `nav a, .pagination a`; URL globs are the only filter. | AR-E11 (AR-O18) |

G1–G5 are closed (M0, M4). None needed a language change; AR-E4 needed a ruling on task ownership (AR-O10, ruled as S13.3.1v2). Phase 2 needs no native work: G11 and G12 limit what a crawl can reach and filter, and neither blocks it.

---

## 4. Design decisions

**AR1v2 — A job archives one URL; a crawl above the jobs follows links (USER, 2026-10-23).** A job stays one URL in, one snapshot out: the page engine fetches the page's own resources (stylesheets, images, fonts, icons) and never follows its `<a>` links, and a gallery or a playlist is one snapshot, because the source presents it as one thing. Following links is a layer above the job. A *crawl* takes seed URLs, archives each as an ordinary job, reads the archived page's links from its stored capture (AR17), and feeds the links its scope, rules and limits admit back as further jobs (AR15, AR16); a *batch* is a crawl that follows nothing (AR14). Crawling therefore lives in this package and `lambda.scrape` is not built (AR-O6, resolved): its orchestration — frontier, de-duplication, per-host politeness, robots.txt, retry — is absorbed here, and its structured-extraction surface is out of scope (Appendix D). *Why:* the user wants a list of URLs or a site archived in one request. Keeping the job unchanged means every page of a crawl is a normal snapshot, searchable, viewable and refreshable like any other, the engines need no crawl logic, and the two failure modes stay apart: a page that fails is one failed job, a crawl that runs away is stopped by its own limits. (~~AR1~~ in Appendix C.)

**AR2v2 — Configuration is data owned by the caller; the package's defaults are complete (USER, 2026-10-21).** The package carries a complete set of defaults, so `open(root)` with no configuration archives sensibly. A caller may supply a configuration: a Lambda data value read with `input()` from any supported format (YAML, TOML, JSON, Mark), merged over the defaults, and checked against the exported `Config` type before use. The demo keeps its configuration in its own directory (`test/demo/archive/archive.yaml`), not in the library. An ordered rule list maps URL predicates to a kind and settings, and the first matching rule wins. When no rule matches, classification falls back to the built-in platform table, then the path extension, then the response `Content-Type` (AR-E1), then sniffing the body. Each snapshot records the effective settings it was made with, so the library stays self-describing per snapshot. *Why:* configuration belongs to the application that archives, and one library can be filled by several callers with different rules.

**AR3 — Pure core, procedural shell.** Classification and planning are `fn`: `plan(url, config)` returns a `Plan` value that names the kind, the engine, its arguments and the outputs to derive. Acquisition is `pn`: it executes the plan and returns a `Capture` record (files written, final URL, response metadata, engine version, log). Deriving outputs from a capture is again `fn` (rewrite, readable, Markdown, text), except PDF and PNG, which go through Radiant's procedural export (D7.5.3v2). Packing and writing are `pn`. *Why:* S12.1.1v3 makes the split checkable; plans can be golden-tested without a network; a stored capture can be re-derived at any time (`refresh`), so improvements to Readability or the Markdown writer reach old snapshots without re-downloading.

**AR4v2 — Every engine is Lambda code; no external tools or libraries (USER, 2026-10-22).** The archiver runs no yt-dlp, gallery-dl, ffmpeg, wget or browser, and links no library for the purpose. Pages, files and single images use the native transfer path (AR-E1, AR-E3v2). **Media** (video, audio) comes from what a URL or a page exposes without a site-specific extractor: a direct media URL; a page's `<video>`, `<audio>` and `<source>` elements, `og:video` / `og:audio`, `<link rel=enclosure>` and a JSON-LD `VideoObject`'s `contentUrl`; and HLS playlists (`.m3u8`): the master playlist's variants are ranked by resolution under the configured cap (never upscaled), the chosen media playlist's segments are downloaded in order and appended to one file — fMP4 after its `#EXT-X-MAP` init segment, or MPEG-TS — with byte ranges and discontinuities honoured, and encrypted segments left to AR-E9. Subtitles are WebVTT: `<track>` elements and `#EXT-X-MEDIA TYPE=SUBTITLES` renditions, turned into the transcript. **Galleries** are a page's images: `<img>` (its largest `srcset` candidate), `<picture>`, `<a href>` to an image file and `og:image`, above a configurable pixel size, with `alt` and `figcaption` captions. Where Lambda script is too slow or lacks a primitive, a small C system function is added (AR-E9) — never a library. A URL whose media is only reachable through player JavaScript, signed URLs or a DASH manifest needing a mux is archived as a page, with the media it names listed as remote links in the record, not downloaded (AR-O14). *Why:* the user requires the archiver to be entirely Lambda; delegating to tools made the package depend on binaries it neither shipped nor controlled. The cost is stated plainly: there is no per-site extractor corpus, so a YouTube link archives as a page. (~~AR4~~ in Appendix C.)

**AR5v2 — The default HTML is a ZIP of the page and its resources (USER, 2026-10-21).** `snapshot.zip` holds `index.html` with every resource reference rewritten to a relative path, the resources under `assets/`, and `manifest.json` (AR10). `<script>`, `on*` handler attributes and `javascript:` URLs are removed, so the snapshot is static and safe to open; the raw response is kept in the ZIP as `source.html` for provenance and re-derivation. The renderings that use the same resources go in the same ZIP: `readable.html` and `article.md` point at the same `assets/` members. Outside the ZIP stay the files other tools read directly: `snapshot.json`, `text.txt` (searched by `io.text_search`), `page.pdf`, `thumb.png`, and `media/` (video and audio stay loose files because `<video>` plays local files and media is already compressed). The ZIP is written with `output(tree, path, 'zip')`; `refresh` reads it back, replaces the derived members, and writes it again, which keeps the original bytes of unchanged members. *Why:* one file per capture is portable and easy to copy, verify (CRCs) and delete; a ZIP avoids SingleFile's base64 growth; and its members can later be shared rather than duplicated (AR13). Viewing in place needs AR-E8.

**AR6 — Every snapshot has the same face, whatever its kind.** Each snapshot has `snapshot.json`, `snapshot.zip` with an `index.html`, and `text.txt`. For a page, `index.html` is the snapshot (AR5v2). For a video it is a generated media page: player, title, uploader, date, description and the transcript from subtitles, with the player pointing at `../media/`. For a gallery it is a generated grid; for a file, a card with name, type, size and a link. *Why:* the viewer, the search, the PDF export and later sharing treat every kind the same way, and kind-specific logic stays in the engines.

**AR7 — The snapshot record is the truth; the index is a cache.** `snapshot.json` in each folder is authoritative. `index.json` at the root is a cache rebuilt by scanning `snapshots/*/snapshot.json` whenever it is missing or older than a folder. A capture is assembled as loose files under `.staging/<id>/`, where the PDF and thumbnail are rendered from those files; it is then packed into `snapshot.zip`, and the folder moves into `snapshots/<id>/` only when its record is written. A crash leaves no half-snapshot in the library. *Why:* deleting or copying a folder by hand stays correct, sync tools see whole folders, and there is no database to repair.

**AR8 — Search without an index.** `search` runs `io.text_search` over `snapshots/*/text.txt` (BM25, phrases, prefixes, negation, snippets) and filters by the metadata in the index cache (kind, host, tag, date). `text.txt` holds the title, byline, description and body text: Readability's `text` for pages, the description and subtitle transcript for video, captions for galleries. Title and URL matches are found in the index and ranked above body-only matches. *Why:* `io.text_search` reads files on every call, so results are always current and nothing can fall out of sync; a video becomes searchable by what is said in it.

**AR9 — Outputs succeed or fail independently.** `snapshot.json` carries a status per output (`ok`, `failed` with the message, `skipped` with the reason). A failed PDF does not lose the snapshot, and `refresh(id, ['pdf'])` retries it. Acquisition failure is the only thing that fails `add`.

**AR10 — Resources go through one manifest.** The page engine records every resource in `manifest.json`: its absolute URL, final URL, local path, content type, byte count, ETag, Last-Modified, the `integrity` value the page declared for it if any, and its digest once AR-E7 exists. One rewrite function applies the manifest to the snapshot, to the readable article (Readability resolves the article's links against the original URL, so its image sources are absolute and map back to members), and to Markdown image links. Hyperlinks stay absolute to the live web. *Why:* three formats, one rule (AGENTS.md rule 13); and the manifest is exactly what Phase 2 needs to find a resource without fetching it (AR13).

**AR11 — Offline-first demo and tests.** The demo ships a sample library and archives local pages over `file:` URLs, reusing the Reader View demo's fictional articles (`test/demo/readability/articles/`) as its "web". Network archiving is optional. The media and gallery engines are tested against a local fixture site — a page with `<video>`/`<audio>` elements, an unencrypted HLS master and media playlists with fMP4 and TS segments, a gallery page — and progress is tested with a local large file, so CI needs no network (AR4v2).

**AR12v2 — Long work runs in a Lambda worker; progress is a callback from the transfer layer, which the job forwards as messages (USER, 2026-10-21; callback form USER, 2026-10-22).** An archive job is a `pn` run as a task. Every transfer it makes takes a `progress` callback (`io.fetch`, AR-E3v2; `io.exec`, AR-E5v2): the runtime calls it on the job's own stack, between parks, with the latest level, at most once per `progress_ms` (default 250). The job's callback turns that level into a message in the shape `{type: 'progress', tag, stage, received, total, rate}`, where `tag` identifies the job and `stage` is `fetch`, `assets`, `media`, `render` or `pack`, and hands it to the caller's `progress` callback (§8), which in the demo sends it on to the view. The source of each update is the lowest layer that knows it: curl's transfer callback for native downloads, the segment count and bytes for HLS media, the asset or image count for pages and galleries. The job reports its own stage changes the same way. Progress is a level, not an event: an update the callback has not yet seen is superseded by a newer one, so a fast download never queues work behind itself, and `send` keeps its S13.2.2 semantics because nothing in the runtime sends (AR-O11). Large files go straight to disk through a temporary file (`io.fetch {to}`), never through a Lambda string. In the demo, one archive service task, owned by the document (USER, 2026-10-21), receives job requests and runs up to N jobs as its own children; the view keeps the service's handle in its state to send it requests, and progress reaches the view as events (AR-E4). *Why:* the user requires downloads to run in Lambda workers with progress observable end to end. Tasks are the worker model Lambda has today: IO parks the task off the loop thread, so the window stays responsive while bytes arrive. CPU-heavy steps are the exception: Readability on a 1 MB page takes about two seconds and a PDF export is synchronous native work, and both would pause the window inside a cooperative task. They run in a child `lambda.exe` process through `io.exec` until thread workers (`mode: 'thread'`, E501 today) exist (AR-O12, resolved).

**AR13 — Phase 3 shares resources across pages and sites (USER, 2026-10-21; Phase 2 until 2026-10-23).** A library-wide resource store keeps each payload once, by digest, and a URL index records what was fetched where and when. Before a resource is downloaded, the page engine checks, in order: a fresh URL hit (reuse it); a stale URL hit with validators (send `If-None-Match` / `If-Modified-Since`, and reuse it on `304`); an `integrity` attribute whose digest is already in the store (reuse it with no request at all, since the page itself names the bytes it expects); otherwise download, digest, and store the payload once even when another URL already brought the same bytes. A snapshot ZIP then holds the page-specific members and references shared ones through `manifest.json`, as WARC's `revisit` records do. `export(id)` materializes a standalone ZIP when a snapshot leaves the library, and `gc()` drops payloads no manifest references. *Why:* fonts, scripts, logos and CDN libraries repeat across pages of a site and across sites; the integrity attribute turns some of that repetition into zero requests. Phase 1 keeps the manifest from the first capture, so Phase 3 can backfill the store from existing snapshots.

**AR14 — A batch is a crawl of depth 0; seeds come from lists, files, bookmarks and sitemaps (proposed, 2026-10-23).** `crawl(lib, seeds, options)` is the one mechanism, and `batch(lib, urls, options)` is `crawl` with `max_depth: 0`. Seeds are an array whose items are URLs or `{url, options}` (per-URL job options such as `kind`, `formats` or `tags`, as `job` takes them), or what `seeds(source, options)` reads from a path or URL (`parse_seeds` is its pure core, AR3): a text file of one URL per line (blank lines and `#` comments ignored); a browser's bookmark export (the Netscape bookmark HTML that every browser writes: each `<a href>`, with its folder path as tags); or an XML sitemap (`<urlset><url><loc>`, each `<lastmod>` kept, a `<sitemapindex>` followed to `max_sitemap_depth`, default 2). The source's kind comes from its content, not its name. A line or entry that is not an absolute http, https or file URL is listed in the crawl record's `rejected` with the reason and does not fail the crawl, in the spirit of AR9. Seeds are de-duplicated by normalized URL (§7), keep their order, and are all depth 0. *Why:* one engine for both requests gives one record, one progress shape, one stop and resume, and one set of limits — `max_pages`, `max_mb` and `max_time_s` bound a 5,000-line bookmark import as they bound a crawl. ArchiveBox's `add --depth=0|1` is the same idea.

**AR15 — Scope, rules and limits decide what a crawl takes (proposed, 2026-10-23).** A link discovered on a page at depth *d* is admitted, at depth *d*+1, when every test holds; the first that fails is the reason counted in the crawl record (AR18):

1. *Scheme* — `http` or `https`, or `file` when the seed it descends from is a `file:` URL (the offline fixtures, AR11).
2. *Depth* — *d*+1 ≤ `max_depth`.
3. *Scope*, measured from the seed it descends from: `prefix` (default) — the seed's scheme, host and port, and a path under the seed's directory (`https://ex.org/docs/intro` admits `https://ex.org/docs/…`), wget's `--no-parent`; `host` — the seed's host; `domain` — the seed's host without a leading `www.`, and its subdomains, matched as a `*.` host glob (§6), so no public-suffix list is needed because the seed names the domain; `any` — every host, accepted only with `max_depth` at most 2, so an unbounded crawl of the web cannot be configured by accident.
4. *Rules* — `include` and `exclude` are lists of match maps in the configuration's rule syntax (`{host, path, query, scheme}` globs, matched by `plan.rule_for`'s matchers, rule 13): a link must match some `include` when the list is given and no `exclude`.
5. *Trap guards* — at most `max_url_length` characters (2,048); no path segment repeated more than three times (`/a/b/a/b/a/b`); at most `max_per_path` (20) URLs that differ only in their query, which bounds calendars, faceted search and session parameters.
6. *Link hints* — `rel=nofollow` is honoured while `obey_nofollow` holds (default true).
7. *Kind* — the kind the URL alone decides (`plan.classify`) is in `follow_kinds` (default `[page]`), so a link to a PDF, an image or a video is recorded as an outlink of its page but not archived unless the crawl asks for that kind. A link whose kind only the response decides is fetched; a job started by a crawl carries `accept_kinds`, and a response of another kind ends the job after the first fetch as `skipped: kind`, with no snapshot.
8. *Robots* — robots.txt allows it (AR16).
9. *New* — its normalized URL is not in the crawl's seen set, which also holds the canonical URL of every page taken (AR17).

Limits end a crawl: `max_pages` (jobs issued, seeds and reused snapshots included; exact, because it is counted when a job is issued), `max_mb` (bytes downloaded by the crawl's jobs — first fetches, assets not taken from the crawl cache, media; soft, because jobs in flight finish, so a crawl may overshoot by at most the jobs in flight times one page's cap, `page.max_total_mb` plus `media.max_mb`), and `max_time_s` (wall time; jobs in flight are cancelled and go back to the frontier). The record names the reason the crawl ended: `frontier` (nothing left), `max_pages`, `max_mb`, `max_time`, `stopped` or `failed`; whatever was not taken stays in the saved frontier for `resume` (AR18). The frontier is breadth-first — seeds, then depth 1 in discovery order, and so on — and within a depth a job goes to the first entry whose host has a free slot (AR16). Defaults: `scope: prefix`, `max_depth: 2`, `max_pages: 100`, `max_mb: 1024`, no time limit; a batch has no page limit unless one is given, since its seeds are its pages (AR-O15). *Why:* Browsertrix's scope types are the established vocabulary; depth, pages, size and time are the limits the user asked for; the trap guards are what keep a breadth-first crawl of a real site from filling its page budget with one calendar.

**AR16 — Politeness: robots.txt, per-host pacing, retry (proposed, 2026-10-23).** *Robots.* For an http or https host a crawl fetches `/robots.txt` once, through the transfer layer, keeps it in the crawl folder, and reads it with `robots.ls` (pure) under RFC 9309: the group for the product token `LambdaArchive` (matched case-insensitively against `User-agent` lines), else `*`; `Allow` and `Disallow` with `*` and `$`, the longest match deciding and `Allow` winning a tie; `Crawl-delay` as that host's minimum delay. A 4xx answer allows everything; a 5xx answer or an unreachable host disallows the host for this crawl and is recorded. `obey_robots` (default true) applies to discovered links; seeds are archived regardless, because a URL the user names is the user's request (AR-O8v2). *Pacing.* At most `per_host_jobs` (default 1) of a crawl's jobs run on one host at a time, and successive job starts on one host are at least `per_host_delay_ms` apart (the existing setting, raised to `Crawl-delay` when robots.txt asks for more); a job's own asset downloads keep their existing pacing (`page.assets_per_host`). *Retry.* A job whose first fetch fails transiently — a timeout, a connection failure, or status 408, 429, 500, 502, 503 or 504 — goes back to the frontier up to `retries` times (default 2), after `retry_ms` × 2ⁿ with jitter (default 2,000 ms) or after the response's `Retry-After` when that is longer; a 429 or a 503 also doubles that host's delay for the rest of the crawl, up to `max_host_delay_ms` (60 s). Other 4xx statuses fail the page without retry. To tell these apart without parsing messages — an error value carries only a code and a message (`doc/Lambda_Error_Handling.md`) — a crawl's jobs run through `jobs.attempt`, which returns `{record}` or `{failure: {message, status, retry_after, transient}}` as a value; the facade's `job` keeps raising (AR9). *Why:* the former web-scrape proposal's E5 put retry and rate limits in Lambda, where `sleep`, `clock` and the worker pool suffice; robots obeyance is what makes a crawler welcome. The latency-driven half of an autothrottle is not needed for one user's archive.

**AR17 — A crawl reads a page's links from its stored capture (proposed, 2026-10-23).** The page engine records the page's hyperlinks in `manifest.json` as `links: [{url, rel?, text?}]`: from `<a href>`, `<area href>`, `<link rel=next|prev>`, and `<iframe src>` and `<frame src>`, resolved against the base (AR10), with the fragment dropped, http, https and file URLs only, once each in document order, `text` the first 80 characters of the link's text. `<link rel=canonical>` becomes the record's `canonical`. When the media, gallery or file engine archived a URL that had a page (`source.html`), the same function collects that page's links. The crawl reads the links of every page from the snapshot's `manifest.json` member — one deferred member read (S14.3.1v3) — whether the job has just written the snapshot or found an existing one (`on_duplicate: skip`), and from its `source.html` with the same function when the snapshot predates Phase 2 and its manifest has no `links`. *Why:* one source of links means a crawl through pages already in the library extends exactly as one that fetches them, an old library can be crawled from without re-fetching, and the outlinks stay in the capture for re-derivation (AR3) and for navigation between a crawl's pages (AR-O17). Snapshot HTML keeps absolute hyperlinks (AR10).

**AR18 — A crawl is a record of its own; its pages are ordinary snapshots (proposed, 2026-10-23).** A crawl lives in `<root>/crawls/<id>/` (§7), with an id formed like a snapshot's: UTC start time plus a slug of the first seed's host, or `batch` for a crawl of depth 0. `crawl.json` holds the seeds, the effective crawl settings, the status (`running`, `done`, `stopped`, `failed`), the reason it ended, counts (`discovered`, `archived`, `reused`, `failed`, `skipped`, `pending`), bytes downloaded, start and end times, the excluded links counted by reason (AR15's test names: `scope`, `rules`, `trap`, `nofollow`, `kind`, `robots`), and `rejected` seeds. `pages.jsonl` is append-only: an `issued` line when a job starts — `{url, normalized, depth, from}` — and an outcome line when it ends — `{normalized, status: archived | reused | failed | skipped, id?, reason?, bytes}` — so a crash loses nothing and `resume` re-queues an issued page with no outcome. `frontier.json` holds the entries not yet issued and, with `crawl.json`, is rewritten through a temporary file every `save_every` outcomes (default 20) and when the crawl ends. A snapshot a crawl creates carries `crawl: {id, depth, from}` as provenance; membership is the crawl's page table, so a snapshot reused by later crawls belongs to each. `index.json` caches the crawl summaries beside the snapshot records, rebuilt from `crawls/*/crawl.json` as AR7 rebuilds snapshots, and `snapshots(lib, {crawl: id})` and `search(…, {crawl: id})` filter by a crawl's pages. Removing a snapshot leaves its line in the page table, shown as removed; `remove_crawl(lib, id, {snapshots: true})` also removes the snapshots the crawl created and no other crawl reused. *Why:* AR7's rule — records on disk are the truth and the index a cache — carries over; append-only lines keep a 10,000-page crawl from rewriting its whole record on every page; and since each page is a normal snapshot, search, the viewer and `refresh` need no crawl case.

**AR19 — Crawls run in the job service; one worker pool, two levels of progress (proposed, 2026-10-23).** The service (AR12v2) gains `{op: 'crawl', seeds, options, tag}` and `{op: 'resume', id, options?, tag}`; `{op: 'cancel', tag}` on a crawl's tag stops it (status `stopped`, frontier saved, the jobs in flight cancelled and returned to the frontier). Each crawl has a coordinator task, the service's child, that owns the frontier, the seen set, the per-host clocks and the counters as its own variables (S1.4: nothing is shared), and draws its jobs from the service's workers: it sends `add` requests marked with its tag, never more than its hosts' free slots allow, so the frontier and its limits stay in the coordinator and the service's queue holds only a few of its jobs. The queue serves requests the user made directly first and the crawls' jobs after them, round-robin across crawls, so a large crawl never holds back a URL typed into the demo. The service passes each finished crawl job to its coordinator, which records the outcome, reads the page's links (AR17) and admits new entries (AR15). Reports: each job's AR12v2 messages carry `crawl: tag` as well as their own tag; the coordinator reports `{type: 'crawl', tag, id, status, counts, bytes, depth}` at most once per `progress_ms`, and `{type: 'crawl_done', tag, crawl}` at the end. In scripts, `archive.crawl(lib, seeds, options)` starts a private service, sends it the crawl, forwards its reports to `options.progress`, and returns the crawl record when the crawl ends, so the script path and the demo path are one implementation, as `add` and `job` are. *Why:* one pool caps the work the machine does whatever mix of single URLs and crawls is running; the coordinator is the actor the former web-scrape proposal leaned toward for crawler state; cancellation reaches transfers through S13.3.1v2's scopes as it already does for jobs.

**AR20 — Within a crawl, an asset is downloaded once (proposed, 2026-10-23).** Pages of one site share stylesheets, fonts, logos and icons, so until Phase 3's store exists (AR13) a crawl keeps a cache in `crawls/<id>/cache/`, keyed by the SHA-256 digest of the asset's normalized URL: the payload as downloaded (stylesheets before their rewrite) and its manifest fields (`final_url`, `content_type`, `bytes`, `etag`, `last_modified`). The crawl passes the folder to its jobs; the page engine looks an asset up there before downloading it and writes what it downloads there with `io.fetch {to}`, whose `.part`-then-rename makes two jobs racing on one asset harmless. Each snapshot ZIP still holds its own copy — storage de-duplication is Phase 3's — and bytes taken from the cache do not count against `max_mb`. The cache is deleted when a crawl ends with an empty frontier and kept while it can be resumed. Phase 3 puts the library's URL index and store behind the same lookup function, so the page engine keeps one call site (rule 13). *Why:* without it a 200-page crawl downloads the site's stylesheet 200 times, which is impolite to the server and spends the size budget on repetition. Whether to bring AR13 forward instead is AR-O16.

---

## 5. Architecture

```
  test/demo/archive  (lambda demo archive)                   user scripts
   view ── on job(evt) ◀── dom.address ◀── service task ◀────┐   │
        └─ send(state.service, {op:'add', url}) ─▶ (AR-E4)     │   │
  ┌──────────────────────────────────────────────────────────┴───┴──────┐
  │ archive.ls  facade: library · add · job · service · refresh ·      │
  │             remove · snapshots · search · doctor · plan · export · gc│
  │             Phase 2: crawl · batch · resume · seeds · crawls ·       │
  │             parse_seeds · crawl_pages · remove_crawl                 │
  ├──────────────────────────────────────────────────────────────────────┤
  │ L4 COLLECT   crawl.ls    coordinator: frontier, seen set, limits,   │
  │  (Phase 2,               per-host slots, retry, pages.jsonl (AR19)  │
  │   pn; scope  scope.ls    admission tests, trap guards (AR15)        │
  │   robots     robots.ls   RFC 9309 groups and matching (AR16)        │
  │   seeds fn)  seeds.ls    URL lists, bookmark HTML, sitemaps (AR14)  │
  │              links.ls    a page's hyperlinks and canonical (AR17)   │
  ├──────────────────────────────────────────────────────────────────────┤
  │ L3 LIBRARY   store.ls   layout, ids, staging, pack, snapshot.json,  │
  │                         index cache, duplicates, crawl records      │
  │              search.ls  io.text_search + metadata filters           │
  │              shared.ls  Phase 3: resource store, URL index (AR13)   │
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
  │              media.ls    media discovery on a page; direct media;   │
  │                          HLS download (hls.ls parses playlists)     │
  │              gallery.ls  a page's images above a size threshold     │
  │              transfer.ls io.fetch {to, progress}, io.exec {progress} │
  │              progress.ls message shapes, stage reports               │
  ├──────────────────────────────────────────────────────────────────────┤
  │ L0 CONFIG    config.ls   defaults, merge, Config type, rules        │
  │              platforms.ls built-in platform table (data)            │
  └──────────────────────────────────────────────────────────────────────┘
        │                │                 │                  │
  io.fetch {to,progress}  lambda.doc.readability  radiant.export_document  output(…,'zip')
  (curl off the loop;     format(…,'markdown')    load/resize/save         io.text_search
   callback, AR-E3v2)     lambda.sys.parse (html, json, vtt text)
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

**The page engine.** Fetch the URL (status, final URL and content type from AR-E1), decode by the declared charset, and parse. Collect resources: `<link rel=stylesheet|icon>`, `<img src|srcset>`, `<picture><source srcset>`, `<video poster>`, inline `style` and `<style>` `url(…)`, and, inside each downloaded stylesheet, `url(…)` and `@import` (recursively, bounded). Resolve each against the final URL or `<base href>` with `url_resolve`, download each once, and record it in the manifest (AR10). Downloads run as child tasks of the job, a few at a time per host, each writing straight to its staged file. Stylesheet text is rewritten with string patterns, since there is no CSS writer and none is needed. Limits come from configuration: total bytes, per-asset bytes, count and timeout; an asset over a limit stays a remote link and is listed in the manifest. `srcset` parsing reuses the Readability package's candidate pattern, promoted to a shared module rather than copied (rule 13). In Phase 2 the same walk records the page's hyperlinks and canonical URL in the manifest (AR17) and consults a crawl's asset cache before each download (AR20).

**The media engine** (`media.ls`, `hls.ls`, `vtt.ls`; AR4v2). *Discovery:* a direct media URL (by extension or response content type) is the medium itself; otherwise the page is fetched and parsed, and candidates are collected in order of preference — `<video>`/`<audio>` `src` and their `<source>` children (by `type` and `media` attributes), `og:video`/`og:audio` (`:secure_url`, `:type`), `<link rel=enclosure>`, and JSON-LD `VideoObject`/`AudioObject` `contentUrl`/`embedUrl`; a candidate ending in `.m3u8` or typed `application/vnd.apple.mpegurl` is an HLS stream, a candidate with a media extension or type a progressive file. The page's title, description, author, date and poster (`og:image`, `poster`) become the media record; `<track kind=subtitles|captions>` give subtitle URLs. *Download:* a progressive file streams to `media/<slug>.<ext>` through `io.fetch {to, progress}`. An HLS master playlist is parsed (`hls.ls`, pure): variants (`#EXT-X-STREAM-INF`: bandwidth, resolution, codecs, audio/subtitle groups) are ranked by height under `media.max_height`, then bandwidth; a muxed variant is preferred, and a variant whose audio lives in a separate rendition group is taken only when its segments are fMP4 and the audio rendition's too, in which case both are downloaded as two files (`media/<slug>.video.mp4`, `media/<slug>.audio.m4a`) and the media page plays them together; otherwise the next muxed variant is used. The media playlist's segments (`#EXTINF`, `#EXT-X-BYTERANGE`, `#EXT-X-MAP`, `#EXT-X-DISCONTINUITY`) are fetched in order, a few at a time, each into memory bounded by `media.max_segment_mb`, and appended to the output with `output(bytes, path, {mode: 'append'})`; progress reports segments done over total and bytes. A playlist with `#EXT-X-KEY` other than `NONE` is skipped until AR-E9's decrypt exists. Subtitle renditions and `<track>` files are fetched as WebVTT and `vtt.ls` turns the cues into the transcript for `text.txt` and the media page. Nothing is muxed or transcoded: the stored file is the container the server sent (fMP4 or TS), which Radiant's `<video>` plays on macOS when the system player does; the record names the container so the viewer can say when it cannot.

**The gallery engine** (`gallery.ls`; AR4v2). The page is fetched and parsed; image candidates are `<img>` (the largest `srcset` candidate by width descriptor, else `src`), `<picture><source>` candidates, `<a href>` pointing at an image file (the full-size image behind a thumbnail, preferred over the thumbnail inside it), and `og:image`. Each distinct URL is downloaded once into `media/`, a few at a time with per-host delay; an image under `gallery.min_pixels` (measured after download with `load`) or over `gallery.max_image_mb` is discarded and listed as skipped. Captions come from the nearest `<figcaption>`, then `alt`, `title`, the link's `title` and the link's text; the page's title and author head the gallery page. Thumbnails come from `load`/`resize`/`save`. Progress reports images done over total. A page with no qualifying image is archived as a page.

**Transfer layer.** `transfer.ls` wraps `io.fetch` for pages and stylesheets (parsed in memory) and `io.fetch` with `to:` for everything else (written to disk) with the configured User-Agent, timeout, per-host delay and the job's progress callback. In Phase 2 it also consults a crawl's asset cache (AR20); cookies (AR-E10) would slot in here, and nothing else does (AR-O6).

**The crawler (Phase 2; AR14–AR20).**

```
seeds ─▶ seeds.ls (list · text · bookmarks · sitemap) ─▶ normalize, de-duplicate   [fn]
      ─▶ crawls/<id>/  crawl.json · pages.jsonl · frontier.json · robots/ · cache/  [pn]
      ┌──────────────────────── coordinator task (crawl.ls) ───────────────────────┐
      │ frontier (breadth-first) ─▶ pick an entry whose host has a free slot        │
      │   ─▶ robots.txt for a new host (robots.ls) ─▶ wait for the host's delay      │
      │   ─▶ issue: pages.jsonl ← issued; send {op:'add', url, crawl: tag} ─▶ service│
      │ service ─▶ worker ─▶ jobs.attempt(lib, url, {accept_kinds, cache}) ─▶ outcome│
      │   ─▶ pages.jsonl ← outcome; counters, bytes; retry or fail on failure        │
      │   ─▶ links of the snapshot (manifest.json member, links.ls) ─────────────────┤
      │   ─▶ admit each: scheme · depth · scope · rules · traps · nofollow · kind ·  │
      │                  robots · new  (scope.ls) ─▶ frontier | excluded[reason]++   │
      │ until the frontier is empty or a limit holds (max_pages · max_mb · max_time) │
      └──────────────────── crawl.json, frontier.json saved; crawl_done ───────────┘
```

The coordinator is the only task that touches the crawl's files, so they need no locking; jobs write only their own staging folders and the shared asset cache, whose writes are atomic renames (AR20). A crawl started with `on_duplicate: new` or `replace` re-captures pages already in the library; the default, `skip`, makes a crawl through an archived site cost one ZIP member read per known page (AR17).

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
  render_timeout_s: 300                  # the worker that derives the renderings (AR-O12)
  page:
    max_assets: 400
    max_asset_mb: 25
    max_total_mb: 200
    assets_per_host: 4
  pdf:
    source: readable                     # readable | snapshot
    pagination: true
    width: 794                           # page size in CSS pixels: A4 at 96 dpi
    height: 1123
  thumbnail: true                        # thumb.png: a 1280×800 viewport drawn at 400×250

media:                                   # the native media engine (AR4v2)
  max_height: 1080                       # the tallest HLS variant taken; never upscaled
  max_mb: 2048
  max_segment_mb: 64
  subtitles: [en]
  prefer: [fmp4, ts]
  segment_retries: 2
  timeout_s: 3600

gallery:                                 # the native gallery engine (AR4v2)
  min_pixels: 40000                      # 200×200: icons and spacers are not gallery images
  max_images: 500
  max_image_mb: 50
  timeout_s: 1800

crawl:                                   # Phase 2: batches and crawls (AR14–AR20)
  scope: prefix                          # prefix | host | domain | any (any needs max_depth ≤ 2)
  max_depth: 2                           # a batch is max_depth 0
  max_pages: 100                         # jobs issued; a batch has none unless given
  max_mb: 1024                           # bytes downloaded; soft (AR15)
  max_time_s: null                       # wall time
  follow_kinds: [page]                   # kinds a link may be archived as
  include: []                            # match maps in the rules' syntax; empty admits all in scope
  exclude:
    - { query: ["*replytocom=*", "*share=*"] }
    - { path: ["*/wp-login.php", "*/feed", "*/feed/"] }
  obey_robots: true                      # discovered links only; seeds are archived regardless (AR-O8v2)
  obey_nofollow: true
  per_host_jobs: 1
  retries: 2
  retry_ms: 2000                         # doubled per retry, with jitter; Retry-After when longer
  max_host_delay_ms: 60000               # the cap for 429/503 slow-down
  max_url_length: 2048
  max_per_path: 20                       # URLs differing only in query, per path
  max_sitemap_depth: 2
  save_every: 20                         # outcomes between crawl.json / frontier.json saves
  cache: true                            # the crawl's asset cache (AR20)

rules:                                   # first match wins
  - match: { host: ["youtube.com", "*.youtube.com", "youtu.be"] }
    kind: video                          # archived as a page with remote media links until AR-O14 is ruled
    media: { subtitles: [en, zh-Hans] }
  - match: { host: "*.bandcamp.com" }
    kind: audio
  - match: { host: ["*.flickr.com", "www.pixiv.net"] }
    kind: gallery
  - match: { host: "*.substack.com" }
    formats: [html, readable, markdown, pdf]
  - match: { path: ["*.pdf", "*.epub", "*.zip"] }
    kind: file
```

**Matching.** `host` and `path` are glob lists (`*` within a label or segment; a leading `*.` matches the domain and its subdomains); `scheme` and `query` are optional. All the fields in one `match` must hold. A rule without `kind` only changes settings, and the kind is still classified (AR2v2). A crawl's `include` and `exclude` entries are match maps of the same syntax, matched by the same functions (AR15).

**Crawl settings and rules.** The `crawl` block applies to every crawl; a `crawl(…)` or `batch(…)` call's options override it, and a seed's own options override the call's for the jobs it and its descendants run (formats, kind, tags — never the crawl's limits). Rules still apply per URL, so a site rule that adds `pdf` to `formats` applies to the pages a crawl takes from that site. The `per_host_delay_ms` default (500) paces a crawl's job starts per host as it already paces a page's asset downloads; the demo's configuration sets it to 0 for its local files.

**Kinds and engines.**

| Kind | Engine (all Lambda, AR4v2) | Primary files | Default outputs |
|---|---|---|---|
| `page` | `page.ls` | `snapshot.zip`: `index.html`, `source.html`, `assets/`, `manifest.json` | html, readable, markdown; pdf optional |
| `video`, `audio` | `media.ls` (+ `hls.ls`, `vtt.ls`) | `media/<slug>.<ext>` (progressive, fMP4 or TS), `media/<slug>.<lang>.vtt`, `media/info.json`, poster | media page, transcript text; readable and markdown from description and transcript |
| `image` | `file.ls` | `media/<name>`, thumbnail | image page |
| `gallery` | `gallery.ls` | `media/*`, `media/info.json` (captions, sources) | gallery page |
| `file` | `file.ls` | `media/<name>` | file card; a PDF file is also viewable directly |

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
  crawls/                      Phase 2 (AR18)
    20261023T091500Z-ledger-example/
      crawl.json               the crawl's record (truth): seeds, settings, status, counts
      pages.jsonl              append-only: one issued line and one outcome line per page
      frontier.json            entries not yet issued, for resume
      robots/                  the robots.txt fetched per host
      cache/                   the crawl's asset cache, until the frontier is empty (AR20)
  resources/                   Phase 3: shared payloads by digest + URL index (AR13)
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

**`manifest.json`** (inside the ZIP) lists one entry per resource: `{url, final_url, path, content_type, bytes, etag, last_modified, integrity, digest}`, with `digest` absent until AR-E7 exists. From Phase 2 it also holds `links: [{url, rel?, text?}]` and `canonical` (AR17). In Phase 3 an entry may carry `shared: true` and no member, the payload living in `resources/` under its digest (AR13).

**Phase 2 fields of `snapshot.json`.** `canonical` (the page's `<link rel=canonical>`, resolved), `links` (the number of outlinks in the manifest), `fetched_bytes` (bytes this capture downloaded, which a crawl's `max_mb` counts), and, on a snapshot a crawl created, `crawl: {id, depth, from}`.

**`crawl.json`.**

```json
{
  "format": 1,
  "id": "20261023T091500Z-ledger-example",
  "kind": "crawl",
  "seeds": [{ "url": "https://ledger.example/news/", "options": {} }],
  "settings": { "scope": "prefix", "max_depth": 2, "max_pages": 100, "max_mb": 1024, "...": "..." },
  "status": "done",
  "ended_by": "max_pages",
  "started": "2026-10-23T09:15:00Z",
  "ended": "2026-10-23T09:31:42Z",
  "counts": { "discovered": 412, "archived": 91, "reused": 9, "failed": 2, "skipped": 3, "pending": 307 },
  "bytes": 48213992,
  "excluded": { "scope": 1210, "rules": 14, "trap": 40, "nofollow": 6, "kind": 52, "robots": 3 },
  "rejected": [],
  "hosts": { "ledger.example": { "robots": "ok", "delay_ms": 500 } },
  "log": ["…"]
}
```

`kind` is `batch` for a crawl of depth 0. `status` is `running`, `done` (the frontier emptied or a limit held), `stopped` (cancelled) or `failed` (the crawl itself could not continue, such as an unwritable library); a crawl whose frontier is not empty can be resumed whatever its status.

**Duplicates.** URLs are normalized (lowercase scheme and host, default port removed, fragment dropped, tracking parameters such as `utm_*` dropped by a configurable list). `on_duplicate: skip` returns the existing snapshot; `new` adds a dated version, and versions of one URL group together in the library; `replace` archives anew and removes the old folder after the new one is in place. A crawl applies the same policy to every page it issues; with `skip` an existing snapshot is `reused` and its stored links extend the crawl (AR17).

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
    let lib = archive.library("temp/archive", {config: input("archive.yaml")^})^
    let logger = start(log_progress)
    let job = start(archive.job, [lib, "https://ledger.example/news/shade-by-design",
                                  {formats: ['html', 'readable', 'markdown', 'pdf'],
                                   progress: pn (m) => { send(logger, m) ^ { null } }, tag: "job-1"}])
    let snap = wait(job)^
    send(logger, {type: 'stop'})^
    print(snap.id, " ", snap.outputs.pdf.status, "\n")

    for (h in archive.search(lib, "\"street trees\" -opinion", {kind: 'page', limit: 20})^)
        print(h.snapshot.title, " — ", h.snippet, "\n")
}
```

| Function | Effect | Result | Notes |
|---|---|---|---|
| `library(root, options?)` | `pn` | `Library^` | `options.config` is a configuration value; without it the package defaults apply. Reads the index cache; creates the layout when missing. (Named `library`, not `open`: `open` is a Lambda keyword, S16.10.1v2) |
| `plan(url, config, options?)` | `fn` | `Plan^` | classification and engine arguments, no IO (AR3) |
| `job(lib, url, options?)` | `pn` | `Snapshot^` | one archive job: acquire, derive, pack, write. `options.progress` is a callback (`fn` or `pn`) that receives AR12v2 messages, `options.tag` labels them. Meant to run as a task; raises only when acquisition fails (AR9) |
| `add(lib, url, options?)` | `pn` | `Snapshot^` | `job` run in the caller's task, for scripts that do not need concurrency |
| `service(lib, notify)` | `pn` | `null^` | a long-running task: receives `{op: 'add', url, options}` and `{op: 'cancel', tag}`, runs up to `jobs` jobs as children, and reports `progress`, `done` and `failed` messages to `notify`. Phase 2 adds `{op: 'crawl', seeds, options, tag}` and `{op: 'resume', id, options?, tag}`, with `crawl` and `crawl_done` reports; `cancel` on a crawl's tag stops the crawl (AR19) |
| `refresh(lib, id, formats?)` | `pn` | `Snapshot^` | re-derive outputs from the stored capture, no network, under the library's current configuration and the formats chosen at capture; `formats` names the outputs to redo; rewrites the ZIP and replaces the folder whole |
| `remove(lib, id)` | `pn` | `bool^` | deletes the folder, updates the index |
| `snapshots(lib, filter?)` | `fn` | `Snapshot[]` | over the library value: kind, host, tag, date range, sort (`list` is a keyword) |
| `search(lib, query, options?)` | `pn` | `Hit[]^` | AR8; `Hit` = `{snapshot, score, snippet}` |
| `doctor(lib)` | `pn` | `map` | configuration problems, library statistics (snapshots and bytes per kind), stale staging folders, snapshots whose files are missing |
| `crawl(lib, seeds, options?)` | `pn` | `Crawl^` | Phase 2 (AR14–AR19): `seeds` is a URL, an array of URLs or `{url, options}`, or `seeds(…)`'s result; `options` are the `crawl` settings plus job options for every page, and `progress` and `tag` as for `job`. Runs a private service; returns the crawl record when the crawl ends. Raises only when the crawl cannot start (no valid seed, invalid settings, unwritable library) |
| `batch(lib, urls, options?)` | `pn` | `Crawl^` | `crawl` with `max_depth: 0` |
| `resume(lib, id, options?)` | `pn` | `Crawl^` | continues a crawl from its saved frontier; `options` may raise its limits (`max_pages`, `max_mb`, `max_time_s`, `max_depth`) but not change its scope or rules |
| `seeds(source, options?)` | `pn` | `Seed[]^` | reads a URL list, a bookmark export or a sitemap from a path or URL (AR14), fetching a sitemap index's children; `options.kind` forces the source's kind |
| `parse_seeds(text, options?)` | `fn` | `Seed[]^` | the same over text already read, with no IO (a sitemap index's children are returned as `{sitemap: url}` entries for the caller to read) |
| `crawls(lib, filter?)` | `fn` | `Crawl[]` | the crawl summaries in the library value: status, kind, host, date range |
| `crawl_pages(lib, id)` | `pn` | `Page[]^` | the crawl's page table from `pages.jsonl`: one row per issued URL with depth, origin, status, snapshot id and reason |
| `remove_crawl(lib, id, options?)` | `pn` | `bool^` | deletes the crawl's folder; `{snapshots: true}` also removes the snapshots it created that no other crawl reused |
| `export(lib, id, path)`, `gc(lib)` | `pn` | | Phase 3: standalone ZIP; drop unreferenced shared payloads |
| `readable(capture)`, `markdown(capture)`, `snapshot_html(capture)` | `fn` | `string^` | the pure derivations, usable on their own |

**Phase 2 in a script.**

```lambda no-run
import archive: lambda.archive

pn main() {
    let lib = archive.library("temp/archive")^
    // a site: from the entry page, two links deep, at most 50 pages or 200 MB
    let c = archive.crawl(lib, "https://ledger.example/news/",
        {max_depth: 2, max_pages: 50, max_mb: 200, exclude: [{path: "*/tag/*"}],
         progress: pn (m) => { if (m.type == 'crawl') print(m.counts.archived, " pages, ", m.bytes, " bytes\n") }})^
    print(c.status, " (", c.ended_by, "): ", c.counts.archived, " archived, ", c.counts.pending, " left\n")
    // the rest of the site later, with a larger page budget
    let more = archive.resume(lib, c.id, {max_pages: 200})^
    // a browser's bookmarks, each URL its own snapshot, tagged by folder
    let marks = archive.batch(lib, archive.seeds("temp/bookmarks.html")^)^
    print(marks.counts.archived, " bookmarks archived, ", len(marks.rejected), " rejected\n")
}
```

`Library` is a value: the root, the merged configuration and the records read at `library` (S1.4). Procedures that change the library return the new record, and a caller that holds a `Library` folds the record in with `with_record(lib, record)`; the demo keeps the listing in view state and replaces it on each `done` event. `problems(config)` lists a configuration's faults without opening anything.

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

**The worker wiring (AR12v2, AR-E4).** The view's `on load` handler starts `archive.service(lib, notify)` owned by the document, so the service outlives the handler, and assigns its handle to the view's state; later handlers reach the service only through that handle (USER, 2026-10-21). The Archive action sends `{op: 'add', url, options}` to it and **Cancel** sends `{op: 'cancel', tag}`. `notify` is the view's event address (AR-E4b): each message the service sends to it arrives as `on job(evt)`, with the message as `evt.detail`, and the model folds a `progress` message into the job row, a `done` message into the listing, a `failed` message into the row's error. Closing the window discards the document, which cancels the service; the service's own scope then cancels its jobs and their transfers. Downloads and engines park their tasks off the loop thread, so the window keeps painting; Readability and PDF rendering run in a child `lambda.exe` through `io.exec` (AR12).

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

**Batches and crawls (Phase 2).** The add bar gains a mode: *Page* (today's behaviour), *Batch* and *Crawl*. *Batch* opens a box for several URLs, one per line, and an **Import…** action that reads a text file, a bookmark export or a sitemap through `seeds`; *Crawl* shows the entry URL with scope, depth, page and size fields filled from the configuration's `crawl` block. Either sends `{op: 'crawl', seeds, options, tag}` to the service. The jobs panel shows a crawl as one row — its counts, bytes, the depth it has reached and, when `max_pages` is set, a bar of pages issued over the limit — that expands to the rows of its jobs in flight; **Stop** sends `cancel`, and a crawl with pages left shows **Resume**. The library rail gains a *Crawls* section listing crawls with their status and page counts; choosing one filters the list to its pages, and its Info view shows the crawl record and the page table (URL, depth, origin, status, reason), with the excluded links counted by reason. Navigating from one archived page to another page of the same crawl inside the viewer is AR-O17.

```
┌──────────────────────────────────────────────────────────────────────────┐
│ (Crawl ▾) [ https://ledger.example/news/ ] prefix▾ 2 deep 100 pp 1 GB ⏎  │
├────────────┬─────────────────────────────┬───────────────────────────────┤
│ Library    │ Jobs                        │ Crawl · ledger.example        │
│  All   142 │ ▼ ledger.example  crawl     │  done (max_pages) · 16 min    │
│ Crawls     │   91 archived · 9 reused    │  archived 91 · reused 9       │
│  ledger ✓  │   depth 2 · 46 MB           │  failed 2 · skipped 3         │
│  bookmarks │   ████████████░░ 100/100    │  excluded: scope 1210 · …     │
│            │   ▶ /news/tide-tables  71%  │  url · depth · from · status  │
│            │   [Stop]                    │  [Resume] [Delete crawl]      │
└────────────┴─────────────────────────────┴───────────────────────────────┘
```

**Checks.** A model golden in `test_lambda_gtest` (`tests_archive_model`): planning against the demo configuration, outputs derived from fixture captures, ZIP packing and reading back, the index rebuild, filters, and search over the sample library. A progress test that downloads a large local fixture through a throttled source and checks that progress messages arrive in order, are rate-limited, never fill the mailbox, and end at the file size. A native replay (`tests/archive.json`) that archives a sample page, watches its job row complete, searches it, switches tabs, re-runs an output and deletes a snapshot. The media and gallery engines are checked against the local fixture site: HLS fMP4 and TS streams reassembled byte-for-byte, a `<video>` page, a gallery page with mixed image sizes (AR11). Phase 2 adds a crawl fixture site over `file:` URLs (§10, M6–M8) and a replay that crawls it from the demo, stops, resumes and filters the list by the crawl.

---

## 10. Phases and milestones

**Phase 1 — archiving with progress.**

| Milestone | Delivers | Acceptance |
|---|---|---|
| **M0 Enablers** | AR-E1 (full response, off the loop thread), AR-E2 (request options), AR-E3v2 (`io.fetch {to, progress}`: to disk, progress callback), AR-E5v2 (`io.exec {progress}`); E1 and E2 of the former web-scrape proposal | A task reads `status`, `headers` and the final URL; a large local download keeps resident memory flat; progress callbacks arrive in order, rate-limited, and end at the file size while a second task keeps running; `io.exec` delivers the latest line of a slow printer |
| **M1 Pages** | configuration and defaults, `plan`, page engine, manifest, snapshot ZIP, readable HTML, Markdown, `text.txt`, store, `open`/`add`/`job`/`list`/`remove` | The Reader View demo's four pages archive over `file:` URLs; goldens for `plan`, ZIP members and `manifest.json`; the snapshot opens from the ZIP with no network |
| **M2 Renderings and search** | PDF and thumbnail from staged files, `refresh` (ZIP rewrite), `search` | PDF and PNG for the sample pages; search finds a phrase, a prefix and a negation with snippets; a forced PDF failure leaves the snapshot `ok` with `pdf: failed` |
| **M3 Media engines** | native media engine: direct files, page media discovery, HLS (fMP4 and TS, variant choice, byte ranges, init segments) with segment progress, WebVTT transcripts; native gallery engine; media, gallery, image and file pages; `doctor` (AR4v2) | Goldens over the local fixture site: an HLS fMP4 and a TS stream reassembled to the fixture's bytes, a `<video>` page, a direct `.mp3`, a gallery with mixed sizes; progress per segment and per image; one recorded manual run against a public-domain HLS stream and a public gallery page |
| **M4 Service and demo** | AR-E4 (document-owned tasks per S13.3.1v2; task-to-view events after AR-O13), `service`, `lambda demo archive` with job rows, browse, filter, search, view, re-run, delete | Archiving a large local file keeps the window responsive and the bar moving; model golden, progress test and native replay pass; catalog entry |
| **M5 In-place viewing** | AR-E8, replacing the view cache | Snapshot and reader pages, images and stylesheets load from `snapshot.zip` members |

**Implementation notes (M0–M2, 2026-10-22).**

- *One derivation path.* A job writes the capture — `source.html`, `manifest.json`, `assets/` — to `.staging/<id>/` and runs the worker: `lambda run lmd/package/archive/worker.ls` through `io.exec`, the executable being `sys.proc.self.argv[0]` and the request's path passed in `LAMBDA_ARCHIVE_REQUEST`, because `lambda run` takes no script arguments. The worker derives `index.html`, `readable.html`, `article.md`, `text.txt`, `page.pdf` and `thumb.png` beside the capture, prints `step <i> <n> <name>` before each (forwarded as `render` progress through AR-E5v2), and writes `derived.json` with the record's fields and a status per output. `refresh` unpacks the ZIP into a staging folder and runs the same worker, so add and refresh cannot drift. A worker that fails leaves every requested output `failed` with its message; the capture is packed regardless (AR9).
- *Readable only when readerable.* The readable page and Markdown are produced when `readability.readerable(doc)` holds, the rule the Reader View demo uses; a home page or a stub keeps its snapshot and text and records `readable: skipped` (“no readable article”).
- *The index is updated from the file.* A job upserts its record into `index.json` as it is on disk, not as the caller's `Library` value saw it, so jobs started from one value keep each other's records.
- *Runtime fixes found on the way.* An imported `pub type` string pattern read the importer's type list (`const_pattern: invalid index`) on both tiers; `print` is now line-buffered on pipes so a Lambda child's lines reach `io.exec` progress before it exits; a root task's wait no longer fails with E502 after 5 s while a timer, an offloaded job or a child process will still wake it (the end-of-run drain keeps its bound, `Lambda_Design_Concurrency` §12.4).
- *Known Radiant cost.* A repeated SVG background is drawn through ThorVG once per tile (`radiant/render_background.cpp`), so the M1 fixture page's tiled 64×32 background takes about 12 s to thumbnail; `pkg_pages` therefore turns thumbnails off and `pkg_render` renders the Reader View pages.

**Implementation notes (M3, 2026-10-22).**

- *One first fetch.* Every job fetches its URL once, to `.staging/<id>/fetched`, with `fetch` progress; the response's content type decides a kind the URL could not (AR2v2), and the engine its kind names (`plan.ENGINE_OF`) takes that file: the page engine reads it as HTML, the media engine as the medium, its playlist or a page naming one, the gallery engine as the page, the file engine moves it into `media/`. No engine fetches the URL a second time.
- *Records of the other kinds.* An engine writes `media/info.json` — `{kind, title, description, author, site, published, url, page_url, poster, files: [{path, role, container, bytes, source, stream?, width?, height?, caption?, thumb?}], subtitles: [{lang, label, path, source}], remote: [{url, reason}], skipped?}` — and, when there was a page, `source.html`. The worker derives the same outputs from it: `index.html` is the media page (players with `<track>`s, description, transcript, remote links), the gallery grid, the image page or the file card; readable and Markdown exist for video and audio (description and transcript) and are `skipped` for the rest; `text.txt` holds the record's fields and the transcript or the captions; `thumb.png` comes from the poster or first image through `load`/`save`, else from drawing the page. `snapshot.json` carries `media` (a summary of the files), `remote` and, for a page that was meant to be media or a gallery, `note` (why it is a page).
- *Media choices.* A page's candidates are taken in the order of §5; among a `<video>`'s sources those the system player plays (`video/mp4`, `video/quicktime`, `audio/mp4`, `audio/mpeg`, `audio/aac`) and streams come first, the others become `alternative source` remote links. `media.prefer` is an allow-list of containers in order. A subtitle rendition (a playlist of WebVTT segments) is joined under one header. An encrypted stream, a player URL (`og:video` pointing at an embed page) or a failed download leaves the page archived as a page with the reason in `note` and the media in `remote` — the AR-O14 proposal, implemented as proposed until it is ruled.
- *Runtime support added.* A local `io.fetch` honours a single `Range: bytes=a-b` (206, `content-range`; 416 past the end), so `#EXT-X-BYTERANGE` works over `file:` URLs; HLS playlists (`*mpegurl`) are text bodies; the MIME table knows `.m3u8`, `.m4s` and `.vtt` (`.ts` stays TypeScript, and segments' content types are never trusted). The JIT lowered `x == w` as a null test when `w` was a `var` initialised to `null` and assigned in a loop — the HLS engine rewrote the init map before every segment — fixed in `mir_is_null_literal_node` (`test/lambda/proc/var_null_loop_compare.ls`).
- *Not done.* The recorded manual run against a public HLS stream and gallery page needs the network and was not made.

**Implementation notes (M4, 2026-10-22).**

- *Document-owned tasks (S13.3.1v2).* `start(…, {owner: 'document'})` is accepted by the front end (the literal symbol `'document'` only), lowered on both tiers to `lambda_task_start_document`, and refused at run time outside a document (error 312). Radiant installs the probe that names the running document and releases a document's tasks in `free_document` while its state and DOM exist: cancel, then join, bounded at 5 s. A document-owned task's descendants belong to the document, and a script run's end-of-run drain does not wait for them — a viewer `<iframe>` loading a document otherwise stalled 5 s on the archive service.
- *The service.* `archive.service(lib, notify)` (`service.ls`) is an actor with `defaults.jobs` worker tasks started at its own level (so S13.3.1v2's block-exit join never serializes jobs); each job runs as a worker's child, so `{op: 'cancel', tag}` reaches its transfers and its worker process. Requests: `add`, `refresh`, `cancel`, `stop`; reports: `queued`, each job's `progress`, `done` with the snapshot, `failed` with the message (`cancelled` for a cancel). A job's duplicate check and index update read `index.json` on disk, so the service's `Library` value never goes stale in a way that matters. A cancelled transfer or worker raises 319 instead of being recorded as a failed asset or output, and a job's staging folder is removed on any failure from one place; two jobs in the same second get distinct staging folders.
- *The demo.* `lambda demo archive` (`test/demo/archive/`): add bar with kind and formats, search, library rail with kind and site counts, list with thumbnails and badges, job rows with progress and Cancel, and a viewer with Snapshot, Reader, Markdown and Info tabs, Re-run and Delete. The library lives in `temp/archive/` (`ARCHIVE_DEMO_ROOT` overrides it, for the replay); a first run archives five local sample pages rather than shipping a pre-built `sample/`. The viewer reads a cache unpacked from `snapshot.zip` under `<root>/.view/<id>/` until AR-E8. Tests: the model golden (`tests_archive_model`) and the replay `tests/archive.json` (sample library, archive a page, search with snippet, tabs, re-run, delete, close; about 12 s).
- *Runtime and Radiant fixes found by the demo.* `on <event>` handlers must follow their `view` directly — a `pn` between them leaves later handlers unattached, reported as a line-continuation error at the document element (a diagnostics gap, not fixed). The headless replay now spends wall time on assertion retries while the Lambda event loop has work, as it already did for document loads. Workers run with `--no-log` so they do not replace the caller's `log.txt`.
- *Known issue, not fixed.* A view whose state changes often (every job message) recreates its `<iframe>` on each rebuild, and a frame's load commits only on a full window-loop turn, so a load that completes after a later rebuild is dropped ("navigation-async: could not present"). The frame shows the snapshot once the job messages stop and the window takes a turn; the replay therefore checks the frame's `src`, as the Reader View replay does, not its content. AR-E8 (reading the members in place) or keeping an unchanged `<iframe>` across a rebuild would remove it.

**Phase 2 — batches and crawls (USER, 2026-10-23; proposed design, not started).** All Lambda, no native work (§3.2). The tests run offline over a crawl fixture site added under `test/lambda/archive/site/crawl/`: an entry page linking to three sections and their pages, a link cycle, links outside the entry's directory and to another `file:` tree, a `rel=nofollow` link, a calendar that generates query variants without end, a page with `<link rel=canonical>` to another page, links to a PDF and an image, a broken link, and a text URL list, a bookmark export and a sitemap index with two sitemaps naming fixture pages.

| Milestone | Delivers | Acceptance |
|---|---|---|
| **M6 Batches** | `seeds.ls` and `parse_seeds`/`seeds` (text lists, bookmark HTML with folder tags, sitemaps and sitemap indexes); `crawl.ls` coordinator with the frontier, `max_pages`, `max_mb`, `max_time_s`; `crawl.json`, `pages.jsonl`, `frontier.json`; `jobs.attempt`; `fetched_bytes`; `batch`, `resume`, `crawls`, `crawl_pages`, `remove_crawl`; the service's `crawl`, `resume` and crawl `cancel`, and its queue order (direct requests first, crawls round-robin); index cache of crawls (AR14, AR18, AR19) | Goldens (`pkg_crawl.ls`): each seed format read to the same URL list, with bad lines in `rejected`; a batch of the fixture pages archived with one snapshot each and the page table in order; `max_pages: 3` ends `done` by `max_pages` with the rest in `frontier.json`, and `resume` with a larger limit archives exactly the rest; a crawl whose process is killed after two issued pages resumes without losing or repeating one; a direct `add` sent during a batch runs before the batch's next job |
| **M7 Following links** | `links.ls` and the manifest's `links` and `canonical` (AR17), with `source.html` fallback for older snapshots; `scope.ls` with the scopes, `include`/`exclude`, trap guards, `obey_nofollow`, `follow_kinds` and `accept_kinds` (AR15); links of reused snapshots; the crawl asset cache (AR20) | Goldens over the fixture site: depth 0, 1 and 2 archive exactly the expected pages under each scope; the cycle is archived once; the calendar stops at `max_per_path`; the canonical page is not fetched twice; the PDF is an outlink unless `follow_kinds` names `file`; excluded links counted by reason; a second crawl over the archived site fetches nothing and reaches the same pages through `reused`; the shared stylesheet and font are downloaded once per crawl (request log) while each snapshot ZIP still holds them |
| **M8 Politeness** | `robots.ls` (RFC 9309 groups, longest match, `$` and `*`, `Crawl-delay`, 4xx and 5xx handling); per-host job slots and delays; retry with backoff, jitter and `Retry-After`; slow-down on 429 and 503 (AR16) | `robots.ls` goldens over RFC 9309's examples and edge cases; the coordinator's choices kept in pure functions and checked by goldens — the next entry for given frontier, host slots and clocks, and the retry delay and host slow-down for a given outcome and attempt — because `file:` URLs have one host and never answer 429 or 503; job start times on a fixture crawl with `per_host_delay_ms` set; one recorded manual crawl of a small public site with robots.txt honoured |
| **M9 Demo** | the add bar's Batch and Crawl modes, Import…, crawl rows with Stop and Resume, the Crawls rail section and crawl Info view (§9) | Model golden extended (crawl rows from `crawl` messages, the rail's crawl filter); replay `tests/archive_crawl.json`: crawl the fixture site, watch the crawl row complete, filter by the crawl, open a page, stop and resume a second crawl, delete a crawl with its snapshots |

**Phase 3 — shared resources (AR13; Phase 2 until 2026-10-23).** AR-E7 (digest; `hash` exists), the resource store and URL index, conditional revalidation, `integrity` reuse, manifest references to shared payloads, `export` and `gc`, a backfill pass that moves existing snapshots' resources into the store, and the crawl asset cache (AR20) replaced by the store behind its lookup function. *Acceptance:* archiving a second page of the same site downloads only its new resources (request log); a page whose scripts carry `integrity` values already in the store makes no request for them; `export` produces a standalone ZIP that opens with the library removed; `gc` after `remove` frees the payloads only that snapshot used; a crawl of 50 pages of one site stores its stylesheet once.

**Later.** JavaScript-rendered pages and the links scripts insert (AR-E6), CSS-selected link following (AR-E11, AR-O18), cookies from a file the user names (AR-E10), a crawl mirror export with links between its pages rewritten to local paths (after Phase 3, AR-O17), WARC and WACZ output (AR-O7).

---

## 11. Native enablers

**AR-E1 — Full fetch response, off the loop thread.** `fetch_response_to_item` (`lambda/runtime/lambda-proc.cpp:442`) returns `{status, ok, url, headers, body}`, with `body` a string for text content types and a binary otherwise (was WebScrape E1). Landed in M0. The transfer moves off the loop thread: either onto the libuv thread pool, parking the calling task as `io.exec` does (`concurrency.cpp:1689`), or onto the curl-multi thread Radiant already runs (`curl_multi_backend.cpp:401`). Either way the transfer thread holds no managed values (D5.3.3); the loop thread builds the result.

**AR-E2 — Request options.** Apply `headers` (now a no-op at `lambda-proc.cpp:562`), `user_agent`, `follow_redirects` and `verify_tls` on the script path (was WebScrape E2). Landed in M0; a proxy option was not built (proxies are a non-goal). Conditional headers (`If-None-Match`, `If-Modified-Since`) need nothing more, and a `304` comes back as an ordinary status.

**AR-E3v2 — `io.fetch(url, {to, progress})`: to disk, with a progress callback (USER, 2026-10-22).** Two options on the function that exists, instead of a new `io.download`. `to: path` streams the response body into `path.part` and renames it to `path` on success; the result is the AR-E1 response map with `body: null` and `bytes` set. `progress: f` names a callback, `fn` or `pn`, that receives `{url, received, total, rate}` (`total` is `null` when the server does not say; `rate` is bytes per second); `progress_ms` (default 250) bounds how often it is called. The transfer runs on the libuv thread pool and holds no managed values (D5.3.3): curl's transfer callback (`CURLOPT_XFERINFOFUNCTION`) updates atomic counters and wakes the loop; the parked task resumes, calls `f` **on its own stack** with the latest counters, and parks again; one final call with the final counters precedes the return. Progress is therefore a level: an update the callback did not see is superseded, never queued, and the runtime sends no messages — what the callback does with the level (send, print, dispatch) is the caller's business, under the caller's own `send` semantics (AR-O11). If `f` returns an error, the transfer is aborted and `io.fetch` raises that error. Cancelling the task aborts the transfer through the same curl callback, which is all the existing callbacks do today, and removes the `.part` file unless `resume` is set (`resume: true` continues an existing `.part` with a `Range` request). Other options: the AR-E2 request options and `max_bytes`. `io.copy` from a URL becomes `io.fetch` with `to`, so it stops buffering in memory. A local file as the source (`file:` URL or path) takes the same path — a chunked copy with the same progress calls — which is how the offline tests exercise the mechanism.

**AR-E4 — Document-owned tasks, and messages to a view.**

*(a) Document ownership (USER, 2026-10-21).* A task can be, and should be, owned by the document, so that it outlives the handler that starts it. To communicate with the task, the handler keeps its handle in state that lives longer than the handler: a view's `state` (S12.1.3). Storing the handle grants the capability to send to it, wait on it and cancel it, not ownership. The spelling is an explicit start option (USER, 2026-10-21), `start(target, args, {owner: 'document'})`, accepted while a document runs Lambda code (its handlers and templates) and an error elsewhere. The handler's block exit neither joins nor cancels such a task; the document cancels and then joins it when the document is discarded (the window closes or navigates away). A task started without the option keeps today's scope, so a library procedure that a handler calls (`archive.add` downloading assets in child tasks) still joins its children at the end of its own block. Ratified 2026-10-21 (spec 71.1.0; design record K30g in [Lambda_Design_Concurrency.md](Lambda_Design_Concurrency.md) §10.8) as:

> **S13.3.1v2** A started handle is a scoped resource owned by the nearest lexical block: normal exit **joins**; error exit **cancels then joins**; ownership escapes only by `return` (visible in the type), or at the start site by `owner: 'document'`, which makes the running document the owner: the document cancels then joins the task when it is discarded. Storing or sending a handle grants capability, not ownership; a handler that must reach a document-owned task later keeps its handle in state that outlives the handler (S12.1.3).

Runtime work: each document gets a task scope that the reactive host joins at teardown, and `start` with the option registers the task there instead of in the current block's scope. The loop needs nothing new: Radiant's window loop already runs the libuv loop every turn (`radiant/window.cpp:95`), so tasks advance between frames.

*(b) Messages to a view (AR-O13, resolved 2026-10-22: `dom.dispatch` from the task).* A view is not a task and has no mailbox, and Lambda has no way to look at a task without parking, so the service needs an address to report to. Proposed, mirroring `dom.request_frame(owner, event)`: `dom.address(owner, event)` returns a value that `send` accepts; each message sent to it is dispatched as `on <event>(evt)` to the owner, with the message as `evt.detail`, in send order (S13.2.3). It extends S13.2.1, under which only task handles are addresses.

**AR-E5v2 — `io.exec(…, {progress})` (USER, 2026-10-22; landed in M0).** No longer an archiver enabler since AR4v2 runs no process; kept as a general runtime feature. The same option on `io.exec`: `progress: f` is called on the task's stack, under the same rate limit (`progress_ms`), with `{line}`, the most recent complete stdout line since the previous call, while the full output is still captured in the result; the last line is always delivered before the result. The capture loop in `lib/shell.c` gains a chunk hook for this; it runs on the worker thread and only splits lines into a mutex-guarded cell. Any one-record-per-line progress protocol fits it.

**AR-E6 — Rendered DOM readback (Later; was WebScrape E6).** Load a page with its scripts on, let it settle, and return the live DOM, for single-page apps and for the links their scripts insert. The pieces exist apart: `lambda.exe layout` runs a page's inline and external scripts, ES modules, `async`/`defer`, timers, `fetch`, microtasks, `requestAnimationFrame` and `MutationObserver` on a real libuv loop, windowless (`radiant/script_runner.cpp`, `radiant/cmd_layout.cpp`), but emits only the view tree; `js --document` exposes the DOM but loads with scripts off. The enabler combines them in an in-process entry the page engine can call, with a configurable settle — a selector to wait for, network idle, or a time — replacing the fixed drain bound (`EVENT_LOOP_DRAIN_TIMEOUT_MS`, `lambda/js/js_event_loop.cpp`), and returns `documentElement.outerHTML` to be archived as the snapshot's source, with the raw response kept beside it. Interaction such as scroll-to-load could reuse `radiant/event_sim.cpp`'s driver. Opt-in per rule (`render: true`), because rendering costs far more than a fetch; WebSocket-driven pages will not hydrate (LambdaJS has no WebSocket). The largest enabler; scheduled after Phase 3.

**AR-E7 — Digest.** The `hash` of the former web-scrape proposal's E7, with SHA-256 for the store and SHA-384/SHA-512 for comparing `integrity` values. Required in Phase 3, and in Phase 2 for the crawl cache's keys (AR20); in Phase 1 the manifest leaves `digest` empty. The first AR-E9 function. *Exists since 2026-10-22:* the mail demo's OAuth work added `hash(value, 'sha256' | 'sha384' | 'sha512')`, returning binary (vibe/Lambda_Demo_Mail.md M4).

**AR-E9 — Small C system functions where script cannot do the work (AR4v2).** Added one at a time, each an ordinary system function with its own tests, never a linked library for the archiver's sake: `hash(bytes, 'sha256' | 'sha384' | 'sha512')` (= AR-E7); `crypto.aes_cbc_decrypt(bytes, key, iv)` for AES-128 HLS segments (optional, after M3). Nothing else is foreseen: HTML, JSON, m3u8 and WebVTT parsing, byte appends and PNG/PDF rendering exist in Lambda already. A proposed function is listed here before it is written.

**AR-E8 — Documents loaded from ZIP members.** Radiant loads an HTML document from a captured ZIP member node and resolves its relative resource URLs against sibling members, without a `zip:` path scheme (Lambda_IO_Zip §3.2); in Phase 3 the resolver also consults the library's resource store for shared entries. The Finder's member previews need the same resolution (Lambda_Demo_Finder §6). PDF and thumbnail rendering do not depend on it, because they render from the staged loose files before packing (AR7).

**AR-E10 — Cookies on the script fetch path (Later; was WebScrape E4).** An optional cookie jar for `io.fetch` — attach `Cookie`, capture `Set-Cookie`, load and save a Netscape cookie file the user names — over the RFC 6265 jar Radiant already has (`lambda/network/cookie_jar.cpp`, with the public-suffix list). It lets a crawl pass a consent interstitial or use the user's own session for a site they can read; it is never used to sign in (logins are a non-goal).

**AR-E11 — CSS selectors over a parsed HTML tree (not scheduled; was WebScrape E3).** A layout-free `dom(html)` that builds the DOM from the HTML5 parser's tree (`build_dom_tree_from_element`, `lambda/input/css/dom_element.cpp`) without a Radiant layout document, so `query_selector_all` (`lambda/input/css/selector_matcher.hpp`) works on a fetched page. The archiver needs it only for CSS-selected link following (AR-O18); resource and link collection walk the tree with `lambda.doc.readability.node`. It is also the foundation a structured-extraction package would need (Appendix D).

---

## 12. Risks

| Risk | Mitigation |
|---|---|
| Archived pages are hostile input | Scripts, `on*` attributes and `javascript:` URLs are removed from the snapshot (AR5v2); Radiant's export disables scripts; `source.html` is never opened by the viewer |
| File and member names from titles or servers escape their folder | Every name is slugged and joined under the snapshot; `..`, separators and control characters never survive; ZIP member paths are checked by the ZIP writer; engine output templates point inside staging |
| Copyright and terms of service | Personal archiving is the use case; the package has no republishing features; the README states that the user is responsible for what they archive; cookies are used only from a file the user names |
| Sites change how they expose media | The media engine reads standard surfaces only (HTML5 media, Open Graph, JSON-LD, HLS); a page whose media sits behind player JavaScript archives as a page with remote links, never a half-download (AR4v2, AR-O14) |
| Encrypted or DASH streams | AES-128 HLS waits for AR-E9; DASH and separate audio/video needing a mux are out of scope and recorded as remote links; a TS file may not play in Radiant's `<video>` on every platform, so the record names the container |
| CPU-heavy steps pause the window | Readability and PDF run in a child process (AR12) until thread workers exist (AR-O12) |
| Progress floods a mailbox | The runtime never sends: it calls back with a level, rate-limited; the callback sends what it chooses under S13.2.2 (AR-E3v2, AR-O11) |
| Rewriting a large ZIP on `refresh` | Derived members are small; the media stays outside the ZIP; unchanged members keep their original bytes |
| Non-UTF-8 pages decode wrongly | AR-E1 exposes the charset; transcode before parsing; record the charset |
| Radiant renders a snapshot differently from Chrome | The ZIP keeps the original markup and CSS, so it unpacks into a folder any browser opens |
| Library growth | Asset limits, `on_duplicate: skip` by default, sizes in the record; a crawl's `max_mb` (AR15); Phase 3 de-duplication |
| A crawl runs away: calendars, faceted search, session parameters, mirrored paths | `max_depth`, `max_pages`, `max_mb` and `max_time_s` always bound a crawl; `prefix` scope by default; trap guards on URL length, repeated segments and query variants; `any` scope only with a shallow depth (AR15) |
| A crawl burdens a site | robots.txt obeyed for discovered links, `Crawl-delay` honoured, one job per host at a time with a delay between starts, a User-Agent that names the archiver, slow-down on 429 and 503, assets downloaded once per crawl (AR16, AR20) |
| The same page under several URLs | Normalized URLs (§7), the tracking-parameter list, and `<link rel=canonical>` added to the seen set (AR17); a page reached by two URLs before its canonical is known is archived twice, which `on_duplicate` and Phase 3's store keep cheap |
| A crash in the middle of a long crawl | `pages.jsonl` is append-only with an issued and an outcome line per page; `frontier.json` and `crawl.json` are saved through a temporary file; `resume` re-queues pages issued without an outcome (AR18) |
| A large crawl starves single URLs | The service serves direct requests before crawl jobs, and crawls round-robin; the coordinator keeps its frontier itself and only a few jobs in the service's queue (AR19) |
| Script-inserted links are missed | Documented until AR-E6; a sitemap or a URL list as seeds reaches such pages (AR-O19) |

---

## 13. Open items

**Resolved (USER; dated per item where later than 2026-10-21).**

- **AR-O1 — Name.** Keep `lambda.archive`. ZIP and other archive-format support is named `lambda.io.zip`, so the two do not collide.
- **AR-O2 — Default HTML form.** A ZIP of the page and its resources (AR5v2).
- **AR-O3 — Background jobs.** Long downloads run in Lambda workers, with progress reported from the low-level IO up to Lambda (AR12).
- **AR-O10 — Ownership of a task started from a view.** A task can be, and should be, owned by the document, so that it outlives the handler that starts it; to communicate with it, its handle is kept in state that lives longer than the handler (AR-E4a). The spelling is the explicit start option `owner: 'document'`, not implicit ownership of every task a handler starts; ratified as S13.3.1v2 (K30g).
- **AR-O11 — Replacing undelivered progress (USER, 2026-10-22).** No ruling on `send` was needed: runtime progress is a callback invoked on the receiving task's own stack with the latest level (AR-E3v2, AR-E5v2). Nothing in the runtime enqueues, so S13.2.2 is untouched; the caller's callback decides what to send and handles `'mailbox_full'` like any sender.
- **AR-O12 — CPU-heavy steps (USER, 2026-10-22).** Readability and PDF rendering run in a child `lambda.exe` through `io.exec` until thread workers exist (AR12v2). The child is Lambda's own executable, not an external tool, so AR4v2 is kept.
- **AR-O6 — Relation to `lambda.scrape` (USER, 2026-10-23).** There is no separate scrape package: the user asked for its proposal to be consolidated into this one, the package actually built, and for crawling to be this package's Phase 2. Crawl orchestration lives here (AR1v2, AR14–AR20); the scrape proposal's extraction surface is recorded as out of scope (Appendix D). Was: a thin `transfer.ls` now, with the scrape session slotting in when that package lands.

**Open.**

- **AR-O14 — Streaming platforms (needs a ruling).** Under AR4v2 a YouTube, Vimeo or TikTok link archives as a page plus the media links the page names; nothing downloads, because those sites expose media only through player JavaScript, signed URLs and DASH/HLS manifests chosen per client. Per-site extractors in Lambda are possible — an `lambda.archive.extractors` package, one module per site, opted into by rule — but they are a perpetually breaking maintenance commitment (yt-dlp changes weekly for this reason) and would add signature deciphering and a muxer. Proposed: out of scope; the record's remote links and the page snapshot are the archive of such a URL.

- **AR-O4 — Configuration language.** Data file (AR2v2, proposed) or a `.ls` module, which would allow real string patterns in `match` and computed settings.
- **AR-O5 — Duplicate policy default.** `skip` (proposed) or `new` (ArchiveBox keeps every capture).
- **AR-O7 — WARC.** Writing WARC files would make captures readable by Wayback tooling and ArchiveBox; the manifest already holds what a response record needs except the raw headers. Deferred.
- **AR-O8v2 — Robots and politeness (revised 2026-10-23 for crawls).** Proposed: a URL the user names — a single URL, a seed of a crawl, every URL of a batch — is archived regardless of `robots.txt` (as ArchiveBox does), with a per-host delay for its assets; links a crawl discovers obey `robots.txt` unless the crawl sets `obey_robots: false` (AR16). Was: bulk import obeys `robots.txt` — but a batch is a list the user chose, URL by URL.
- **AR-O9 — Default library root for scripts.** The demo uses `temp/archive/`; `open` takes an explicit root. Whether a user-level default should exist is open.
- **AR-O13 — How a task's messages reach a view (resolved 2026-10-22).** The handle in state carries messages from the view to the service; the reverse direction uses `dom.dispatch(owner, {type, bubbles: false, detail})` called from the task, the second candidate this item named, because the check it asked for held: the window advances tasks between frames inside the document's evaluator and state batch, so the view's `on <type>(evt)` handler runs with the message as `evt.detail` and the view re-renders once at the end of the dispatch. The handler must not park (it would park the task mid-handler); the archive demo's `on job` does only synchronous work. `dom.address` (an extension of S13.2.1) is therefore unnecessary. The task looks the element up by id on each message, since a rebuild may replace a captured one.

**Open (Phase 2, 2026-10-23).**

- **AR-O15 — Crawl defaults.** Proposed: `scope: prefix`, `max_depth: 2`, `max_pages: 100`, `max_mb: 1024`, no time limit, `follow_kinds: [page]`, `per_host_jobs: 1`, the `per_host_delay_ms` default of 500 ms; a batch without a page limit. ArchiveBox stops at depth 1 and Browsertrix sets no page limit by default; a page limit that always applies to a crawl is the safer default for a desktop archive.
- **AR-O16 — Asset reuse within a crawl.** Proposed: the crawl-scoped cache of AR20 in Phase 2, replaced in Phase 3 by the library store behind the same lookup. Alternative: bring AR13 (the shared store, `export`, `gc`) forward into Phase 2, which saves the interim cache and makes a crawl's storage small at once, at the cost of a Phase 2 roughly twice the size and a viewer that needs AR-E8's store lookup before shared members can be shown.
- **AR-O17 — Moving between a crawl's pages in the viewer.** Snapshots keep absolute hyperlinks (AR10), so a link clicked inside an archived page goes to the live web. Proposed: when the demo unpacks a snapshot into its view cache, a link whose normalized URL is a page of a crawl the snapshot belongs to is rewritten to that page's snapshot, unpacked on demand, so following links inside the frame stays in the archive; the stored files are never changed, so a snapshot stays independent of the crawls that reach it, and a page archived later is linked as soon as it exists. A mirror export that rewrites links inside the files (wget `-k`) is Later, after Phase 3. Alternative: rewrite at capture time, which only works for pages archived before the linking page.
- **AR-O18 — CSS-selected link following.** A `follow: {select: "nav a, .pagination a"}` option would let a crawl take only a site's navigation. It needs AR-E11. Proposed: not in Phase 2; `include` and `exclude` globs cover scope.
- **AR-O19 — Sites whose links only scripts insert.** Until AR-E6 a crawl sees the server's HTML only, so a single-page app yields its entry page and little else. Proposed: a documented limitation; sitemap or URL-list seeds are the workaround, and the crawl record says when a page had no links.

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
| `lmd/package/archive/page.ls` (`refs`, `base_of`), `plan.ls` (`normalize`, `rule_for`, `host_glob`, `path_glob`, `classify`), `service.ls` | what Phase 2 builds on: element walk, URL normalization, rule matching, the worker pool |
| `lmd/package/archive/jobs.ls` (`run`, `build`) | the job a crawl runs through `jobs.attempt` (AR16) |
| `lambda/network/cookie_jar.cpp`, `public_suffix.cpp` | AR-E10 |
| `lambda/input/css/dom_element.cpp` (`build_dom_tree_from_element`), `lambda/input/css/selector_matcher.hpp` | AR-E11 |
| `radiant/script_runner.cpp`, `radiant/cmd_layout.cpp`, `radiant/event_sim.cpp`, `lambda/js/js_event_loop.cpp` (`EVENT_LOOP_DRAIN_TIMEOUT_MS`) | AR-E6 |

## Appendix B. Package module map (as built through M4; Phase 2 and Phase 3 modules marked)

`lmd/package/archive.ls` is the facade (it also holds `search`, `snapshots`, `doctor` and `remove`); modules live under `lmd/package/archive/` and are imported as `lambda.archive.<module>` (D7.2.4v3, D7.2.6).

| Module | Role | Effect |
|---|---|---|
| `config.ls` | `Config` and `Rule` types, defaults, merge, validation | `fn` |
| `platforms.ls` | built-in host → kind table | data |
| `plan.ls` | URL normalization, rule matching, classification, `Plan` | `fn` |
| `transfer.ls` | User-Agent, timeout, headers, byte limit and the job's progress callback over `io.fetch` (AR-E3v2) | `pn` |
| `page.ls` | resource collection and download, manifest | `pn` |
| `jobs.ls` | the job and refresh procedures behind the facade, importable by the service | `pn` |
| `media.ls` | media discovery on a page, direct and HLS download, the media record | `pn` (discovery `fn`) |
| `hls.ls` | m3u8 parsing, variant ranking, segment list with byte ranges and init segments | `fn` |
| `vtt.ls` | WebVTT cues → transcript text and the media page's track | `fn` |
| `gallery.ls` | image candidates, size threshold, captions, download, thumbnails | `pn` (candidates `fn`) |
| `file.ls` | files and single images: the first fetch moved into `media/`, image size and thumbnail | `pn` |
| `rewrite.ls` | manifest rewrite, script stripping, CSS `url()` rewrite | `fn` |
| `readable.ls`, `markdown.ls`, `text.ls` | derived outputs | `fn` |
| `derive.ls` | the capture parsed once; the article (only when `readerable`); the record's fields; every derivation | `fn` |
| `render.ls` | PDF and thumbnail export | `pn` |
| `worker.ls` | the child `lambda run` that derives and renders a staged capture (AR-O12) | `pn` |
| `workers.ls` | the caller's side: request, `io.exec` with step progress, `derived.json` | `pn` |
| `store.ls` | ids, staging, packing, records, index cache, duplicates (`finish`, not `commit`: a keyword) | `pn` (record building `fn`) |
| `service.ls` | the job service: queue, `jobs` workers, cancellation, refresh requests, reports | `pn` |
| `seeds.ls` | Phase 2: URL lists, bookmark HTML, sitemaps → seeds (AR14) | `fn` (`seeds` over a path `pn`) |
| `links.ls` | Phase 2: a page's hyperlinks and canonical URL, shared by the page engine and the crawl's fallback (AR17) | `fn` |
| `scope.ls` | Phase 2: admission tests and trap guards; the next-entry and retry-delay choices (AR15, AR16) | `fn` |
| `robots.ls` | Phase 2: robots.txt parsing and matching (AR16) | `fn` |
| `crawl.ls` | Phase 2: the coordinator, crawl records, resume, the asset cache's lifetime (AR18–AR20) | `pn` |
| `shared.ls` | Phase 3 resource store, URL index, revalidation, `export`, `gc` | `pn` |

## Appendix C. Superseded drafts (2026-10-21 onward)

- ~~**AR1** — an archiver, not a crawler: one URL in, one snapshot out; link-following, frontier management and politeness belong to `lambda.scrape`; a later bulk import feeds URLs one at a time.~~ Replaced by **AR1v2** (USER, 2026-10-23): the job is unchanged, and batches and crawls are a layer above it in this package (AR14–AR20); `lambda.scrape` is not built (AR-O6).
- ~~**AR2** — the configuration lives at `<root>/archive.yaml`, so a library describes itself.~~ Replaced by **AR2v2**: the caller owns the configuration (the demo's lives in its directory), the package's defaults are complete, and each snapshot records its effective settings (USER).
- ~~**AR5** — the default HTML is a folder snapshot (`index.html` + `assets/`); a single file with `data:` URIs is an option.~~ Replaced by **AR5v2**: one `snapshot.zip` holds the page, its resources and the renderings that share them (USER).
- ~~**AR-E3** (first draft) — streaming `io.copy` to disk, progress deferred until the view question settles; **AR-E5** optional.~~ Replaced by **AR-E3** `io.download` with progress messages and a required **AR-E5**, because progress must reach Lambda from the transfer layer (USER).
- ~~**AR-O3** — run demo jobs synchronously or in a separate polled worker process.~~ Resolved by **AR12**: Lambda worker tasks (USER); the remaining ownership question is **AR-O10**.
- ~~**AR-E4** (second draft) — `dom.start(owner, target, args)` starts a task owned by the owner element and cancelled when the element is removed.~~ Replaced by **AR-E4a**: the task is owned by the document, and its handle lives in view state (USER). The task-to-view address stays open as **AR-E4b** / **AR-O13**.
- ~~**AR-E3** (second draft) — `io.download(url, path, options)` producing runtime progress *messages* `{type: 'progress', tag, …}` delivered to a handle, a newer update replacing an undelivered one (AR-O11); **AR12** — the runtime sends those messages.~~ Replaced by **AR-E3v2** and **AR12v2** (2026-10-22): a `progress` callback and a `to` option on `io.fetch`, invoked on the task's own stack with the latest level; the runtime produces no messages, so AR-O11 needed no ruling (USER).
- ~~**AR-E5** (first draft) — `io.exec` option `progress: handle` delivering `{type: 'line', tag, text}` messages.~~ Replaced by **AR-E5v2**: `progress: f` callback receiving `{line}` (USER, 2026-10-22).
- ~~**AR4** — delegate video and audio to yt-dlp and galleries to gallery-dl through `io.exec`, user-installed, with `doctor()` reporting their versions; **§6 `engines`** settings (paths, format selector, cookies); **M3** stub-executable goldens.~~ Replaced by **AR4v2** (USER, 2026-10-22): every engine is Lambda code — page media discovery, direct and HLS download, a page's images — with no external tools or libraries; `media` and `gallery` settings; AR-E9 for the few primitives script lacks; AR-O14 opened for streaming platforms.

## Appendix D. The former web-scrape proposal (absorbed 2026-10-23)

`vibe/Lambda_Pkg_WebScrape.md` (drafted 2026-07-18) proposed `lambda.scrape`: a one-shot `get` with a layout-free `dom()` and BeautifulSoup-style selection, a Colly-style collector, a Scrapy/Crawlee-style crawler with sessions and item pipelines, and a render mode for JavaScript pages, over native enablers E1–E7. The package was never built; this package built its first enablers and now takes its crawling. At the user's request (2026-10-23) the document was deleted and its content is accounted for here.

| Web-scrape item | Disposition |
|---|---|
| E1 — the full fetch response (status, headers, final URL, binary bodies) | Built as AR-E1 (M0) |
| E2 — request options (headers, timeout, redirects, TLS, proxy) | Built as AR-E2 (M0), without the proxy (a non-goal) |
| E3 — a layout-free `dom(html)` with CSS selectors | AR-E11, not scheduled; only AR-O18 would use it |
| E4 — cookies and sessions on the script fetch path | AR-E10, Later; never for signing in |
| E5 — retry, backoff and rate limits in Lambda | AR16 (Phase 2) |
| E6 — rendered DOM readback with a configurable settle | AR-E6, Later, with the proposal's design kept there |
| E7 — `url_parse`/`url_encode`, CSV output, `hash`/`base64`/`uuid`, regex capture groups | `hash`, `uuid` and `url_encode` exist (2026-10-22, the mail demo); `hash` is AR-E7. URL decomposition is `plan.parse` in this package; CSV output, base64 and capture groups have no owner here |
| Tier 1 — one-shot `get`, `dom(body).css(…)`, `::text`/`::attr` extraction | Out of scope: structured extraction is not archiving |
| Tier 2 — the collector (`on_html`, `on_response`, `visit`, `emit`, `export`) | Replaced by `crawl` (AR14–AR19), which archives each page instead of calling back per element |
| Tier 3 — the crawler: frontier, de-duplication, worker pool, autothrottle, session pool, proxies, labelled router, item pipeline with schema validation, dataset export | Frontier, de-duplication, worker pool, per-host pacing, retry and robots: AR15–AR19. Autothrottle beyond AR16's slow-down, session pools, proxy rotation, routing, item pipelines and dataset export: out of scope |
| Render mode (`render: true`, `wait_for`, `settle`, page interaction) | AR-E6 (Later) |
| Extraction helpers: readability, Markdown, metadata, tables | Readability and Markdown are archive formats (§6); Open Graph and JSON-LD metadata are read by `derive.ls` and the media engine; HTML tables to row maps is out of scope |
| robots.txt obeyance, per-host rate limits, a User-Agent by default | AR16, with `user_agent` in the defaults (§6) |
| Risks: charset decoding, response-shape change, heavy rendering, shared crawler state, no WebSocket | Charset: §12 (AR-E1 exposes it); the response shape changed in M0 (AR-E1); rendering: AR-E6; crawler state lives in the coordinator task (AR19); WebSocket: AR-E6 |
| Open questions: node identity under `==`, relation to the `dom` package, where crawler state lives, the autothrottle signal, overlap with `lambda serve`'s cookie parser | Crawler state: the coordinator's own variables (AR19), as the proposal leaned. The others concern the selector API and cookies, and go with AR-E11 and AR-E10 |

A structured-extraction package, if one is wanted later, would be a separate package built on AR-E11 and on this package's transfer layer and crawl coordinator, rather than a second crawler.
