// each writer uses the common registry; thumbnails keep aspect ratio and array shape.
let image = reshape([for (i in 0 to 95) if (i % 4 == 3) 255 else (i * 7) % 256], [4, 6, 4])
for (codec in ['png', 'jpeg', 'webp', 'bmp', 'tiff', 'ico']) {
    let path = "./temp/image_io_" ++ string(codec) ++ ".bin"
    let written = save(image, {path:path, format:codec, quality:100, lossless:true})^
    let decoded = load(path)^
    let thumbnail = load({data:path, thumbnail:3})^
    ; [codec, written, shape(decoded), shape(thumbnail)]
}

// AVIF uses the decoder-only backend; writing reports an ordinary codec error.
let avif = "./test/input/image/avif/orientation_0_0.avif"
"AVIF input:"; [shape(load(avif)^), shape(load({data:avif, thumbnail:3})^)]
"AVIF output unavailable:"; save(image, {path:"./temp/image_io_avif.bin", format:'avif'}) ^ { true }

// a source limit still applies when the requested thumbnail is small.
"source limit:"; load({data:"./temp/image_io_png.bin", thumbnail:1, max_pixels:2}) ^ { true }
"unknown option:"; load({data:"./temp/image_io_png.bin", thumbs:3}) ^ { true }
"strict AVIF preview:"; load({data:avif, thumbnail:1, allow_full_decode:false}) ^ { true }
"no upscale:"; shape(load({data:"./temp/image_io_png.bin", thumbnail:{width:256, height:256}})^)
"binary source:"; shape(load({data:input("./temp/image_io_png.bin", 'binary')^, thumbnail:3})^)
