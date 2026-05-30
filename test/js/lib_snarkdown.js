// Snarkdown 2.0.0 Library Support Tests
// Minimal markdown parser; exercises regex tokenization and recursive
// inline parsing. Substitutes for markdown-it/marked which trip Lambda's
// regex-literal tokenizer / segfault during parse.

globalThis.global = globalThis;
globalThis.self = globalThis;
var module = { exports: {} };
var exports = module.exports;
!function(e,n){"object"==typeof exports&&"undefined"!=typeof module?module.exports=n():"function"==typeof define&&define.amd?define(n):(e=e||self).snarkdown=n()}(this,function(){var e={"":["<em>","</em>"],_:["<strong>","</strong>"],"*":["<strong>","</strong>"],"~":["<s>","</s>"],"\n":["<br />"]," ":["<br />"],"-":["<hr />"]};function n(e){return e.replace(RegExp("^"+(e.match(/^(\t| )+/)||"")[0],"gm"),"")}function r(e){return(e+"").replace(/"/g,"&quot;").replace(/</g,"&lt;").replace(/>/g,"&gt;")}return function t(o,a){var c,s,l,g,u,p=/((?:^|\n+)(?:\n---+|\* \*(?: \*)+)\n)|(?:^``` *(\w*)\n([\s\S]*?)\n```$)|((?:(?:^|\n+)(?:\t|  {2,}).+)+\n*)|((?:(?:^|\n)([>*+-]|\d+\.)\s+.*)+)|(?:!\[([^\]]*?)\]\(([^)]+?)\))|(\[)|(\](?:\(([^)]+?)\))?)|(?:(?:^|\n+)([^\s].*)\n(-{3,}|={3,})(?:\n+|$))|(?:(?:^|\n+)(#{1,6})\s*(.+)(?:\n+|$))|(?:`([^`].*?)`)|(  \n\n*|\n{2,}|__|\*\*|[_*]|~~)/gm,f=[],i="",d=a||{},m=0;function h(n){var r=e[n[1]||""],t=f[f.length-1]==n;return r?r[1]?(t?f.pop():f.push(n),r[0|t]):r[0]:n}function $(){for(var e="";f.length;)e+=h(f[f.length-1]);return e}for(o=o.replace(/^\[(.+?)\]:\s*(.+)$/gm,function(e,n,r){return d[n.toLowerCase()]=r,""}).replace(/^\n+|\n+$/g,"");l=p.exec(o);)s=o.substring(m,l.index),m=p.lastIndex,c=l[0],s.match(/[^\\](\\\\)*\\$/)||((u=l[3]||l[4])?c='<pre class="code '+(l[4]?"poetry":l[2].toLowerCase())+'"><code'+(l[2]?' class="language-'+l[2].toLowerCase()+'"':"")+">"+n(r(u).replace(/^\n+|\n+$/g,""))+"</code></pre>":(u=l[6])?(u.match(/\./)&&(l[5]=l[5].replace(/^\d+/gm,"")),g=t(n(l[5].replace(/^\s*[>*+.-]/gm,""))),">"==u?u="blockquote":(u=u.match(/\./)?"ol":"ul",g=g.replace(/^(.*)(\n|$)/gm,"<li>$1</li>")),c="<"+u+">"+g+"</"+u+">"):l[8]?c='<img src="'+r(l[8])+'" alt="'+r(l[7])+'">':l[10]?(i=i.replace("<a>",'<a href="'+r(l[11]||d[s.toLowerCase()])+'">'),c=$()+"</a>"):l[9]?c="<a>":l[12]||l[14]?c="<"+(u="h"+(l[14]?l[14].length:l[13]>"="?1:2))+">"+t(l[12]||l[15],d)+"</"+u+">":l[16]?c="<code>"+r(l[16])+"</code>":(l[17]||l[1])&&(c=h(l[17]||"--"))),i+=s,i+=c;return(i+o.substring(m)+$()).replace(/^\n+|\n+$/g,"")}});
//# sourceMappingURL=snarkdown.umd.js.map

const snarkdown = module.exports.default || module.exports;

// === Test 1: loaded ===
console.log(typeof snarkdown);

// === Test 2: Plain text passthrough ===
console.log(snarkdown('hello world'));

// === Test 3: Heading levels ===
console.log(snarkdown('# H1'));
console.log(snarkdown('## H2'));
console.log(snarkdown('### H3'));

// === Test 4: Bold ===
console.log(snarkdown('**bold**'));
console.log(snarkdown('__bold__'));

// === Test 5: Italic ===
console.log(snarkdown('_italic_'));

// === Test 6: Strikethrough ===
console.log(snarkdown('~~strike~~'));

// === Test 7: Mixed inline ===
console.log(snarkdown('**bold** and _italic_'));

// === Test 8: Inline code ===
console.log(snarkdown('use `code` here'));

// === Test 9: Fenced code block ===
console.log(snarkdown('```\nfoo\n```'));

// === Test 10: Code with language ===
console.log(snarkdown('```js\nx=1\n```'));

// === Test 11: Unordered list ===
console.log(snarkdown('- a\n- b\n- c'));

// === Test 12: Ordered list ===
console.log(snarkdown('1. one\n2. two'));

// === Test 13: Link ===
console.log(snarkdown('[click](https://example.com)'));

// === Test 14: Image ===
console.log(snarkdown('![alt](img.png)'));

// === Test 15: Blockquote ===
console.log(snarkdown('> quoted'));

// === Test 16: Horizontal rule ===
console.log(snarkdown('\n---\n'));

// === Test 17: Line break (two trailing spaces) ===
console.log(snarkdown('line1  \nline2'));

// === Test 18: Reference-style link ===
console.log(snarkdown('[ref][1]\n[1]: https://x.com'));

// === Test 19: Escaping HTML in output ===
console.log(snarkdown('<script>'));

// === Test 20: Empty input ===
console.log(JSON.stringify(snarkdown('')));

console.log("SNARKDOWN_DONE");
