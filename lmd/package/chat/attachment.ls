// D7.2.4v4 / S12.1.1v3: codecs are pure; file acquisition/publication is procedural.
import media: lambda.doc.media
import img: lambda.image

pub let MAX_SOURCE_BYTES = 8 * 1024 * 1024
pub let MAX_BYTES = 4 * 1024 * 1024
pub let FRAME_BYTES = 8 * 1024 * 1024
let IMAGE_TYPES = ["image/png", "image/jpeg", "image/webp", "image/gif", "image/bmp", "image/tiff", "image/avif", "image/x-icon"]
pub fn valid_name(filename) bool^ => filename is string and len(filename) > 0 and len(filename) <= 240
    and not (filename in [".", ".."]) and not contains(filename, "/")
    and not contains(filename, "\\") and not contains(filename, chr(0))
pub fn bytes(attachment) binary^ => media.data_uri_bytes(attachment.data)^

pub fn checked(a) map^ {
    let metadata_valid = if (not (a is map) or not (a.kind in ["file", "image"]) or not valid_name(a.name)^
        or not (a.size is int) or a.size < 1 or a.size > MAX_BYTES
        or not (a.source_size is int) or a.source_size < 0 or a.source_size > MAX_SOURCE_BYTES
        or not (a.data is string) or len(a.data) > 4 * int(ceil(MAX_BYTES / 3)) + 128)
        raise error("Chat: invalid attachment or attachment exceeds 4 MiB") else true
    let mime = if (a.kind == "file") "application/zip" else a.mime
    let mime_valid = if (a.mime != mime or (a.kind == "image" and not (mime in IMAGE_TYPES))
        or not starts_with(a.data, "data:" ++ mime ++ ";base64,"))
        raise error("Chat: unsupported attachment type") else true
    let payload = bytes(a)^
    // canonical re-encoding rejects extra Mark syntax and mismatched byte counts.
    let payload_valid = if (len(payload) != a.size or media.data_uri(payload, mime)^ != a.data)
        raise error("Chat: attachment bytes do not match their metadata") else true
    if (a.kind == "file") {
        let name_valid = if (not valid_name(a.original_name)^ or a.name != a.original_name ++ ".zip")
            raise error("Chat: invalid ZIP filename") else true
        let archive = parse(payload, {type:'zip', max_archive_bytes:MAX_BYTES,
            max_member_bytes:MAX_SOURCE_BYTES, max_expanded_bytes:MAX_SOURCE_BYTES,
            max_entries:1, max_path_depth:1})^
        let members = content(archive)^
        let member_valid = if (len(members) != 1 or members[0].is_dir or members[0].name != a.original_name
            or len(parse(members[0], 'binary')^) != a.source_size)
            raise error("Chat: ZIP must contain exactly the original file") else true;
        {kind:"file", name:a.name, original_name:a.original_name, mime:mime,
            size:a.size, source_size:a.source_size, data:a.data}
    } else {
        let pixels = load({data:payload, max_pixels:16000000})^
        let dimensions = shape(pixels)
        let dimensions_valid = if (a.width != dimensions[1] or a.height != dimensions[0])
            raise error("Chat: image dimensions do not match their metadata") else true;
        {kind:"image", name:a.name, mime:mime, size:a.size, source_size:a.source_size,
            width:a.width, height:a.height, data:a.data}
    }
}

pub pn zip_file(filename, payload, scratch) map^ {
    if (not valid_name(filename)^ or not (payload is binary) or len(payload) > MAX_SOURCE_BYTES)
        raise error("Chat: choose a file of at most 8 MiB")
    io.mkdir(scratch)^
    let path = scratch ++ "/" ++ uuid()^ ++ ".zip"
    let result = {
        output(<fs kind:'dir', <fs name:filename, kind:'file', payload>>, path,
            {format:'zip', compression:'deflate', compression_level:6, deterministic:true,
                max_archive_bytes:MAX_BYTES, max_member_bytes:MAX_SOURCE_BYTES})^
        let zipped = input(path, 'binary')^;
        {kind:"file", name:filename ++ ".zip", original_name:filename, mime:"application/zip",
            size:len(zipped), source_size:len(payload), data:media.data_uri(zipped, "application/zip")^}
    } ^ { ^ }
    if (exists(path)) io.delete(path)^;
    if (result is error) raise result
    return checked(result)^
}

pub fn pasted_image(uri, compress = true) map^ {
    let source_valid = if (not (uri is string) or len(uri) > 4 * int(ceil(MAX_SOURCE_BYTES / 3)) + 128)
        raise error("Chat: clipboard image exceeds 8 MiB") else true
    let raw = media.data_uri_bytes(uri)^
    let size_valid = if (len(raw) > MAX_SOURCE_BYTES) raise error("Chat: clipboard image exceeds 8 MiB") else true
    let mime = slice(uri, 5, index_of(uri, ";"))
    let mime_valid = if (not (mime in IMAGE_TYPES)) raise error("Chat: clipboard does not contain a supported raster image") else true
    let pixels = load({data:raw, max_pixels:16000000, normalize_orientation:compress,
        *:if (compress) {thumbnail:1600} else {}})^
    let dimensions = shape(pixels)
    let encoded = if (compress) img.encode(img.from_array(pixels)^, {format:'webp', quality:80, lossless:false})^ else raw
    let data = encoded
    let output_mime = if (compress) "image/webp" else mime
    let ext = if (output_mime == "image/jpeg") "jpg" else slice(output_mime, 6);
    checked({kind:"image", name:"clipboard." ++ ext, mime:output_mime, size:len(data), source_size:len(raw),
        width:dimensions[1], height:dimensions[0], data:media.data_uri(data, output_mime)^})^
}
