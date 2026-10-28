"""Reproduce the synthetic PPTX corpus with deterministic ZIP metadata and stdlib only."""
from pathlib import Path
from html import escape
import struct
import zipfile
import zlib

ROOT = Path(__file__).parent
P = "http://schemas.openxmlformats.org/presentationml/2006/main"
A = "http://schemas.openxmlformats.org/drawingml/2006/main"
R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
PKG = "http://schemas.openxmlformats.org/package/2006/relationships"
CT = "http://schemas.openxmlformats.org/package/2006/content-types"
MAIN = "application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml"
EMU = 9525


def rel(rid, kind, target, external=False):
    return f'<Relationship Id="{rid}" Type="{R}/{kind}" Target="{target}"' + (' TargetMode="External"' if external else '') + '/>'


def rels(value):
    return f'<Relationships xmlns="{PKG}">{value}</Relationships>'


def document(tag, body):
    return f'<p:{tag} xmlns:p="{P}" xmlns:a="{A}" xmlns:r="{R}">{body}</p:{tag}>'


def transform(x, y, w, h, namespace="a", tail=""):
    return (f'<{namespace}:xfrm><a:off x="{x*EMU}" y="{y*EMU}"/>'
            f'<a:ext cx="{w*EMU}" cy="{h*EMU}"/>{tail}</{namespace}:xfrm>')


def run(value, props=""):
    return f'<a:r><a:rPr {props}/><a:t>{escape(value)}</a:t></a:r>'


def body(value, props="", para="", raw=False):
    return ('<p:txBody><a:bodyPr/><a:lstStyle/><a:p>' + para +
            (value if raw else run(value, props)) + '</a:p></p:txBody>')


def shape(ident, value="", *, x=48, y=38, w=864, h=90, ph="", props="", geom="rect", fill="", text=None):
    tx = text if text is not None else body(value, props) if value else ""
    return (f'<p:sp><p:nvSpPr><p:cNvPr id="{ident}" name="Object {ident}"/>'
            f'<p:cNvSpPr/><p:nvPr>{ph}</p:nvPr></p:nvSpPr><p:spPr>' +
            (transform(x, y, w, h) if x is not None else "") +
            f'<a:prstGeom prst="{geom}"/>{fill}</p:spPr>{tx}</p:sp>')


def scene(shapes, extra="", background=""):
    return document("sld", f'<p:cSld>{background}<p:spTree>{shapes}</p:spTree></p:cSld>{extra}')


def save(name, entries):
    with zipfile.ZipFile(ROOT / name, "w") as archive:
        for key, value in sorted(entries.items()):
            info = zipfile.ZipInfo(key, (2020, 1, 2, 3, 4, 6))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, value)


def package(shapes=None, *, main="ppt/presentation.xml"):
    parts = {
        "[Content_Types].xml": f'<Types xmlns="{CT}"><Default Extension="xml" ContentType="application/xml"/><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="png" ContentType="image/png"/><Override PartName="/{main}" ContentType="{MAIN}"/></Types>',
        "_rels/.rels": rels(rel("main", "officeDocument", "/" + main)),
        main: document("presentation", '<p:sldIdLst><p:sldId id="256" r:id="slide"/></p:sldIdLst>'
                       f'<p:sldSz cx="{960*EMU}" cy="{540*EMU}"/>'),
        str(Path(main).parent / "_rels" / (Path(main).name + ".rels")): rels(rel("slide", "slide", "/slides/first.xml")),
        "slides/first.xml": scene(shapes if shapes is not None else shape(2, "Hello, PPTX!", props='sz="3000"')),
    }
    return parts


base = package()
save("minimal.pptx", base)
save("relocated.pptx", package(main="decks/main document.xml"))
strict = {k: v.replace(P, "http://purl.oclc.org/ooxml/presentationml/main")
          .replace(A, "http://purl.oclc.org/ooxml/drawingml/main")
          .replace(R, "http://purl.oclc.org/ooxml/officeDocument/relationships") for k, v in base.items()}
