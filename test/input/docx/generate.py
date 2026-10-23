"""Reproduce the synthetic DOCX corpus with Python's standard library, from any cwd."""
from pathlib import Path
from io import BytesIO
import struct
import zipfile
import zlib

ROOT = Path(__file__).parent
W = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
P = "http://schemas.openxmlformats.org/package/2006/relationships"
CT = "http://schemas.openxmlformats.org/package/2006/content-types"
MAIN = "application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"
def png_chunk(kind, value):
    return struct.pack(">I", len(value)) + kind + value + struct.pack(">I", zlib.crc32(kind + value))


# A visible blue pixel makes the native image smoke observable on a white page.
PNG = (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0))
       + png_chunk(b"IDAT", zlib.compress(b"\x00\x24\x6d\xc1")) + png_chunk(b"IEND", b""))


def paragraph(text, properties="", run_properties=""):
    return f'<w:p>{properties}<w:r>{run_properties}<w:t xml:space="preserve">{text}</w:t></w:r></w:p>'


def relationship(rid, kind, target, external=False):
    return f'<Relationship Id="{rid}" Type="{R}/{kind}" Target="{target}"' + (' TargetMode="External"' if external else '') + '/>'


def package(name, body, *, main="word/document.xml", extras=None, rels="", root_rels=None, mime=MAIN, raw=None, omit=(), extra_types=""):
    entries = {
        "[Content_Types].xml": f'<Types xmlns="{CT}"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Default Extension="png" ContentType="image/png"/><Default Extension="gif" ContentType="image/gif"/><Default Extension="svg" ContentType="image/svg+xml"/>{extra_types}<Override PartName="/{main}" ContentType="{mime}"/></Types>',
        "_rels/.rels": f'<Relationships xmlns="{P}">' + (root_rels if root_rels is not None else relationship("main", "officeDocument", "/" + main)) + '</Relationships>',
        main: raw if raw is not None else f'<w:document xmlns:w="{W}" xmlns:r="{R}" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing"><w:body>{body}</w:body></w:document>',
    }
    if rels:
        owner = Path(main)
        entries[str(owner.parent / "_rels" / (owner.name + ".rels"))] = f'<Relationships xmlns="{P}">{rels}</Relationships>'
    entries.update(extras or {})
    data = BytesIO()
    with zipfile.ZipFile(data, "w") as archive:
        for key, value in sorted(entries.items()):
            if key in omit:
                continue
            info = zipfile.ZipInfo(key, (2020, 1, 2, 3, 4, 6))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, value)
    (ROOT / name).write_bytes(data.getvalue())


def cell(text, properties=""):
    return f'<w:tc><w:tcPr>{properties}</w:tcPr>{paragraph(text)}</w:tc>'


def numbered(text, num, level=0):
    return paragraph(text, f'<w:pPr><w:numPr><w:ilvl w:val="{level}"/><w:numId w:val="{num}"/></w:numPr></w:pPr>')


package("minimal.docx", paragraph("Hello, DOCX!"))
package("relocated.docx", paragraph("  Café 世界 &amp; &lt;XML&gt;  "), main="stories/body.xml")
package("empty.docx", '<w:p/>' + paragraph(""))

# References use story-local IDs and relationships, including relocated annotation parts.
def annotation(kind, ident):
    return f'<w:r><w:{kind}Reference w:id="{ident}"/></w:r>'


note_body = paragraph("Foot ") + '<w:p><w:hyperlink r:id="shared"><w:r><w:t>note</w:t></w:r></w:hyperlink></w:p>'
notes = f'<w:footnotes xmlns:w="{W}" xmlns:r="{R}"><w:footnote w:id="9">{note_body}</w:footnote><w:footnote w:id="2">{paragraph("Second")}</w:footnote></w:footnotes>'
endnotes = f'<w:endnotes xmlns:w="{W}"><w:endnote w:id="3">{paragraph("End")}</w:endnote></w:endnotes>'
comments = f'<w:comments xmlns:w="{W}"><w:comment w:id="2">{paragraph("Comment")}</w:comment></w:comments>'
story_rels = relationship("notes","footnotes","../annotations/footnotes.xml")
story_rels += relationship("endnotes","endnotes","endnotes.xml") + relationship("comments","comments","comments.xml")
story_rels += relationship("shared","hyperlink","https://example.com/main",True)
annotation_parts = {"annotations/footnotes.xml":notes,"word/endnotes.xml":endnotes,"word/comments.xml":comments,
    "annotations/_rels/footnotes.xml.rels":f'<Relationships xmlns="{P}">{relationship("shared","hyperlink","https://example.com/note",True)}</Relationships>'}
