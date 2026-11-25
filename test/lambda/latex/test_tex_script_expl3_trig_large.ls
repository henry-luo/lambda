import compare: ._tex_script_fp_compare
let expressions=["sin(1e16)","cos(1e16)","sin(-1e16)","cos(-1e16)",
 "sin(1e17)","cos(1e17)","sin(-1e17)","cos(-1e17)",
 "sin(1e18)","cos(1e18)","sin(-1e18)","cos(-1e18)",
 "sin(14148475504056880)","cos(14148475504056880)",
 "sin(1e20)","cos(1e20)","sin(-1e25)","cos(-1e25)","sin(1e30)","cos(1e30)"];
let results=[for(expr in expressions) compare.check(expr)^];
{cases:len(results),failures:[for(result in results where result.issue!=null or not result.text_match or
 not result.messages_match or not result.diagnostics_match) result]}
