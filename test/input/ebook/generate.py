"""Authored, deterministic ebook fixtures; no downloaded publications."""
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parent
OCF = 'urn:oasis:names:tc:opendocument:xmlns:container'
OPF = 'http://www.idpf.org/2007/opf'
XHTML = 'http://www.w3.org/1999/xhtml'

def archive(name, parts):
    with zipfile.ZipFile(ROOT / name, 'w') as out:
        for path, text in {'mimetype': 'application/epub+zip', **parts}.items():
            info = zipfile.ZipInfo(path, (2026, 1, 1, 0, 0, 0))
            out.writestr(info, text)

parts = {
    'META-INF/container.xml': f'<container xmlns="{OCF}" version="1.0"><rootfiles><rootfile full-path="Books/novel.opf" media-type="application/oebps-package+xml"/></rootfiles></container>',
    'Books/novel.opf': f'''<package xmlns="{OPF}" version="3.0" unique-identifier="book-id">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>A Small Book</dc:title><dc:creator>Lambda</dc:creator><dc:language>en</dc:language><dc:identifier id="book-id">lambda-ebook-fixture</dc:identifier></metadata>
<manifest><item id="one" href="text/one.xhtml" media-type="application/xhtml+xml"/><item id="two" href="text/two.xhtml" media-type="application/xhtml+xml"/><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/><item id="css" href="styles/book.css" media-type="text/css"/><item id="picture" href="images/dot.svg" media-type="image/svg+xml" properties="cover-image"/></manifest>
<spine><itemref idref="one"/><itemref idref="two"/></spine></package>''',
    'Books/nav.xhtml': f'<html xmlns="{XHTML}" xmlns:epub="http://www.idpf.org/2007/ops"><body><nav epub:type="toc"><ol><li><a href="text/one.xhtml#start">First chapter</a><ol><li><a href="text/two.xhtml#start">Next chapter</a></li></ol></li></ol></nav><nav epub:type="landmarks"><ol><li><a href="text/one.xhtml#start">Start reading</a></li></ol></nav><nav epub:type="page-list"><ol><li><a href="text/two.xhtml#start">2</a></li></ol></nav></body></html>',
    'Books/text/one.xhtml': f'<html xmlns="{XHTML}"><head><title>First</title><link rel="stylesheet" href="../styles/book.css"/></head><body><h1 id="start">First chapter</h1><p>Café 世界 😀. A passage to remember.</p><p><a href="two.xhtml#start">Continue reading</a></p><img src="../images/dot.svg" alt="A red dot"/></body></html>',
    'Books/text/two.xhtml': f'<html xmlns="{XHTML}"><body><h1 id="start">Next chapter</h1><p>The last passage.</p></body></html>',
    'Books/styles/book.css': 'body {font-family:serif;color:#25354a} h1 {color:#963020} p {line-height:1.6} .picture {background-image:url("../images/dot.svg")}',
    'Books/images/dot.svg': '<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24"><circle cx="12" cy="12" r="10" fill="red"/></svg>',
}
archive('minimal.epub', parts)
archive('missing-spine.epub', {**parts, 'Books/novel.opf': parts['Books/novel.opf'].replace('idref="one"', 'idref="absent"')})
archive('escape.epub', {**parts, 'META-INF/container.xml': parts['META-INF/container.xml'].replace('Books/novel.opf', '../book.opf')})
archive('protected.epub', {**parts, 'META-INF/encryption.xml': '<encryption xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><EncryptedData xmlns="http://www.w3.org/2001/04/xmlenc#"><EncryptionMethod Algorithm="urn:unsupported"/><CipherData><CipherReference URI="Books/text/one.xhtml"/></CipherData></EncryptedData></encryption>'})

archive('epub2.epub', {**parts,
    'Books/novel.opf': parts['Books/novel.opf'].replace('version="3.0"', 'version="2.0"').replace('<spine>', '<spine toc="ncx">').replace('</manifest>', '<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest>').replace(' properties="nav"', ''),
    'Books/toc.ncx': '<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/"><navMap><navPoint id="one"><navLabel><text>NCX first</text></navLabel><content src="text/one.xhtml#start"/></navPoint></navMap></ncx>'})
archive('fixed.epub', {**parts,
    'Books/novel.opf': parts['Books/novel.opf'].replace('</metadata>', '<meta property="rendition:layout">pre-paginated</meta></metadata>'),
    'Books/text/one.xhtml': f'<html xmlns="{XHTML}"><head><meta name="viewport" content="width=600,height=800"/></head><body><h1>Fixed page</h1><p>Authored at 600 by 800.</p></body></html>',
    'Books/text/two.xhtml': f'<html xmlns="{XHTML}"><head><meta name="viewport" content="width=600,height=800"/></head><body><h1>Second fixed page</h1></body></html>'})
archive('paged.epub', {**parts,
    'Books/text/one.xhtml': f'<html xmlns="{XHTML}"><head/><body><h1>Long chapter</h1>' + ''.join(f'<p>Paragraph {i}. This authored passage fills several reader pages for navigation testing.</p>' for i in range(60)) + '</body></html>'})
archive('svg.epub', {**parts, 'Books/novel.opf': parts['Books/novel.opf'].replace('idref="one"', 'idref="picture"')})
archive('fallback.epub', {**parts, 'Books/novel.opf': parts['Books/novel.opf'].replace('</manifest>', '<item id="foreign" href="unsupported.bin" media-type="application/x-test" fallback="one"/></manifest>').replace('idref="one"', 'idref="foreign"')})
archive('unsafe.epub', {**parts,
    'Books/text/one.xhtml': parts['Books/text/one.xhtml'].replace('</body>', '<script>alert(1)</script><iframe src="https://example.invalid"/><img src="https://example.invalid/x.png"/><p onclick="alert(1)">Safe text</p></body>')})
archive('css-import.epub', {**parts, 'Books/styles/book.css': '@import "nested/extra.css"; ' + parts['Books/styles/book.css'],
    'Books/styles/nested/extra.css': '.imported{background:url("../../images/dot.svg")} .external{background:url(https://example.invalid/image)}'})
archive('css-cycle.epub', {**parts, 'Books/styles/book.css': '@import "book.css";'})


archive('raw-style.epub', {**parts, 'Books/styles/book.css': 'p::after{content:"</style><script>alert(1)</script>"}'})

archive('encoded-links.epub', {**parts,
    'Books/text/one.xhtml': parts['Books/text/one.xhtml'].replace('two.xhtml#start', 'two.xhtml#part%252F'),
    'Books/text/two.xhtml': parts['Books/text/two.xhtml'].replace('</body>', '<div style="height:3000px"/><p id="part%2F" style="position:absolute;top:1200px">Encoded anchor</p></body>')})