annotation_body = '<w:p><w:hyperlink r:id="shared"><w:r><w:t>Body</w:t></w:r></w:hyperlink>'
annotation_body += annotation("footnote",9) + annotation("footnote",9) + annotation("endnote",3) + annotation("comment",2) + '</w:p>'
annotation_body += '<w:tbl><w:tr><w:tc><w:p>' + annotation("footnote",2) + '</w:p></w:tc></w:tr></w:tbl>'
package("annotations.docx",annotation_body,rels=story_rels,extras=annotation_parts)
for name, content in [
    ("duplicate-note",notes.replace('w:id="2"','w:id="9"')),
    ("missing-note-id",notes.replace(' w:id="9"','')),
    ("invalid-notes-root",notes.replace('w:footnotes','w:endnotes')),
    ("nested-note",notes.replace(paragraph("Foot "),'<w:p>' + annotation("footnote",9) + '</w:p>')),
    ("unsafe-note-link",notes),
]:
    parts = {**annotation_parts,"annotations/footnotes.xml":content}
    if name == "unsafe-note-link":
        parts["annotations/_rels/footnotes.xml.rels"] = f'<Relationships xmlns="{P}">{relationship("shared","hyperlink","javascript:alert(1)",True)}</Relationships>'
    package(name + ".docx",annotation_body,rels=story_rels,extras=parts)
package("external-notes.docx",'<w:p>' + annotation("footnote",9) + '</w:p>',
    rels=relationship("notes","footnotes","https://example.invalid/notes.xml",True))

styles = f'''<w:styles xmlns:w="{W}">
<w:docDefaults><w:rPrDefault><w:rPr><w:i/></w:rPr></w:rPrDefault></w:docDefaults>
<w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/></w:style>
<w:style w:type="paragraph" w:styleId="Heading1"><w:name w:val="Heading 1"/><w:basedOn w:val="Normal"/><w:pPr><w:outlineLvl w:val="0"/></w:pPr><w:rPr><w:b/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Custom"><w:name w:val="Custom Heading"/><w:basedOn w:val="Heading1"/></w:style>
<w:style w:type="character" w:styleId="Emphasis"><w:rPr><w:b/></w:rPr></w:style>
</w:styles>'''
body = paragraph("Inherited heading", '<w:pPr><w:pStyle w:val="Custom"/></w:pPr>')
body += '<w:p><w:r><w:rPr><w:i w:val="0"/><w:b/><w:u w:val="single"/><w:strike/></w:rPr><w:t>Marks</w:t></w:r><w:r><w:rPr><w:vertAlign w:val="superscript"/></w:rPr><w:t>2</w:t></w:r><w:r><w:tab/><w:br/><w:t>tail</w:t></w:r></w:p>'
body += paragraph("Toggled bold", '<w:pPr><w:pStyle w:val="Heading1"/></w:pPr>', '<w:rPr><w:rStyle w:val="Emphasis"/></w:rPr>')
package("styles.docx", body, rels=relationship("styles","styles","styles.xml"), extras={"word/styles.xml":styles})
package("paragraph-mark.docx", paragraph("Visible text", '<w:pPr><w:rPr><w:b/><w:i w:val="0"/></w:rPr></w:pPr>'), rels=relationship("styles","styles","styles.xml"), extras={"word/styles.xml":styles})
numbering = f'<w:numbering xmlns:w="{W}">'
for aid, fmt in [("a", "decimal"), ("b", "bullet")]:
    numbering += f'<w:abstractNum w:abstractNumId="{aid}">'
    for level in range(3):
        numbering += f'<w:lvl w:ilvl="{level}"><w:start w:val="1"/><w:numFmt w:val="{fmt}"/><w:lvlText w:val="%{level+1}."/></w:lvl>'
    numbering += '</w:abstractNum>'
