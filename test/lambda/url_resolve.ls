// url_resolve follows the WHATWG URL parser (Lambda_Sys_Func.md, url_resolve)

let base = "http://fakehost/test/page.html";
'1. relative references';
[url_resolve(base, "img/a.png"), url_resolve(base, "../a/./b"), url_resolve(base, "//cdn.example.com/x"),
 url_resolve(base, "?q=1"), url_resolve(base, "#top"), url_resolve(base, "a b.png")]

'2. schemes and special hosts are lowercased; opaque hosts keep case';
[url_resolve(base, "HTTP://WWW.TeAra.govt.nz/en"), url_resolve(base, "Mailto:Someone@Example.COM")]

'3. a file URL normalizes its Windows drive letter';
[url_resolve(base, "file:///C|/Documents/a.gif"), url_resolve("file:///C:/docs/", "b.gif")]

'4. an opaque path keeps spaces and backslashes';
[url_resolve(base, "data:image/svg+xml;utf8,<svg viewBox='0 0 1 1'></svg>"), url_resolve(base, "mailto:a\\b@c.d")]

'5. long URLs are not truncated';
let long_data = "data:text/plain," ++ join(fill(5000, "a"), "");
[len(url_resolve(base, long_data)) == len(long_data), len(url_resolve(base, join(fill(3000, "seg/"), ""))) > 12000]
