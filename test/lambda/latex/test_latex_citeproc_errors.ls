// Invalid declarative inputs use the raised error channel (S7.4.1-S7.4.4).
import style: lambda.latex.citeproc.style
import c: lambda.latex.citeproc.common
let prefix = "<style xmlns=\"http://purl.org/net/xbiblio/csl\" version=\"1.0\" class=\"in-text\">"
let suffix = "<citation><layout><text variable=\"title\"/></layout></citation></style>"
let minimal = prefix ++ suffix
let cases = [
 replace(minimal, "http://purl.org/net/xbiblio/csl", "urn:wrong"),
 replace(minimal, "<text variable=\"title\"/>", "<script>raise bad()</script>"),
 replace(minimal, "variable=\"title\"", "macro=\"undefined\""),
 prefix ++ "<macro name=\"a\"><text macro=\"b\"/></macro><macro name=\"b\"><text macro=\"a\"/></macro>" ++ suffix,
 replace(minimal, "<text variable=\"title\"/>", "<text variable=\"title\" onclick=\"bad()\"/>"),
 replace(minimal, "<citation>", "<citation collapse=\"year-suffix-ranged\">")];
[for (value in cases) style.compile(value) ^ { c.error_issue(^).code }];
let dependent = prefix ++ "<info><link rel=\"independent-parent\" href=\"parent\"/></info></style>";
[style.compile(dependent) ^ { c.error_issue(^).code },
 style.compile(dependent,{parent:dependent}) ^ { c.error_issue(^).code },
 (style.compile(dependent,{parent:minimal})^).class];
let chain = join([for (i in 0 to 130) "<macro name=\"m" ++ string(i) ++ "\"><text " ++
    (if (i == 130) "variable=\"title\"" else "macro=\"m" ++ string(i + 1) ++ "\"") ++ "/></macro>"], "");
style.compile(prefix ++ chain ++ suffix) ^ { c.error_issue(^).code }