numbering += '<w:num w:numId="1"><w:abstractNumId w:val="a"/><w:lvlOverride w:ilvl="0"><w:startOverride w:val="3"/></w:lvlOverride></w:num><w:num w:numId="2"><w:abstractNumId w:val="b"/></w:num></w:numbering>'
body = numbered("First", "1") + numbered("Nested", "1", 1) + numbered("Next", "1") + paragraph("Interruption") + numbered("Continued", "1") + numbered("Bullet", "2")
package("lists.docx", body, rels=relationship("numbers","numbering","numbering.xml"), extras={"word/numbering.xml":numbering})
table = '<w:tbl><w:tr><w:trPr><w:tblHeader/></w:trPr>' + cell("Heading A") + cell("Heading B") + '</w:tr>'
table += '<w:tr>' + cell("Merged", '<w:gridSpan w:val="2"/><w:vMerge w:val="restart"/>') + '</w:tr><w:tr>' + cell("", '<w:gridSpan w:val="2"/><w:vMerge/>') + '</w:tr></w:tbl>'
package("tables.docx", table)
# Omitted columns are grid slots, not source cells or merged-cell continuations.
grid = '<w:tblGrid>' + '<w:gridCol w:w="1200"/>' * 4 + '</w:tblGrid>'
gap_row = '<w:tr><w:trPr><w:gridBefore w:val="1"/><w:gridAfter w:val="2"/></w:trPr>' + cell("Offset") + '</w:tr>'
package("table-gaps.docx", '<w:tbl>' + grid + '<w:tr>' + ''.join(cell(t) for t in ["A","B","C","D"]) + '</w:tr>' + gap_row + '</w:tbl>')
# Cached nested fields can cross paragraphs; instruction text is never display text or executable input.
def field_char(kind):
    return f'<w:r><w:fldChar w:fldCharType="{kind}"/></w:r>'


def field_code(text):
    return f'<w:r><w:instrText xml:space="preserve">{text}</w:instrText></w:r>'


body = '<w:p>' + field_char("begin") + field_code(' TOC \\h ') + field_char("separate") + '</w:p>'
body += '<w:p><w:hyperlink w:anchor="entry"><w:r><w:t>Summary</w:t></w:r></w:hyperlink><w:r><w:tab/></w:r>'
body += field_char("begin") + field_code(' PAGEREF entry \\h ') + field_char("separate") + '<w:r><w:rPr><w:b/></w:rPr><w:t>3</w:t></w:r>' + field_char("end") + '</w:p>'
body += '<w:p>' + field_char("end") + '<w:fldSimple w:instr="INCLUDETEXT &quot;https://example.invalid/secret&quot;"><w:r><w:t>Saved result</w:t></w:r></w:fldSimple></w:p>'
body += '<w:p><w:bookmarkStart w:id="1" w:name="entry"/><w:r><w:t>Summary body</w:t></w:r><w:bookmarkEnd w:id="1"/></w:p>'
package("cached-fields.docx",body)
image = '<w:r><w:drawing><wp:inline><wp:extent cx="914400" cy="457200"/><wp:docPr id="1" name="pixel" descr="Pixel &amp; alt"/><a:graphic><a:graphicData><a:blip r:embed="image"/></a:graphicData></a:graphic></wp:inline></w:drawing></w:r>'
links = '<w:hyperlink r:id="link"><w:r><w:t>Example</w:t></w:r></w:hyperlink><w:hyperlink w:anchor="mark"><w:r><w:t>Jump</w:t></w:r></w:hyperlink>'
body = '<w:p><w:bookmarkStart w:id="1" w:name="mark"/>' + links + image + '</w:p>'
props = '<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Example document</dc:title><dc:creator>Lambda</dc:creator></cp:coreProperties>'
root_rels = relationship("main", "officeDocument", "word/document.xml") + '<Relationship Id="props" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>'
package("media.docx", body, rels=relationship("image","image","media/%C3%A9%20pixel.png") + relationship("link","hyperlink","https://example.com/?a=1&amp;b=2",True), root_rels=root_rels, extras={"word/media/é pixel.png":PNG,"docProps/core.xml":props})

# Layer order is independent of source order and includes the full unsigned OOXML range.
WPS = "http://schemas.microsoft.com/office/word/2010/wordprocessingShape"
MC = "http://schemas.openxmlformats.org/markup-compatibility/2006"
A = "http://schemas.openxmlformats.org/drawingml/2006/main"


