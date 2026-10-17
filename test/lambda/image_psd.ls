import img: lambda.image

let red = img.blank({width: 2, height: 1, color: [1.0, 0.0, 0.0, 1.0]})^
let blue = img.blank({width: 1, height: 1, color: [0.0, 0.0, 1.0, 1.0]})^
let alpha = img.blank({width: 1, height: 1, channels: 1, color: [0.0]})^
let original = <image width: 2, height: 1,
    <stack
        <stack name: "色 🎨", opacity: (128 / 255), isolation: 'isolated',
            <layer name: "Blue", kind: 'raster', position: [1.0, 0.0], blue>
        >
        <layer name: "Red", kind: 'raster', red; <mask default: 1.0, alpha>>
    >
>
let bytes = img.encode(original, {format: 'psd'})^
let decoded = img.decode(bytes)^
let stack = img.composition(decoded)
; [type(bytes), decoded.width, decoded.height, name(stack), len(content(stack))]
; [name(stack[0]), stack[0].name, stack[0][0].name, stack[1].name]
; [name(stack[1][0]), name(stack[1][1]), stack[1][1].default]
let a = img.render_frame(original)^
let b = img.render_frame(decoded)^
; [a.pixels == b.pixels, b.a[0, 0], b.a[0, 1], b.b[0, 1]]
let second = img.decode(img.encode(decoded, {format: 'psd'})^)^
; [img.render_frame(second)^.pixels == b.pixels, img.composition(second)[0].name]
; img.decode(b'\x3842505300020000000000000004000000010000000100080003') ^ { true }

let no_preview = img.decode(slice(bytes, 0, len(bytes) - 10))^
; [len([for (child in content(no_preview) where name(child) == 'preview') child]),
   img.render_frame(no_preview)^ == b]
let empty = <image width: 1, height: 1, <stack <layer kind: 'empty', name: "Blank">>>
let empty_copy = img.decode(img.encode(empty, {format: 'psd'})^)^
; [img.composition(empty_copy)[0].kind, img.composition(empty_copy)[0].name,
   img.raster_of(img.composition(empty_copy)[0]), img.render_frame(empty_copy)^.a[0, 0]]
