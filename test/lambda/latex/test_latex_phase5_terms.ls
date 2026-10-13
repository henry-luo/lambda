// Phase V M7 (vibe/impl/Lambda_Impl_Latex_Packages.md §11): the term registry:
// glossaries, acronym, nomencl, makeidx and imakeidx.
import latex: lambda.latex.latex

fn doc(docclass, preamble, body) =>
    "\\documentclass{" ++ docclass ++ "}\n" ++ preamble ++ "\n\\begin{document}\n" ++ body ++ "\n\\end{document}\n"

fn run(src) {
    let ast = latex.parse_source(src, "test/lambda/latex/fixtures") ^ { null }
    let result = latex.render_result(ast, {base_uri: "test/lambda/latex/fixtures"})
    {html: latex.render_to_html(ast, {base_uri: "test/lambda/latex/fixtures"}),
     issues: [for (issue in result.diagnostics) issue.code ++ " " ++ string(issue.item)]}
}

fn has(text, part) => index_of(text, part) != null

// ---- glossaries ----
let glossary = run(doc("article", "\\usepackage[acronym,toc]{glossaries}\\makeglossaries\\renewcommand{\\glossaryname}{Terms}" ++
    "\\newglossaryentry{cpu}{name={CPU}, description={central unit}, plural={CPUs}}" ++
    "\\newglossaryentry{ram}{name=memory, description={fast store}, first={random-access memory}}" ++
    "\\newacronym{gpu}{GPU}{graphics unit}\\newglossaryentry{unused}{name=zzz, description=never}\\loadglsentries{phase5_glossary}",
    "\\tableofcontents\\section{Intro}A \\gls{cpu} and \\Glspl{cpu}, \\gls{gpu} then \\gls{gpu}. \\gls{ram}, \\gls{ram}, " ++
    "\\acrlong{gpu}, \\GLS{cpu}, \\glsdesc{cpu}, \\glspl{closure}.\\section{More}\\glsreset{gpu}\\gls{gpu} \\gls{cpu} \\gls{nope}\\printglossaries"))
"first and later forms:"; has(glossary.html, ">graphics unit (GPU)</a> then <a class=\"latex-gls\"") and
    has(glossary.html, ">random-access memory</a>, <a class=\"latex-gls\"") and has(glossary.html, "href=\"#gls-ram\">memory</a>")
"plural and case:"; has(glossary.html, "href=\"#gls-cpu\">CPUs</a>") and has(glossary.html, "style=\"text-transform:uppercase\">CPU</a>")
"glsreset:"; has(glossary.html, "<h2 id=\"more\"><span class=\"sec-num\">2 </span>More</h2><p><a class=\"latex-gls\" id=\"glsuse-")
"loaded entries:"; has(glossary.html, "href=\"#gls-closure\">closures</a>")
"glossary list:"; has(glossary.html, "<h2 class=\"latex-list-heading\" id=\"glossary\">Terms</h2><dl class=\"latex-glossary-list\"><dt id=\"gls-closure\"><strong>closure</strong></dt><dd>a function with its captured scope. <span class=\"latex-locators\">")
"listed only:"; not has(glossary.html, "zzz")
"locators:"; has(glossary.html, "<dt id=\"gls-cpu\"><strong>CPU</strong></dt><dd>central unit. <span class=\"latex-locators\"><a href=\"#glsuse-")
"acronym list and toc:"; has(glossary.html, "<li class=\"toc-l2\"><a href=\"#glossary\">Terms</a></li><li class=\"toc-l2\"><a href=\"#acronyms\">Acronyms</a></li>") and
    has(glossary.html, "<dt id=\"gls-gpu\"><strong>GPU</strong></dt><dd>graphics unit.")
"glossary issues:"; glossary.issues
let styled = run(doc("article", "\\usepackage[style=long]{glossaries}\\setacronymstyle{short-long}", ""))
"style issues:"; styled.issues

