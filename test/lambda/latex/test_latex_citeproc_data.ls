// Canonical data and inert markup are independent of citation style (S1.8).
import c: lambda.latex.citeproc.common
import data: lambda.latex.citeproc.data
import markup: lambda.latex.citeproc.markup
import html: lambda.latex.to_html
let source = "@string{pub={Example} # { Press}}\n@proceedings{parent,title={Collected Papers},publisher=pub,year=2024}\n@inproceedings{child,author={de la Cruz, Jr, Jos{\\'e} and {Research and Development}},title={A {CSL} Study},crossref={parent},pages={101--109}}\n@phdthesis{thesis,author={Doe, Jane},title={A thesis},year=2020}\n@misc{child,title={Duplicate}}"
let loaded = data.bibtex(source, "references.bib")
let ref = loaded.references[1];
[ref.type, ref.title, ref['container-title'], ref.publisher, ref.issued['date-parts'], ref.page,
 ref.author[0].family, ref.author[0].given, ref.author[0]['non-dropping-particle'], ref.author[0].suffix,
 ref.author[1].literal, loaded.references[2].type, loaded.references[2].genre,
 [for (issue in loaded.diagnostics) issue.code]];
let decoded = markup.bibtex("G{\\\"o}del and \\emph{logic} with {CSL} and \\unknown{word}");
[decoded.text, [for(issue in decoded.diagnostics) issue.code],
 index_of(html.to_html(decoded.content), "font-style:italic") != null];
let json = data.json([{id:"rich",type:"book",title:"A <i>rich</i> <span class=\"nocase\" onclick=\"bad()\">CSL</span> title"}])^
let rich_html = html.to_html(json.references[0].rich.title);
[json.references[0].title, index_of(rich_html,"onclick") == null, index_of(rich_html,"nocase") != null,
 json.diagnostics == []];
[data.json([{id:"same",type:"book"},{id:"same",type:"book"}]) ^ { c.error_issue(^).code },
 data.json({id:"bad"}) ^ { c.error_issue(^).code },
 data.bibtex("@misc{a,crossref={b}}@misc{b,crossref={a}}").diagnostics[0].code]
