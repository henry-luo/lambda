import d: lambda.doc.docbook
let source = input("test/input/docbook/verbatim.xml",{type:"xml",flavor:"document"})^
let result = d.convert(source,{unsupported:"error"})^
let html = d.to_html(source)^
let custom = d.to_html(source,{parameters:{["verbatim-number-every-nth"]:2,
    ["verbatim-number-first-line"]:false,["verbatim-number-separator"]:""}})^
let fallback = d.to_html(source,{parameters:{["verbatim-style-default"]:"plain"}})^;
[
    len(result.diagnostics) == 0,
    contains(html,"class=\"language-none long numbered programlisting verbatim verblines\""),
    contains(html,"<span class=\"ln\"> 1 <span class=\"nsep\">|</span>"),
    contains(html,"<span class=\"ln\">10 <span class=\"nsep\">|</span>"),
    contains(html,"<span class=\"ld\"><code> </code></span>"),
    contains(html,"<pre class=\"screen verbatim verblines\"><span class=\"line\">"),
    contains(html,"<pre class=\"literallayout verbatim\">  aligned\n \n  text</pre>"),
    contains(html,"<pre class=\"funcsynopsisinfo verbatim\"><code>type declarations</code></pre>"),
    not contains(custom,"class=\"nsep\""),
    contains(custom,"<span class=\"ln\"> 2 </span>"),
    fallback == html,
    (d.convert(source,{parameters:{["verbatim-number-every-nth"]:0}}) ^ { ^.code }) == 7701
]