def anchor(graphic, x, y, layer, behind=True, relative="column", wrap='<wp:wrapNone/>', clearance=None):
    distances = '' if clearance is None else ''.join(f' dist{side}="{clearance}"' for side in 'TRBL')
    return (f'<w:drawing><wp:anchor simplePos="0" behindDoc="{int(behind)}" relativeHeight="{layer}"{distances}>'
            f'<wp:positionH relativeFrom="{relative}"><wp:posOffset>{x}</wp:posOffset></wp:positionH>'
            f'<wp:positionV relativeFrom="paragraph"><wp:posOffset>{y}</wp:posOffset></wp:positionV>'
            '<wp:extent cx="1905000" cy="952500"/>' + wrap +
            '<a:graphic><a:graphicData>' + graphic + '</a:graphicData></a:graphic></wp:anchor></w:drawing>')


def rectangle(fill='', style='', geometry='rect', rotation=0):
    return (f'<wps:wsp xmlns:wps="{WPS}"><wps:spPr><a:xfrm rot="{rotation}">'
            '<a:off x="0" y="0"/><a:ext cx="1905000" cy="952500"/></a:xfrm>'
            f'<a:prstGeom prst="{geometry}"/>{fill}<a:ln><a:noFill/></a:ln>'
            f'</wps:spPr><wps:style>{style}</wps:style><wps:bodyPr/></wps:wsp>')


def alternate(choice, fallback='<w:r><w:t>Fallback</w:t></w:r>'):
    return (f'<mc:AlternateContent xmlns:mc="{MC}"><mc:Choice Requires="wps" xmlns:wps="{WPS}">'
            + choice + '</mc:Choice><mc:Fallback>' + fallback + '</mc:Fallback></mc:AlternateContent>')


theme = (f'<a:theme xmlns:a="{A}"><a:themeElements><a:clrScheme><a:accent1><a:srgbClr val="123456"/>'
         '</a:accent1></a:clrScheme><a:fmtScheme><a:fillStyleLst><a:solidFill><a:schemeClr val="phClr"/>'
         '</a:solidFill><a:gradFill/></a:fillStyleLst></a:fmtScheme></a:themeElements></a:theme>')
theme_ref = '<a:fillRef idx="1"><a:schemeClr val="accent1"/></a:fillRef>'
flat_fill = '<a:solidFill><a:srgbClr val="00AA11"/></a:solidFill>'
cropped_image = ('<a:blipFill><a:blip r:embed="image"/><a:srcRect l="25000" r="25000" t="10000" b="10000"/>'
                 '<a:stretch><a:fillRect/></a:stretch></a:blipFill>')
layer_rels = relationship("image","image","media/pixel.png") + relationship("theme","theme","theme.xml")
layer_parts = {"word/media/pixel.png":PNG,"word/theme.xml":theme}
body = '<w:p><w:r>' + alternate(anchor(rectangle(style=theme_ref),0,952500,4294967295))
body += anchor(cropped_image,-228600,-114300,0) + '</w:r></w:p>'
body += '<w:p><w:pPr><w:ind w:left="300"/></w:pPr><w:r>' + anchor(rectangle(flat_fill),457200,228600,4294967295,False)
body += '<w:t>Foreground</w:t></w:r></w:p>'
package("drawing-layers.docx",body,rels=layer_rels,extras=layer_parts)
body = '<w:p><w:r>' + alternate(anchor(rectangle(flat_fill,geometry='ellipse'),0,0,1))
body += anchor(rectangle(flat_fill,rotation=5400000),0,0,1)
body += anchor(rectangle(style=theme_ref.replace('idx="1"','idx="2"')),0,0,1)
body += anchor(cropped_image.replace('l="25000"','l="90000"'),0,0,1)
body += anchor(cropped_image,0,0,1,relative='page') + '</w:r></w:p>'
package("drawing-unsupported.docx",body,rels=layer_rels,extras=layer_parts)

