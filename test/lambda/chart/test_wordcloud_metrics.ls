import radiant

let source = "<html><body style='margin:0'><span style='display:inline-block;white-space:pre;font:24px serif'>iii</span><span style='display:inline-block;white-space:pre;font:24px serif'>WWW</span><span style='display:none'>hidden</span></body></html>"
let first_result = radiant.measure_html(source, 600, 400)
let second_result = radiant.measure_html(source, 600, 400)
let empty = radiant.measure_html("<html><body></body></html>", 600, 400);
[
    len(first_result) == 3, first_result[0].width < first_result[1].width,
    first_result[0].height > 0, first_result[0].baseline > 0,
    first_result[0].baseline <= first_result[0].height,
    first_result[2] == null, first_result == second_result, len(empty) == 0,
    radiant.measure_html(source, 0, 400) == null
]
