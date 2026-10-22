// The built-in platform table (AR2v2's second classification step) and the
// extension tables after it. Data only; the rules in a configuration win.

pub let TABLE = [
    {kind: "video", hosts: ["youtube.com", "*.youtube.com", "youtu.be", "vimeo.com", "*.vimeo.com",
        "dailymotion.com", "*.dailymotion.com", "twitch.tv", "*.twitch.tv", "bilibili.com", "*.bilibili.com",
        "tiktok.com", "*.tiktok.com", "rumble.com", "odysee.com", "*.odysee.com", "ted.com", "*.ted.com",
        "nicovideo.jp", "*.nicovideo.jp", "peertube.tv", "*.peertube.tv"]},
    {kind: "audio", hosts: ["soundcloud.com", "*.soundcloud.com", "*.bandcamp.com", "mixcloud.com",
        "*.mixcloud.com", "audiomack.com", "*.audiomack.com"]},
    {kind: "gallery", hosts: ["flickr.com", "*.flickr.com", "pixiv.net", "*.pixiv.net", "deviantart.com",
        "*.deviantart.com", "imgur.com", "*.imgur.com", "unsplash.com", "*.unsplash.com", "500px.com",
        "*.500px.com", "instagram.com", "*.instagram.com", "artstation.com", "*.artstation.com"]}
]

// an HLS playlist is classified as video; the media engine reads whether it is audio-only
pub let VIDEO_EXT = ["mp4", "webm", "mkv", "mov", "m4v", "avi", "ogv", "m3u8"]
pub let AUDIO_EXT = ["mp3", "m4a", "ogg", "oga", "flac", "wav", "aac", "opus"]
pub let IMAGE_EXT = ["jpg", "jpeg", "png", "gif", "webp", "svg", "avif", "bmp", "tif", "tiff", "heic", "ico"]
pub let FILE_EXT = ["pdf", "epub", "mobi", "zip", "tar", "gz", "tgz", "bz2", "xz", "7z", "rar", "dmg", "iso",
    "doc", "docx", "xls", "xlsx", "ppt", "pptx", "odt", "ods", "odp", "csv", "json", "xml", "txt", "md", "rtf"]
