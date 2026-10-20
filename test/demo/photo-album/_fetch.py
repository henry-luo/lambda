"""Select and download the photo album demo corpus from Wikimedia Commons.

Standard library only. Typical use from the repository root:

    python3 -I test/demo/photo-album/_fetch.py             # download photos.json's selection
    python3 -I test/demo/photo-album/_fetch.py --refresh   # re-resolve the selection first
    python3 -I test/demo/photo-album/_fetch.py --verify    # check local files, download nothing
    python3 -I test/demo/photo-album/_fetch.py --refresh --no-download   # selection only

The selection is pinned in photos.json (committed). Without --refresh the script only
downloads what the manifest lists. --refresh crawls the album categories in ALBUMS,
filters candidates (JPEG, capture date, camera make, free license, size, content) and
picks a deterministic set of TOTAL photos. API responses are cached under
temp/photo-album/api/ so a repeated --refresh does not query Commons again.

Photos are the 1280 px-wide Commons thumbnails, written to photos/ (git-ignored).
The manifest's sha1/bytes describe that downloaded copy; source_sha1 is the original's.
"""
import argparse, hashlib, html, json, os, re, sys, time, unicodedata, urllib.error, urllib.parse, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
MANIFEST = os.path.join(HERE, "photos.json")
PHOTOS = os.path.join(HERE, "photos")
CACHE = os.path.join(ROOT, "temp", "photo-album", "api")
API = "https://commons.wikimedia.org/w/api.php"
UA = "LambdaScriptDemo/1.0 (https://github.com/henry-luo/lambda; photo album demo corpus)"
WIDTH = 1280          # a standard Commons thumbnail step
TOTAL = 1000
MIN_SHORT_EDGE = 1600 # of the original
MAX_ASPECT = 3.0      # skip extreme panoramas
LICENSES = re.compile(r"^(CC0|CC BY(-SA)? [0-9.]+|Public domain|PD\b)", re.I)

# album key, title, quota, [(category, depth)]; depth 1 also reads direct subcategories.
# Order matters: a file listed under several albums belongs to the first.
ALBUMS = [
    ("landscapes", "Landscapes", 120, [("landscapes", 0), ("forests", 0), ("lakes", 0), ("waterfalls", 0), ("volcanoes", 0)]),
    ("mountains", "Mountains", 80, [("mountains", 1)]),
    ("coasts", "Coasts and sea", 100, [("coasts", 1), ("beaches", 0), ("lighthouses", 1), ("boats", 0), ("islands", 0)]),
    ("cities", "Cities", 120, [("cityscapes", 1), ("skylines", 0), ("trams", 0)]),
    ("architecture", "Architecture", 100, [("buildings", 1), ("palaces", 0), ("castles", 1), ("bridges", 1), ("churches", 1)]),
    ("animals", "Animals", 120, [("birds", 1), ("mammals", 1), ("animals", 1), ("cats", 0), ("dogs", 0), ("reptiles", 1)]),
    ("plants", "Plants and flowers", 80, [("flowers", 1), ("trees", 0), ("fungi", 0), ("plants", 0)]),
    ("sky", "Night and sky", 60, [("sunsets", 0), ("astronomy", 1), ("the Milky Way", 0), ("clouds", 0), ("lightning", 0), ("storms", 0)]),
    ("food", "Food", 40, [("food", 1)]),
    ("weather", "Fog, snow and ice", 60, [("fog", 0), ("snow", 0), ("ice", 0), ("Christmas", 0)]),
    ("people", "People and events", 120, [("people", 1), ("children", 0), ("concerts", 1), ("sports", 1), ("dance", 1), ("musicians", 1)]),
]
assert sum(a[2] for a in ALBUMS) == TOTAL

# Keep a family-friendly demo that may remix photos into collages and films: skip
# sensitive subjects, and photos Commons flags with a personality-rights warning
# (identifiable people whose likeness needs consent for such uses).
BLOCK = re.compile(r"nud(e|ity)|naked|erotic|topless|lingerie|corpse|dead bod|cadaver|execution|blood|wound|war crime|"
                   r"slaughter|hunting trophies|prostitut|beggar|malnou?rish|starv|poverty|child labou?r|working child|"
                   r"child working|protest|riot|refugee|funeral|bullfight|prison|weapon|firearm|rifle|military|soldier|"
                   r"airmen|army|personality rights", re.I)
