// lambda.mail.sanitize (vibe/Lambda_Demo_Mail.md ML5v2, §5.8): the second
// layer behind the script-free frame: scripts, handlers, embedded documents,
// forms and navigation removed; remote loads blocked until allowed; cid:
// parts inlined as data: URLs; <noscript> parsed and cleaned.
import san: lambda.mail.sanitize

let hostile = "<html><head><title>T</title><meta http-equiv=\"refresh\" content=\"0;url=http://evil\"><link rel=\"stylesheet\" href=\"http://x/a.css\"><style>@import url(http://x/i.css); p { background: URL('http://x/b.png') } q { background: url(cid:logo@x) }</style><script>alert(1)</script></head><body bgcolor=\"#fff\" onload=\"go()\"><p onclick=\"x()\" style=\"background:url(https://t/p.gif) no-repeat\">Hi <a href=\"java\tscript:alert(1)\">bad</a> <a href=\"https://example.com/x\">good</a> <a href=\"#top\">top</a> <a href=\"mailto:ada@example.org\" title=\"Write\">mail</a></p><img src=\"http://t/pixel.gif\" srcset=\"http://t/a.png 1x, cid:logo@x 2x\"><img src=\"cid:logo@x\"><img src=\"cid:missing@x\"><img src=\"data:image/png;base64,AAAA\"><img src=\"data:text/html,<script>\"><img src=\"local.png\"><noscript><img src=\"http://t/ns.gif\"><p>no script</p></noscript><form action=\"http://x/post\"><input name=\"q\" formaction=\"http://y\"><button>Go</button></form><iframe srcdoc=\"<script>1</script>\"></iframe><object data=\"x.swf\"></object><webview src=\"https://x\"></webview><svg><script>2</script><image href=\"http://t/s.png\"/></svg><!--[if mso]>x<![endif]--></body></html>"
let cids = san.cid_urls([{cid: "logo@x", type: "image/png", content: b'\x89504E47'}, {cid: null, type: "text/plain", content: "x"}]);

"=== cid parts ==="
cids

"=== blocked ==="
let blocked = san.clean(hostile, {cids: cids});
blocked.blocked
blocked.html

"=== allowed ==="
let allowed = san.clean(hostile, {remote: true, cids: cids});
allowed.blocked
allowed.html

"=== style text ==="
san.clean_css("a { background: url(\"http://x/y.png\") } @IMPORT 'z.css'; b { list-style-image: url(cid:logo@x) }", {remote: false, cids: cids})

"=== not HTML ==="
san.clean("", {}).blocked
san.clean("plain words", {}).html
