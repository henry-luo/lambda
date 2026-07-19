# Lambda `scrape` Package — Design Proposal

> **Status**: proposal (drafted 2026-07-18).
> **Scope**: a feature-rich web-scraping package for Lambda Script, layered over the runtime's
> existing HTML5 parser, CSS selector engine, curl network stack, actor concurrency, and the
> Radiant headless-browser pipeline.
> **Companion docs**: `vibe/Lambda_Design_DOM_Pkg.md` (headless-DOM package, Obscura-parity),
> `vibe/radiant/Radiant_vs_Obscura.md` (headless-browser API gap analysis),
> `vibe/Module_Web_Server2.md` (`lambda/serve` HTTP infra), `doc/Lambda_Sys_Func.md` (`fetch`, concurrency).

---

## 1. Purpose and goals

Build `scrape` — a pure-Lambda package (with a small set of native enablers) that lets a Lambda
script fetch, parse, query, extract, and export web data at the ergonomic level of Python's
`requests`+`BeautifulSoup`/`parsel`, Scrapy, or JS's Crawlee — and, for JavaScript-rendered
pages, drive Radiant as a headless browser the way Crawlee drives Playwright.

**Product goals**

1. **Three usage tiers, one package.** (a) a one-shot `requests`+`BeautifulSoup` style API for
   quick extraction; (b) a Colly-style callback *collector* for link-following crawls; (c) a
   Scrapy/Crawlee-style declarative *crawler* with a request frontier, dedup, autothrottled
   concurrency, sessions, and item pipelines.
2. **Static and dynamic in one surface.** The same extraction API works whether the DOM came from
   a raw HTTP fetch (fast path) or from Radiant executing the page's own JavaScript (render path),
   mirroring Crawlee's unified HTTP-crawler / browser-crawler interface.
3. **Reuse, don't rebuild.** Lambda already contains a spec-grade HTML5 parser, a full CSS3/4
   selector engine, a production RFC-6265 cookie jar, retry/backoff, an LRU HTTP cache, a shared
   connection pool, and a libuv-backed headless JS runtime. The package's job is to *expose and
   compose* these, not reimplement them (§4 is the honest gap list).
4. **Records are typed and validated.** Scraped items are validated against a Lambda schema
   (`lambda/validator/`) before export — a differentiator over untyped Python/JS scrapers.
5. **Good citizen by default.** robots.txt obeyance, per-host rate limiting, and a configurable
   User-Agent are on by default; turning them off is explicit.

**Non-goals (v1)**: anti-bot/stealth fingerprinting, CAPTCHA solving, a proxy *marketplace*
(proxy *rotation* is in scope; sourcing proxies is not), and a distributed multi-machine crawl
coordinator.

---

## 2. Prior art — what to adopt

| Tool | Language | Model | Best ideas to adopt | What to avoid |
|---|---|---|---|---|
| **requests + BeautifulSoup / parsel** | Python | imperative one-shot | dead-simple `get → soup → .select()/.css()`; `::text`/`::attr()` pseudo-extractors; `.get()`/`.getall()` | no crawl orchestration; global state |
| **Scrapy** | Python | spider + middleware + item pipeline | request scheduler with dedup, `Request(callback=)`, item pipelines, AutoThrottle, feed exports, robots obeyance, `Rule`/`LinkExtractor` | Twisted (async coloring); heavy class hierarchy; no JS rendering |
| **Crawlee** | JS / Python | unified crawler + storages | `RequestQueue` (dedup + resumable), `SessionPool` (rotation on block), `AutoscaledPool` (concurrency from CPU/mem/loop-lag signals), `Dataset`/`KeyValueStore`, label-based router, one API for HTTP vs browser | Node/Apify platform coupling |
| **Colly** | Go | callback collector | tiny surface: `OnHTML(sel, cb)`, `OnRequest`, `OnResponse`, `OnError`; `collector.Visit(url)`; async + rate-limit rules | no JS rendering; Go-specific |
| **Playwright / Puppeteer** | JS | browser automation | `waitForSelector`, `waitForLoadState('networkidle')`, `page.$$eval`, interaction (click/type/scroll) | full browser weight; not needed for static |
| **Firecrawl / Crawl4AI** | Py/JS | scrape-to-LLM | page → clean **Markdown**; main-content (readability) extraction; JSON-LD/OpenGraph metadata harvest; structured "extract by schema" | SaaS coupling; LLM dependency |

**Design consensus these converge on** (and this proposal follows):

- **Separate the four concerns**: *fetch* (transport, sessions, retry, rate-limit) · *parse/select*
  (DOM + selectors) · *orchestrate* (frontier, dedup, concurrency, throttle) · *pipeline*
  (validate + export). Scrapy's middleware/pipeline split and Crawlee's storage split are both this.
