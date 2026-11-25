import compare: ._tex_script_fp_compare
let expressions = ["trunc(1.7)", "trunc(-1.7)", "trunc(-0.1)", "trunc(-0)",
    "floor(1.7)", "floor(-1.7)", "floor(-0)", "ceil(1.7)",
    "ceil(-1.7)", "ceil(-0.1)", "ceil(-0)", "trunc(12.345,2)",
    "floor(12.345,2)", "ceil(12.345,2)", "trunc(-12.345,-1)",
    "floor(-12.345,-1)", "ceil(-12.345,-1)", "trunc(-0.001,2)",
    "floor(0.001,2)", "ceil(-0.001,2)", "ceil(-1e-100,99)",
    "floor(1e100,-100)", "trunc(1e100,-101)", "floor(1.234567890123456e-199,200)"];
let results = [for(expr in expressions) compare.check(expr)^];
{cases:len(results),failures:[for(result in results where result.issue!=null or not result.text_match or not result.messages_match or not result.diagnostics_match) result]}
