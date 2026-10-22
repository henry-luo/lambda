#!/usr/bin/env python3
"""Generates the media fixtures of the archive test site (M3,
vibe/Lambda_Pkg_Archive.md): HLS trees (fMP4, TS, byte ranges, split audio,
encrypted), WebVTT tracks, a progressive clip and an audio file, PNG images of
known sizes, and the pages that expose them. The media bytes are labelled
filler, not playable streams: the engine never decodes media, it reassembles
the bytes the server sent, which is what the goldens compare.

Run from the repository root: python3 test/lambda/archive/site/make_media.py
"""
import os
import struct
import zlib

SITE = os.path.dirname(os.path.abspath(__file__))


def write(rel, data):
    path = os.path.join(SITE, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    mode = "wb" if isinstance(data, bytes) else "w"
    with open(path, mode) as f:
        f.write(data)


def filler(label, size):
    """`size` deterministic bytes that name what they stand for."""
    line = ("%s|" % label).encode()
    return (line * (size // len(line) + 1))[:size]


def png(width, height, rgb):
    raw = b"".join(b"\x00" + bytes(rgb) * width for _ in range(height))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


GIF_1X1 = (b"GIF89a\x01\x00\x01\x00\x80\x00\x00\x00\x00\x00\xff\xff\xff!\xf9\x04\x01\x00\x00\x00\x00"
           b",\x00\x00\x00\x00\x01\x00\x01\x00\x00\x02\x02D\x01\x00;")


def media_playlist(segments, head=(), map_line=None, end=True):
    lines = ["#EXTM3U", "#EXT-X-VERSION:7", "#EXT-X-TARGETDURATION:4", "#EXT-X-MEDIA-SEQUENCE:0",
             "#EXT-X-PLAYLIST-TYPE:VOD", *head]
    if map_line:
        lines.append(map_line)
    for seg in segments:
        lines.extend(seg)
    if end:
        lines.append("#EXT-X-ENDLIST")
    return "\n".join(lines) + "\n"


VTT_EN = """WEBVTT

00:00:00.000 --> 00:00:02.000
The tide comes in over the old pier.

00:00:02.000 --> 00:00:04.000
<v Narrator>Gulls circle the <b>lamp house</b>.

00:00:04.000 --> 00:00:06.000
<v Narrator>Gulls circle the <b>lamp house</b>.

NOTE a repeated cue is one line of the transcript

00:00:06.000 --> 00:00:08.500 align:start
By noon the harbour is quiet again.
"""

VTT_FR = """WEBVTT

1
00:00:00.000 --> 00:00:02.000
La marée monte sur la vieille jetée.
"""


def make_hls():
    # fMP4 master: four variants; the 1080p one wins under max_height 1080
    variants = [(360, 640, 800000), (720, 1280, 2500000), (1080, 1920, 5000000), (2160, 3840, 12000000)]
    master = ["#EXTM3U", "#EXT-X-VERSION:7", "#EXT-X-INDEPENDENT-SEGMENTS",
              '#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID="subs",NAME="English",LANGUAGE="en",DEFAULT=YES,URI="subs/en.m3u8"',
              '#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID="subs",NAME="Français",LANGUAGE="fr",URI="subs/fr.m3u8"']
    for height, width, bandwidth in variants:
        master.append('#EXT-X-STREAM-INF:BANDWIDTH=%d,RESOLUTION=%dx%d,CODECS="avc1.640028,mp4a.40.2",SUBTITLES="subs"'
                      % (bandwidth, width, height))
        master.append("v%d/index.m3u8" % height)
        write("hls/fmp4/v%d/init.mp4" % height, filler("fmp4 init %dp" % height, 96))
        for i in range(4):
            write("hls/fmp4/v%d/seg%d.m4s" % (height, i), filler("fmp4 %dp segment %d" % (height, i), 700 + 37 * i))
        write("hls/fmp4/v%d/index.m3u8" % height, media_playlist(
            [["#EXTINF:4.000,", "seg%d.m4s" % i] for i in range(4)], map_line='#EXT-X-MAP:URI="init.mp4"'))
    write("hls/fmp4/master.m3u8", "\n".join(master) + "\n")
    write("hls/fmp4/subs/en.vtt", VTT_EN)
    write("hls/fmp4/subs/fr.vtt", VTT_FR)
    for lang in ("en", "fr"):
        write("hls/fmp4/subs/%s.m3u8" % lang, media_playlist([["#EXTINF:8.500,", "%s.vtt" % lang]]))

    # MPEG-TS media playlist, no master, a discontinuity before the last segment
    segments = []
    for i in range(3):
        write("hls/ts/part%d.ts" % i, filler("ts segment %d" % i, 188 * (4 + i)))
        seg = ["#EXTINF:3.000,", "part%d.ts" % i]
        if i == 2:
            seg.insert(0, "#EXT-X-DISCONTINUITY")
        segments.append(seg)
    write("hls/ts/index.m3u8", media_playlist(segments, head=["#EXT-X-VERSION:3"]))

    # byte ranges into one file: the init map, an explicit offset, then implied ones
    init, parts = filler("range init", 120), [filler("range segment %d" % i, 300 + 50 * i) for i in range(3)]
    write("hls/range/stream.mp4", init + b"".join(parts))
    offset = len(init)
    segs = []
    for i, part in enumerate(parts):
        spec = "%d@%d" % (len(part), offset) if i == 0 else "%d" % len(part)
        segs.append(["#EXTINF:4.000,", "#EXT-X-BYTERANGE:" + spec, "stream.mp4"])
        offset += len(part)
    write("hls/range/index.m3u8", media_playlist(segs, map_line='#EXT-X-MAP:URI="stream.mp4",BYTERANGE="%d@0"' % len(init)))

    # video and audio in separate fMP4 renditions: two files
    write("hls/split/master.m3u8", "\n".join([
        "#EXTM3U", "#EXT-X-VERSION:7",
        '#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="aud",NAME="Main",LANGUAGE="en",DEFAULT=YES,URI="audio/index.m3u8"',
        '#EXT-X-STREAM-INF:BANDWIDTH=3000000,RESOLUTION=1280x720,CODECS="avc1.64001f",AUDIO="aud"',
        "video/index.m3u8"]) + "\n")
    for track in ("video", "audio"):
        write("hls/split/%s/init.mp4" % track, filler("split %s init" % track, 80))
        for i in range(2):
            write("hls/split/%s/s%d.m4s" % (track, i), filler("split %s segment %d" % (track, i), 400))
        write("hls/split/%s/index.m3u8" % track, media_playlist(
            [["#EXTINF:4.000,", "s%d.m4s" % i] for i in range(2)], map_line='#EXT-X-MAP:URI="init.mp4"'))

    # AES-128: not DRM, but script cannot decrypt it until AR-E9
    write("hls/locked/index.m3u8", media_playlist(
        [["#EXTINF:4.000,", "c%d.ts" % i] for i in range(2)],
        head=['#EXT-X-KEY:METHOD=AES-128,URI="key.bin",IV=0x00000000000000000000000000000001']))


def page(title, head, body):
    return ("<!DOCTYPE html>\n<html lang=\"en\"><head><meta charset=\"utf-8\"><title>%s</title>\n%s\n</head>\n"
            "<body>\n%s\n</body></html>\n" % (title, head, body))


def make_pages():
    write("video.html", page("Harbour at dawn — Harbour Notes",
        '<meta name="description" content="Four minutes of the harbour waking up, filmed from the pier.">\n'
        '<meta name="author" content="R. Mariner">\n'
        '<meta property="article:published_time" content="2026-09-14T06:30:00Z">',
        '<h1>Harbour at dawn</h1>\n'
        '<video controls poster="media/poster.png" width="640">\n'
        '  <source src="media/clip.webm" type="video/webm">\n'
        '  <source src="media/clip.mp4" type="video/mp4">\n'
        '  <track kind="subtitles" srclang="en" label="English" src="media/clip.en.vtt" default>\n'
        '  <track kind="chapters" srclang="en" src="media/chapters.vtt">\n'
        '</video>\n<p>Filmed from the end of the old pier.</p>'))
    write("stream.html", page("The pier, streamed",
        '<meta property="og:title" content="The pier, streamed">\n'
        '<meta property="og:description" content="An adaptive stream of the pier at four sizes.">\n'
        '<meta property="og:image" content="media/poster.png">',
        '<video controls><source src="hls/fmp4/master.m3u8" type="application/vnd.apple.mpegurl"></video>'))
    write("og.html", page("Night ferry",
        '<meta property="og:title" content="Night ferry">\n'
        '<meta property="og:video" content="hls/ts/index.m3u8">\n'
        '<meta property="og:video:type" content="application/x-mpegURL">',
        '<div id="player" data-src="hls/ts/index.m3u8"></div>'))
    write("jsonld.html", page("Lighthouse timelapse",
        '<script type="application/ld+json">{"@context": "https://schema.org", "@type": "VideoObject",'
        ' "name": "Lighthouse timelapse", "description": "A day at the lamp house in ninety seconds.",'
        ' "uploadDate": "2026-08-02", "thumbnailUrl": "media/poster.png", "contentUrl": "hls/range/index.m3u8"}</script>',
        '<div class="player"></div>'))
    write("split.html", page("Two tracks",
        '<meta property="og:title" content="Two tracks">',
        '<video controls src="hls/split/master.m3u8"></video>'))
    write("locked.html", page("Locked stream",
        '<meta property="og:title" content="Locked stream">',
        '<video controls src="hls/locked/index.m3u8"></video>'))
    write("podcast.html", page("Shore sounds, episode 4",
        '<meta name="description" content="Waves, gulls and a foghorn.">\n'
        '<link rel="enclosure" type="audio/mpeg" href="media/tone.mp3">',
        '<h1>Shore sounds, episode 4</h1>\n<p>Recorded on the north beach.</p>'))
    write("novideo.html", page("Nothing to play",
        '<meta property="og:video" content="https://player.example/embed/abc123">',
        '<p>The player loads by script.</p>'))
    write("gallery.html", page("Harbour in pictures",
        '<meta name="author" content="R. Mariner">\n<meta property="og:image" content="pics/cover.png">',
        '<h1>Harbour in pictures</h1>\n'
        '<img src="pics/icon.png" alt="site icon" width="32" height="32">\n'
        '<figure><img src="pics/boats.png" alt="Boats at rest"><figcaption>Boats at rest in the inner basin</figcaption></figure>\n'
        '<a href="pics/full/nets.png" title="Drying nets"><img src="pics/thumbs/nets.png" alt="nets"></a>\n'
        '<img src="pics/wide-480.png" srcset="pics/wide-480.png 480w, pics/wide-960.png 960w" sizes="100vw" alt="The whole bay">\n'
        '<picture><source srcset="pics/gull.png 1x"><img src="pics/gull-small.png" alt="A gull"></picture>\n'
        '<img src="pics/spacer.gif" alt="">\n'
        '<img src="pics/boats.png" alt="Boats again">'))
    write("plain.html", page("No pictures here", "", "<p>Only words.</p>"))


def make_files():
    write("media/clip.mp4", filler("progressive mp4", 40000))
    write("media/tone.mp3", filler("mp3 audio", 9000))
    write("media/clip.en.vtt", VTT_EN)
    write("media/chapters.vtt", "WEBVTT\n\n00:00:00.000 --> 00:00:04.000\nDawn\n")
    write("media/poster.png", png(320, 180, (40, 90, 140)))
    write("pics/icon.png", png(32, 32, (200, 30, 30)))
    write("pics/cover.png", png(400, 300, (20, 120, 60)))
    write("pics/boats.png", png(300, 300, (30, 60, 160)))
    write("pics/thumbs/nets.png", png(150, 100, (160, 140, 40)))
    write("pics/full/nets.png", png(600, 400, (160, 140, 40)))
    write("pics/wide-480.png", png(480, 270, (90, 90, 90)))
    write("pics/wide-960.png", png(960, 540, (90, 90, 90)))
    write("pics/gull.png", png(250, 250, (230, 230, 230)))
    write("pics/gull-small.png", png(100, 100, (230, 230, 230)))
    write("pics/spacer.gif", GIF_1X1)
    write("docs/tide-table.pdf", b"%PDF-1.4\n% fixture: a file, not rendered\n" + filler("pdf body", 2000) + b"\n%%EOF\n")


if __name__ == "__main__":
    make_hls()
    make_pages()
    make_files()
