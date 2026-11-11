// The TeX primitive names and expansion classes supported by the native engine.
// Unimplemented operations are rejected by the experimental script engine.
let PRIMITIVES = [
    "expandafter", "noexpand", "csname", "string", "number", "romannumeral",
    "the", "unexpanded", "detokenize", "meaning", "fontname", "jobname",
    "input", "endinput", "scantokens", "expanded", "eTeXrevision", "pdfstrcmp",
    "topmark", "firstmark", "botmark", "splitfirstmark", "splitbotmark", "topmarks",
    "firstmarks", "botmarks", "splitfirstmarks", "splitbotmarks", "pdfuniformdeviate", "pdfnormaldeviate",
    "pdftexrevision", "pdfescapestring", "pdfescapename", "pdfescapehex", "pdfunescapehex", "pdfmdfivesum",
    "pdffilesize", "pdffilemoddate", "pdfcreationdate", "if", "ifcat", "ifnum",
    "ifdim", "ifodd", "ifvmode", "ifhmode", "ifmmode", "ifinner",
    "ifvoid", "ifhbox", "ifvbox", "ifx", "ifeof", "iftrue",
    "iffalse", "ifcase", "ifdefined", "ifcsname", "iffontchar", "ifpdfprimitive",
    "ifincsname", "unless", "fi", "else", "or", "relax",
    "endcsname", "def", "gdef", "edef", "xdef", "let",
    "futurelet", "global", "long", "outer", "protected", "chardef",
    "mathchardef", "countdef", "dimendef", "skipdef", "muskipdef", "toksdef",
    "count", "dimen", "skip", "muskip", "toks", "advance",
    "multiply", "divide", "catcode", "lccode", "uccode", "sfcode",
    "mathcode", "delcode", "numexpr", "dimexpr", "glueexpr", "muexpr",
    "gluestretch", "glueshrink", "gluestretchorder", "glueshrinkorder", "gluetomu", "mutoglue",
    "fontcharwd", "fontcharht", "fontchardp", "fontcharic", "begingroup", "endgroup",
    "aftergroup", "afterassignment", "ignorespaces", "uppercase", "lowercase", "message",
    "errmessage", "show", "showthe", "showbox", "showlists", "showtokens",
    "showgroups", "showifs", "immediate", "write", "openout", "closeout",
    "openin", "closein", "read", "readline", "special", "errorstopmode",
    "scrollmode", "nonstopmode", "batchmode", "interactionmode", "end", "dump",
    "font", "nullfont", "fontdimen", "hyphenchar", "skewchar", "textfont",
    "scriptfont", "scriptscriptfont", "patterns", "hyphenation", "parshape", "interlinepenalties",
    "clubpenalties", "widowpenalties", "displaywidowpenalties", "letterspacefont", "setbox", "wd",
    "ht", "dp", "pagegoal", "pagetotal", "pagestretch", "pagefilstretch",
    "pagefillstretch", "pagefilllstretch", "pageshrink", "pagedepth", "pdfliteral", "pdfinfo",
    "pdfcatalog", "pdfnames", "pdftrailer", "pdfmapfile", "pdfmapline", "pdfglyphtounicode",
    "pdfobj", "pdfannot", "pdfstartlink", "pdfendlink", "pdfsavepos", "pdfresettimer",
    "pdfsetrandomseed", "lastpenalty", "lastkern", "badness", "inputlineno", "eTeXversion",
    "currentgrouplevel", "currentgrouptype", "currentiflevel", "currentiftype", "currentifbranch", "lastnodetype",
    "pdftexversion", "pdfelapsedtime", "pdfrandomseed", "spacefactor", "prevgraf", "deadcycles",
    "insertpenalties", "prevdepth", "lastskip", "parshapelength", "parshapeindent", "parshapedimen",
    "pdflastxpos", "pdflastypos", "pdfshellescape", "par", "indent", "noindent",
    "hskip", "vskip", "hfil", "hfill", "hss", "hfilneg",
    "vfil", "vfill", "vss", "vfilneg", "kern", "mkern",
    "mskip", "penalty", "unpenalty", "unkern", "unskip", "hbox",
    "vbox", "vtop", "box", "copy", "lastbox", "vsplit",
    "unhbox", "unhcopy", "unvbox", "unvcopy", "raise", "lower",
    "moveleft", "moveright", "hrule", "vrule", "leaders", "cleaders",
    "xleaders", "mark", "marks", "insert", "vadjust", "halign",
    "valign", "noalign", "omit", "span", "cr", "crcr",
    "char", "accent", "discretionary", "-", "/", " ",
    "shipout", "noboundary", "pagediscards", "splitdiscards", "vcenter", "mathord",
    "mathop", "mathbin", "mathrel", "mathopen", "mathclose", "mathpunct",
    "mathinner", "underline", "overline", "displaystyle", "textstyle", "scriptstyle",
    "scriptscriptstyle", "limits", "nolimits", "displaylimits", "nonscript", "eqno",
    "leqno", "mathchoice", "left", "right", "middle", "over",
    "atop", "above", "overwithdelims", "atopwithdelims", "abovewithdelims", "mathchar",
    "delimiter", "radical", "mathaccent", "pretolerance", "tolerance", "linepenalty",
    "hyphenpenalty", "exhyphenpenalty", "clubpenalty", "widowpenalty", "displaywidowpenalty", "brokenpenalty",
    "binoppenalty", "relpenalty", "predisplaypenalty", "postdisplaypenalty", "interlinepenalty", "doublehyphendemerits",
    "finalhyphendemerits", "adjdemerits", "mag", "delimiterfactor", "looseness", "time",
    "day", "month", "year", "showboxbreadth", "showboxdepth", "hbadness",
    "vbadness", "pausing", "tracingonline", "tracingmacros", "tracingstats", "tracingparagraphs",
    "tracingpages", "tracingoutput", "tracinglostchars", "tracingcommands", "tracingrestores", "uchyph",
    "outputpenalty", "maxdeadcycles", "hangafter", "floatingpenalty", "globaldefs", "fam",
    "escapechar", "defaulthyphenchar", "defaultskewchar", "endlinechar", "newlinechar", "language",
    "lefthyphenmin", "righthyphenmin", "holdinginserts", "errorcontextlines", "tracingassigns", "tracinggroups",
    "tracingifs", "tracingscantokens", "tracingnesting", "predisplaydirection", "lastlinefit", "savingvdiscards",
    "savinghyphcodes", "TeXXeTstate", "pdfoutput", "pdfcompresslevel", "pdfdecimaldigits", "pdfminorversion",
    "pdfobjcompresslevel", "pdfpkresolution", "pdfdraftmode", "pdfgentounicode", "pdfadjustspacing", "pdfprotrudechars",
    "pdftracingfonts", "pdfimageresolution", "pdfinclusionerrorlevel", "parindent", "mathsurround", "lineskiplimit",
    "hsize", "vsize", "maxdepth", "splitmaxdepth", "boxmaxdepth", "hfuzz",
    "vfuzz", "delimitershortfall", "nulldelimiterspace", "scriptspace", "predisplaysize", "displaywidth",
    "displayindent", "overfullrule", "hangindent", "hoffset", "voffset", "emergencystretch",
    "pdfpagewidth", "pdfpageheight", "pdfhorigin", "pdfvorigin", "pdflinkmargin", "pdfdestmargin",
    "pdfthreadmargin", "pdfpxdimen", "lineskip", "baselineskip", "parskip", "abovedisplayskip",
    "belowdisplayskip", "abovedisplayshortskip", "belowdisplayshortskip", "leftskip", "rightskip", "topskip",
    "splittopskip", "tabskip", "spaceskip", "xspaceskip", "parfillskip", "thinmuskip",
    "medmuskip", "thickmuskip", "output", "everypar", "everymath", "everydisplay",
    "everyhbox", "everyvbox", "everyjob", "everycr", "errhelp", "everyeof",
    "pdfpagesattr", "pdfpageattr", "pdfpageresources", "pdfpkmode"
]
let EXPANDABLE = [
    "expandafter", "noexpand", "csname", "string", "number", "romannumeral",
    "the", "unexpanded", "detokenize", "meaning", "fontname", "jobname",
    "input", "endinput", "scantokens", "expanded", "eTeXrevision", "pdfstrcmp",
    "topmark", "firstmark", "botmark", "splitfirstmark", "splitbotmark", "topmarks",
    "firstmarks", "botmarks", "splitfirstmarks", "splitbotmarks", "pdfuniformdeviate", "pdfnormaldeviate",
    "pdftexrevision", "pdfescapestring", "pdfescapename", "pdfescapehex", "pdfunescapehex", "pdfmdfivesum",
    "pdffilesize", "pdffilemoddate", "pdfcreationdate", "if", "ifcat", "ifnum",
    "ifdim", "ifodd", "ifvmode", "ifhmode", "ifmmode", "ifinner",
    "ifvoid", "ifhbox", "ifvbox", "ifx", "ifeof", "iftrue",
    "iffalse", "ifcase", "ifdefined", "ifcsname", "iffontchar", "ifpdfprimitive",
    "ifincsname", "unless", "fi", "else", "or"
]
pub fn exists(name) => contains(PRIMITIVES, name)
pub fn expandable(name) => contains(EXPANDABLE, name)

