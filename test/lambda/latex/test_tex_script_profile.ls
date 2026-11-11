import profile: lambda.doc.latex.tex_engine.profile
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn report(source, kernel_source = kernel) any^ {
    let result = profile.expand_with_kernel(source, kernel_source, {source_name: "main.tex"}, 100000)^;
    {text: join([for (t in result.output) t.value], ""), issue: result.issue,
        unsupported: result.unsupported, diagnostics: result.diagnostics,
        sources: [for (entry in result.sources) [entry.id, entry.name, entry.origin]]}
}
"kernel constructors:"; report("\\textbf{Hello} \\newif\\iffoo \\footrue\\iffoo Yes\\else No\\fi")
"kernel makeatletter:"; report("\\makeatletter\\@firstoftwo{Y}{N}\\makeatother\\number\\catcode`\\@")
"kernel document macro:"; report("\\def\\hi#1{Hello #1}\\hi{world}")
"latex command unsupported:"; report("\\usepackage{geometry}")
"failed preload:"; report("ignored", "\\setbox0=\\hbox{}")
let direct = profile.expand("\\textbf{Hi}", {source_name: "direct.tex"}, 100000)^;
"packaged kernel:"; {text: join([for (t in direct.output) t.value], ""), issue: direct.issue,
    sources: [for (entry in direct.sources) [entry.id, entry.name, entry.origin]]}
"packaged newcommand:"; report("\\newcommand{\\pair}[2]{#1/#2}\\pair{left}{right}")
"packaged environment:"; report("\\newenvironment{boxy}[1]{<#1>}{!}\\begin{boxy}{A}X\\end{boxy}")
"article document:"; report("\\documentclass[12pt]{article}\\begin{document}Hi \\textbf{there}\\end{document}")
"chaptered class:"; report("\\documentclass{book}\\chapter{Intro}")
let details = profile.expand_with_kernel("\\documentclass[12pt]{book}\\makeatletter\\@ifclassloaded{book}{Y}{N}\\makeatother", kernel, {source_name: "main.tex"}, 100000)^;
"class metadata:"; {packages: details.packages, class_options: details.class_options,
    text: join([for (t in details.output) t.value], ""), issue: details.issue}
let unknown = profile.expand_with_kernel("\\documentclass{mystery}", kernel, {resources: {local: {}}}, 100000)^;
"unknown captured class:"; {packages: unknown.packages, issue: unknown.issue}
let uncertain = profile.expand_with_kernel("\\documentclass{mystery}", kernel, {}, 100000)^;
"uncaptured unknown class:"; [uncertain.issue, uncertain.unsupported]
let local = profile.expand_with_kernel("\\documentclass{mystery}", kernel, {resources: {local: {'mystery.cls': {data: ""}}}}, 100000)^;
"empty local class:"; [local.issue, local.unsupported, local.packages]
"packaged document hooks:"; report("\\AtBeginDocument{A}\\AtEndDocument{B}\\begin{document}X\\end{document}")

fn report_resources(source, resources) any^ {
    let r = profile.expand_with_kernel(source, kernel, {source_name: "main.tex", resources: resources}, 100000)^;
    {text: join([for (t in r.output) t.value], ""), issue: r.issue, unsupported: r.unsupported,
        diagnostics: r.diagnostics, packages: r.packages,
        sources: [for (entry in r.sources) [entry.name, entry.origin]]}
}
"local package:"; report_resources("\\documentclass{article}\\usepackage{sample}\\begin{document}\\sample/\\hooked\\end{document}",
    {local: {'sample.sty': {data: "\\NeedsTeXFormat{LaTeX2e}\\ProvidesPackage{sample}[2026/11/11]\\newcommand{\\sample}{P}\\AtEndOfPackage{\\def\\hooked{E}}\\endinput\nignored"}}, bundled: {}})
"nested packages:"; report_resources("\\usepackage{outer}\\outercmd/\\innercmd", {local: {
    'outer.sty': {data: "\\ProvidesPackage{outer}\\RequirePackage{inner}\\newcommand\\outercmd{O}"},
    'inner.sty': {data: "\\ProvidesPackage{inner}\\newcommand\\innercmd{I}"}}, bundled: {}})
"package at code scope:"; report_resources("\\usepackage{sample}\\useit/\\number\\catcode`\\@", {local: {
    'sample.sty': {data: "\\def\\@private{V}\\newcommand\\useit{\\@private}"}}, bundled: {}})
"adapter and local packages:"; report_resources("\\usepackage{geometry,custom,mystery}\\custom", {local: {
    'custom.sty': {data: "\\newcommand\\custom{C}"}}, bundled: {}})
"bundled package precedence:"; report_resources("\\usepackage{chosen}\\which", {local: {
    'chosen.sty': {data: "\\newcommand\\which{L}"}}, bundled: {'chosen.sty': {data: "\\newcommand\\which{B}"}}})
"duplicate package:"; report_resources("\\usepackage{sample}\\usepackage{sample}\\sample", {local: {
    'sample.sty': {data: "\\newcommand\\sample{A}"}}, bundled: {}})
