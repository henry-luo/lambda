import img: lambda.image

let red = img.blank({width: 2, height: 1, color: [1.0, 0.0, 0.0, 1.0]})^
let blue = img.blank({width: 1, height: 1, color: [0.0, 0.0, 1.0, 1.0]})^
let scene = <image width: 2, height: 1,
    <assets <font id: "unused-font", family: "Example Sans">>
    <stack
        <layer kind: 'raster', position: [1.0, 0.0], blue>
        <layer kind: 'raster', red>
    >
>
let composed = img.render_frame(scene)^
; [composed.r[0, 0], composed.b[0, 0], composed.r[0, 1], composed.b[0, 1]]
let group = <image width: 1, height: 1,
    <stack
        <stack opacity: 0.5, isolation: 'isolated', <layer blue>>
        <layer red>
    >
>
let blended = img.render_frame(group)^
; [blended.r[0, 0], blended.b[0, 0], blended.a[0, 0]]
let mask = img.blank({width: 1, height: 1, channels: 1, color: [0.0]})^
let masked = <image width: 2, height: 1,
    <stack
        <layer kind: 'raster',
            red;
            <mask default: 1.0, mask>
        >
    >
>
let cut = img.render_frame(masked)^
; [cut.a[0, 0], cut.a[0, 1]]
let animation = <image width: 2, height: 1,
    <frame duration: {ticks: 1, rate: 10}, <stack <layer red>>>
    <frame duration: {ticks: 2, rate: 10}, <stack <layer blue>>>
>
; [img.render_frame(animation, {frame: 1})^.b[0, 0],
   img.render_frame(animation, {frame: 5}) ^ { true }]
let hidden = <image width: 1, height: 1, <stack <layer visible: false, red>>>
; img.render_frame(hidden)^.a[0, 0]
let multiply = <image width: 1, height: 1, <stack <layer blend: 'multiply', red>>>
; img.sample(img.render_frame(multiply)^,0,0)^ == [255,0,0,255]
