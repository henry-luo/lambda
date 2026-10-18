import img: lambda.image

let bytes = b'\x47494638396101000100800000FF00000000FF21FF0B4E45545343415045322E30030102000021F90400000000002C000000000100010000020244010021F90400030000002C00000000010001000002024C01003B'
let animated = img.decode(bytes)^
let frames = img.frames(animated)
; [name(animated), animated.width, animated.height, animated.plays, len(frames)]
; [frames[0].duration, frames[1].duration]
; [name(frames[0][0]), name(frames[0][0][0]), name(frames[0][0][0][0])]
; [img.render_frame(animated, {frame: 0})^.r[0, 0],
   img.render_frame(animated, {frame: 1})^.b[0, 0]]
; [name(img.assets(animated)), img.assets(animated)[0].data == bytes]

let still = img.decode(input("test/demo/photo/tests/rgba.png", 'binary')^)^
; [name(still), len(img.frames(still)), name(still[1]), name(still[1][0]),
   name(img.raster_of(still[1][0]))]

let encoded = img.encode(animated, {format: 'gif'})^
let replay = img.decode(encoded)^
; [replay.plays, len(img.frames(replay)), img.frames(replay)[0].duration,
   img.frames(replay)[1].duration]
; [img.render_frame(replay, {frame: 0})^ == img.render_frame(animated, {frame: 0})^,
   img.render_frame(replay, {frame: 1})^ == img.render_frame(animated, {frame: 1})^]

let jpeg = img.decode(input("test/input/image/sample.jpg", 'binary')^)^
; [name(jpeg), jpeg.width > 0, jpeg.height > 0, len(img.frames(jpeg)),
   name(img.raster_of(img.composition(jpeg)[0]))]