save("strict.pptx", strict)
save("alternate-prefix.pptx", {k: v.replace("xmlns:p=", "xmlns:deck=").replace("<p:", "<deck:").replace("</p:", "</deck:") for k, v in base.items()})

# Relationship order and part filenames deliberately differ from authored slide order.
rich = package()
rich["ppt/presentation.xml"] = rich["ppt/presentation.xml"].replace('<p:sldId id="256" r:id="slide"/>', '<p:sldId id="300" r:id="second"/><p:sldId id="256" r:id="slide"/>')
rich["ppt/_rels/presentation.xml.rels"] = rels(rel("slide", "slide", "/slides/first.xml") + rel("second", "slide", "/slides/named title.xml"))
title_ph = '<p:ph type="title"/>'
title = shape(2, "A clearer story", x=None, ph=title_ph)
paragraphs = (body(run("Bold ", 'b="1" sz="2200"') + run("and italic", 'i="1" sz="2200"'), raw=True)[:-11] +
              '<a:p><a:pPr marL="228600"><a:buChar char="•"/><a:defRPr sz="1800"/></a:pPr>' + run("Café 世界") + '</a:p></p:txBody>')
picture = ('<p:pic><p:nvPicPr><p:cNvPr id="4" name="Blue square" descr="Embedded blue image"/><p:cNvPicPr/><p:nvPr/></p:nvPicPr>'
           '<p:blipFill><a:blip r:embed="image"/><a:stretch><a:fillRect/></a:stretch></p:blipFill><p:spPr>' + transform(720, 170, 120, 120) + '</p:spPr></p:pic>')
table = ('<p:graphicFrame><p:nvGraphicFramePr><p:cNvPr id="5" name="Data"/><p:cNvGraphicFramePr/><p:nvPr/></p:nvGraphicFramePr>' +
         transform(60, 350, 500, 100, "p") + '<a:graphic><a:graphicData><a:tbl><a:tblPr/><a:tblGrid><a:gridCol w="2381250"/><a:gridCol w="2381250"/></a:tblGrid>' +
         ''.join('<a:tr h="476250">' + ''.join('<a:tc>' + body(t).replace('p:txBody', 'a:txBody') + '<a:tcPr/></a:tc>' for t in row) + '</a:tr>' for row in [["Quarter", "Revenue"], ["Q1", "$42m"]]) +
         '</a:tbl></a:graphicData></a:graphic></p:graphicFrame>')
group = ('<p:grpSp><p:nvGrpSpPr><p:cNvPr id="6" name="Group"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr>' +
         transform(700, 370, 160, 80, tail='<a:chOff x="95250" y="190500"/><a:chExt cx="762000" cy="381000"/>') + '</p:grpSpPr>' +
         shape(7, x=10, y=20, w=80, h=40, fill='<a:solidFill><a:schemeClr val="accent1"/></a:solidFill>') + '</p:grpSp>')
rich["slides/named title.xml"] = scene(title + shape(3, x=60, y=165, w=540, h=150, text=paragraphs) + picture + table + group)
rich["slides/_rels/named title.xml.rels"] = rels(rel("layout", "slideLayout", "../layouts/main.xml") + rel("image", "image", "../media/blue.png") + rel("notes", "notesSlide", "../notes/speaker.xml"))
rich["slides/_rels/first.xml.rels"] = rels(rel("layout", "slideLayout", "../layouts/main.xml"))
rich["layouts/main.xml"] = document("sldLayout", '<p:cSld><p:spTree>' + shape(20, x=48, y=42, w=864, h=100, ph=title_ph) + '</p:spTree></p:cSld>')
rich["layouts/_rels/main.xml.rels"] = rels(rel("master", "slideMaster", "../masters/base.xml"))
rich["masters/base.xml"] = document("sldMaster", '<p:cSld><p:bg><p:bgPr><a:solidFill><a:schemeClr val="bg1"/></a:solidFill></p:bgPr></p:bg><p:spTree>' +
    shape(30, x=48, y=20, w=864, h=4, fill='<a:solidFill><a:schemeClr val="accent1"/></a:solidFill>') + '</p:spTree></p:cSld>' +
    '<p:clrMap bg1="lt1" tx1="dk1" accent1="accent1"/><p:txStyles><p:titleStyle><a:lvl1pPr><a:defRPr sz="3600" b="1"><a:solidFill><a:schemeClr val="tx1"/></a:solidFill><a:latin typeface="+mj-lt"/></a:defRPr></a:lvl1pPr></p:titleStyle></p:txStyles>')
