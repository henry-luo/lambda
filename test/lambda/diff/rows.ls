import diff: lambda.doc.diff
import rows: lambda.doc.diff.rows

let patch = "--- before\n+++ after\n@@ -1,2 +1,2 @@\n one\n-two\n+three\n@@ -9 +9 @@ end\n-old\n\\ No newline at end of file\n+new\n\\ No newline at end of file\n"
let document = diff.parse(patch)^
let change = document.files[0]
let projection = rows.index(change)
let window = rows.window(document,change,projection,3,2)^
let html = format(diff.to_html(document)^,"html")^
let selected = rows.selection(rows.point(1,1),rows.point(5,2));
[projection.starts == [0,4],projection.count == 7,
 window[0].text == "three" and window[1].kind == "hunk",
 rows.hunk_step(projection,0,1) == 4 and rows.hunk_step(projection,4,-1) == 0,
 rows.search(document,change,projection,"THR")^ == [3],
 rows.copy_text(document,change,projection,"old")^ == "one\ntwo\nold",
 rows.copy_text(document,change,projection,"new")^ == "one\nthree\nnew",
 rows.copy_text(document,change,projection,"unified",selected)^ == "ne\ntwo\nthree\nol",
 contains(html,"new") and contains(html,"No newline at end of file"),
 contains(format(diff.viewer(document,{rows:2})^,"html")^,"diff-interactive"),
 contains(format(diff.repository(".")^,"html")^,"No changes."),
 diff.viewer(document,{rows:0}) ^ { ^.code == 7810 },
 diff.repository(".",{file_rows:-1}) ^ { ^.code == 7810 }]
