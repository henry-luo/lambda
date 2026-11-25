// D10.2.2v2: retain the native engine's encoding dispatch and error cases.
import facade: lambda.doc.latex.tex_engine.facade
let source = "\\makeatletter\\def\\cf@encoding{TST}" ++
    "\\def\\simple{A}\\@current@cmd\\simple\\simple" ++
    "\\expandafter\\def\\csname TST\\string\\localcmd\\endcsname{L}" ++
    "\\expandafter\\def\\csname ?\\string\\fallbackcmd\\endcsname{F}" ++
    "\\@changed@cmd\\localcmd\\relax\\@changed@cmd\\fallbackcmd\\relax" ++
    "\\let\\protect\\@unexpandable@protect" ++
    "\\edef\\saved{\\@current@cmd\\simple\\relax\\@changed@cmd\\localcmd\\relax}" ++
    "\\let\\protect\\@typeset@protect\\def\\simple{P}\\def\\localcmd{Q}\\saved\\makeatother";
let missing = "\\makeatletter\\def\\cf@encoding{TST}" ++
    "\\def\\missingtext{X}\\@changed@cmd\\missingtext\\relax\\makeatother";
let result=facade.tex(source)^;
let absent=facade.tex(missing)^;
[
    result.issue == null and len(result.root.diagnostics) == 0,
    contains(result.root.text,"ALF")^,
    contains(result.root.text,"PQ")^,
    absent.issue == null and any([for (d in absent.root.diagnostics)
        d.code == "tex-errmessage" and contains(d.message,"unavailable in encoding TST")^])
]