rich["masters/_rels/base.xml.rels"] = rels(rel("theme", "theme", "../themes/colors.xml"))
rich["themes/colors.xml"] = f'<a:theme xmlns:a="{A}"><a:themeElements><a:clrScheme name="Ocean">' + ''.join(f'<a:{k}><a:srgbClr val="{v}"/></a:{k}>' for k, v in {"lt1":"F1F6F5", "dk1":"103B4A", "accent1":"008C86"}.items()) + '</a:clrScheme><a:fontScheme name="Office"><a:majorFont><a:latin typeface="Georgia"/></a:majorFont><a:minorFont><a:latin typeface="Arial"/></a:minorFont></a:fontScheme></a:themeElements></a:theme>'
rich["notes/speaker.xml"] = document("notes", '<p:cSld><p:spTree>' + shape(2, "Introduce the opportunity.", ph='<p:ph type="body"/>') + shape(3, "300", ph='<p:ph type="sldNum"/>') + '</p:spTree></p:cSld>')

def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

rich["media/blue.png"] = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(b'\x00\x24\x6d\xc1')) + chunk(b'IEND', b'')
rich["_rels/.rels"] = rich["_rels/.rels"].replace('</Relationships>', '<Relationship Id="core" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="properties/core.xml"/></Relationships>')
rich["properties/core.xml"] = '<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Feature showcase</dc:title><dc:creator>Lambda tests</dc:creator></cp:coreProperties>'
save("features.pptx", rich)

def numbered(value, level=0, start=None, kind="arabicPeriod"):
    return '<a:p><a:pPr lvl="' + str(level) + '"><a:buAutoNum type="' + kind + '"' + ('' if start is None else ' startAt="' + str(start) + '"') + '/></a:pPr>' + run(value) + '</a:p>'

formatted_body = '<p:txBody><a:bodyPr/><a:lstStyle/><a:p>' + run('Introduction') + '</a:p>' + numbered('First', start=3) + numbered('Nested', 1, 1) + numbered('Second') + '<a:p><a:pPr><a:buNone/></a:pPr>' + run('Interruption') + '</a:p>' + numbered('Restart', start=7, kind='arabicParenBoth') + '<a:p><a:r><a:rPr><a:hlinkClick r:id="safe"/></a:rPr><a:t>Safe link</a:t></a:r><a:r><a:rPr><a:hlinkClick r:id="unsafe"/></a:rPr><a:t>Unsafe label</a:t></a:r></a:p></p:txBody>'
formatting = package(shape(2, x=40, y=30, w=600, h=400, text=formatted_body) +
                     shape(3, x=700, y=30, w=0, h=300, geom='line', fill='<a:ln w="19050"><a:solidFill><a:srgbClr val="E04E39"/></a:solidFill></a:ln>'))
formatting['slides/_rels/first.xml.rels'] = rels(rel('safe', 'hyperlink', 'https://example.com/page', True) + rel('unsafe', 'hyperlink', 'javascript:alert(1)', True))
save('formatting.pptx', formatting)

# Numeric paths preserve opposite-winding holes; an empty txBody is not a text box.
frame = ('<a:custGeom><a:pathLst><a:path w="200" h="160" stroke="0">'
         '<a:moveTo><a:pt x="0" y="0"/></a:moveTo><a:lnTo><a:pt x="200" y="0"/></a:lnTo>'
         '<a:lnTo><a:pt x="200" y="160"/></a:lnTo><a:lnTo><a:pt x="0" y="160"/></a:lnTo><a:close/>'
         '<a:moveTo><a:pt x="20" y="20"/></a:moveTo><a:lnTo><a:pt x="20" y="140"/></a:lnTo>'
         '<a:lnTo><a:pt x="180" y="140"/></a:lnTo><a:lnTo><a:pt x="180" y="20"/></a:lnTo><a:close/>'
         '</a:path></a:pathLst></a:custGeom>')