"package hooks and metadata:"; report_resources("\\usepackage{outer}\\makeatletter\\@ifpackageloaded{inner}{Y}{N}\\makeatother/\\result\\begin{document}D\\end{document}", {local: {
    'outer.sty': {data: "\\ProvidesPackage{outer}[2026/11/11]\\AtEndOfPackage{\\def\\result{E}}\\RequirePackage{inner}\\AtBeginDocument{B}\\AtEndDocument{Z}"},
    'inner.sty': {data: "\\ProvidesPackage{inner}\\AtEndOfPackage{\\def\\innerend{I}}"}}, bundled: {}})
"package macro spaces:"; report_resources("\\usepackage{sample}R \\spaced S", {local: {
    'sample.sty': {data: "\\def\\spaced{A B} \\spaced"}}, bundled: {}})
"uncaptured packages:"; report_resources("\\usepackage{missing}", {local: {}})
let options_style = "\\def\\result{}\\DeclareOption{b}{\\edef\\result{\\result B}}\\DeclareOption{a}{\\edef\\result{\\result A}}\\DeclareOption*{\\edef\\result{\\result [\\CurrentOption]}}\\ExecuteOptions{a}\\ProcessOptions\\relax";
"package options:"; report_resources("\\documentclass[a]{article}\\PassOptionsToPackage{z}{opts}\\usepackage[b,q]{opts}\\result", {local: {'opts.sty': {data: options_style}}, bundled: {}})
"starred package options:"; report_resources("\\documentclass[a]{article}\\usepackage[b,q]{opts}\\result", {local: {'opts.sty': {data: "\\def\\result{}\\DeclareOption{b}{\\edef\\result{\\result B}}\\DeclareOption{a}{\\edef\\result{\\result A}}\\DeclareOption*{\\edef\\result{\\result [\\CurrentOption]}}\\ProcessOptions*\\relax"}}, bundled: {}})
"unknown package option:"; report_resources("\\usepackage[z]{opts}X", {local: {'opts.sty': {data: "\\ProcessOptions\\relax"}}, bundled: {}})

"local class with options:"; report_resources("\\documentclass[wide]{custom}\\begin{document}\\myclass/\\ending\\end{document}", {local: {
    'custom.cls': {data: "\\ProvidesClass{custom}\\DeclareOption{wide}{\\def\\myclass{W}}\\ProcessOptions\\relax\\LoadClassWithOptions{book}\\AtEndOfClass{\\def\\ending{E}}"}}, bundled: {}})
"package option queries:"; report_resources("\\documentclass[a,b]{article}\\usepackage[x,y]{opts}\\makeatletter\\edef\\choice{\\@ifclasswith{article}{b,a}{T}{F}\\@ifpackagewith{opts}{x,y}{Y}{N}\\@ifpackagewith{opts}{z}{F}{T}\\@ifpackagewith{none}{}{F}{T}}\\makeatother\\choice", {local: {'opts.sty': {data: ""}}, bundled: {}})
"captured file probes:"; report_resources("\\IfFileExists{a}{Y}{N}\\IfFileExists{missing}{N}{Y}\\InputIfFileExists{a}{B}{N}Z", {local: {'a.tex': {data: "I"}}, bundled: {}})
"uncaptured file probes:"; report_resources("\\IfFileExists{missing}{Y}{N}", {bundled: {}})
fn report_echo(source) any^ {
    let r = profile.expand_with_kernel(source, kernel, {echo_definitions: true}, 100000)^;
    let text = join([for (token in r.output) token.value], "");
    let native = parse(source, {type: "tex"})^;
    {text: text, matches_native: text == native.text, incomplete: r.echo_incomplete, issue: r.issue,
        spans: [for (token in r.output where token.kind == "source-span") [token.start, token.end]]}
}
"definition source echo:"; report_echo("\\def\\x{A}\\x")
"newcommand source echo:"; report_echo("\\newcommand{\\hi}[1]{Hello #1}\\hi{Bob}")
"foreign expansion source echo:"; report_echo("\\def\\x{A}\\edef\\y{\\x}\\y")
"prefixed source echo:"; report_echo("\\global\\def\\x{A}\\x")
"prefixed assignment is not echoed:"; report_echo("\\global\\count0=3 \\the\\count0")
fn report_nextchar(source) any^ {
    let r = profile.expand_with_kernel(source, kernel, {}, 100000)^;
    let text = join([for (token in r.output) token.value], "");
    {text: text, issue: r.issue, matches_native: text == (parse(source, {type: "tex"})^.text)}
}
"next character and star:"; report_nextchar("\\makeatletter\\@ifnextchar x{Y}{N}x/\\@ifnextchar x{Y}{N}z/\\@ifstar{S}{N}*/\\@ifstar{S}{N}q\\makeatother")
"next character meanings:"; report_nextchar("\\makeatletter\\let\\a=x\\let\\b=x\\@ifnextchar\\a{Y}{N}\\b\\makeatother")
"star branch parameter marks:"; report_nextchar("\\makeatletter\\@ifstar{##}{N}*\\makeatother")
"xspace punctuation:"; report_nextchar("\\makeatletter X\\lambda@xspace word/X\\lambda@xspace,/X\\lambda@xspace{Y}\\makeatother")
"expandable xspace:"; report_echo("\\makeatletter\\edef\\x{X\\lambda@xspace word}\\x\\makeatother")