# A near-edge square float leaves no room for text on its starting side inside the clearance.
square_wrap = '<wp:wrapSquare wrapText="bothSides"/>'
raw_image = '<a:blip r:embed="image"/>'
edge_image = anchor(raw_image,19050,76200,0,False,wrap=square_wrap,clearance=57150)
modern_shape = anchor(rectangle(flat_fill,geometry='ellipse'),0,0,0,False)
saved_picture = anchor(raw_image,0,0,0,False)
empty_footer = f'<w:ftr xmlns:w="{W}"><w:p><w:r><w:fldChar w:fldCharType="begin"/>'
empty_footer += '<w:instrText>PAGE</w:instrText><w:fldChar w:fldCharType="end"/></w:r></w:p></w:ftr>'
empty_header = f'<w:hdr xmlns:w="{W}"><w:p><w:pPr><w:shd w:val="clear" w:fill="auto"/>'
empty_header += '<w:pBdr><w:top w:val="nil"/></w:pBdr></w:pPr><w:r><w:rPr><w:b/></w:rPr></w:r></w:p></w:hdr>'
edge_rels = layer_rels + relationship("footer","footer","footer.xml") + relationship("header","header","header.xml")
edge_parts = {**layer_parts,"word/footer.xml":empty_footer,"word/header.xml":empty_header}
edge_body = '<w:p><w:r>' + edge_image + '<w:t>Wrapped text</w:t></w:r></w:p>'
edge_body += '<w:p><w:r>' + alternate(modern_shape,saved_picture) + '</w:r></w:p>'
package("drawing-edge-fallback.docx",edge_body,rels=edge_rels,extras=edge_parts)
package("drawing-interior.docx",'<w:p><w:r>' +
        anchor(raw_image,66675,0,0,False,wrap=square_wrap,clearance=57150) + '</w:r></w:p>',
        rels=layer_rels,extras=layer_parts)
package("drawing-fallback-partial.docx",'<w:p><w:r>' +
        alternate(modern_shape,anchor(raw_image * 2,0,0,0,False)) + '</w:r></w:p>',rels=layer_rels,extras=layer_parts)
visible_parts = {**layer_parts,"word/footer.xml":empty_footer.replace('<w:fldChar w:fldCharType="end"/>',
    '<w:fldChar w:fldCharType="separate"/><w:t>7</w:t><w:fldChar w:fldCharType="end"/>'),
    "word/header.xml":empty_header.replace('w:val="nil"','w:val="single"'),
    "word/shading.xml":empty_header.replace('w:fill="auto"','w:fill="ABCDEF"')}
package("visible-stories.docx",paragraph("Body"),
        rels=edge_rels + relationship("shading","header","shading.xml"),extras=visible_parts)
package("invalid-header-root.docx",paragraph("Body"),rels=edge_rels,
        extras={**edge_parts,"word/header.xml":empty_footer})

# SmartArt labels and OLE previews are atomic visuals, not inline XML metadata.
DGM = "http://schemas.openxmlformats.org/drawingml/2006/diagram"
V = "urn:schemas-microsoft-com:vml"
O = "urn:schemas-microsoft-com:office:office"
smartart = (f'<w:r><w:drawing><wp:inline><wp:extent cx="914400" cy="457200"/>'
            f'<a:graphic><a:graphicData><d:relIds xmlns:d="{DGM}" r:dm="diagram"/>'
            '</a:graphicData></a:graphic></wp:inline></w:drawing></w:r>')
diagram_data = (f'<d:dataModel xmlns:d="{DGM}" xmlns:a="{A}"><d:ptLst>'
                '<d:pt type="doc"><d:t><a:p><a:r><a:t>Document metadata</a:t></a:r></a:p></d:t></d:pt>'
                '<d:pt modelId="one"><d:t><a:p><a:r><a:t>First &amp; label</a:t></a:r><a:br/>'
                '<a:r><a:t>next line</a:t></a:r></a:p><a:p><a:r><a:t>&lt;img src=x&gt;</a:t></a:r></a:p></d:t></d:pt>'
                '<d:pt type="pres"><d:t><a:p><a:r><a:t>Presentation duplicate</a:t></a:r></a:p></d:t></d:pt>'
                '<d:pt type="node"><d:t><a:p><a:r><a:t>Second</a:t></a:r></a:p></d:t></d:pt>'
                '<d:pt type="asst"><d:t><a:p><a:r><a:t>Assistant</a:t></a:r></a:p></d:t></d:pt>'
                '</d:ptLst></d:dataModel>')