// LaTeX names are enabled only in the LaTeX engine profile.
let LATEX_PRIMITIVES = ["newcommand", "renewcommand", "providecommand", "DeclareRobustCommand", "newenvironment", "renewenvironment", "begin", "end", "@@end", "newcounter", "setcounter", "addtocounter", "stepcounter", "refstepcounter", "newlength", "setlength", "addtolength", "settowidth", "settoheight", "settodepth", "documentclass", "LoadClass", "LoadClassWithOptions", "usepackage", "RequirePackage", "RequirePackageWithOptions", "DeclareOption", "ExecuteOptions", "ProcessOptions", "PassOptionsToPackage", "PassOptionsToClass", "@yargdef", "filename@parse", "@ifpackagewith", "@ifclasswith", "IfFileExists", "InputIfFileExists", "AtBeginDocument", "AtEndDocument", "AtEndOfPackage", "AtEndOfClass", "verb", "lstinline", "Verb", "mint", "mintinline", "lambdaconstructor", "ProvidesPackage", "ProvidesClass", "ProvidesFile", "NeedsTeXFormat", "include", "newif", "@ifnextchar", "kernel@ifnextchar", "@ifstar", "@for", "@tfor", "arabic", "roman", "Roman", "alph", "Alph", "fnsymbol", "value", "(", ")", "[", "]", "lambda@Roman", "NewDocumentCommand", "RenewDocumentCommand", "ProvideDocumentCommand", "DeclareDocumentCommand", "NewDocumentEnvironment", "RenewDocumentEnvironment", "ProvideDocumentEnvironment", "DeclareDocumentEnvironment", "IfNoValueTF", "IfNoValueT", "IfNoValueF", "IfValueTF", "IfValueT", "IfValueF", "IfBooleanTF", "IfBooleanT", "IfBooleanF", "AddToHook", "IfFormatAtLeastTF", "lambda@xspace"]
let LATEX_EXPANDABLE = ["@ifpackagewith", "@ifclasswith", "arabic", "roman", "Roman", "alph", "Alph", "fnsymbol", "value", "lambda@Roman", "IfNoValueTF", "IfNoValueT", "IfNoValueF", "IfValueTF", "IfValueT", "IfValueF", "IfBooleanTF", "IfBooleanT", "IfBooleanF", "IfFormatAtLeastTF", "lambda@xspace"]
pub fn latex_exists(name) => contains(LATEX_PRIMITIVES, name)
pub fn latex_expandable(name) => contains(LATEX_EXPANDABLE, name)

