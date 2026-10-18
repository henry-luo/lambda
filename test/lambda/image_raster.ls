import img: lambda.image

let r = img.blank({width: 3, height: 2, color: [1.0, 0.5, 0.0, 1.0]})^
; [name(r), type(r), r.width, r.height, r.sample_type, r.channels, r.alpha_mode]
; [shape(r.pixels), shape(r.r), r.r[1, 2], r.g[0, 0], r.b[0, 1], r.a[1, 1]]
let region = img.read_region(r, [1, 0, 3, 1])^
; [shape(region), region[0, 1, 1]]
; [shape(img.channel(r, 'g')^), img.channel(r, 'g')^[1, 0]]
; [r == img.materialize(r), len(r), len(content(r))]
let gray = img.blank({width: 2, height: 1, channels: 1, color: [0.25]})^
; [gray.channels, gray.r, shape(img.channel(gray, 'gray')^), gray.pixels[0, 0, 0]]
let doc = img.of_raster(r)
; [name(doc), name(doc[0]), name(doc[0][0]), img.raster_of(doc[0][0]) == r]
let changed = img.fill(r, [0.0, 0.0, 1.0, 1.0])^
; [r.r[0, 0], changed.r[0, 0], changed.b[0, 0]]
; [img.read_region(r, [-1, 0, 1, 1]) ^ { true },
 img.channel(r, 'missing') ^ { true },
 img.blank({width: 0, height: 1}) ^ { true },
 img.blank({width: 1, height: 1, typo: true}) ^ { true }]
