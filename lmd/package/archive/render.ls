// PDF and thumbnail export (AR7): Radiant lays out a staged page, whose
// relative assets/ references resolve against the staging folder, and writes
// the file. Each export succeeds or fails on its own (AR9).
import radiant

// the list thumbnail: a 1280×800 viewport drawn at 400×250
let THUMB = {width: 1280, height: 800, scale: 0.3125}

pub pn export(html, dest, dir, options) map {
    let done = radiant.export_document(html, dest, {*: options, base_url: "file://" ++ dir ++ "/", local_resources: true});
    if (done == true) {status: "ok"} else {status: "failed", message: string(done)}
}

pub pn pdf(html, dest, dir, settings) map {
    export(html, dest, dir, {format: 'pdf', width: int(settings.width), height: int(settings.height), pagination: settings.pagination})
}

pub pn thumbnail(html, dest, dir) map { export(html, dest, dir, {format: 'png', *: THUMB}) }

// the list thumbnail from an image (a poster, a gallery's first image): no
// larger than the page thumbnail, aspect kept, never enlarged
pub pn image_thumbnail(src, dest) map {
    var failure = null
    var img = null
    load({data: input(src, 'binary')^, thumbnail: {width: int(THUMB.width * THUMB.scale), height: int(THUMB.height * THUMB.scale)}}) ^ { failure = ^ } ~ { img = ~ }
    if (failure is error) { return {status: "failed", message: failure.message} }
    save(img, dest) ^ { failure = ^ }
    if (failure is error) {status: "failed", message: failure.message} else {status: "ok"}
}