ole_preview = (f'<w:r><w:object xmlns:v="{V}" xmlns:o="{O}" w:dxaOrig="1440" w:dyaOrig="720">'
               '<v:shapetype id="metadata"><v:formulas><v:f eqn="Metadata formula"/></v:formulas></v:shapetype>'
               '<v:shape id="unrelated"><v:imagedata r:id="absent"/></v:shape>'
               '<v:shape id="preview"><v:imagedata r:id="image"/></v:shape>'
               '<o:OLEObject ProgID="Package" ShapeID="preview" r:id="ole"/>'
               '</w:object></w:r>')
legacy_parts = {"word/diagrams/data.xml":diagram_data,"word/media/pixel.png":PNG}
legacy_rels = relationship("diagram","diagramData","diagrams/data.xml") + relationship("image","image","media/pixel.png")
# the binary OLE target is deliberately absent: only its saved image is read.
legacy_rels += relationship("ole","oleObject","embeddings/object.bin")
legacy_body = '<w:p>' + smartart + ole_preview + '</w:p>'
package("legacy-graphics.docx",legacy_body,rels=legacy_rels,extras=legacy_parts)
package("legacy-graphics-external.docx",legacy_body,
        rels=relationship("diagram","diagramData","https://example.invalid/diagram.xml",True) +
             relationship("image","image","https://example.invalid/preview.png",True),extras=legacy_parts)
package("legacy-graphics-missing.docx",legacy_body,rels=legacy_rels.replace('Id="image"','Id="other"'),
        extras={"word/media/pixel.png":PNG})
package("legacy-graphics-root.docx",legacy_body,rels=legacy_rels,
        extras={**legacy_parts,"word/diagrams/data.xml":f'<d:wrong xmlns:d="{DGM}"/>'})

# A separate composite template varies names, proportions and direction; source order differs from child order.
diagram_graph = (f'<d:dataModel xmlns:d="{DGM}" xmlns:a="{A}"><d:ptLst>'
    '<d:pt modelId="doc" type="doc"/><d:pt modelId="main"><d:t><a:p><a:r><a:t>Header</a:t></a:r></a:p></d:t></d:pt>'
    '<d:pt modelId="right"><d:t><a:p><a:r><a:t>Right</a:t></a:r></a:p></d:t></d:pt>'
    '<d:pt modelId="left"><d:t><a:p><a:r><a:t>Left</a:t></a:r></a:p></d:t></d:pt>'
    '</d:ptLst><d:cxnLst><d:cxn srcId="doc" destId="main"/>'
    '<d:cxn srcId="main" destId="right" srcOrd="1"/><d:cxn srcId="main" destId="left" srcOrd="0"/>'
    '</d:cxnLst></d:dataModel>')
diagram_template = (f'<d:layoutDef xmlns:d="{DGM}"><d:layoutNode name="canvas"><d:varLst><d:dir val="rev"/></d:varLst>'
    '<d:alg type="composite"/><d:shape/><d:constrLst>'
    '<d:constr type="w" for="ch" forName="heading" refType="w"/>'
    '<d:constr type="h" for="ch" forName="heading" refType="h" fact="0.25"/>'
    '<d:constr type="w" for="ch" forName="row" refType="w"/>'
    '<d:constr type="h" for="ch" forName="row" refType="h" fact="0.75"/>'
    '<d:constr type="t" for="ch" forName="row" refType="h" fact="0.25"/>'
    '<d:constr type="primFontSz" for="ch" forName="heading" val="24"/>'
    '<d:constr type="primFontSz" for="des" forName="cell" val="18"/>'
    '</d:constrLst><d:forEach axis="ch" cnt="1">'
    '<d:layoutNode name="heading" styleLbl="flat"><d:alg type="tx"/><d:shape type="rect"/>'
    '<d:presOf axis="self"/><d:constrLst><d:constr type="lMarg" refType="primFontSz" fact="0.05"/>'
    '<d:constr type="rMarg" refType="primFontSz" fact="0.05"/></d:constrLst></d:layoutNode>'
    '<d:layoutNode name="row"><d:choose><d:if func="var" arg="dir" op="equ" val="rev">'
    '<d:alg type="lin"><d:param type="linDir" val="fromR"/></d:alg></d:if>'
    '<d:else><d:alg type="lin"><d:param type="linDir" val="fromL"/></d:alg></d:else></d:choose>'
    '<d:shape/><d:constrLst><d:constr type="w" for="ch" forName="cell" refType="w"/>'
    '<d:constr type="h" for="ch" forName="cell" refType="h"/></d:constrLst>'
    '<d:forEach axis="ch"><d:layoutNode name="cell" styleLbl="flat"><d:alg type="tx"/>'
    '<d:shape type="rect"/><d:presOf axis="desOrSelf"/></d:layoutNode></d:forEach>'
    '</d:layoutNode></d:forEach></d:layoutNode></d:layoutDef>')