curve = ('<a:custGeom><a:pathLst><a:path w="200" h="160" fill="none">'
         '<a:moveTo><a:pt x="0" y="160"/></a:moveTo><a:cubicBezTo>'
         '<a:pt x="0" y="0"/><a:pt x="200" y="0"/><a:pt x="200" y="160"/></a:cubicBezTo>'
         '<a:quadBezTo><a:pt x="100" y="80"/><a:pt x="0" y="160"/></a:quadBezTo>'
         '</a:path></a:pathLst></a:custGeom>')

def freeform(ident, geometry, **kwargs):
    return shape(ident, **kwargs).replace('<a:prstGeom prst="rect"/>', geometry)

def stops(values):
    return '<a:gsLst>' + ''.join(f'<a:gs pos="{pos}"><a:srgbClr val="{rgb}">{mods}</a:srgbClr></a:gs>' for pos, rgb, mods in values) + '</a:gsLst>'

linear = '<a:gradFill>' + stops([(100000, '0000FF', ''), (0, 'FF0000', '')]) + '<a:lin ang="0" scaled="0"/></a:gradFill>'
radial = ('<a:gradFill>' + stops([(100000, 'FF0000', '<a:alpha val="0"/>'), (0, 'FF0000', '')]) +
          '<a:path path="circle"><a:fillToRect l="50000" t="50000" r="50000" b="50000"/></a:path></a:gradFill>')
duotone = ('<a:blip r:embed="gray"><a:duotone><a:srgbClr val="FF0000"/><a:srgbClr val="0000FF"/></a:duotone>'
           '<a:extLst><a:ext uri="test-dpi-metadata"/></a:extLst></a:blip>')
image_fill = '<a:blipFill>' + duotone + '<a:stretch><a:fillRect/></a:stretch></a:blipFill>'
cropped_fill = ('<a:blipFill><a:blip r:embed="gray"/><a:srcRect l="25000"/>'
                '<a:stretch><a:fillRect l="-25000" r="-25000"/></a:stretch></a:blipFill>')
paint = package(
    freeform(2, frame, x=20, y=20, w=200, h=160, fill='<a:solidFill><a:srgbClr val="0066CC"/></a:solidFill>', text=body('')) +
    freeform(3, curve, x=250, y=20, w=200, h=160, fill='<a:ln w="28575"><a:solidFill><a:srgbClr val="008800"/></a:solidFill></a:ln>') +
    shape(4, x=20, y=220, w=200, h=120, fill=linear) +
    shape(5, x=250, y=220, w=200, h=120, geom='ellipse', fill=radial) +
    shape(6, x=500, y=20, w=300, h=160, fill=image_fill) +
    shape(7, x=500, y=220, w=300, h=160, fill=cropped_fill))
paint['slides/_rels/first.xml.rels'] = rels(rel('gray', 'image', '../media/gray.png'))
paint['media/gray.png'] = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 4, 1, 8, 6, 0, 0, 0)) +
                         chunk(b'IDAT', zlib.compress(b'\x00\x00\x00\x00\xff\x80\x80\x80\xff\xff\xff\xff\xff\xff\xff\xff\x00')) + chunk(b'IEND', b''))
save('paint.pptx', paint)
unknown_paint = dict(paint)
unknown_paint['slides/first.xml'] = scene(
    freeform(2, frame.replace('<a:close/>', '<a:arcTo wR="10" hR="10" stAng="0" swAng="5400000"/>', 1), x=20, y=20, w=200, h=160) +
    shape(3, fill=radial.replace('path="circle"', 'path="rect"')) +
    shape(4, fill=image_fill.replace('<a:duotone>', '<a:blur rad="1000"/><a:duotone>')) +
    shape(5, fill=linear.replace('</a:gradFill>', '<a:tileRect l="10000"/></a:gradFill>')) +
    shape(6, fill=image_fill.replace('<a:srgbClr val="FF0000"/>', '<a:srgbClr val="FF0000"><a:alpha val="50000"/></a:srgbClr>')))