- **Requests are values with callbacks/labels**, not imperative call sites — enables a scheduler,
  retries, and resume.
- **Concurrency is resource-aware and rate-limited**, not a fixed thread count (Crawlee's
  AutoscaledPool; Scrapy's AutoThrottle).
- **HTTP-first, browser-on-demand**: render JS only when the page needs it (94% of modern sites
  are client-rendered, but browserless is 10–50× cheaper when it works). Offer both behind one API.
- **Extraction ergonomics matter most** to day-to-day users: `css(sel) |> text` should be a
  one-liner.

---

## 3. Feasibility — are current Lambda features sufficient?

**Short answer: the language and runtime are sufficient and in several ways *better*-suited than
Python/JS; the gaps are all at the native network-script boundary and DOM-bridge exposure, and the
machinery to close them already exists in-tree.** Evidence, capability-by-capability:

### 3.1 Already present and strong (no work needed)

| Capability | Status in Lambda | Reference |
|---|---|---|
| **HTML parsing** | **Spec-grade WHATWG HTML5** tree-construction: 23 insertion modes, adoption-agency (misnested tags), foster-parenting, quirks mode, full 2125-entry entity table, RAWTEXT/RCDATA. Real-world-malformed-HTML tolerant. | `lambda/input/html5/` (`html5_tree_builder.cpp`, `html5_tokenizer.cpp`) |
| **XML parsing** | Recursive-descent with CDATA, PIs, entities, DOCTYPE. | `lambda/input/input-xml.cpp` |
| **CSS selector engine** | **Full CSS3/4**: type/class/id, all attr operators (`~= ^= $= *= |=`, `i`/`s` flags), `:nth-*`, `:first/last/only-*`, state pseudo-classes, `:is()/:where()/:not()/:has()`, all combinators. Ready-made `selector_matcher_find_all` / `find_first`. | `lambda/input/css/selector_matcher.{hpp,cpp}` |
| **DOM API (snake_case, to `.ls`)** | `query_selector(_all)`, `matches`, `closest`, `get_element_by_id`, `get_elements_by_tag_name/_class_name`, `get_attribute`, `text_content`, sibling/child navigation — already surfaced to Lambda scripts via the `radiant` module. | `lambda/module/radiant/radiant_dom_bridge.cpp`, `radiant_dom_iface.cpp` |
| **Native tree query** | `page?<a>` (descendant), `el[<img>]` (child), attribute/value predicates, `that (...)` filters, `|>` map — works directly on `input()` output without any DOM build. | `lambda-eval.cpp` (`fn_query`), `doc/Lambda_Expr_Stam.md` |
| **Concurrency** | **Real, tested, colorless**: `start worker(args)` → handle, bounded-FIFO mailboxes, `wait`/`select(timeout:)`, `sleep(ms)`, `cancel`, shared libuv loop. Ideal for a crawler worker pool + rate-limit + frontier-as-mailbox. | `lambda/concurrency.cpp`, `test/lambda/conc/` (24 tests) |
| **Regex** | RE2-backed string patterns: `\d+`, `[n,m]`, char classes, alternation, anchoring; `find`/`replace`/`split` accept patterns. | `lambda/re2_wrapper.cpp`, `test/lambda/string_pattern.ls` |
| **Error handling** | `T^E` return types, `raise`, `^` propagation, `let a^err` destructure, errors are falsy (`f() or default`). Fetch/parse/io all raise and enforce handling. | `doc/Lambda_Error_Handling.md` |
| **Schema validation** | Define record shape in Lambda type syntax, `validate(schema, data)` → `{valid, errors[]}`; element/document schemas too. | `lambda/validator/`, `doc/Lambda_Validator_Guide.md` |
| **Module/package system** | `import alias: lambda.package.<name>.<name>`, `pub` exports, script-relative resolution, seven existing package precedents (chart/graph/latex/math/pdf/openapi/editor). | `lambda/package/`, `vibe/Lambda_Package2.md` |
| **Output formats** | `format(data, 'json'|'yaml'|'xml'|'html'|'markdown'|...)`, `output(data, path)`. | `doc/Lambda_Sys_Func.md` |
| **Content extraction** | A **1,701-line Mozilla Readability port already exists in Lambda** — main-content/title/byline extraction works today. | `utils/readability2.ls` |
| **Network machinery (in-tree, not yet script-exposed)** | RFC-6265 cookie jar + public-suffix, ETag/Cache-Control LRU cache, exponential-backoff retry, `CURLSH` connection pool + HTTP/2 multiplex, per-origin concurrency caps, curl-multi async backend. | `lambda/network/` (cookie_jar, enhanced_file_cache, network_downloader, network_scheduler) |
| **Headless JS rendering** | `lambda.exe layout page.html` runs the page's inline+external(HTTP) scripts, ES modules, async/defer, on a real libuv loop (timers, `fetch`, microtasks, rAF, MutationObserver), windowless/GPU-less, with a bounded settle-drain. Event simulation (click/type/scroll/…) via a JSON driver. | `radiant/script_runner.cpp`, `radiant/cmd_layout.cpp`, `radiant/event_sim.cpp`, `radiant/ui_context.cpp` |

**Why Lambda is in some ways a better host than Python/JS for this**: colorless concurrency (no
`async` coloring to thread through every extractor), a spec-grade HTML5 parser *and* a real CSS
engine *and* a headless JS runtime *in the same binary* (Python needs `lxml`+`requests`+Playwright;
you get all three natively), error-as-value with enforced handling (no silent `None`), and schema
validation of records built in.

### 3.2 Gaps — all closable, machinery mostly exists

These are the honest blockers. Ranked by leverage; each is a native (C+) enabler, not a language
limitation. §6 turns them into work items.

| # | Gap | Why it blocks scraping | Machinery already in-tree? |
|---|---|---|---|
| **G1** | **`fetch()` returns only the body string** — status, headers, redirect chain, content-type are discarded (`fetch_response_to_item`, `lambda/lambda-proc.cpp:550`, `// TODO: map structure`). | Can't tell 200 from 404, can't read `Location`/`Content-Type`/`Set-Cookie`, can't branch on status. **This is the #1 blocker.** | Trivial — data is fetched, just not surfaced. |
| **G2** | **Custom request headers from scripts are a no-op** (`lambda-proc.cpp:670` "not fully implemented"); `input()` exposes none. | No auth tokens, `Referer`, `Accept`, custom UA, `If-None-Match`. | `http_fetch` already accepts headers (`input_http.cpp:575`); just wire the script path. |
| **G3** | **No CSS selectors over `input()`/`parse()` HTML.** Selectors need the `DomElement` tree; `input(url,'html')` returns the Mark tree; the bridge (`build_dom_tree_from_element`) isn't exposed to `.ls`. `radiant.load()` does it but is **file-path-only** and drags in the whole layout engine. | The core extraction ergonomic (`css(".price")`) is unavailable on fetched HTML without heavyweight `radiant.load`. **#2 blocker.** | Both halves exist: the bridge (`dom_element.cpp:3131`) + the matcher (`selector_matcher_find_all`). Need a lightweight, layout-free `dom(html_string)` sysfunc. |
| **G4** | **No cookies/sessions on the script fetch path.** The RFC-6265 jar is wired only to the Radiant DOM downloader, not to `fetch()`/`input()`. | No login-then-scrape, no session continuity across requests. | `lambda/network/cookie_jar.cpp` is production-ready; wire it to a session handle. |
| **G5** | **No retry/backoff, proxy, timeout/TLS knobs on script fetch.** Backoff & per-origin caps exist but are Radiant-only; no `CURLOPT_PROXY` anywhere; `input()` hardcodes timeout/TLS. | No resilience, no proxy rotation, no per-target tuning. | Backoff logic exists (`network_resource_manager.cpp:874`); proxy is a new curl opt (small). |
| **G6** | **No single "fetch URL → run its JS → hand back settled DOM" mode.** `layout` runs page scripts but emits only the view tree; `js --document` exposes the DOM but loads with `execute_scripts=false`. | Dynamic (SPA) scraping needs both at once. **#3 blocker (Phase 2).** | All primitives exist; needs a new host mode combining them + a configurable settle (wait-for-selector / network-idle vs the current hardcoded 5s). |
| **G7** | Ergonomic sys-func gaps: no `url_parse`/percent-encode (only `url_resolve`), no CSV output formatter, no hashing/base64 (cache keys, dedup), regex `find()` exposes no capture groups. | Each is a papercut with a `cmd()`/JS fallback, not a blocker. | Small, independent additions. |

**Verdict**: G1–G3 (full response object, header wiring, layout-free `dom()`) unlock ~80% of static
scraping and are all "expose existing machinery." G4–G5 make it production-grade. G6 unlocks dynamic
pages. None require new *language* features — the earlier `pn_fetch`, cookie jar, CSS matcher, and
headless runtime already exist; the work is surfacing them to scripts.

---

## 4. Architecture

### 4.1 Layered stack

```
  User Lambda script  (import scrape: lambda.package.scrape.scrape)
      │
  ┌───────────────────────────────────────────────────────────────────────┐
  │ [L4]  ORCHESTRATION  (Lambda)                                          │
  │   crawler.ls  — request frontier, URL dedup (seen-set), depth/limit,   │
  │                 autothrottle, worker pool (start/mailbox), router      │
  │   collector.ls — Colly-style OnHTML/OnRequest/OnResponse callbacks     │
  │   robots.ls   — robots.txt fetch/parse, allow/deny, crawl-delay        │
  ├───────────────────────────────────────────────────────────────────────┤
  │ [L3]  EXTRACT / SELECT  (Lambda)                                       │
  │   dom.ls      — HTML string/URL → CSS-queryable node; css()/xpath-lite │
  │   select.ls   — extractors: text, attr, html; ::text/::attr combinators│
  │   extract.ls  — readability (wraps utils/readability2.ls), tables,     │
  │                 JSON-LD / OpenGraph / microdata metadata, page→markdown│
  │   item.ls     — item builder + schema validation + dedup keys          │
  ├───────────────────────────────────────────────────────────────────────┤
  │ [L2]  FETCH / SESSION  (Lambda over native)                           │
  │   http.ls     — Request/Response model, retry+backoff, per-host        │
  │                 rate-limit, cache policy                               │
  │   session.ls  — cookie jar handle, default headers, proxy pool,        │
  │                 session rotation (Crawlee SessionPool)                 │
  │   render.ls   — headless-browser bridge to Radiant for JS pages (P2)   │
  ├───────────────────────────────────────────────────────────────────────┤
  │ [L1]  NATIVE ENABLERS  (C+ — §6; expose existing machinery)           │
  │   full-response fetch · dom_from_html() · session/cookie wiring ·      │
  │   proxy/retry/timeout opts · headless render+settle mode               │
  └───────────────────────────────────────────────────────────────────────┘
      │                                    │
  lambda/input/html5  ·  lambda/input/css/selector_matcher  ·  lambda/network/*
  radiant/script_runner (headless JS)  ·  lambda/concurrency  ·  lambda/validator
```

### 4.2 Data-flow (declarative crawler)

```
seed URLs ──▶ [frontier: bounded mailbox + seen-set dedup]
                   │  (worker pool: N × `start fetch_worker()`; N from autothrottle)
                   ▼
             [session: attach cookies + headers + proxy]
                   ▼
             [fetch: Request → Response^E]  ── retryable? ──▶ backoff+requeue
                   │ ok                                          (sleep, jitter)
                   ▼
             [render? if page needs JS → Radiant headless settle]  (P2)
                   ▼
             [dom(response.body) → CSS-queryable node]
                   ▼
             [user handler(response, dom, ctx)]
                   ├──▶ ctx.enqueue(url, label)   → frontier (dedup)
                   └──▶ ctx.emit(item)            → pipeline
                                                     ▼
                              [validate against schema] ──▶ [export: json/jsonl/yaml/csv]
```

### 4.3 Concurrency model (maps 1:1 onto Lambda actors)

- **Frontier** = a bounded FIFO **mailbox** on a coordinator `pn`. Requests are `send()`-ed in; workers `receive()`.
- **Worker pool** = `N` children via `start fetch_worker(session, sink)`. `N` starts small and is
  adjusted by the **autothrottle** signal (below), Crawlee-style.
- **Rate limiting** = per-host `sleep(delay)` before each fetch, `delay` from robots crawl-delay or
  a target-configured minimum, plus `math.random` jitter.
- **Dedup** = a `seen` set keyed by normalized-URL (or a content hash once G7 lands) held by the
  coordinator; `start` capture rules mean workers report new URLs back via mailbox, not shared state.
- **Autothrottle** = adjust concurrency/delay from observed latency + error rate (Lambda has
  `clock()` for latency; Crawlee's CPU/mem/loop-lag signals are a Phase-3 refinement once a
  resource-probe sysfunc exists). v1 uses latency-and-error AIMD, which needs no new primitive.
- **Graceful stop** = coordinator closes the frontier and `wait`s on all worker handles; error-exit
  cancels then joins (Lambda's structured-concurrency default).

---

## 5. Components and API

Package lives at `lambda/package/scrape/` (matching the chart/graph/latex convention), entry module
`scrape.ls`. Imported as `import scrape: lambda.package.scrape.scrape`.

All examples use **verified** Lambda syntax. Signatures are written in Lambda type syntax (which
doubles as machine-checkable docs, per the package convention).

### 5.1 Tier 1 — one-shot (requests + BeautifulSoup style)

```lambda
import scrape: lambda.package.scrape.scrape

pn main() {
    // fetch → full response object (G1), then CSS-query the DOM (G3)
    let page = scrape.get("https://news.ycombinator.com")^

    print(page.status)                       // 200
    print(page.headers.content_type)          // "text/html; charset=utf-8"

    let doc = scrape.dom(page.body)           // HTML string → queryable node
    let titles = doc.css(".titleline > a")    // full CSS3/4 selector (G3)
        |> ~.text                             // ::text extraction combinator

    let items = doc.css(".athing") |> {
        rank:  ~.css_first(".rank").text,
        title: ~.css_first(".titleline > a").text,
        url:   ~.css_first(".titleline > a").attr("href")
    }
    output(items |> format(~, 'json'), "./temp/hn.json")
}
```

**Core one-shot API** (`scrape.ls` facade):

| Signature | Purpose |
|---|---|
| `pub pn get(url: string) Response^` | GET → full response (status, headers, url, body, cookies) |
| `pub pn get(url: string, opts: RequestOpts) Response^` | with headers/timeout/proxy/... |
| `pub pn post(url: string, opts: RequestOpts) Response^` | POST/PUT/DELETE via `opts.method` |
| `pub fn dom(html: string) Node` | parse HTML string → CSS-queryable node (layout-free) |
| `pub pn fetch_dom(url: string, opts: RequestOpts?) Node^` | get + dom in one call |

### 5.2 The Node / selection API (BeautifulSoup / parsel ergonomics)

Backed by the native CSS matcher (§6 E3). `Node` wraps a `DomElement`; a node-list is a Lambda array,
so `|>`, `that`, `for`, and slicing all work on results for free.

| Member | Returns | Notes |
|---|---|---|
| `n.css(sel: string)` | `Node[]` | `query_selector_all` — full CSS3/4 |
| `n.css_first(sel: string)` | `Node?` | `query_selector` |
| `n.text` | `string` | collapsed `text_content` |
| `n.html` | `string` | `innerHTML` serialization |
| `n.attr(name: string)` | `string?` | attribute value |
| `n.attrs` | `{...}` | all attributes as a map |
| `n.name` | `symbol` | tag name |
| `n.matches(sel)` / `n.closest(sel)` | `bool` / `Node?` | |
| `n.parent` / `n.children` / `n.next` / `n.prev` | `Node?`/`Node[]` | navigation |

Because results are ordinary Lambda arrays, extraction reads like data processing:

```lambda
// prices over $100, as floats, sorted desc
let expensive = doc.css(".product") that (~.css_first(".price").text |> to_price > 100.0)
    |> { name: ~.css_first("h2").text, price: ~.css_first(".price").text |> to_price }
    |> sort(~, { key: fn(p) => p.price, desc: true })
```

For users who prefer the native tree-query (no DOM build, works on `input()` directly), the package
re-documents it as the "lite" path: `page?<a>`, `el[<img>]`, `that (...)` — weaker (no class-token or
combinator matching) but zero-overhead.

### 5.3 Tier 2 — collector (Colly style)

```lambda
pn main() {
    let c = scrape.collector({
        user_agent: "LambdaBot/1.0 (+https://example.com/bot)",
        max_depth: 3,
        rate_limit: { per_host: 1, delay_ms: 500 },   // 1 concurrent/host, 500ms gap
        obey_robots: true
    })

    // fire on every element matching the selector, on every visited page
    c.on_html("a[href]", fn(el, ctx) {
        ctx.visit(el.attr("href"))                    // enqueue (auto-dedup, depth-tracked)
    })
    c.on_html("article.post", fn(el, ctx) {
        ctx.emit({
            title: el.css_first("h1").text,
            body:  el.css_first(".content").text,
            url:   ctx.request.url
        })
    })
    c.on_error(fn(err, ctx) { print("failed:", ctx.request.url, err.message) })

    c.visit("https://blog.example.com")^
    c.export("./temp/posts.jsonl", 'jsonl')^         // collected items → file
}
```

| Signature | Purpose |
|---|---|
| `pub fn collector(opts: CollectorOpts) Collector` | build a collector |
| `Collector.on_html(sel, cb: fn(Node, Ctx))` | callback per matching element per page |
| `Collector.on_response(cb: fn(Response, Ctx))` / `.on_request` / `.on_error` | lifecycle hooks |
| `Collector.visit(url) ok^` | seed + run to frontier exhaustion |
| `Collector.export(path, fmt) ok^` | flush items |

`Ctx` carries `request`, `response`, `visit(url)`/`enqueue(url, label?)`, `emit(item)`, and `depth`.

### 5.4 Tier 3 — declarative crawler (Scrapy / Crawlee style)

The full-power tier: a request frontier with dedup, a labeled router, autothrottled concurrency, a
session pool, and an item pipeline. This is where Lambda's actor concurrency does the heavy lifting.

```lambda
pn main() {
    let crawler = scrape.crawler({
        concurrency: { min: 2, max: 16, autothrottle: true },   // AutoscaledPool-style
        session_pool: { size: 8, rotate_on_status: [403, 429] }, // Crawlee SessionPool
        retry: { max: 3, backoff_ms: 1000, on_status: [429, 500, 502, 503] },
        proxies: [ "http://p1:8080", "http://p2:8080" ],         // rotation (G5)
        obey_robots: true,
        item_schema: Product                                     // validated before export
    })

    // label-routed handlers (Crawlee router pattern)
    crawler.route("list", fn(res, dom, ctx) {
        for link in dom.css("a.product-link") {
            ctx.enqueue(link.attr("href"), "detail")
        }
        let next = dom.css_first("a.next")
        if (next != null) ctx.enqueue(next.attr("href"), "list")
    })
    crawler.route("detail", fn(res, dom, ctx) {
        ctx.emit(<Product
            name:  dom.css_first("h1").text,
            price: dom.css_first(".price").text |> to_price,
            sku:   dom.css_first("[data-sku]").attr("data-sku")
        >)
    })

    crawler.enqueue("https://shop.example.com/catalog", "list")
    let stats = crawler.run()^                    // blocks until frontier drains
    print("scraped", stats.items, "items,", stats.requests, "requests")
    crawler.dataset().export("./temp/products.csv", 'csv')^
}

// record shape — validated on every emit; invalid items routed to an error sink
type Product {
    name: string that (len(~) > 0),
    price: float that (~ >= 0.0),
    sku: string?
}
```

| Signature | Purpose |
|---|---|
| `pub fn crawler(opts: CrawlerOpts) Crawler` | build a crawler |
| `Crawler.route(label, handler: fn(Response, Node, Ctx))` | register a labeled handler |
| `Crawler.enqueue(url, label, meta?) ok` | add to frontier (dedup by normalized URL) |
| `Crawler.run() Stats^` | run worker pool to exhaustion; returns run stats |
| `Crawler.dataset()` / `.key_value_store()` | Crawlee-style storages (export/resume) |

### 5.5 Dynamic pages — render mode (Phase 2)

Same handler API; the only change is `render: true`, which routes the fetch through Radiant's
headless pipeline (execute the page's JS, settle, then hand back the live DOM — §6 E5).

```lambda
crawler.route("spa", fn(res, dom, ctx) {
    // dom here is the POST-JavaScript DOM (SPA content hydrated)
    ctx.emit({ data: dom.css(".lazy-loaded-row") |> ~.text })
}, { render: true, wait_for: ".lazy-loaded-row", settle: "networkidle" })
```

`wait_for` (selector) and `settle` (`networkidle` | `load` | `ms:N`) replace the current hardcoded
5-second drain (G6). Interaction (scroll-to-load, click-through) reuses `radiant/event_sim.cpp`'s
existing driver: `ctx.page.click(sel)`, `.type(sel, text)`, `.scroll_to(sel)`.

### 5.6 Extraction helpers (Firecrawl / Crawl4AI style)

```lambda
let doc = scrape.fetch_dom("https://example.com/article")^

let article  = scrape.readability(doc)        // wraps utils/readability2.ls → {title, byline, content, text}
let markdown = scrape.to_markdown(doc)        // clean page → Markdown (format(_, 'markdown'))
let meta     = scrape.metadata(doc)           // {og:{...}, json_ld:[...], title, description, canonical}
let tables   = scrape.tables(doc)             // [[{col: val}...]] — <table> → array of row-maps
```

| Signature | Purpose |
|---|---|
| `pub fn readability(doc: Node) Article` | main-content extraction (existing `readability2.ls`) |
| `pub fn to_markdown(doc: Node) string` | page/subtree → Markdown |
| `pub fn metadata(doc: Node) Meta` | OpenGraph + JSON-LD + microdata + `<title>`/canonical |
| `pub fn tables(doc: Node) any[]` | HTML tables → row-map arrays |

### 5.7 Session and request model (L2)

```lambda
let sess = scrape.session({
    headers: { "User-Agent": "LambdaBot/1.0", "Accept-Language": "en" },
    cookies_persist: "./temp/cookies.txt"      // reuse the RFC-6265 jar (G4)
})
sess.post("https://site/login", { form: { user: "u", pass: "p" } })^   // sets session cookies
let dash = sess.get("https://site/dashboard")^                          // cookies auto-attached
```

`RequestOpts`: `{ method, headers, body|form|json, timeout_ms, proxy, follow_redirects, verify_tls,
retry, cache }`. `Response`: `{ status, ok, headers, url (final, post-redirect), body, cookies }`.

---

## 6. Native enablers (the C+ work)

Everything below is *exposing/composing existing machinery*, ordered by leverage. Each is an
independent, testable unit. The package can ship Tier-1/2 static scraping after E1–E3.

**E1 — Full response object from `fetch` (closes G1).** Change `fetch_response_to_item`
(`lambda/lambda-proc.cpp:550`) to return a map `{status, headers, url, body, cookies}` instead of the
body string. The `FetchResponse` struct already carries status/headers internally — this is surfacing,
not fetching. *Back-compat*: keep body accessible; add `scrape.get` wrapper so scripts never touch the
raw shape. ~½ day.

**E2 — Wire request options on the script path (closes G2, part of G5).** In `pn_fetch`
(`lambda/lambda-proc.cpp:567`) and `http_fetch` (`lambda/input/input_http.cpp:518`): apply the parsed
`headers` map (currently no-op at `:670`), and thread `timeout`, `follow_redirects`, `verify_tls`, and
`proxy` (new `CURLOPT_PROXY`) from options to the curl handle. Machinery for headers already exists at
`input_http.cpp:575`. ~1 day.

**E3 — Layout-free `dom(html_string)` sysfunc (closes G3 — highest extraction leverage).** New sysfunc
that runs `build_dom_tree_from_element` (`lambda/input/css/dom_element.cpp:3131`) over the Mark tree
from the HTML5 parser and returns a node wrapping the resulting `DomElement`, **without** the Radiant
layout document. Expose `css`/`css_first`/`text`/`attr`/`html`/navigation on it via the existing
`radiant_dom_iface` member table (`radiant_dom_iface.cpp:235`) and `selector_matcher_find_all`
(`selector_matcher.hpp:237`). The in-memory HTML loader `radiant_load_html_source`
(`radiant_module.cpp:900`) already exists but is `static`/layout-coupled — the new path skips layout.
~2–3 days (the meatiest static-path item).

**E4 — Session/cookie wiring (closes G4).** Give the script fetch path an optional session handle that
carries a `CookieJar*` (`lambda/network/cookie_jar.cpp` — already RFC-6265-complete with public-suffix
and persistence). Attach outgoing `Cookie:`, capture `Set-Cookie`, persist on demand. The jar is
production-ready; the work is plumbing it to `http_fetch` and exposing a `session` handle to `.ls`.
~2 days.

**E5 — Retry/backoff + rate-limit on the script path (closes rest of G5).** These can live **in
Lambda** (the `crawler.ls`/`http.ls` modules) using `sleep`, `select(timeout:)`, `clock()`, and
`math.random` — no native work strictly required. Optionally reuse the native backoff
(`network_resource_manager.cpp:874`) later. ~0 native / in-package.

**E6 — Headless render+readback mode (closes G6 — Phase 2, dynamic pages).** Add a host mode (CLI
`lambda.exe render page.html --settle networkidle --wait-for SEL -o dom.html`, and an in-process entry
for the package) that loads with `execute_scripts=true` (as `layout` does, `cmd_layout.cpp:6569`),
runs a **configurable** settle (wait-for-selector / network-idle) replacing the hardcoded
`EVENT_LOOP_DRAIN_TIMEOUT_MS` (`js_event_loop.cpp:1611`), then returns the live DOM
(`documentElement.outerHTML` or a scraper hook in the same realm) — combining what `layout` and
`js --document` each do half of today. ~1–2 weeks (largest item; Phase 2 gate).

**E7 — Ergonomic sys-funcs (closes G7; independent, do as needed).** `url_parse`/`url_encode`
/`query_string` (URL decomposition + percent-encoding); a CSV output formatter in `lambda/format/`
(input CSV exists, output doesn't); `hash`/`base64`/`uuid` sys-funcs (cache keys, content-dedup —
`lambda/js/js_crypto.cpp` has implementations to bind); expose regex capture groups from `find()`
(`re2_wrapper.cpp` uses groups internally already). Each ~½–1 day.

**Effort summary**: static scraping (Tiers 1–2) needs **E1+E2+E3 ≈ 4–5 days native**. Production
crawler (Tier 3) adds **E4 ≈ 2 days** (E5 is in-package). Dynamic pages add **E6 ≈ 1–2 weeks**. E7 is
opportunistic polish. No new language features.

---

## 7. Phasing

**Phase 0 — enablers for static (E1, E2, E3).** Full response object, request-option wiring,
layout-free `dom()`. *Gate*: a `.ls` script can `scrape.get(url)`, read status/headers, and
`css()`-query the result. Ship Tier-1 one-shot API + the Node/selection API + extraction helpers
(readability/markdown/metadata/tables) — all of §5.1, §5.2, §5.6.

**Phase 1 — orchestration + sessions (E4; E5 in-package).** `collector.ls` (Tier 2) and `crawler.ls`
(Tier 3): frontier, dedup, worker pool, autothrottle, retry/rate-limit, session pool, item pipeline
with schema validation, dataset export. *Gate*: crawl a paginated multi-page static site end-to-end
with concurrency + robots + validated export.

**Phase 2 — dynamic rendering (E6).** `render.ls` bridge to Radiant headless with configurable settle
and interaction. *Gate*: scrape a JS-rendered SPA (content absent from raw HTML, present after settle);
same handler API as static.

**Phase 3 — polish (E7 + refinements).** URL/CSV/hash sys-funcs, resource-aware autothrottle (needs a
CPU/mem probe sysfunc), resumable frontier (KeyValueStore persistence), and a robots/sitemap crawler.

---

## 8. Risks

| Risk | Mitigation |
|---|---|
| `dom()` (E3) re-implements too much of `radiant.load` | Reuse `build_dom_tree_from_element` + `selector_matcher_*` verbatim; only skip the layout document. It's a narrower entry to the same code, not a fork. |
| Response-shape change (E1) breaks existing `fetch()` callers | Only `proc_fetch.ls` / `test_io_copy_url.ls` use it in-tree; provide a `scrape.get` wrapper and keep body reachable. Small, contained. |
| Headless render (E6) is heavyweight / flaky settle | Ship static first (Phases 0–1 stand alone); make `render:true` opt-in per route; configurable settle replaces the fixed watchdog. |
| Charset: non-UTF-8 pages mis-decode (parser is UTF-8-centric) | Detect `Content-Type`/`<meta charset>` in E1/E3; transcode via curl/iconv before parse. Track as a known limitation in Phase 0. |
| Concurrency correctness (shared frontier/seen-set) | Lambda's `start`-capture rules forbid shared mutable capture; frontier/dedup live on the coordinator and communicate via mailboxes by construction — the language enforces the safe pattern. |
| No WebSocket in LambdaJS → WS-streaming SPAs won't hydrate | Documented Phase-2 limitation; most SPAs hydrate via `fetch`/XHR which do work headless. |
| Regex without capture groups limits field extraction | CSS selectors cover most extraction; E7 exposes groups; JS-side RegExp is a fallback via cross-language import. |

---

## 9. Open questions

1. **Node identity vs Lambda value semantics.** `dom()` returns nodes wrapping `DomElement*`. Confirm
   `==`/`is` over these behave coherently (same hazard class as the ArrayNum `==` finding). Decide on
   the `MutationObserver`-parity work in `Lambda_Design_DOM_Pkg.md` §5.1.
2. **Relationship to the `dom` package.** `Lambda_Design_DOM_Pkg.md` proposes a broader headless-DOM/
   Web-API package. Should `scrape`'s Node API *be* that package's DOM surface (scrape = thin layer on
   `dom`), or ship independently first and converge later? Leaning: ship `scrape` on the minimal
   `dom()` sysfunc now; adopt the fuller `dom` package's node type when it lands.
3. **Where does crawler state live?** Same "Lambda package mutable state" question as the DOM package
   (§5.1/Q1 there): coordinator-`pn`-local `var` (leaning this — actor model fits) vs a context object.
4. **Autothrottle signal source.** v1 uses latency+error AIMD (no new primitive). Crawlee-style
   CPU/mem/event-loop-lag needs a resource-probe sysfunc — Phase 3, shared with any future scheduler.
5. **`serve` overlap.** `lambda/serve` has a cookie parser and HTTP infra. Is there shared code between
   the *server* cookie handling and the *client* jar? Probably not (client jar is richer) — note it.

---

## Appendix A. Source map

| File | Relevance |
|---|---|
| `lambda/lambda-proc.cpp` (`pn_fetch` :567, `fetch_response_to_item` :550) | E1/E2 — script fetch entry; response shaping |
| `lambda/input/input_http.cpp` (`http_fetch` :518, headers :575) | E2 — curl request construction |
| `lambda/input/html5/` | HTML5 parser (present, spec-grade) |
| `lambda/input/css/selector_matcher.{hpp,cpp}` (`find_all` :237) | E3 — CSS query engine |
| `lambda/input/css/dom_element.cpp` (`build_dom_tree_from_element` :3131) | E3 — Mark → DomElement bridge |
| `lambda/module/radiant/radiant_dom_bridge.cpp`, `radiant_dom_iface.cpp` (:235) | E3 — DOM member table already surfaced to `.ls` |
| `lambda/network/cookie_jar.cpp`, `public_suffix.cpp` | E4 — RFC-6265 jar (present) |
| `lambda/network/network_resource_manager.cpp` (backoff :874), `network_scheduler.h` | E5 — retry/backoff, per-origin caps (present, Radiant-only) |
| `radiant/script_runner.cpp`, `radiant/cmd_layout.cpp` (:6569), `radiant/event_sim.cpp` | E6 — headless JS execution + settle + interaction |
| `lambda/js/js_event_loop.cpp` (`EVENT_LOOP_DRAIN_TIMEOUT_MS` :1611) | E6 — settle watchdog to make configurable |
| `lambda/concurrency.cpp`, `test/lambda/conc/` | crawler worker pool / frontier |
| `lambda/validator/`, `doc/Lambda_Validator_Guide.md` | item schema validation |
| `utils/readability2.ls` | main-content extraction (existing Lambda port) |
| `lambda/package/{chart,graph,latex}/` | package layout precedent |
