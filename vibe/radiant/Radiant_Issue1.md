# Radiant Issue #1: Vercel-Protected Badge Image Fails to Load

## Symptom

In `test/layout/data/markdown/md_commander-readme.html`, the 4th install-size
badge fails to render while the other three render correctly.

The view tree shows `img-NOT-LOADED` for:

```
https://packagephobia.now.sh/badge?p=commander
```

The other three badges in the same document load fine because they have been
rewritten to local cached files under `test/layout/data/support/images/`.

## Root Cause

`packagephobia.now.sh` is hosted on Vercel and protected by Vercel's bot
mitigation. Every request from Radiant (libcurl) receives:

```
HTTP/2 429
x-vercel-mitigated: challenge
```

This is **not** an issue with Radiant's HTTP code path — `download_http_content()`
in `lambda/input/input_http.cpp` works correctly for all normal endpoints
(verified end-to-end with `https://avatars.githubusercontent.com/u/1?s=128`
PNG and `https://img.shields.io/badge/test-passing-green` SVG; both downloaded,
decoded, and rendered fine).

The failure is server-side fingerprint-based bot detection.

### Why a real browser succeeds without solving any visible challenge

Vercel's `challenge` mode is tiered. It scores each request on many low-level
signals; if the score is "browser-like enough", the response is a normal 200
and **no JS challenge is ever served**. Signals include:

- TLS fingerprint (JA3 / JA4 — cipher suite order, ALPN, extensions)
- HTTP/2 SETTINGS and HEADERS frame ordering
- Header *order* and *casing*
- Presence of `sec-ch-ua`, `sec-ch-ua-mobile`, `sec-ch-ua-platform`
- Presence of `sec-fetch-site`, `sec-fetch-mode`, `sec-fetch-dest`, `sec-fetch-user`
- `Accept`, `Accept-Language`, `Accept-Encoding` values matching browser defaults
- `upgrade-insecure-requests`
- TCP/IP and TLS timing characteristics

Stock libcurl + OpenSSL fails this fingerprint check before any JS challenge
page is even returned, so we just get 429.

## What We Tried

| Attempt | Result |
|---|---|
| Default UA `Lambda-Script/1.0` | HTTP 429 + `x-vercel-mitigated: challenge` |
| Spoofed UA `Mozilla/5.0 (Macintosh; ... Chrome/120 ...)` via curl `-A` | HTTP 429 (UA alone is one of ~20 signals; ignored) |
| UA + browser-style `Accept`, `Accept-Language`, `sec-ch-ua`, `sec-fetch-*` headers via curl `-H` | HTTP 429 (TLS/HTTP-2 fingerprint still wrong) |
| Followed redirects, enabled HTTP/2, gzip — already on by default in `download_http_content()` | No effect |

Reproduction (still 429 with full browser-like headers):

```bash
curl -sI -L \
  -A "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36" \
  -H "Accept: image/avif,image/webp,*/*" \
  -H "Accept-Language: en-US,en;q=0.9" \
  -H "sec-ch-ua: \"Chromium\";v=\"120\"" \
  -H "sec-fetch-dest: image" \
  -H "sec-fetch-mode: no-cors" \
  -H "sec-fetch-site: cross-site" \
  "https://packagephobia.now.sh/badge?p=commander" | grep -iE "HTTP|x-vercel"
```

This proves the gating is at the TLS / HTTP-2 layer, not the HTTP header layer.

## Why Header-Level Fixes Cannot Work

libcurl built against OpenSSL has a fixed TLS ClientHello (cipher list, curve
list, extension order, ALPN entries) that does not match any released browser.
Vercel's edge classifies it as "automated" within milliseconds. No combination
of `CURLOPT_USERAGENT` / `CURLOPT_HTTPHEADER` can change this.

## Possible Options

| Option | Cost | Coverage | Notes |
|---|---|---|---|
| **Cache locally in test data** | Low | Only the specific failing URL | Matches how the other 3 badges are already handled. Recommended for the immediate test fix. |
| **Use a non-Vercel equivalent** | Low | Only this badge | e.g. `https://img.shields.io/bundlephobia/minzip/commander` or a `badgen.net` URL. shields.io is not Vercel-gated and works today. |
| **Document as a known limitation** | Zero | None | Accept `img-NOT-LOADED` placeholder in the rendered output. |
| **Switch to [`curl-impersonate`](https://github.com/lwthiker/curl-impersonate)** | High — custom build of libcurl against BoringSSL/NSS, larger binary, ongoing maintenance to track Chrome/Firefox releases | Most Vercel/Cloudflare/Akamai fingerprint-protected sites | Patches cipher list, extension order, and HTTP/2 frames to match Chrome/Firefox JA3. Drop-in API-compatible with libcurl. |
| **Headless browser fallback (Chromium via CDP) for blocked images** | Very high — bundle/launch Chromium, IPC, sandboxing, large dependency | Everything, including pages that require running JS challenges | Reasonable if Radiant ever needs to load arbitrary modern web pages, but enormous scope creep for an image-loader. |
| **Out-of-band prefetch step** | Medium | Anything fetchable by an external tool the user picks | A `make` target / preprocessor that uses the user's system browser or `curl-impersonate` to mirror remote assets into the test data dir before layout. Keeps Radiant's runtime simple. |

## Recommendation

For the failing test:
1. Replace the `packagephobia.now.sh` `<img src>` with a locally cached copy
   (download once via real browser, place under
   `test/layout/data/support/images/`, rewrite the URL).

For Radiant in general:
1. Keep stock libcurl. The vast majority of image-hosting CDNs (GitHub, shields.io,
   imgix, generic S3/CloudFront, npm, etc.) work today.
2. If broader compatibility with bot-protected sites becomes important, evaluate
   `curl-impersonate` as a build-time alternative HTTP backend, gated behind a
   config flag in `build_lambda_config.json`.

## Verified Working HTTP Image Loading

The HTTP image pipeline is fully functional. Reproducer:

```html
<!-- temp/test_http_img.html -->
<!DOCTYPE html>
<html><body>
<p>
<img src="https://avatars.githubusercontent.com/u/1?s=128" alt="avatar">
<img src="https://img.shields.io/badge/test-passing-green" alt="badge">
</p>
</body></html>
```

```bash
./lambda.exe render temp/test_http_img.html -o temp/test_http_img.png
```

Both images download (HTTP 200), decode (PNG raster + SVG vector), and render
correctly into the output PNG.