save('unsupported-paint.pptx', unknown_paint)

# Shared-master warnings repeat across slides but occupy one bounded disclosure in the player.
warnings = package()
warnings['ppt/presentation.xml'] = document('presentation', '<p:sldIdLst>' + ''.join(
    f'<p:sldId id="{256+i}" r:id="s{i}"/>' for i in range(6)) + '</p:sldIdLst>' + f'<p:sldSz cx="{960*EMU}" cy="{540*EMU}"/>')
warnings['ppt/_rels/presentation.xml.rels'] = rels(''.join(rel(f's{i}', 'slide', f'../slides/warning{i}.xml') for i in range(6)))
for i in range(6):
    warnings[f'slides/warning{i}.xml'] = scene(shape(2, f'Visible slide {i+1}'), '<p:timing/>')
    warnings[f'slides/_rels/warning{i}.xml.rels'] = rels(rel('layout', 'slideLayout', '../layouts/warnings.xml'))
warnings['layouts/warnings.xml'] = document('sldLayout', '<p:cSld><p:spTree/></p:cSld>')
warnings['layouts/_rels/warnings.xml.rels'] = rels(rel('master', 'slideMaster', '../masters/warnings.xml'))
warnings['masters/warnings.xml'] = document('sldMaster', '<p:cSld><p:spTree>' + ''.join(
    shape(10+i, x=0, y=0, w=1, h=1, geom=f'unknown{i}') for i in range(12)) +
    shape(30, x=0, y=0, w=1, h=1, fill='<a:pattFill prst="pct5"/>') + '</p:spTree></p:cSld>')
save('warnings.pptx', warnings)

unsupported = package(shape(2, "Still editable", geom="hexagon") + '<p:graphicFrame>' + transform(0, 0, 100, 100, "p") + '<a:graphic><a:graphicData uri="unsupported"/></a:graphic></p:graphicFrame>')
unsupported["slides/first.xml"] = unsupported["slides/first.xml"].replace('</p:sld>', '<p:transition><p:fade/></p:transition><p:timing/></p:sld>')
save("unsupported.pptx", unsupported)
external = package(picture)
external['slides/_rels/first.xml.rels'] = rels(rel('image', 'image', 'https://example.invalid/image.png', True))
save('external-image.pptx', external)
for filename, mutate in {
    "no-main": lambda p: p.update({'_rels/.rels': rels('')}),
    "escape": lambda p: p.update({'_rels/.rels': rels(rel('main', 'officeDocument', '../escape.xml'))}),
    "encoded-escape": lambda p: p.update({'_rels/.rels': rels(rel('main', 'officeDocument', '%2e%2e/escape.xml'))}),
    "macro": lambda p: p.update({'[Content_Types].xml': p['[Content_Types].xml'].replace(MAIN, 'application/vnd.ms-powerpoint.presentation.macroEnabled.main+xml')}),
    "missing-slide": lambda p: p.pop('slides/first.xml'),
    "invalid-slide": lambda p: p.update({'slides/first.xml': document('notes', '')}),
    "malformed": lambda p: p.update({'slides/first.xml': '<p:sld>'}),
    "dtd": lambda p: p.update({'slides/first.xml': '<!DOCTYPE sld [<!ENTITY bad "data">]>' + p['slides/first.xml']}),
    "duplicate-slide": lambda p: p.update({'ppt/presentation.xml': p['ppt/presentation.xml'].replace('<p:sldId id="256" r:id="slide"/>', '<p:sldId id="256" r:id="slide"/><p:sldId id="256" r:id="slide"/>')}),
    "external-slide": lambda p: p.update({'ppt/_rels/presentation.xml.rels': rels(rel('slide', 'slide', 'https://example.invalid/slide.xml', True))}),
    "invalid-size": lambda p: p.update({'ppt/presentation.xml': p['ppt/presentation.xml'].replace(f'cx="{960*EMU}"', 'cx="0"')}),
}.items():
    parts = dict(base)
    mutate(parts)
    save(filename + '.pptx', parts)