diagram_colors = (f'<d:colorsDef xmlns:d="{DGM}" xmlns:a="{A}"><d:styleLbl name="flat">'
    '<d:fillClrLst><a:srgbClr val="4080C0"><a:shade val="50000"/></a:srgbClr></d:fillClrLst>'
    '<d:txFillClrLst><a:srgbClr val="FFFFFF"/></d:txFillClrLst></d:styleLbl></d:colorsDef>')
diagram_style = (f'<d:styleDef xmlns:d="{DGM}" xmlns:a="{A}"><d:styleLbl name="flat"><d:style>'
    '<a:lnRef idx="0"/><a:effectRef idx="0"/><a:fontRef idx="minor"/></d:style></d:styleLbl></d:styleDef>')
diagram_parts = {"word/diagrams/data.xml":diagram_graph,"word/diagrams/layout.xml":diagram_template,
    "word/diagrams/colors.xml":diagram_colors,"word/diagrams/style.xml":diagram_style}
diagram_rels = ''.join(relationship(key,kind,'diagrams/'+file+'.xml') for key,kind,file in [
    ('diagram','diagramData','data'),('layout','diagramLayout','layout'),
    ('colors','diagramColors','colors'),('style','diagramQuickStyle','style')])
diagram_body = '<w:p>' + smartart.replace('r:dm="diagram"','r:dm="diagram" r:lo="layout" r:cs="colors" r:qs="style"') + '</w:p>'
package("diagram-composite.docx",diagram_body,rels=diagram_rels,extras=diagram_parts)
package("diagram-cycle.docx",diagram_body,rels=diagram_rels,extras={**diagram_parts,
    "word/diagrams/data.xml":diagram_graph.replace('</d:cxnLst>','<d:cxn srcId="left" destId="main"/></d:cxnLst>')})
package("diagram-unsupported.docx",diagram_body,rels=diagram_rels,extras={**diagram_parts,
    "word/diagrams/layout.xml":diagram_template.replace('type="composite"','type="cycle"')})


def emf_record(kind, payload):
    payload += b'\0' * (-len(payload) % 4)
    return struct.pack('<II',kind,8+len(payload)) + payload


