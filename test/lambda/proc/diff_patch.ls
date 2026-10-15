import diff: lambda.doc.diff
pn main() {
    let document = diff.read("test/input/diff/review.patch")^
    let html = format(diff.to_html(document)^,"html")^;
    [len(document.files) == 2,document.files[0].hunks[1].lines[1].new_no_newline,
     contains(html,"new tail"),contains(html,"old-name"),contains(html,"new-name"),
     contains(html,"&lt;script&gt;") and not contains(html,"<script>"),
     contains(html,"class=\"diff-review diff-static\"")]
}
