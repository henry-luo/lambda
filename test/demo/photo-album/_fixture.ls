// Generate the committed synthetic test corpus in fixture/ (24 gradient photos, no
// third-party content). Run from the repository root:
//     ./lambda.exe run test/demo/photo-album/_fixture.ls
// Tests point the demo at it with PHOTO_ALBUM_DIR=test/demo/photo-album/fixture.
import corpus: .mod_corpus

let DIR = "test/demo/photo-album/fixture/"
let ALBUMS = [{key:"landscapes", title:"Landscapes"}, {key:"cities", title:"Cities"},
    {key:"animals", title:"Animals"}, {key:"weather", title:"Fog, snow and ice"}]
// width, height pairs covering landscape, portrait, square, wide and panorama shapes
let SIZES = [[360,240], [240,360], [300,300], [384,216], [320,240], [450,150]]
let PALETTE = [[[214,96,58],[242,201,96],[64,96,160]], [[52,120,90],[160,200,120],[30,60,50]],
    [[90,80,150],[200,160,220],[40,40,80]], [[220,226,236],[150,170,200],[90,110,140]]]
let NAMES = ["Morning ridge", "Harbour lights", "Red fox", "Snowy pass", "Old town", "River bend",
    "Heron", "Frozen lake", "Market street", "Dune crest", "Owl", "Fog bank"]

fn pad2(n) => if (n < 10) "0" ++ string(n) else string(n)
fn pad4(n) => slice("0000", 0, 4 - len(string(n))) ++ string(n)

fn entry(i) {
    let album = ALBUMS[i % 4]
    let size = SIZES[i % 6]
    let caption = NAMES[i % 12] ++ (if (i >= 12) " II" else "")
    let year = 2019 + i % 5
    let snow = album.key == "weather" and i % 8 != 7
    {
        id: i + 1,
        file: pad4(i + 1) ++ "-fixture-" ++ album.key ++ ".jpg",
        title: caption,
        description: "Synthetic gradient " ++ string(i + 1) ++ " for the " ++ album.title ++ " album.",
        album: album.key,
        taken: string(year) ++ "-" ++ pad2(1 + (i * 5) % 12) ++ "-" ++ pad2(1 + (i * 7) % 28) ++ "T" ++ pad2(6 + i % 12) ++ ":30:00",
        width: size[0], height: size[1], color: null,
        camera: {make:"Fixture", model:"Gradient " ++ string(1 + i % 3), lens:"", focal_mm:35, aperture:8, exposure:"1/250", iso:100},
        gps: if (i % 2 == 0) {lat: 40.0 + i, lon: -3.0 + i * 2} else null,
        keywords: if (snow) ["Snow", album.title] else [album.title],
        author: "Lambda test fixture", license: "CC0 1.0",
        license_url: "https://creativecommons.org/publicdomain/zero/1.0/",
        source_url: "", url: "", pageid: i + 1, source_sha1: "", sha1: null, bytes: null
    }
}

// a 3x2 colour seed per album, shifted per photo, upscaled into a soft gradient
fn pixels(i) {
    let p = PALETTE[i % 4]
    let shift = (i * 23) % 60
    fn c(rgb, k) => [min(255, rgb[0] + shift * k), min(255, rgb[1] + shift), max(0, rgb[2] - shift * k), 255]
    let seed = as_ubyte([[c(p[0],1), c(p[1],0), c(p[2],1)], [c(p[2],0), c(p[0],0), c(p[1],1)]])
    image_process(seed, {output_width:SIZES[i % 6][0], output_height:SIZES[i % 6][1]})
}

pn main() {
    for (sub in ["", "photos"]) { if (not exists(DIR ++ sub)) { io.mkdir(DIR ++ sub)^ } }
    let photos = [for (i in 0 to 23) entry(i)]
    for (i, p in photos) { save(pixels(i)^, {path:DIR ++ "photos/" ++ p.file, format:'jpeg', quality:85})^ }
    output({source:"Lambda synthetic fixture", generator:"test/demo/photo-album/_fixture.ls", width:450,
        albums:[for (a in ALBUMS) {key:a.key, title:a.title, count:len([for (p in photos where p.album == a.key) p])}],
        photos:photos}, DIR ++ "photos.json", {format:'json', atomic:true})^
    let result = corpus.build(DIR)^
    print("fixture photos " ++ string(len(photos)) ++ ", thumbnails " ++ string(result.made) ++ "\n")
}