# categories that describe the file rather than the picture
HOUSEKEEPING = re.compile(r"photograph|picture|image|upload|user:|\bby\b|flickr|featured|quality|valued|potd|poty|"
                          r"assessment|aspect ratio|warning|rights|license|gfdl|cc-|pd-|wiki|taken|selection|"
                          r"notification|review|focus stack|hdr|exposure|rule of thirds|symmetry|changes of|reused|"
                          r"with known|with address|extracted|edited|cropped|retouch|panoram|media|files|template|"
                          r"commons|camera|lens|f-number|iso |self-published|supported by|created with|\bfal\b|"
                          r"special|derivative|vrts|permission|pages |migrated|parameter|identifier|mérimée|lateral aspect|"
                          r"\.(ch|com|org|net)\b|^\d+ (person|people)|\d{4}-\d\d-\d\d|^\w+ \d{4} ", re.I)

# ---------------------------------------------------------------- HTTP

def get(url):
    """GET with a descriptive User-Agent, honouring 429 Retry-After and backing off on 5xx."""
    for attempt in range(8):
        request = urllib.request.Request(url, headers={"User-Agent": UA})
        try:
            with urllib.request.urlopen(request, timeout=120) as response:
                return response.read()
        except urllib.error.HTTPError as failure:
            if failure.code not in (429, 500, 502, 503, 504):
                raise
            wait = float(failure.headers.get("Retry-After") or 0) or 5 * (attempt + 1)
        except (urllib.error.URLError, TimeoutError, ConnectionError) as failure:
            wait = 5 * (attempt + 1)
        print("retry in %.0fs: %s" % (wait, url[:100]), file=sys.stderr)
        time.sleep(wait)
    sys.exit("giving up: " + url)

def api(params):
    """One API call, cached by its query string so --refresh reruns are offline."""
    params = dict(params, action="query", format="json", formatversion="2", maxlag="5")
    query = urllib.parse.urlencode(sorted(params.items()))
    key = os.path.join(CACHE, hashlib.sha1(query.encode()).hexdigest() + ".json")
    if os.path.exists(key):
        with open(key) as f:
            return json.load(f)
    data = json.loads(get(API + "?" + query))
    if "error" in data:
        if data["error"].get("code") == "maxlag":
            time.sleep(5)
            return api(params)
        sys.exit("API error: %s" % data["error"])
    os.makedirs(CACHE, exist_ok=True)
    with open(key, "w") as f:
        json.dump(data, f)
    return data

def api_all(params):
    """Follow API continuation and yield each response."""
    cont = {}
    while True:
        data = api(dict(params, **cont))
        yield data
        if "continue" not in data:
            return
        cont = data["continue"]

# ---------------------------------------------------------------- candidates

def subcategories(title):
    out = []
    for data in api_all({"list": "categorymembers", "cmtitle": title, "cmtype": "subcat", "cmlimit": "500"}):
        out += [m["title"] for m in data["query"]["categorymembers"]]
    return sorted(out)

def files_in(title):
    """Pages with image info and coordinates for every file directly in a category."""
    pages = {}
    for data in api_all({"generator": "categorymembers", "gcmtitle": title, "gcmtype": "file", "gcmlimit": "50",
                         "prop": "imageinfo|coordinates", "iiprop": "url|size|sha1|mime|extmetadata|commonmetadata",
                         "iiurlwidth": str(WIDTH), "iiextmetadatalanguage": "en"}):
        for page in data.get("query", {}).get("pages", []):
            seen = pages.setdefault(page["pageid"], {})
            for k, v in page.items():
                if k not in seen or (k == "imageinfo" and v):
                    seen[k] = v
    return pages.values()

def text(value):
    """Plain text from an extmetadata HTML value."""
    if isinstance(value, dict):
        value = value.get("en") or next(iter(value.values()), "")
    value = re.sub(r"<[^>]+>", " ", str(value or ""))
    return re.sub(r"\s+", " ", html.unescape(value)).strip()

def common(info, name):
    for item in info.get("commonmetadata", []):
        if item.get("name") == name:
            value = item.get("value")
            if isinstance(value, list):
                value = value[0].get("value") if value and isinstance(value[0], dict) else (value[0] if value else None)
            return value
    return None

def ratio(value):
    """EXIF rational "110/10" -> 11.0."""
    try:
        num, _, den = str(value).partition("/")
        value = round(float(num) / float(den or 1), 2)
        return int(value) if value.is_integer() else value  # Lambda's json output writes 25.0 as 25
    except (ValueError, ZeroDivisionError):
        return None