def emf_preview(label):
    font = bytearray(360)
    struct.pack_into('<Iiiiii',font,0,1,-10,0,0,0,400)
    font[32:32+12] = 'Arial\0'.encode('utf-16le')
    bitmap = bytearray(108)
    struct.pack_into('<II',bitmap,0,114,176)
    struct.pack_into('<iiii',bitmap,24,4,0,16,16)
    bitmap[42:44] = bytes([255,1])
    struct.pack_into('<6f',bitmap,52,1,0,0,1,0,0)
    struct.pack_into('<7I',bitmap,80,0,108,52,160,16,2,2)
    dib = struct.pack('<IiiHHIIiiIIIII',40,2,2,1,32,3,16,0,0,0,0,16711680,65280,255)
    pixels = bytes([255,0,0,255, 0,128,0,128, 0,0,255,255, 0,0,0,0])
    text = label.encode('utf-16le')
    text_record = bytearray(68)
    struct.pack_into('<I',text_record,16,1)
    struct.pack_into('<ff',text_record,20,1,1)
    struct.pack_into('<iiIII',text_record,28,20,18,len(text)//2,76,0)
    records = [emf_record(10,struct.pack('<ii',0,0)),emf_record(9,struct.pack('<ii',40,32)),
        emf_record(82,font),emf_record(37,struct.pack('<I',1)),emf_record(24,struct.pack('<I',0)),
        bytes(bitmap)+dib+pixels,emf_record(22,struct.pack('<I',6)),emf_record(84,bytes(text_record)+text),
        emf_record(14,struct.pack('<III',0,16,20))]
    header = bytearray(108)
    struct.pack_into('<IIiiii',header,0,1,108,0,0,40,32)
    struct.pack_into('<IIII',header,40,1179469088,65536,len(header)+sum(map(len,records)),1+len(records))
    return bytes(header)+b''.join(records)


emf_bytes = emf_preview('Clip 🐦 & <x>')
(ROOT / 'preview.emf').write_bytes(emf_bytes)
(ROOT / 'preview-truncated.emf').write_bytes(emf_bytes[:-4])
emf_types = '<Default Extension="emf" ContentType="image/x-emf"/>'
for name, value in [('metafile-preview',emf_bytes),('metafile-truncated',emf_bytes[:-4])]:
    package(name+'.docx','<w:p>'+ole_preview+'</w:p>',rels=relationship('image','image','media/preview.emf'),
        extras={'word/media/preview.emf':value},extra_types=emf_types)

unsupported = '<w:p><w:r><w:drawing/></w:r><w:hyperlink r:id="evil"><w:r><w:t>Unsafe</w:t></w:r></w:hyperlink><w:r><w:footnoteReference w:id="2"/></w:r></w:p>'
package("unsupported.docx", unsupported, rels=relationship("evil","hyperlink","javascript:alert(1)",True))
package("external-image.docx", '<w:p>' + image + '</w:p>', rels=relationship("image","image","https://example.invalid/pixel.png",True))
package("missing-style.docx", paragraph("Body",'<w:pPr><w:pStyle w:val="Absent"/></w:pPr>'))
cycle = styles.replace('<w:basedOn w:val="Heading1"/>','<w:basedOn w:val="Custom"/>')
package("style-cycle.docx", body=paragraph("Cycle",'<w:pPr><w:pStyle w:val="Custom"/></w:pPr>'), rels=relationship("styles","styles","styles.xml"), extras={"word/styles.xml":cycle})
package("no-main.docx", paragraph("unused"), root_rels="")
package("escape.docx", paragraph("unused"), root_rels=relationship("main","officeDocument","../../escape.xml"))
package("encoded-escape.docx", paragraph("unused"), root_rels=relationship("main","officeDocument","%2E%2E/escape.xml"))
package("macro.docx", paragraph("unused"), mime="application/vnd.ms-word.document.macroEnabled.main+xml")
package("malformed.docx", "", raw=f'<w:document xmlns:w="{W}"><w:body><w:p></w:body></w:document>')
package("unbound-prefix.docx", "", raw='<w:document><w:body/></w:document>')
package("dtd.docx", "", raw=f'<!DOCTYPE x [<!ENTITY entity "secret">]><w:document xmlns:w="{W}"><w:body/></w:document>')
package("alternate-prefix.docx", "", raw=f'<x:document xmlns:x="{W}"><x:body><x:p><x:r><x:t>Prefix independent</x:t></x:r></x:p></x:body></x:document>')
strict_w = "http://purl.oclc.org/ooxml/wordprocessingml/main"
strict_r = "http://purl.oclc.org/ooxml/officeDocument/relationships/officeDocument"
package("strict.docx", "", raw=f'<s:document xmlns:s="{strict_w}"><s:body><s:p><s:r><s:t>Strict namespace</s:t></s:r></s:p></s:body></s:document>', root_rels=f'<Relationship Id="main" Type="{strict_r}" Target="word/document.xml"/>')

package("nul.docx", "", raw=f'<w:document xmlns:w="{W}"><w:body/></w:document>\x00ignored')
package("missing-main-part.docx", "", omit=("word/document.xml",))
package("duplicate-rel.docx", "", root_rels=relationship("same","officeDocument","word/document.xml") * 2)
package("duplicate-types.docx", "", extras={"[Content_Types].xml":f'<Types xmlns="{CT}"><Default Extension="xml" ContentType="application/xml"/><Default Extension="xml" ContentType="application/xml"/></Types>'})
package("unsupported-table-child.docx", '<w:tbl><w:sdt/></w:tbl>')
# Alter the stored CRC rather than the payload, forcing the archive's lazy integrity check.
damaged = bytearray((ROOT / "minimal.docx").read_bytes())
with zipfile.ZipFile(BytesIO(damaged)) as archive:
    offset = archive.start_dir
    damaged[offset + 16] ^= 1
    local = archive.infolist()[0].header_offset
    damaged[local + 14] ^= 1
(ROOT / "corrupt-member.docx").write_bytes(damaged)
(ROOT / "space.xml").write_text('<x:t xmlns:x="urn:example" xml:space="preserve">  leading &amp; trailing  </x:t>\n')
