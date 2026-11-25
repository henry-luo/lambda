// D10.2.2v2: preserve the native fraction, delimiter and arrow source forms.
let examples = ["\\binom{n}{k}", "\\dbinom{n}{k}", "\\tbinom{n}{k}",
    "\\genfrac{(}{)}{2pt}{0}{a}{b}", "{a\\above2pt b}",
    "\\cfrac[l]{1}{12345}", "\\xrightarrow[below]{above}", "\\bigl( x \\bigr)",
    "\\includegraphics[width=1em]{image.png}"];
let expected = ["\\binom{n}{k}", "\\dbinom{n}{k}", "\\tbinom{n}{k}",
    "\\genfrac{(}{)}{2pt}{0}{a}{b}", "{a \\above2pt b}",
    "\\cfrac[l]{1}{12345}", "\\xrightarrow[b e l o w]{a b o v e}", "\\bigl( x \\bigr)",
    "\\includegraphics[width=1em]{image.png}"];
[for (i in 0 to len(examples) - 1) format(parse(examples[i],'math')^,{type:'math',flavor:'latex'})^ == expected[i]]
