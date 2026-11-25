// S1.8, D10.2.2v2: format checks use the active version, including changed formats.
let source="\\IfFormatAtLeastTF{2020-10-01}{old-yes}{old-no}/" ++
    "\\IfFormatAtLeastTF{2024/11/01}{equal-yes}{equal-no}/" ++
    "\\IfFormatAtLeastTF{2099-01-01}{future-yes}{future-no}/" ++
    "\\def\\fmtversion{2019-01-01}\\IfFormatAtLeastTF{2020-10-01}{changed-yes}{changed-no}";
let result=parse(source,"tex")^;
{text:result.text, diagnostics:result.diagnostics}
