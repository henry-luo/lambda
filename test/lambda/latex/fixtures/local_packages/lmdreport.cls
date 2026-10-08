\NeedsTeXFormat{LaTeX2e}
\ProvidesClass{lmdreport}[2026/10/07 Phase IV fixture class]
\DeclareOption*{\PassOptionsToClass{\CurrentOption}{article}}
\ProcessOptions\relax
\LoadClass{article}
\newcommand{\coursecode}[1]{\def\@coursecode{#1}}
\newcommand{\showcourse}{Course \@coursecode}
