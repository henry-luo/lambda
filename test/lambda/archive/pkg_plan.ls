import archive: lambda.archive
import planning: lambda.archive.plan
import config: lambda.archive.config

// Planning is pure (AR3): URL parts, normalization, rule matching, the
// built-in platform table and the extension fallback, without any IO.
let RULES = [
    {match: {host: ["youtube.com", "*.youtube.com", "youtu.be"]}, kind: "video", media: {subtitles: ["en", "zh-Hans"]}},
    {match: {host: "*.bandcamp.com"}, kind: "audio"},
    {match: {host: ["*.flickr.com", "www.pixiv.net"]}, kind: "gallery"},
    {match: {host: "*.substack.com"}, formats: ["html", "readable", "markdown", "pdf"]},
    {match: {path: ["*.pdf", "*.epub", "*.zip"]}, kind: "file"}
]

pn main() {
    let cfg = config.resolve({rules: RULES, defaults: {timeout_s: 20}}).config
    let u = planning.parse("https://User@Www.Example.com:443/a/b.html?x=1&utm_source=mail#top")
    let video = archive.plan("https://www.youtube.com/watch?v=abc", cfg)^
    let audio = archive.plan("https://band.bandcamp.com/album/x", cfg)^
    let news = archive.plan("https://news.substack.com/p/post?utm_campaign=c&id=7", cfg)^
    let paper = archive.plan("https://example.org/files/paper.PDF", cfg)^
    let photo = archive.plan("https://example.org/pic.jpeg", cfg)^
    let page = archive.plan("https://example.org/blog/post", cfg, {tag: "t", kind: "page"})^
    let unknown = archive.plan("https://example.org/download?id=3", cfg)^
    let bad = archive.plan("ftp://example.org/x", cfg) ^ { ^ }
    let problems = archive.problems({defaults: {formats: ["html", "docx"], jobs: -1, colour: "red"},
        media: {codec: "av1"}, rules: [{kind: "video"}, {match: {host: 3}, kind: "slides"}]});
    [u.scheme == "https" and u.host == "www.example.com" and u.port == "443" and u.path == "/a/b.html" and
        u.query == "x=1&utm_source=mail" and u.fragment == "top",
     planning.normalize("https://User@Www.Example.com:443/a/b.html?x=1&utm_source=mail#top", cfg.defaults) ==
        "https://www.example.com/a/b.html?x=1",
     planning.host_glob("*.youtube.com", "youtube.com") and planning.host_glob("*.youtube.com", "m.youtube.com") and
        not planning.host_glob("*.youtube.com", "notyoutube.com") and planning.path_glob("*.pdf", "/files/paper.pdf") and
        not planning.path_glob("*.pdf", "/files/paper.pdf.html"),
     video.kind == "video" and video.engine == "media" and video.media.subtitles == ["en", "zh-Hans"] and
        video.media.max_height == 1080 and video.settings.timeout_s == 20,
     audio.kind == "audio" and audio.engine == "media" and audio.media.subtitles == ["en"],
     news.kind == null and news.formats == ["html", "readable", "markdown", "pdf"] and
        news.normalized == "https://news.substack.com/p/post?id=7",
     planning.refine(news, "text/html; charset=utf-8").kind == "page" and planning.refine(news, "application/pdf").kind == "file",
     paper.kind == "file" and photo.kind == "image" and page.kind == "page" and page.tag == "t",
     unknown.kind == null and unknown.engine == null,
     bad is error,
     problems == ["config.defaults.formats: expected formats among html, readable, markdown, pdf",
        "config.defaults.jobs: expected a non-negative number", "config.defaults.colour: unknown option",
        "config.media.codec: unknown option", "config.rules[0].match: a rule needs a match",
        "config.rules[1].match.host: expected a glob or a list of globs",
        "config.rules[1].kind: expected a kind among page, video, audio, image, gallery, file"]]
}
