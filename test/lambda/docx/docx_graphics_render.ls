import d: lambda.doc.docx
import metafile: lambda.doc.docx.metafile
import utf16: lambda.io.utf16
import theme: lambda.doc.docx.theme
import c: lambda.doc.docx.common
fn tone_color(tones) => theme.drawing_color(c.xml_node(
    <solidFill xmlns:c.A,<srgbClr val:"4080C0",*tones>>),{},null,true)
let r = d.convert("test/input/docx/diagram-composite.docx",{unsupported:"error"})^
let diagram = content(content(content(r.document)[0])[0])[0]
let shapes = content(diagram)
let preview = d.convert("test/input/docx/metafile-preview.docx",{unsupported:"error"})^
let icon = content(content(content(preview.document)[0])[0])[0]
let cyclic = d.convert("test/input/docx/diagram-cycle.docx")^
let unsupported = d.convert("test/input/docx/diagram-unsupported.docx")^
let truncated = d.convert("test/input/docx/metafile-truncated.docx")^
let raw = input("test/input/docx/preview.emf","binary")^;
[
    len(r.diagnostics) == 0 and name(diagram) == 'svg',
    len(shapes) == 6 and name(shapes[0]) == 'rect' and shapes[0].height == "12",
    shapes[2].x == "48" and shapes[4].x == "0" and shapes[2].y == "12",
    content(shapes[3])[0] == "Left" and content(shapes[5])[0] == "Right",
    shapes[0].fill == "rgb(32,64,96)",
    r.text == "Header\nRight\nLeft\n\n",
    cyclic.diagnostics[0].code == "unsupported-diagram-layout" and contains(cyclic.text,"Header"),
    unsupported.diagnostics[0].code == "unsupported-diagram-layout" and not contains(unsupported.html,"<svg"),
    len(preview.diagnostics) == 0 and preview.text == "Clip 🐦 & <x>\n\n",
    name(icon) == 'svg' and name(content(icon)[0]) == 'image',
    contains(preview.html,"data:image/bmp;base64,") and contains(preview.html,"&amp; &lt;x&gt;"),
    truncated.diagnostics[0].code == "unsupported-object-preview" and truncated.text == "[Embedded object (Package)]\n\n",
    metafile.read(input("test/input/docx/preview-truncated.emf","binary")^,80,64) == null,
    metafile.read(b'\x00010203',80,64) == null,
    metafile.read("invalid bytes",80,64) == null,
    metafile.read(slice(raw,0,108) ++ b'\x63000000' ++ slice(raw,112),80,64) == null,
    utf16.decode(b'\x3DD826DC',true)^ == "🐦",
    (utf16.decode(b'\x00D8',true) ^ { "invalid" }) == "invalid",
    tone_color([<tint val:"0">]) == "rgb(255,255,255)",
    tone_color([<tint val:"100000">]) == "rgb(64,128,192)",
    tone_color([<tint val:"25000">]) == "rgb(207,223,239)",
    tone_color([<shade val:"50000">,<tint val:"25000">]) == "rgb(199,207,215)"
]
