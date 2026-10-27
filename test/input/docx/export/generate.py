"""Deterministic DOCX writer inputs; no Office installation is required."""
from pathlib import Path
import base64, struct, zlib
ROOT = Path(__file__).parent

def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 8, 4, 8, 2, 0, 0, 0)) +
       chunk(b"IDAT", zlib.compress(b"".join(b"\x00" + bytes([24, 100, 200]) * 8 for _ in range(4)))) + chunk(b"IEND", b""))
(ROOT / "blue.png").write_bytes(png)
uri = "data:image/png;base64," + base64.b64encode(png).decode()
(ROOT / "features.html").write_text(f'''<!doctype html><html><head><meta charset="utf-8"><title>Export &amp; 世界</title></head><body>
<h1 id="intro">DOCX export 世界</h1>
<p style="text-align:center;line-height:1.5;margin:6pt 0 9pt">A <strong>bold</strong> <em>italic</em> <u>underline</u> <s>strike</s> <sub>sub</sub> <sup>sup</sup>.</p>
<p><span style="font-family:Georgia;font-size:18pt;color:#2468ac;background-color:#fff2cc;letter-spacing:1px">Styled &amp; &lt;XML&gt;</span></p>
<div style="text-align:right;line-height:1.5"><p>Inherited alignment</p></div>
<p><a href="https://example.com/report?a=1&amp;b=2">External link</a> <a href="#intro">Back to heading</a></p>
<ol start="3"><li>Third</li><li>Fourth<ul><li>Nested</li></ul><p>Same item continuation</p></li></ol>
<ol start="3"><li>Restart at three</li></ol>
<table style="width:100%"><thead><tr><th rowspan="2" style="background-color:#edf3f6;border:1px solid #334455;padding:4px">Merged</th><th colspan="2">Two columns</th></tr></thead><tbody><tr><td>A</td><td>B</td></tr></tbody></table>
<table><tr><td>One</td><td>Two</td><td>Three</td><td rowspan="2">Far merge</td></tr><tr><td>Second row</td></tr></table>
<pre>tab\tline\nnext  line</pre>
<p><img src="{uri}" width="96" height="48" alt="Blue picture"><img src="{uri}" height="24" alt="Same picture smaller"></p>
<p style="page-break-before:always">New page</p>
</body></html>''', encoding="utf-8")
(ROOT / "losses.html").write_text('''<article><p>Still usable <span style="transform:rotate(2deg);color:rgba(0,0,0,.5)">text</span> <a href="javascript:alert(1)">unsafe label</a></p><p><img src="https://example.invalid/never-fetch.png" alt="Remote image"></p><svg><text>Vector label</text></svg></article>''')
(ROOT / "plain.txt").write_text("Literal <xml> & Unicode 世界\nSecond line\n", encoding="utf-8")
(ROOT / "styled.html").write_text('<html><head><style>p { color: red; }</style></head><body><p>Visible text</p></body></html>')
