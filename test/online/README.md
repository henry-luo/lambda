# Radiant online page inventory

`pages.csv` is the shared URL list used by `test_radiant_online_view_gtest` and
the snapshot capture script. Its columns are the case label, page URL, and
whether the online-view smoke test expects linked resources.
The online GTest discovers one `Loads_<label>` test per catalog row, so labels
stay attached to their URLs when rows are removed or reordered. Generated
labels can have gaps. For example, filter one case with
`--gtest_filter=RadiantOnlineViewTest.Loads_lua_pil`.

On 2026-11-12, 94 of the 95 unresolved URLs were removed using the existing
Chromium headless-shell failures, leaving 1,067 pages. PIL remains because
Chromium loads it. The removal audit and previous results are preserved under
`snapshots/removed_pages-20261112.json` and
`snapshots/removed_capture_results-20261112.csv`.

From the repository root, run:

```sh
node test/online/capture_snapshots.cjs
node test/online/capture_snapshots.cjs --failed-only
node test/online/capture_snapshots.cjs --failed-only --renderer=lambda --labels=arduino_docs
```

The script saves matching 1200 × 800 Lambda and Chromium PNGs under
`test/online/snapshots/`. It also writes `capture_results.csv` there. Set
`CHROME_HEADLESS_SHELL` to select a specific Puppeteer headless shell, or set
`RADIANT_ONLINE_CAPTURE_JOBS` to change the default four concurrent page cases.
`RADIANT_ONLINE_CAPTURE_TIMEOUT_MS` and `RADIANT_ONLINE_RENDER_TIMEOUT_MS`
override browser navigation and Lambda render timeouts.
Each attempt records its selected timeout in `capture_results.csv`; older
attempts can have an empty timeout field. Complex pages can complete after the
default parent deadline because native layout does not count toward the script
CPU watchdog. Use the render-timeout override for a longer bounded capture.

Retries run only the failed or missing renderer side and preserve successful
images and the other manifest rows. Results are saved after each renderer
attempt. `--renderer=browser` retries only Chromium; `--labels=a,b` limits the
page cases. HTTP rejections, TLS failures, crashes and timeouts remain failures.

Lambda keeps per-page `-lambda.log`, `-lambda-output.log` and timing reports in
the snapshots directory. Its parent deadline follows the engine's maximum
script watchdog plus the online launcher's cleanup grace. The override above
can select a different wall-time budget. A successful process must also report
document-load completion and write a complete PNG before the new image is
promoted to `-lambda.png`. Failed images use `.failed.png`; they are diagnostic
artifacts. Chromium captures after the final main document loads, including
script redirects, without waiting for recurring analytics requests to stop.
Each Chromium attempt replaces its `-browser.json` diagnostic, referenced by the
manifest's `browser_log` column. It records document responses, selected server
headers, load events, failed/pending resources and a bounded visible-text
excerpt. Cookie and authorization headers are omitted. This distinguishes an
origin challenge or rejection from a page waiting on a resource.

Focused capture checks use a local HTTP server and Puppeteer's headless shell:

```sh
node --test test/online/capture_snapshots.test.cjs
```

`test_page_load_gtest` writes its local fixture timing and memory measurements
to `page_load_metrics.csv` in this directory.
