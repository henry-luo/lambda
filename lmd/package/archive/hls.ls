// HLS playlists (RFC 8216), parsed without IO (AR4v2). A master playlist
// gives its variants and renditions; a media playlist its segments, each
// with its byte range, init map, discontinuity and key method. Every URI is
// resolved against the playlist's URL.
import rewrite: .rewrite

// one attribute: NAME=value, the value quoted (commas allowed inside) or bare
type attr_pair = \(("A" to "Z" | "0" to "9" | "-")+ "=" ("\"" (!"\"")* "\"" | (!",")+))

fn unquote(v) => if (len(v) >= 2 and starts_with(v, "\"") and ends_with(v, "\"")) slice(v, 1, len(v) - 1) else v
// an attribute list as a map keyed by the attribute names
pub fn attrs(text) => reduce([{}, *[for (m in find(text or "", attr_pair)) m.value]],
    (acc, kv) => (let eq = index_of(kv, "="), {*: acc, [slice(kv, 0, eq)]: unquote(slice(kv, eq + 1))}))
fn after(line, tag) => slice(line, len(tag))
fn to_number(v) => if (v == null) null else (let n = float(v), if (n is error) null else n)
fn whole(v) => if (v == null) null else (let n = int(v), if (n is error) null else n)
fn size_of(res) => if (res == null) [null, null]
    else (let x = index_of(lower(res), "x"), if (x == null) [null, null] else [whole(slice(res, 0, x)), whole(slice(res, x + 1))])

// "n@o" or "n": {length, start}, the start null when it continues the previous range
pub fn byte_range(spec) => if (spec == null) null
    else (let at = index_of(spec, "@"), if (at == null) {length: whole(spec), start: null}
        else {length: whole(slice(spec, 0, at)), start: whole(slice(spec, at + 1))})

fn lines_of(text) => [for (l in split(replace(text or "", "\r", ""), "\n"), let t = trim(l) where t != "") t]

pub fn is_master(text) => any([for (l in lines_of(text)) starts_with(l, "#EXT-X-STREAM-INF:")])

// the variants of a master playlist, each with the URI on the line after its tag
pub fn variants(text, url) {
    let ls = lines_of(text);
    [for (i, l in ls where starts_with(l, "#EXT-X-STREAM-INF:") and i + 1 < len(ls) and not starts_with(ls[i + 1], "#"))
        (let a = attrs(after(l, "#EXT-X-STREAM-INF:")), let wh = size_of(a.RESOLUTION),
         {uri: rewrite.resolve(url, ls[i + 1]), bandwidth: whole(a.BANDWIDTH), width: wh[0], height: wh[1],
          codecs: a.CODECS, audio: a.AUDIO, subtitles: a.SUBTITLES})]
}

// the alternative renditions (#EXT-X-MEDIA): audio, subtitles, closed captions
pub fn renditions(text, url) => [for (l in lines_of(text) where starts_with(l, "#EXT-X-MEDIA:"))
    (let a = attrs(after(l, "#EXT-X-MEDIA:")),
     {type: a.TYPE, group: a.'GROUP-ID', name: a.NAME, language: a.LANGUAGE, default: a.DEFAULT == "YES",
      uri: if (a.URI == null) null else rewrite.resolve(url, a.URI)})]

// the media playlist's state as each line is read: the tags pending for the
// next URI, the current map and key, where an implied byte range starts
let START = {segments: [], duration: null, range: null, discontinuity: false, map: null, key: "NONE", next: 0, endlist: false}

fn step(st, line, url) {
    if (starts_with(line, "#EXTINF:")) {*: st, duration: to_number(split(after(line, "#EXTINF:"), ",")[0])}
    else if (starts_with(line, "#EXT-X-BYTERANGE:")) {*: st, range: byte_range(after(line, "#EXT-X-BYTERANGE:"))}
    else if (line == "#EXT-X-DISCONTINUITY") {*: st, discontinuity: true}
    else if (line == "#EXT-X-ENDLIST") {*: st, endlist: true}
    else if (starts_with(line, "#EXT-X-KEY:")) {*: st, key: (attrs(after(line, "#EXT-X-KEY:")).METHOD or "NONE")}
    else if (starts_with(line, "#EXT-X-MAP:"))
        (let a = attrs(after(line, "#EXT-X-MAP:")), let r = byte_range(a.BYTERANGE),
         {*: st, map: {uri: rewrite.resolve(url, a.URI), range: if (r == null) null else {length: r.length, start: r.start or 0}}})
    else if (starts_with(line, "#")) st
    else (let r = st.range,
        let span = if (r == null) null else {length: r.length, start: if (r.start == null) st.next else r.start},
        let seg = {uri: rewrite.resolve(url, line), duration: st.duration, range: span, map: st.map,
            discontinuity: st.discontinuity, key: st.key},
        {*: st, segments: [*st.segments, seg], duration: null, range: null, discontinuity: false,
         next: if (span == null) 0 else span.start + span.length})
}

fn container_of(segments) => if (len(segments) == 0) null
    else if (segments[0].map != null) "fmp4"
    else (let path = lower(split(split(segments[0].uri, "?")[0], "#")[0]),
        if (ends_with(path, ".ts")) "ts" else if (ends_with(path, ".aac")) "aac"
        else if (ends_with(path, ".m4s") or ends_with(path, ".mp4")) "fmp4" else "ts")

// a media playlist: its segments, container, total duration and whether any segment is encrypted
pub fn media(text, url) {
    let st = reduce([START, *lines_of(text)], (acc, line) => step(acc, line, url))
    let segs = st.segments;
    {segments: segs, container: container_of(segs), endlist: st.endlist,
     duration: sum([for (s in segs) s.duration or 0]), encrypted: any([for (s in segs) s.key != "NONE"])}
}

// the variants in the order to try: at most `max_height` tall (never upscaled;
// when every variant is taller, the smallest), then the tallest and widest bandwidth first
pub fn ranked(vs, max_height) {
    let fit = [for (v in vs where v.height == null or v.height <= max_height) v]
    let pool = if (len(fit) > 0) fit else take(sort(vs, {by: (v) => v.height or 0}), 1);
    sort(pool, {dir: 'desc', by: (v) => [v.height or 0, v.bandwidth or 0]})
}
