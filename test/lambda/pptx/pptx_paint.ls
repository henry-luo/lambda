import pptx: lambda.doc.pptx
import slide: lambda.slide
import theme: lambda.doc.docx.theme
import c: lambda.doc.docx.common
fn toned(rgb,tones) => theme.drawing_color(c.xml_node(<solidFill xmlns:c.A,<srgbClr val:rgb,*tones>>),{},null,true)
let result = pptx.convert("test/input/pptx/paint.pptx",{unsupported:"error"})^
let objects = slide.compile(result.document)^.slides[0].objects
let frame = content(content(objects[0].source)[0])[0]
let curve = content(content(objects[1].source)[0])[0]
let unsupported = pptx.convert("test/input/pptx/unsupported-paint.pptx")^;
[
    len(result.diagnostics)==0 and len(objects)==6,
    objects[0].tag=='content' and name(frame)=='path' and frame.fill=="#0066CC",
    frame.d=="M0 0 L200 0 L200 160 L0 160 Z M20 20 L20 140 L180 140 L180 20 Z",
    contains(curve.d,"C0 0 200 0 200 160") and contains(curve.d,"Q100 80 0 160") and curve.fill=="none",
    contains(objects[2].source.style,"linear-gradient(90deg,#FF0000 0%,#0000FF 100%)"),
    contains(objects[3].source.style,"radial-gradient(ellipse farthest-corner at 50% 50%,#FF0000 0%,rgba(255,0,0,0) 100%)"),
    contains(objects[3].source.style,"border-radius:50%"),
    contains(objects[4].source.style,"data:image/svg+xml;base64,") and contains(objects[4].source.style,"background-size:100% 100%"),
    contains(objects[5].source.style,"background-size:200% 100%") and contains(objects[5].source.style,"background-position:75% 0px"),
    [for (issue in unsupported.diagnostics) issue.code]==["unsupported-geometry","unsupported-gradient","unsupported-image-effect","unsupported-gradient","unsupported-image-effect"],
    (pptx.convert("test/input/pptx/unsupported-paint.pptx",{unsupported:"error"}) ^ { ^.code })==7706,
    toned("FF0000",[<hueOff val:"7200000">])=="rgb(0,255,0)",
    toned("FF0000",[<satMod val:"50000">])=="rgb(191,64,64)",
    toned("FF0000",[<lumMod val:"50000">])=="rgb(128,0,0)",
    toned("FF0000",[<lumOff val:"25000">])=="rgb(255,128,128)",
    toned("FF0000",[<alpha val:"50000">,<alphaMod val:"50000">,<alphaOff val:"-5000">])=="rgba(255,0,0,0.2)",
    theme.drawing_color(c.xml_node(<solidFill xmlns:c.A,<schemeClr val:"phClr",<shade val:"50000">>>),{},"#204060",true)=="rgb(16,32,48)",
    toned("FF0000",[<unrecognized val:"50000">])==null
]
