import ui: lambda.ui.dtna
let small = ui.render(ui.avatar({size:'small',shape:'square',label:"Ada"},"A")^)
let custom = ui.render(ui.avatar({size:64,icon:ui.icon({name:"user"})^})^)
let photo = ui.render(ui.avatar({src:"photo.png",srcset:"photo@2x.png 2x",alt:"Portrait",draggable:false})^)
let responsive = ui.render(ui.avatar({size:{xs:24,md:48,lg:64}},"A")^);
[
    ["text semantics",name(small) == 'span' and small["aria-label"] == "Ada" and content(content(small)[0])[0] == "A"],
    ["small square",contains(small.class,"dtna-size-small") and contains(small.class,"dtna-avatar-square") and contains(small.style,"24px")],
    ["numeric icon size",contains(custom.class,"dtna-avatar-icon") and contains(custom.style,"64px") and contains(custom.style,"font-size:32px")],
    ["icon presentation",name(content(custom)[0]) == 'svg'],
    ["native image props",content(photo)[0].src == "photo.png" and content(photo)[0].srcset == "photo@2x.png 2x" and content(photo)[0].alt == "Portrait" and content(photo)[0].draggable == "false"],
    ["image background",contains(photo.class,"dtna-avatar-image")],
    ["custom source",name(content(ui.render(ui.avatar({src:<span "Custom">})^))[0]) == 'span'],
    ["responsive sizes",contains(responsive.style,"--dtna-avatar-size-xs:24px;") and contains(responsive.style,"--dtna-avatar-size-md:48px;") and contains(responsive.style,"--dtna-avatar-size-lg:64px;")],
    ["missing breakpoint resets",contains(responsive.style,"--dtna-avatar-size-sm:32px;") and contains(responsive.style,"--dtna-avatar-size-xl:32px;")],
    ["dimension compatibility",contains((ui.render(ui.avatar({dimension:56},"A")^)).style,"--dtna-avatar-size-base:56px")],
    ["invalid size",(ui.avatar({size:'huge'}) or null) == null],
    ["zero size",(ui.avatar({size:0}) or null) == null],
    ["nonfinite size",(ui.avatar({size:inf}) or null) == null],
    ["null size",(ui.avatar({size:null}) or null) == null],
    ["invalid breakpoint",(ui.avatar({size:{tablet:32}}) or null) == null],
    ["invalid responsive dimension",(ui.avatar({size:{md:-1}}) or null) == null],
    ["empty responsive map",(ui.avatar({size:{}}) or null) == null],
    ["invalid shape",(ui.avatar({shape:'round'}) or null) == null],
    ["invalid source",(ui.avatar({src:1}) or null) == null],
    ["invalid image props",(ui.avatar({draggable:1}) or null) == null and (ui.avatar({alt:1}) or null) == null]
]