let INT_PARAMETERS = ["pretolerance", "tolerance", "linepenalty", "hyphenpenalty", "exhyphenpenalty", "clubpenalty", "widowpenalty", "displaywidowpenalty", "brokenpenalty", "binoppenalty", "relpenalty", "predisplaypenalty", "postdisplaypenalty", "interlinepenalty", "doublehyphendemerits", "finalhyphendemerits", "adjdemerits", "mag", "delimiterfactor", "looseness", "time", "day", "month", "year", "showboxbreadth", "showboxdepth", "hbadness", "vbadness", "pausing", "tracingonline", "tracingmacros", "tracingstats", "tracingparagraphs", "tracingpages", "tracingoutput", "tracinglostchars", "tracingcommands", "tracingrestores", "uchyph", "outputpenalty", "maxdeadcycles", "hangafter", "floatingpenalty", "globaldefs", "fam", "escapechar", "defaulthyphenchar", "defaultskewchar", "endlinechar", "newlinechar", "language", "lefthyphenmin", "righthyphenmin", "holdinginserts", "errorcontextlines", "tracingassigns", "tracinggroups", "tracingifs", "tracingscantokens", "tracingnesting", "predisplaydirection", "lastlinefit", "savingvdiscards", "savinghyphcodes", "TeXXeTstate", "pdfoutput", "pdfcompresslevel", "pdfdecimaldigits", "pdfminorversion", "pdfobjcompresslevel", "pdfpkresolution", "pdfdraftmode", "pdfgentounicode", "pdfadjustspacing", "pdfprotrudechars", "pdftracingfonts", "pdfimageresolution", "pdfinclusionerrorlevel"]

let DIMEN_PARAMETERS = ["parindent", "mathsurround", "lineskiplimit", "hsize", "vsize", "maxdepth", "splitmaxdepth", "boxmaxdepth", "hfuzz", "vfuzz", "delimitershortfall", "nulldelimiterspace", "scriptspace", "predisplaysize", "displaywidth", "displayindent", "overfullrule", "hangindent", "hoffset", "voffset", "emergencystretch", "pdfpagewidth", "pdfpageheight", "pdfhorigin", "pdfvorigin", "pdflinkmargin", "pdfdestmargin", "pdfthreadmargin", "pdfpxdimen"]

let GLUE_PARAMETERS = ["lineskip", "baselineskip", "parskip", "abovedisplayskip", "belowdisplayskip", "abovedisplayshortskip", "belowdisplayshortskip", "leftskip", "rightskip", "topskip", "splittopskip", "tabskip", "spaceskip", "xspaceskip", "parfillskip", "thinmuskip", "medmuskip", "thickmuskip"]

let TOKS_PARAMETERS = ["output", "everypar", "everymath", "everydisplay", "everyhbox", "everyvbox", "everyjob", "everycr", "errhelp", "everyeof", "pdfpagesattr", "pdfpageattr", "pdfpageresources", "pdfpkmode"]
pub fn parameter_kind(name) => if (contains(INT_PARAMETERS, name)) "int"
    else if (contains(DIMEN_PARAMETERS, name)) "dimen"
    else if (contains(GLUE_PARAMETERS, name)) if (contains(["thinmuskip", "medmuskip", "thickmuskip"], name)) "mu" else "glue"
    else if (contains(TOKS_PARAMETERS, name)) "toks" else null