def taken(info):
    match = re.match(r"(\d{4})[:-](\d\d)[:-](\d\d)[ T](\d\d):(\d\d)(?::(\d\d))?", str(common(info, "DateTimeOriginal") or ""))
    if not match:
        return None
    y, mo, d, h, mi, s = match.groups()
    if not (1995 <= int(y) <= 2026 and 1 <= int(mo) <= 12 and 1 <= int(d) <= 31 and int(h) < 24 and int(mi) < 60):
        return None
    return "%s-%s-%sT%s:%s:%s" % (y, mo, d, h, mi, s or "00")

def slug(title):
    ascii_title = unicodedata.normalize("NFKD", title).encode("ascii", "ignore").decode()
    return re.sub(r"[^a-z0-9]+", "-", ascii_title.lower()).strip("-")[:40].rstrip("-") or "photo"

def candidate(page, album):
    """Manifest entry for a page, or None when it fails a filter."""
    info = (page.get("imageinfo") or [None])[0]
    if not info or info.get("mime") != "image/jpeg" or "thumburl" not in info:
        return None
    meta = info.get("extmetadata", {})
    when, make = taken(info), str(common(info, "Make") or "").strip()
    license = text(meta.get("LicenseShortName", {}).get("value"))
    w, h = info.get("width", 0), info.get("height", 0)
    if not when or not make or not LICENSES.match(license) or min(w, h) < MIN_SHORT_EDGE or max(w, h) > MAX_ASPECT * min(w, h):
        return None
    title = text(meta.get("ObjectName", {}).get("value")) or re.sub(r"\.jpe?g$", "", page["title"][5:], flags=re.I)
    categories = [c for c in text(meta.get("Categories", {}).get("value")).split("|") if c]
    if BLOCK.search(title) or any(BLOCK.search(c) for c in categories):
        return None
    gps = None
    for c in page.get("coordinates", []):
        if c.get("primary") and c.get("globe", "earth") == "earth":
            gps = {"lat": round(c["lat"], 5), "lon": round(c["lon"], 5)}
    model = str(common(info, "Model") or "").strip()
    camera = {"make": make, "model": model, "lens": str(common(info, "Lens") or common(info, "LensModel") or "").strip(),
              "focal_mm": ratio(common(info, "FocalLength")), "aperture": ratio(common(info, "FNumber")),
              "exposure": str(common(info, "ExposureTime") or ""), "iso": common(info, "ISOSpeedRatings")}
    try:
        camera["iso"] = int(camera["iso"]) if camera["iso"] is not None else None
    except (TypeError, ValueError):
        camera["iso"] = None
    return {
        "pageid": page["pageid"], "album": album, "title": title[:120],
        "description": text(meta.get("ImageDescription", {}).get("value"))[:400],
        "taken": when, "width": info["thumbwidth"], "height": info["thumbheight"], "camera": camera, "gps": gps,
        "keywords": [c for c in categories if not HOUSEKEEPING.search(c) and len(c) <= 40][:10],
        "author": (text(meta.get("Artist", {}).get("value")) or text(meta.get("Credit", {}).get("value")))[:120],
        "license": license, "license_url": text(meta.get("LicenseUrl", {}).get("value")),
        "source_url": info["descriptionurl"], "url": info["thumburl"].split("?")[0], "source_sha1": info["sha1"],
    }

def spread(entries, quota):
    """Deterministic pick favouring GPS, then cycling through capture years."""
    by_year = {}
    for e in sorted(entries, key=lambda e: (e["gps"] is None, e["pageid"])):
        by_year.setdefault(e["taken"][:4], []).append(e)
    years, picked = sorted(by_year), []
    while len(picked) < quota and any(by_year.values()):
        for y in years:
            if by_year[y] and len(picked) < quota:
                picked.append(by_year[y].pop(0))
    return picked

def select():
    claimed, sources, pools = set(), set(), {}
    for key, name, quota, categories in ALBUMS:
        titles = []
        for category, depth in categories:
            title = "Category:Featured pictures of " + category
            titles.append(title)
            if depth:
                titles += subcategories(title)
        pool = []
        for title in titles:
            for page in files_in(title):
                entry = None if page["pageid"] in claimed else candidate(page, key)
                if entry and entry["source_sha1"] not in sources:
                    claimed.add(page["pageid"])
                    sources.add(entry["source_sha1"])
                    pool.append(entry)
        pools[key] = pool
        print("%-13s %4d candidates from %d categories" % (key, len(pool), len(titles)))
    # albums short of their quota hand the deficit to the albums with the largest surplus
    quotas = {key: min(quota, len(pools[key])) for key, _, quota, _ in ALBUMS}
    deficit = TOTAL - sum(quotas.values())
    for key in sorted(quotas, key=lambda k: (-(len(pools[k]) - quotas[k]), k)):
        extra = min(deficit, len(pools[key]) - quotas[key])
        quotas[key] += extra
        deficit -= extra
    if deficit:
        sys.exit("only %d usable photos; need %d" % (TOTAL - deficit, TOTAL))
    photos = []
    for key, _, quota, _ in ALBUMS:
        picked = spread(pools[key], quotas[key])
        if quotas[key] != quota:
            print("%-13s quota %d -> %d" % (key, quota, quotas[key]))
        photos += sorted(picked, key=lambda e: (e["taken"], e["pageid"]))
    for i, e in enumerate(photos, 1):
        e["id"] = i
        e["file"] = "%04d-%s.jpg" % (i, slug(e["title"]))
    return photos

