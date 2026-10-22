import archive: lambda.archive
import hls: lambda.archive.hls
import vtt: lambda.archive.vtt

// M3 acceptance over the local fixture site (site/make_media.py): the media
// engine reassembles HLS streams to the fixture's bytes — fMP4 after its init
// map at the 1080p cap, MPEG-TS across a discontinuity, byte ranges into one
// file, split audio and video renditions — streams a progressive <video> and
// a direct .mp3, turns WebVTT into the transcript; the gallery engine keeps
// the images over the pixel threshold; pages naming nothing downloadable are
// archived as pages with the reason (AR4v2, AR-O14); `doctor` reports.
let SITE = "test/lambda/archive/site/"

// the reassembled file's bytes are the parts' bytes, in order
pn same_as(path, parts) bool {
    var want = null
    for (p in parts) {
        let b = input(SITE ++ p, 'binary')^
        want = if (want == null) b else want ++ b
    }
    input(path, 'binary')^ == want
}

pn collect() {
    var media = []
    var running = true
    while (running) {
        let m = receive()^
        if (m.type == 'stop') {
            send(m.reply, media)^
            running = false
        } else if (m.type == "progress" and m.stage == "media") {
            media = [*media, [m.received, m.total]]
        }
    }
}

pn main() {
    // a folder per process: the T0 and JIT runs of this test may overlap
    let root = "temp/archive_test/media-" ++ string(sys.proc.self.pid#)
    if (exists(root)) io.delete(root)^
    let site = "file://" ++ sys.proc.self.cwd# ++ "/" ++ SITE
    let lib = archive.library(root, {config: {defaults: {per_host_delay_ms: 0}}})^
    let dir_of = (s) => root ++ "/snapshots/" ++ s.id
    let c = start(collect)
    let stream = archive.add(lib, site ++ "stream.html", {kind: "video", progress: pn (m) => { send(c, m)^ }})^
    send(c, {type: 'stop', reply: self()})^
    let segment_progress = receive()^
    let video = archive.add(lib, site ++ "video.html", {kind: "video"})^
    let ts = archive.add(lib, site ++ "og.html", {kind: "video"})^
    let ranged = archive.add(lib, site ++ "jsonld.html", {kind: "video"})^
    let two = archive.add(lib, site ++ "split.html", {kind: "video"})^
    let podcast = archive.add(lib, site ++ "podcast.html", {kind: "audio"})^
    let mp3 = archive.add(lib, site ++ "media/tone.mp3")^
    let direct = archive.add(lib, site ++ "hls/ts/index.m3u8")^
    let locked = archive.add(lib, site ++ "locked.html", {kind: "video"})^
    let novideo = archive.add(lib, site ++ "novideo.html", {kind: "video"})^
    let pics = archive.add(lib, site ++ "gallery.html", {kind: "gallery"})^
    let plain = archive.add(lib, site ++ "plain.html", {kind: "gallery"})^
    let image = archive.add(lib, site ++ "pics/boats.png")^
    let pdf = archive.add(lib, site ++ "docs/tide-table.pdf")^
    let info = input(dir_of(stream) ++ "/media/info.json", 'json')^
    let gallery_info = input(dir_of(pics) ++ "/media/info.json", 'json')^
    let video_text = input(dir_of(video) ++ "/text.txt", 'text')^
    let page_html = input([for (m in content(input(dir_of(video) ++ "/snapshot.zip")^)^ where m.name == "index.html") m][0], 'text')^
    let renewed = archive.refresh(lib, video.id)^
    let hits = archive.search(lib, "\"lamp house\"")^
    let health = archive.doctor(archive.library(root)^)^

    let results = [
     // the parsers on their own (fn): variant ranking under the cap, implied byte-range offsets
     [for (v in hls.ranked(hls.variants(input(SITE ++ "hls/fmp4/master.m3u8", 'text')^, site), 1080)) v.height] == [1080, 720, 360] and
        [for (s in hls.media(input(SITE ++ "hls/range/index.m3u8", 'text')^, site).segments) s.range.start] == [120, 420, 770],
     vtt.transcript(input(SITE ++ "media/clip.en.vtt", 'text')^) ==
        "The tide comes in over the old pier.\nGulls circle the lamp house.\nBy noon the harbour is quiet again.",
     // fMP4 at 1080p: the init map then four segments, its English subtitles, the poster
     stream.kind == "video" and stream.engine.name == "media" and len(stream.media.files) == 1 and
        same_as(dir_of(stream) ++ "/" ++ stream.media.files[0].path, ["hls/fmp4/v1080/init.mp4", "hls/fmp4/v1080/seg0.m4s",
            "hls/fmp4/v1080/seg1.m4s", "hls/fmp4/v1080/seg2.m4s", "hls/fmp4/v1080/seg3.m4s"]) and
        info.files[0].container == "fmp4" and info.files[0].stream.variant.height == 1080 and stream.media.subtitles == ["en"] and
        info.poster != null and stream.outputs.thumb.status == "ok",
     segment_progress[len(segment_progress) - 1] == [4, 4],
     // a progressive <video>: the playable source over the webm one, its track as the transcript
     video.title == "Harbour at dawn — Harbour Notes" and same_as(dir_of(video) ++ "/" ++ video.media.files[0].path, ["media/clip.mp4"]) and
        video.media.subtitles == ["en"] and video.remote[0].reason == "alternative source" and
        contains(video_text, "Gulls circle the lamp house.") and len(split(video_text, "lamp house")) == 2 and
        video.outputs.readable.status == "ok",
     contains(page_html, "<video controls") and contains(page_html, "src=\"media/") and contains(page_html, "<track kind=\"subtitles\""),
     // MPEG-TS across a discontinuity; byte ranges into one file; split renditions as two files
     same_as(dir_of(ts) ++ "/" ++ ts.media.files[0].path, ["hls/ts/part0.ts", "hls/ts/part1.ts", "hls/ts/part2.ts"]) and
        ts.media.files[0].container == "ts",
     same_as(dir_of(ranged) ++ "/" ++ ranged.media.files[0].path, ["hls/range/stream.mp4"]) and ranged.title == "Lighthouse timelapse",
     [for (f in two.media.files) f.role] == ["video", "audio"] and
        same_as(dir_of(two) ++ "/" ++ two.media.files[1].path, ["hls/split/audio/init.mp4", "hls/split/audio/s0.m4s", "hls/split/audio/s1.m4s"]),
     // audio from an enclosure and a direct file; a direct playlist
     podcast.kind == "audio" and same_as(dir_of(podcast) ++ "/" ++ podcast.media.files[0].path, ["media/tone.mp3"]) and
        mp3.kind == "audio" and direct.kind == "video" and direct.media.files[0].container == "ts",
     // nothing downloadable: archived as a page, saying why (AR-O14)
     locked.kind == "page" and locked.note == "encrypted stream (AES-128): needs AR-E9" and len(locked.remote) == 1 and
        novideo.kind == "page" and novideo.note == "no downloadable media" and
        novideo.remote[0].url == "https://player.example/embed/abc123",
     // the gallery: full sizes behind thumbnails, the widest srcset, <picture>, og:image; icons and spacers out
     pics.kind == "gallery" and [for (f in gallery_info.files) f.path] ==
        ["media/boats.png", "media/nets.png", "media/wide-960.png", "media/gull.png", "media/cover.png"] and
        [for (f in gallery_info.files) f.caption] == ["Boats at rest in the inner basin", "nets", "The whole bay", "A gull", null] and
        [for (x in gallery_info.skipped) x.reason] == ["too small (32×32)", "too small (1×1)"] and
        all([for (f in gallery_info.files) exists(dir_of(pics) ++ "/" ++ f.thumb)]) and pics.media.skipped == 2,
     plain.kind == "page" and plain.note == "no image of at least 40000 pixels",
     image.kind == "image" and image.media.files[0].width == 300 and pdf.kind == "file" and
        same_as(dir_of(pdf) ++ "/" ++ pdf.media.files[0].path, ["docs/tide-table.pdf"]),
     // refresh keeps media/ and re-derives from it; search reads transcripts, a description match first
     renewed.outputs.html.status == "ok" and exists(dir_of(video) ++ "/" ++ video.media.files[0].path) and renewed.refreshed != null,
     hits[0].snapshot.id == ranged.id and sort([for (h in hits) h.snapshot.id]) == sort([ranged.id, stream.id, video.id]) and
        all([for (h in hits) contains(h.snippet, "lamp house")]),
     health.problems == [] and health.snapshots == 14 and health.kinds.video.count == 6 and health.kinds.page.count == 3 and
        health.kinds.gallery.count == 1 and health.missing == [] and health.staging == [] and health.bytes > 0]
    io.delete(root)^
    results
}
