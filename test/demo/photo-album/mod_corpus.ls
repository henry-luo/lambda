// Corpus preparation shared by _thumbs.ls (downloaded photos) and _fixture.ls (test corpus).
// A corpus directory holds photos.json, photos/ and thumbs/; paths here end with "/".

pub let THUMB_SIZE = 512
let DIGITS = "0123456789abcdef"

fn hex2(v) {
    let n = int(round(v * 255.0))
    slice(DIGITS, int(floor(n / 16)), int(floor(n / 16)) + 1) ++ slice(DIGITS, n % 16, n % 16 + 1)
}

// as_float first: axis reductions over ubyte arrays wrap modulo 256 (issue LR05-16)
pub fn colour(pixels) {
    let c = avg(avg(as_float(pixels), 0), 0)
    "#" ++ hex2(c[0]) ++ hex2(c[1]) ++ hex2(c[2])
}

// corpus directory from PHOTO_ALBUM_DIR (relative to cwd), else the demo's own directory
pub fn directory(fallback) {
    let env = sys.proc.self.env.PHOTO_ALBUM_DIR#
    let dir = if (env is string and env != "") env else fallback
    if (ends_with(dir, "/")) dir else dir ++ "/"
}

// Writes thumbs/<file> for every present photo and records its average colour in photos.json.
// Reruns skip photos whose thumbnail and colour already exist.
pub pn build(dir) map^ {
    let doc = input(dir ++ "photos.json", 'json')^
    if (not exists(dir ++ "thumbs")) { io.mkdir(dir ++ "thumbs")^ }
    var photos = []
    var made = 0
    var coloured = 0
    var missing = 0
    for (p in doc.photos) {
        let src = dir ++ "photos/" ++ p.file
        let dst = dir ++ "thumbs/" ++ p.file
        if (not exists(src)) {
            missing = missing + 1
            photos = [*photos, p]
        }
        else if (exists(dst) and p.color is string) {
            photos = [*photos, p]
        }
        else {
            let thumb = if (exists(dst)) load({data:input(dst,'binary')^, thumbnail:THUMB_SIZE})^
                else load({data:input(src,'binary')^, thumbnail:THUMB_SIZE})^
            if (not exists(dst)) {
                save(thumb, {path:dst, format:'jpeg', quality:82})^
                made = made + 1
            }
            coloured = coloured + 1
            photos = [*photos, {*:p, color:colour(thumb)}]
        }
    }
    if (coloured > 0) { output({*:doc, photos:photos}, dir ++ "photos.json", {format:'json', atomic:true})^ }
    return {made:made, coloured:coloured, missing:missing}
}