# ---------------------------------------------------------------- manifest and download

FIELDS = ["id", "file", "title", "description", "album", "taken", "width", "height", "color", "camera", "gps", "keywords",
          "author", "license", "license_url", "source_url", "url", "pageid", "source_sha1", "sha1", "bytes"]

def save_manifest(photos):
    albums = [{"key": k, "title": t, "count": sum(p["album"] == k for p in photos)} for k, t, _, _ in ALBUMS]
    doc = {"source": "Wikimedia Commons featured pictures", "generator": "test/demo/photo-album/_fetch.py",
           "width": WIDTH, "albums": albums,
           "photos": [{k: p.get(k) for k in FIELDS if k in p or k in ("color", "sha1", "bytes")} for p in photos]}
    tmp = MANIFEST + ".part"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(doc, f, ensure_ascii=False, indent=2)  # byte-identical to Lambda's json output (_thumbs.ls)
    os.replace(tmp, MANIFEST)

def load_manifest():
    with open(MANIFEST, encoding="utf-8") as f:
        return json.load(f)["photos"]

def sha1_of(path):
    with open(path, "rb") as f:
        return hashlib.sha1(f.read()).hexdigest()

def download(photos, verify_only):
    os.makedirs(PHOTOS, exist_ok=True)
    fetched = skipped = changed = missing = 0
    for n, p in enumerate(photos, 1):
        path = os.path.join(PHOTOS, p["file"])
        if os.path.exists(path) and (p.get("sha1") is None or os.path.getsize(path) == p.get("bytes")):
            digest = sha1_of(path)
            if p.get("sha1") in (None, digest):
                p["sha1"], p["bytes"] = digest, os.path.getsize(path)
                skipped += 1
                continue
        if verify_only:
            missing += 1
            print("missing or different:", p["file"])
            continue
        data = get(p["url"])
        digest = hashlib.sha1(data).hexdigest()
        if p.get("sha1") and p["sha1"] != digest:
            # Commons re-rendered the thumbnail since the manifest was pinned; keep the new bytes
            changed += 1
            print("re-rendered upstream:", p["file"], file=sys.stderr)
        with open(path + ".part", "wb") as f:
            f.write(data)
        os.replace(path + ".part", path)
        p["sha1"], p["bytes"] = digest, len(data)
        fetched += 1
        if fetched % 50 == 0:
            print("%d/%d downloaded" % (n, len(photos)))
            save_manifest(photos)
        time.sleep(0.2)
    stray = sorted(set(os.listdir(PHOTOS)) - {p["file"] for p in photos})
    print("downloaded %d, already present %d, re-rendered %d, missing %d, stray files %d, total %.1f MB"
          % (fetched, skipped, changed, missing, len(stray), sum(p.get("bytes") or 0 for p in photos) / 1e6))
    return missing == 0

def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--refresh", action="store_true", help="re-resolve the selection from Commons")
    parser.add_argument("--verify", action="store_true", help="check local files against the manifest only")
    parser.add_argument("--no-download", action="store_true", help="stop after writing the manifest")
    args = parser.parse_args()
    if args.refresh or not os.path.exists(MANIFEST):
        old = {p["pageid"]: p for p in load_manifest()} if os.path.exists(MANIFEST) else {}
        photos = select()
        for p in photos:  # keep known hashes and colours for files that stay selected under the same name
            prior = old.get(p["pageid"])
            if prior and prior["file"] == p["file"]:
                p.update({k: prior.get(k) for k in ("sha1", "bytes", "color")})
        save_manifest(photos)
    photos = load_manifest()
    if args.no_download:
        return
    ok = download(photos, args.verify)
    if not args.verify:
        save_manifest(photos)
    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
