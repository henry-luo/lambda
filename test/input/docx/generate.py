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


def package(name, body, *, main="word/document.xml", extras=None, rels="", root_rels=None, mime=MAIN, raw=None, omit=()):
    entries = {
        "[Content_Types].xml": f'<Types xmlns="{CT}"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Default Extension="png" ContentType="image/png"/><Default Extension="gif" ContentType="image/gif"/><Default Extension="svg" ContentType="image/svg+xml"/><Override PartName="/{main}" ContentType="{mime}"/></Types>',
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
image = '<w:r><w:drawing><wp:inline><wp:extent cx="914400" cy="457200"/><wp:docPr id="1" name="pixel" descr="Pixel &amp; alt"/><a:graphic><a:graphicData><a:blip r:embed="image"/></a:graphicData></a:graphic></wp:inline></w:drawing></w:r>'
links = '<w:hyperlink r:id="link"><w:r><w:t>Example</w:t></w:r></w:hyperlink><w:hyperlink w:anchor="mark"><w:r><w:t>Jump</w:t></w:r></w:hyperlink>'
body = '<w:p><w:bookmarkStart w:id="1" w:name="mark"/>' + links + image + '</w:p>'
props = '<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Example document</dc:title><dc:creator>Lambda</dc:creator></cp:coreProperties>'
root_rels = relationship("main", "officeDocument", "word/document.xml") + '<Relationship Id="props" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>'
package("media.docx", body, rels=relationship("image","image","media/%C3%A9%20pixel.png") + relationship("link","hyperlink","https://example.com/?a=1&amp;b=2",True), root_rels=root_rels, extras={"word/media/é pixel.png":PNG,"docProps/core.xml":props})
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
