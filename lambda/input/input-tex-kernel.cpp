// TeX expansion engine: the Lambda-owned LaTeX2e programming-layer kernel.
//
// A clean-room profile of the documented package-author interface, written
// in TeX on top of the engine's native LaTeX layer (input-tex-latex.cpp).
// Document-level commands are declared as constructors: defined to the
// engine, passed through to the script adapters (Lambda_Pkg_Latex3 §9.3).
#ifndef LAMBDA_NO_LATEX

#include "input-tex-internal.hpp"

namespace tex {

static const char KERNEL[] = R"TEX(\catcode`\@=11
\let\bgroup={\let\egroup=}
\def\makeatletter{\catcode`\@11\relax}\def\makeatother{\catcode`\@12\relax}
\def\@empty{}\let\empty\@empty
\long\def\@gobble#1{}\long\def\@gobbletwo#1#2{}\long\def\@gobblethree#1#2#3{}
\long\def\@gobblefour#1#2#3#4{}
\long\def\@firstofone#1{#1}\long\def\@firstoftwo#1#2{#1}\long\def\@secondoftwo#1#2{#2}
\long\def\@iden#1{#1}\let\@@iden\@iden
\def\space{ }\def\@spaces{\space\space\space\space}
\def\^^M{\ }\def\^^I{\ }
{\def\:{\global\let\@sptoken= } \: }
\chardef\active=13 \chardef\@ne=1 \chardef\tw@=2 \chardef\thr@@=3 \chardef\sixt@@n=16
\chardef\@cclv=255 \mathchardef\@cclvi=256 \mathchardef\@m=1000 \mathchardef\@M=10000
\mathchardef\@MM=20000
\countdef\count@=255 \dimendef\dimen@=0 \dimendef\dimen@i=1 \dimendef\dimen@ii=2
\skipdef\skip@=0 \toksdef\toks@=0
\countdef\m@ne=22 \m@ne=-1
\count10=22 \count11=9 \count12=9 \count13=9 \count14=9 \count15=9 \count16=-1 \count17=-1
\count18=3 \count19=0 \count20=255
\countdef\insc@unt=20 \countdef\allocationnumber=21
\def\wlog{\immediate\write\m@ne}
\def\alloc@#1#2#3#4#5{\global\advance\count1#1\@ne\ch@ck#1#4#2%
  \allocationnumber\count1#1\global#3#5\allocationnumber
  \wlog{\string#5=\string#2\the\allocationnumber}}
\def\ch@ck#1#2#3{\ifnum\count1#1<#2\else\errmessage{No room for a new #3}\fi}
\def\newcount{\alloc@0\count\countdef\insc@unt}
\def\newdimen{\alloc@1\dimen\dimendef\insc@unt}
\def\newskip{\alloc@2\skip\skipdef\insc@unt}
\def\newmuskip{\alloc@3\muskip\muskipdef\@cclvi}
\def\newbox{\alloc@4\box\chardef\insc@unt}
\def\newtoks{\alloc@5\toks\toksdef\@cclvi}
\def\newread{\alloc@6\read\chardef\sixt@@n}
\def\newwrite{\alloc@7\write\chardef\sixt@@n}
\def\newfam{\alloc@8\fam\chardef\sixt@@n}
\def\newlanguage{\alloc@9\language\chardef\@cclvi}
\def\newinsert#1{\global\advance\insc@unt\m@ne\ch@ck0\insc@unt\count\ch@ck1\insc@unt\dimen
  \ch@ck2\insc@unt\skip\ch@ck4\insc@unt\box\allocationnumber\insc@unt
  \global\chardef#1\allocationnumber\wlog{\string#1=\string\insert\the\allocationnumber}}
\newdimen\z@ \newdimen\p@ \p@=1pt \newdimen\maxdimen \maxdimen=16383.99999pt
\newskip\z@skip \z@skip=0pt plus0pt minus0pt
\newskip\hideskip \hideskip=-1000pt plus1fill
\newdimen\@tempdima \newdimen\@tempdimb \newdimen\@tempdimc
\newcount\@tempcnta \newcount\@tempcntb
\newskip\@tempskipa \newskip\@tempskipb
\newtoks\@temptokena \newbox\@tempboxa \newbox\voidb@x
\def\@settodim#1#2#3{\setbox\@tempboxa\hbox{{#3}}#2#1\@tempboxa\setbox\@tempboxa\box\voidb@x}
\newif\if@tempswa \newif\if@filesw \newif\if@twocolumn \newif\if@twoside
\newif\if@noskipsec \newif\if@nobreak \newif\if@inlabel \newif\if@newlist
\newif\if@noparitem \newif\if@minipage \newif\if@insert \newif\if@afterindent
\newif\if@nmbrlist \newif\if@restonecol \newif\if@titlepage \newif\if@openright
\newif\if@mainmatter \@mainmattertrue
\def\@plus{plus}\def\@minus{minus}\def\@height{height}\def\@depth{depth}\def\@width{width}
\def\hb@xt@{\hbox to}
\def\@namedef#1{\expandafter\def\csname#1\endcsname}
\def\@nameuse#1{\csname#1\endcsname}
\def\@ifundefined#1{\ifcsname#1\endcsname
  \expandafter\ifx\csname#1\endcsname\relax
    \expandafter\expandafter\expandafter\@firstoftwo
  \else
    \expandafter\expandafter\expandafter\@secondoftwo
  \fi
\else\expandafter\@firstoftwo\fi}
% #2 runs after the conditionals close: it may turn #1 into an \if (\newif),
% which skipped text must not contain
\long\def\@ifdefinable#1#2{\ifdefined#1\ifx#1\relax\else\@notdefinable#1\fi\fi
  \ifdefined#1\ifx#1\relax\expandafter\expandafter\expandafter\@firstofone
  \else\expandafter\expandafter\expandafter\@gobble\fi\else\expandafter\@firstofone\fi{#2}}
\def\@notdefinable#1{\@latex@error{Command \string#1 already defined}\@ehc}
\let\@@ifdefinable\@ifdefinable
\long\def\@testopt#1#2{\kernel@ifnextchar[{#1}{#1[{#2}]}}
\def\@protected@testopt#1{\ifx\protect\@typeset@protect\expandafter\@testopt\else\@x@protect#1\fi}
\def\@x@protect#1\fi#2#3{\fi\protect#1}
\def\x@protect#1{\ifx\protect\@typeset@protect\else\@x@protect#1\fi}
\let\l@ngrel@x\relax
\def\@star@or@long#1{\@ifstar{\let\l@ngrel@x\relax#1}{\let\l@ngrel@x\long#1}}
\def\new@command#1{\@testopt{\@newcommand#1}0}
\def\@newcommand#1[#2]{\kernel@ifnextchar[{\@xargdef#1[#2]}{\@argdef#1[#2]}}
\long\def\@argdef#1[#2]#3{\@ifdefinable#1{\@yargdef#1\@ne{#2}{#3}}}
\long\def\@xargdef#1[#2][#3]#4{\@ifdefinable#1{\expandafter\def\expandafter#1\expandafter
  {\expandafter\@protected@testopt\expandafter#1\csname\string#1\endcsname{#3}}%
  \expandafter\@yargdef\csname\string#1\endcsname\tw@{#2}{#4}}}
\def\@reargdef#1[#2]{\@yargdef#1\@ne{#2}}
\def\renew@command#1{\@ifundefined{\expandafter\@gobble\string#1}%
  {\@latex@error{Command \string#1 undefined}\@ehc}\relax
  \let\@ifdefinable\@rc@ifdefinable\new@command#1}
\long\def\@rc@ifdefinable#1#2{\let\@ifdefinable\@@ifdefinable#2}
\let\@ehc\@empty \let\@eha\@empty \let\@ehb\@empty \let\@ehd\@empty
\newif\ifin@
\def\in@#1#2{\def\in@@##1#1##2##3\in@@{\ifx\in@##2\in@false\else\in@true\fi}%
  \in@@#2#1\in@\in@@}
\def\@expandtwoargs#1#2#3{\edef\reserved@a{\noexpand#1{#2}{#3}}\reserved@a}
\def\strip@prefix#1>{}
\def\@onelevel@sanitize#1{\edef#1{\expandafter\strip@prefix\meaning#1}}
{\catcode`\|=0 \catcode`\\=12 |gdef|@backslashchar{\}}
\def\@clsextension{cls}\def\@pkgextension{sty}
\let\@filelist\@empty \let\@classoptionslist\@empty \let\@unusedoptionlist\@empty
\def\@currname{}\def\@currext{}
\def\@ifl@aded#1#2{\expandafter\ifx\csname ver@#2.#1\endcsname\relax
  \expandafter\@secondoftwo\else\expandafter\@firstoftwo\fi}
\def\@ifpackageloaded{\@ifl@aded\@pkgextension}\def\@ifclassloaded{\@ifl@aded\@clsextension}
\let\IfPackageLoadedTF\@ifpackageloaded \let\IfClassLoadedTF\@ifclassloaded
\let\@endpreamblehook\@empty \let\@afterendpreamblehook\@empty
\let\@afterenddocumenthook\@empty \let\@unprocessedoptions\relax
\def\@nocounterr#1{\@latex@error{No counter `#1' defined}\@eha}
\long\def\@dblarg#1{\kernel@ifnextchar[{#1}{\@xdblarg{#1}}}
\long\def\@xdblarg#1#2{#1[{#2}]{#2}}
\long\def\g@addto@macro#1#2{\begingroup\toks@\expandafter{#1#2}\xdef#1{\the\toks@}\endgroup}
\long\def\l@addto@macro#1#2{\toks@\expandafter{#1#2}\edef#1{\the\toks@}}
\def\@onlypreamble#1{}
\def\@makeother#1{\catcode`#1=12\relax}
\def\typeout#1{\immediate\write16{#1}}
\def\on@line{ on input line \the\inputlineno}
% messages end with \on@line, which the NoLine forms remove with \@gobble
\def\GenericWarning#1#2{\immediate\write16{#2\on@line.}}
\def\GenericInfo#1#2{\wlog{#2\on@line.}}
\def\GenericError#1#2#3#4{\errmessage{#2}}
\def\PackageWarning#1#2{\GenericWarning{(#1)}{Package #1 Warning: #2}}
\def\PackageWarningNoLine#1#2{\PackageWarning{#1}{#2\@gobble}}
\def\PackageInfo#1#2{\GenericInfo{(#1)}{Package #1 Info: #2}}
\def\PackageError#1#2#3{\errmessage{Package #1 Error: #2}}
\def\ClassWarning#1#2{\GenericWarning{(#1)}{Class #1 Warning: #2}}
\def\ClassWarningNoLine#1#2{\ClassWarning{#1}{#2\@gobble}}
\def\ClassInfo#1#2{\GenericInfo{(#1)}{Class #1 Info: #2}}
\def\ClassError#1#2#3{\errmessage{Class #1 Error: #2}}
\def\@latex@warning#1{\GenericWarning{}{LaTeX Warning: #1}}
\def\@latex@error#1#2{\errmessage{LaTeX Error: #1}}
\let\@latex@info\@gobble
\let\MessageBreak\space
\let\protect\relax \let\@typeset@protect\relax
\def\@unexpandable@protect{\noexpand\protect\noexpand}
\def\protected@edef{\let\@@protect\protect\let\protect\@unexpandable@protect
  \afterassignment\restore@protect\edef}
\def\protected@xdef{\let\@@protect\protect\let\protect\@unexpandable@protect
  \afterassignment\restore@protect\xdef}
\def\restore@protect{\let\protect\@@protect}
\def\@car#1#2\@nil{#1}\def\@cdr#1#2\@nil{#2}\def\@carcube#1#2#3#4\@nil{#1#2#3}
\def\@nnil{\@nil}
\def\strip@pt{\expandafter\lmd@strippt\the}
{\catcode`p=12 \catcode`t=12 \gdef\lmd@strippt#1.#2pt{#1\ifnum#2>0 .#2\fi}}
\def\@arabic#1{\number#1}\def\@roman#1{\romannumeral#1}\def\@Roman#1{\lambda@Roman#1}
\def\@alph#1{\ifcase#1\or a\or b\or c\or d\or e\or f\or g\or h\or i\or j\or k\or l\or m\or
  n\or o\or p\or q\or r\or s\or t\or u\or v\or w\or x\or y\or z\fi}
\def\@Alph#1{\ifcase#1\or A\or B\or C\or D\or E\or F\or G\or H\or I\or J\or K\or L\or M\or
  N\or O\or P\or Q\or R\or S\or T\or U\or V\or W\or X\or Y\or Z\fi}
\def\@fnsymbol#1{\ifcase#1\or*\or\dag\or\ddag\or\S\or\P\or\textbardbl\or**\or\dag\dag
  \or\ddag\ddag\fi}
\def\loop#1\repeat{\def\body{#1}\iterate}
\def\iterate{\body\let\next\iterate\else\let\next\relax\fi\next}
\let\repeat=\fi
% loop bodies run after the test's \fi, so they may contain any conditionals
\long\def\@whilenum#1\do#2{\ifnum#1\relax\expandafter\@firstofone\else
  \expandafter\@gobble\fi{#2\@whilenum#1\do{#2}}}
\long\def\@whiledim#1\do#2{\ifdim#1\relax\expandafter\@firstofone\else
  \expandafter\@gobble\fi{#2\@whiledim#1\do{#2}}}
\long\def\@whilesw#1\fi#2{#1\expandafter\@firstofone\else\expandafter\@gobble\fi
  {#2\@whilesw#1\fi{#2}}}
\def\@addtofilelist#1{\xdef\@filelist{\@filelist,#1}}
\long\def\@ifl@t@r#1#2#3#4{#3}
\def\fmtname{LaTeX2e}\def\fmtversion{2024-11-01}
\def\BooleanTrue{true}\def\BooleanFalse{false}
\def\baselinestretch{1}
\newdimen\textwidth \textwidth=345pt \newdimen\textheight \textheight=550pt
\newdimen\linewidth \linewidth=345pt \newdimen\columnwidth \columnwidth=345pt
\newdimen\columnsep \columnsep=10pt \newdimen\columnseprule
\newdimen\marginparwidth \marginparwidth=65pt \newdimen\marginparsep \marginparsep=11pt
\newdimen\marginparpush \marginparpush=5pt
\newdimen\oddsidemargin \oddsidemargin=62pt \newdimen\evensidemargin \evensidemargin=62pt
\newdimen\topmargin \topmargin=27pt \newdimen\headheight \headheight=12pt
\newdimen\headsep \headsep=25pt \newdimen\footskip \footskip=30pt
\newdimen\paperwidth \paperwidth=614.295pt \newdimen\paperheight \paperheight=794.96999pt
\newdimen\fboxsep \fboxsep=3pt \newdimen\fboxrule \fboxrule=.4pt
\newdimen\arraycolsep \arraycolsep=5pt \newdimen\tabcolsep \tabcolsep=6pt
\newdimen\arrayrulewidth \arrayrulewidth=.4pt \newdimen\doublerulesep \doublerulesep=2pt
\newdimen\labelwidth \labelwidth=20pt \newdimen\labelsep \labelsep=5pt
\newdimen\itemindent \newdimen\listparindent \newdimen\leftmargin \leftmargin=25pt
\newdimen\rightmargin \newdimen\unitlength \unitlength=1pt \newdimen\jot \jot=3pt
\newskip\abovecaptionskip \abovecaptionskip=10pt \newskip\belowcaptionskip
\newskip\smallskipamount \smallskipamount=3pt plus1pt minus1pt
\newskip\medskipamount \medskipamount=6pt plus2pt minus2pt
\newskip\bigskipamount \bigskipamount=12pt plus4pt minus4pt
\newskip\topsep \topsep=8pt plus2pt minus4pt \newskip\itemsep \itemsep=4pt plus2pt minus1pt
\newskip\parsep \parsep=4pt plus2pt minus1pt \newskip\partopsep \partopsep=2pt plus1pt minus1pt
\newskip\floatsep \floatsep=12pt plus2pt minus2pt
\newskip\textfloatsep \textfloatsep=20pt plus2pt minus4pt
\newskip\intextsep \intextsep=12pt plus2pt minus2pt
\parindent=15pt \parskip=0pt plus1pt \baselineskip=12pt \lineskip=1pt \hsize=345pt \vsize=550pt
\font\tenrm=cmr10 \tenrm
\lambdaconstructor\part \lambdaconstructor\section \lambdaconstructor\subsection
\lambdaconstructor\subsubsection \lambdaconstructor\paragraph \lambdaconstructor\subparagraph
\lambdaconstructor\textbf \lambdaconstructor\textit \lambdaconstructor\textsl
\lambdaconstructor\textsc \lambdaconstructor\texttt \lambdaconstructor\textrm
\lambdaconstructor\textsf \lambdaconstructor\textup \lambdaconstructor\textmd
\lambdaconstructor\textnormal \lambdaconstructor\emph \lambdaconstructor\bfseries
\lambdaconstructor\itshape \lambdaconstructor\slshape \lambdaconstructor\scshape
\lambdaconstructor\ttfamily \lambdaconstructor\rmfamily \lambdaconstructor\sffamily
\lambdaconstructor\upshape \lambdaconstructor\mdseries \lambdaconstructor\normalfont
\lambdaconstructor\bf \lambdaconstructor\it \lambdaconstructor\sl \lambdaconstructor\sc
\lambdaconstructor\tt \lambdaconstructor\rm \lambdaconstructor\sf \lambdaconstructor\em
\lambdaconstructor\tiny \lambdaconstructor\scriptsize \lambdaconstructor\footnotesize
\lambdaconstructor\small \lambdaconstructor\normalsize \lambdaconstructor\large
\lambdaconstructor\Large \lambdaconstructor\LARGE \lambdaconstructor\huge
\lambdaconstructor\Huge \lambdaconstructor\footnote \lambdaconstructor\footnotemark
\lambdaconstructor\footnotetext \lambdaconstructor\label \lambdaconstructor\ref
\lambdaconstructor\pageref \lambdaconstructor\cite \lambdaconstructor\nocite
\lambdaconstructor\bibliography \lambdaconstructor\bibliographystyle
\lambdaconstructor\bibitem \lambdaconstructor\item \lambdaconstructor\maketitle
\lambdaconstructor\title \lambdaconstructor\author \lambdaconstructor\date
\lambdaconstructor\thanks \lambdaconstructor\and \lambdaconstructor\today
\lambdaconstructor\tableofcontents \lambdaconstructor\listoffigures
\lambdaconstructor\listoftables \lambdaconstructor\caption \lambdaconstructor\centering
\lambdaconstructor\raggedright \lambdaconstructor\raggedleft \lambdaconstructor\newline
\lambdaconstructor\linebreak \lambdaconstructor\nolinebreak \lambdaconstructor\pagebreak
\lambdaconstructor\nopagebreak \lambdaconstructor\newpage \lambdaconstructor\clearpage
\lambdaconstructor\cleardoublepage \lambdaconstructor\hspace \lambdaconstructor\vspace
\lambdaconstructor\smallskip \lambdaconstructor\medskip \lambdaconstructor\bigskip
\lambdaconstructor\mbox \lambdaconstructor\makebox \lambdaconstructor\fbox
\lambdaconstructor\framebox \lambdaconstructor\parbox \lambdaconstructor\raisebox
\lambdaconstructor\rule \lambdaconstructor\LaTeX \lambdaconstructor\TeX
\lambdaconstructor\LaTeXe \lambdaconstructor\ldots \lambdaconstructor\dots
\lambdaconstructor\textbackslash \lambdaconstructor\textasciitilde
\lambdaconstructor\textasciicircum \lambdaconstructor\textbar \lambdaconstructor\textbardbl
\lambdaconstructor\S \lambdaconstructor\P \lambdaconstructor\copyright
\lambdaconstructor\pounds \lambdaconstructor\dag \lambdaconstructor\ddag
\lambdaconstructor\slash \lambdaconstructor\i \lambdaconstructor\j \lambdaconstructor\ae
\lambdaconstructor\oe \lambdaconstructor\ss \lambdaconstructor\o \lambdaconstructor\l
\lambdaconstructor\aa \lambdaconstructor\AE \lambdaconstructor\OE \lambdaconstructor\O
\lambdaconstructor\L \lambdaconstructor\AA \lambdaconstructor\' \lambdaconstructor\`
\lambdaconstructor\^ \lambdaconstructor\" \lambdaconstructor\~ \lambdaconstructor\=
\lambdaconstructor\. \lambdaconstructor\u \lambdaconstructor\v \lambdaconstructor\H
\lambdaconstructor\c \lambdaconstructor\d \lambdaconstructor\b \lambdaconstructor\t
\lambdaconstructor\k \lambdaconstructor\\ \lambdaconstructor\, \lambdaconstructor\;
\lambdaconstructor\: \lambdaconstructor\! \lambdaconstructor\{ \lambdaconstructor\}
\lambdaconstructor\$ \lambdaconstructor\& \lambdaconstructor\# \lambdaconstructor\_
\lambdaconstructor\% \lambdaconstructor\@ \lambdaconstructor\frac \lambdaconstructor\sqrt
\lambdaconstructor\ensuremath \lambdaconstructor\underbrace \lambdaconstructor\overbrace
\lambdaconstructor\mathrm \lambdaconstructor\mathbf \lambdaconstructor\mathit
\lambdaconstructor\mathsf \lambdaconstructor\mathtt \lambdaconstructor\mathcal
\lambdaconstructor\mathnormal \lambdaconstructor\boldmath \lambdaconstructor\unboldmath
\lambdaconstructor\stackrel \lambdaconstructor\pmod \lambdaconstructor\bmod
\lambdaconstructor\cdots \lambdaconstructor\vdots \lambdaconstructor\ddots
\lambdaconstructor\sum \lambdaconstructor\prod \lambdaconstructor\int \lambdaconstructor\lim
\lambdaconstructor\infty \lambdaconstructor\cdot \lambdaconstructor\times
\lambdaconstructor\alpha \lambdaconstructor\beta \lambdaconstructor\gamma
\lambdaconstructor\delta \lambdaconstructor\epsilon \lambdaconstructor\pi
\lambdaconstructor\sigma \lambdaconstructor\theta \lambdaconstructor\lambda
\lambdaconstructor\mu \lambdaconstructor\omega \lambdaconstructor\phi
\lambdaconstructor\url \lambdaconstructor\href \lambdaconstructor\appendix
\lambdaconstructor\abstractname \lambdaconstructor\contentsname
\lambdaconstructor\refname \lambdaconstructor\figurename \lambdaconstructor\tablename
\lambdaconstructor\thepage \lambdaconstructor\thesection \lambdaconstructor\theequation
\lambdaconstructor\thefigure \lambdaconstructor\thetable \lambdaconstructor\marginpar
\lambdaconstructor\index \lambdaconstructor\glossary \lambdaconstructor\addcontentsline
\lambdaconstructor\addtocontents \lambdaconstructor\pagestyle
\lambdaconstructor\thispagestyle \lambdaconstructor\pagenumbering
\lambdaconstructor\twocolumn \lambdaconstructor\onecolumn \lambdaconstructor\samepage
\lambdaconstructor\enlargethispage \lambdaconstructor\newtheorem
\lambdaconstructor\usebox \lambdaconstructor\savebox \lambdaconstructor\sbox
\lambdaconstructor\newsavebox \lambdaconstructor\textcircled \lambdaconstructor\textregistered
\lambdaconstructor\texttrademark \lambdaconstructor\textemdash \lambdaconstructor\textendash
\lambdaconstructor\textquoteleft \lambdaconstructor\textquoteright
\lambdaconstructor\textquotedblleft \lambdaconstructor\textquotedblright
\lambdaconstructor\textbullet \lambdaconstructor\textperiodcentered
\lambdaconstructor\textunderscore \lambdaconstructor\textless \lambdaconstructor\textgreater
\lambdaconstructor\textdagger \lambdaconstructor\textdaggerdbl \lambdaconstructor\textsection
\lambdaconstructor\textparagraph \lambdaconstructor\textdollar \lambdaconstructor\textsterling
\lambdaconstructor\nobreakspace
% LaTeX's file hooks (ltexpl), filled in when expl3 is preloaded; the engine keeps
% the file stack itself, so \@pushfilename and \@popfilename only call them
\def\@expl@sys@load@backend@@{}\def\@expl@push@filename@@{}
\def\@expl@push@filename@aux@@{}\def\@expl@pop@filename@@{}
\def\@expl@finalise@setup@@{}
\def\@pushfilename{\@expl@push@filename@@\@expl@push@filename@aux@@}
\def\@popfilename{\@expl@pop@filename@@}
% LaTeX preloads expl3; here its first use restarts the run from the expl3 format
\protected\def\ExplSyntaxOn{\RequirePackage{expl3}\ExplSyntaxOn}
\protected\def\ProvidesExplPackage{\RequirePackage{expl3}\ProvidesExplPackage}
\protected\def\ProvidesExplClass{\RequirePackage{expl3}\ProvidesExplClass}
\protected\def\ProvidesExplFile{\RequirePackage{expl3}\ProvidesExplFile}
\makeatother
)TEX";

void run_preload(Engine* e, const char* name, const char* source, size_t length) {
    int32_t file = add_source(e, name, source, (uint32_t)length);
    // a preload is Lambda's own: what it inputs comes from the bundled resources
    e->files[(size_t)file].bundled = true;
    push_file(e, file, false);
    main_control(e);
    // a preload only defines; anything it typesets is not document output
    e->out.clear();
    e->stop_requested = false;
}

void load_kernel(Engine* e) {
    run_preload(e, "<lambda-kernel>", KERNEL, sizeof(KERNEL) - 1);
}

} // namespace tex

#endif // LAMBDA_NO_LATEX
