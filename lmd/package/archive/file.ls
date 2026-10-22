// Files and single images (AR4v2): the job's first fetch is the payload; it
// moves into media/ under its own name, and an image gets its pixel size and
// a thumbnail.
import planning: .plan
import page: .page
import radiant

let THUMB = {width: 400, height: 400}

pub pn acquire(p, staging, settings, notify, r, fetched) map^ {
    io.mkdir(staging ++ "/media")^
    let ctype = page.media_type(r.headers.'content-type')
    let name = page.file_name(r.url, ctype)
    let path = "media/" ++ name
    io.move(fetched, staging ++ "/" ++ path)^
    let kind = if (p.kind == "image" or (ctype != null and starts_with(ctype, "image/"))) "image" else "file"
    let bytes = if (kind == "image") input(staging ++ "/" ++ path, 'binary')^ else null
    let size = if (bytes is binary) radiant.image_metrics(bytes) else null
    // a thumbnail when the image decodes (SVG and the like have none)
    let img = if (bytes is binary) (load({data: bytes, thumbnail: THUMB}) ^ { null }) else null
    var thumb = null
    if (img != null) {
        var failure = null
        save(img, staging ++ "/media/" ++ name ++ ".thumb.png") ^ { failure = ^ }
        if (not (failure is error)) { thumb = "media/" ++ name ++ ".thumb.png" }
    }
    let u = planning.parse(r.url);
    {info: {kind: kind, title: name, description: null, author: null, site: if (u == null) null else u.host, published: null,
        url: p.url, page_url: null, poster: thumb, subtitles: [], remote: [],
        files: [{path: path, role: kind, source: r.url, bytes: r.bytes, content_type: ctype,
            width: if (size == null) null else size.width, height: if (size == null) null else size.height, thumb: thumb}]},
     source: null}
}
