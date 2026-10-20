// Build the photo album's grid thumbnails and placeholder colours.
// Run from the repository root after _fetch.py has downloaded photos/:
//     ./lambda.exe run test/demo/photo-album/_thumbs.ls
// Writes thumbs/<file> (512 px JPEG) for every downloaded photo and records each
// photo's average colour as "#rrggbb" in photos.json. Reruns skip finished work.

let DIR = "test/demo/photo-album/"
let SIZE = 512
let DIGITS = "0123456789abcdef"

fn hex2(v) {
    let n = int(round(v * 255.0))
    slice(DIGITS, int(floor(n / 16)), int(floor(n / 16)) + 1) ++ slice(DIGITS, n % 16, n % 16 + 1)
}

// as_float first: axis reductions over ubyte arrays wrap modulo 256 (issue LR05-16)
fn colour(pixels) {
    let c = avg(avg(as_float(pixels), 0), 0)
    "#" ++ hex2(c[0]) ++ hex2(c[1]) ++ hex2(c[2])
}

pn main() {
    let doc = input(DIR ++ "photos.json", 'json')^
    if (not exists(DIR ++ "thumbs")) { io.mkdir(DIR ++ "thumbs")^ }
    var photos = []
    var made = 0
    var coloured = 0
    var missing = 0
    for (p in doc.photos) {
        let src = DIR ++ "photos/" ++ p.file
        let dst = DIR ++ "thumbs/" ++ p.file
        if (not exists(src)) {
            missing = missing + 1
            photos = [*photos, p]
        }
        else if (exists(dst) and p.color is string) {
            photos = [*photos, p]
        }
        else {
            let thumb = if (exists(dst)) load({data:input(dst,'binary')^, thumbnail:SIZE})^
                else load({data:input(src,'binary')^, thumbnail:SIZE})^
            if (not exists(dst)) {
                save(thumb, {path:dst, format:'jpeg', quality:82})^
                made = made + 1
            }
            coloured = coloured + 1
            photos = [*photos, {*:p, color:colour(thumb)}]
        }
    }
    output({*:doc, photos:photos}, DIR ++ "photos.json", {format:'json', atomic:true})^
    print("thumbnails made " ++ string(made) ++ ", colours set " ++ string(coloured) ++ ", photos missing " ++ string(missing) ++ "\n")
}