// ---- acronym ----
let acronyms = run(doc("article", "\\usepackage[printonlyused]{acronym}\\renewcommand*{\\acsfont}[1]{\\textsc{#1}}",
    "\\ac{NN} then \\ac{NN}, \\acp{NN}, \\acl{CNN}, \\Acf{CNN}.\n" ++
    "\\begin{acronym}[CNN]\\acro{NN}{neural net}\\acro{CNN}[ConvNN]{convolutional net}\\acro{XX}{unused}\\end{acronym}"))
"acronym uses:"; has(acronyms.html, "<a class=\"latex-acronym\" href=\"#acro-nn\">neural net (NN)</a> then <a class=\"latex-acronym\" href=\"#acro-nn\"><span style=\"font-variant:small-caps\">NN</span></a>")
"acronym forms:"; has(acronyms.html, ">convolutional net</a>, <a class=\"latex-acronym\" href=\"#acro-cnn\">Convolutional net (ConvNN)</a>")
"printonlyused:"; has(acronyms.html, "<dl class=\"latex-acronyms\"><dt id=\"acro-nn\"><strong>NN</strong></dt><dd>neural net</dd><dt id=\"acro-cnn\">") and
    not has(acronyms.html, "unused")
"acronym issues:"; acronyms.issues
"acronym page option:"; run(doc("article", "\\usepackage[withpage]{acronym}", "")).issues

// ---- nomencl ----
let symbols = run(doc("book", "\\usepackage[intoc]{nomencl}\\makenomenclature\\renewcommand{\\nomname}{List of Symbols}",
    "\\tableofcontents\\chapter{C}\\nomenclature[b]{v}{speed}\\nomenclature[a]{c}{light}\\printnomenclature"))
"nomenclature:"; has(symbols.html, "<h1 class=\"latex-list-heading\" id=\"nomenclature\">List of Symbols</h1><dl class=\"latex-nomenclature-list\"><dt>c</dt><dd>light</dd><dt>v</dt><dd>speed</dd></dl>")
"nomenclature in toc:"; has(symbols.html, "<li class=\"toc-l1\"><a href=\"#nomenclature\">List of Symbols</a></li>")
"nomencl issues:"; symbols.issues

// ---- the index ----
let index = run(doc("article", "\\usepackage{makeidx}\\makeindex\\renewcommand{\\indexname}{Idx}",
    "\\section{One}Apple\\index{apple} and\\index{fruit!pear|textbf} \\index{zebra@\\textit{Zebra}}\\index{range|(}" ++
    "\\section{Two}\\index{apple}\\index{fruit!pear}\\index{apple}\\index{range|)}\\index{beet|see{fruit}}\\index{\"!bang}\\printindex"))
"index anchor:"; has(index.html, "Apple<a class=\"latex-index-anchor\" id=\"idx-")
"index entries:"; has(index.html, "<h2 class=\"latex-list-heading\">Idx</h2><div class=\"latex-index-body\" style=\"column-count:2\">")
"one locator per section:"; has(index.html, ">apple, <a href=\"#idx-") and has(index.html, ">1</a>, <a href=\"#idx-") and
    not has(index.html, ">2</a>, <a href=\"#idx-")
"subentry and format:"; has(index.html, "latex-index-level-1\">fruit</div><div class=\"latex-index-item latex-index-level-2\">pear, <strong><a href=\"#idx-")
"range:"; has(index.html, ">1\u20132</a>")
"see:"; has(index.html, "beet, <em>see</em> fruit")
"display and quote:"; has(index.html, "<em>Zebra</em>, <a href=") and has(index.html, ">!bang, <a href=")
"index issues:"; index.issues
let named = run(doc("report", "\\usepackage{imakeidx}\\makeindex[name=people,title=People,intoc,columns=3]",
    "\\tableofcontents\\chapter{C}Ann\\index[people]{Ann}\\printindex[people]"))
"imakeidx:"; has(named.html, "<h1 class=\"latex-list-heading\" id=\"people\">People</h1><div class=\"latex-index-body\" style=\"column-count:3\">") and
    has(named.html, "<a href=\"#people\">People</a>")
