import diff: lambda.doc.diff

let prefix = "diff --git a/file b/file\n--- a/file\n+++ b/file\n"
let ordinary = prefix ++ "@@ -10,2 +10,3 @@ title\n same\n-old\n+new\n+extra\n"
let parsed = diff.parse(ordinary)^
let change = parsed.files[0]
let hunk = change.hunks[0]
let header_payload = diff.parse(prefix ++ "@@ -1 +1 @@\n--- a/literal\n+++ b/literal\n")^
let generic = diff.parse("--- first\told\n+++ first\tnew\n@@ -1 +1 @@\n-a\n+b\n--- second\n+++ second\n@@ -0,0 +1 @@\n+c\n")^
let mode = diff.parse("diff --git a/a b/a\nold mode 100644\nnew mode 100755\n")^
let rename = diff.parse("diff --git a/old b/new\nsimilarity index 100%\nrename from old\nrename to new\n")^
let quoted = diff.parse("diff --git \"a/space\\tfile\" \"b/space\\tfile\"\nold mode 100644\nnew mode 100755\n")^
let invalid_bytes = binary(prefix ++ "@@ -1 +1 @@\n-a\n+") ++ b'\xFF' ++ binary("\n")
let invalid = diff.parse(invalid_bytes)^
let byte_header = binary("@@ -1 +1 @@ ") ++ b'\xFF'
let invalid_header = diff.parse(binary(prefix) ++ byte_header ++ binary("\n-a\n+b\n"))^;

[
    len(diff.parse("")^.files) == 0,
    len(parsed.files) == 1,
    change.old_name == "file" and change.new_name == "file",
    change.added == 2 and change.removed == 1,
    hunk.lines[0].old_line == 10 and hunk.lines[0].new_line == 10,
    hunk.lines[1].old_line == 11 and hunk.lines[1].new_line == null,
    hunk.lines[2].new_line == 11 and hunk.lines[3].new_line == 12,
    header_payload.files[0].removed == 1 and header_payload.files[0].added == 1,
    len(generic.files) == 2 and generic.files[1].added == 1,
    generic.files[0].old_name == "first",
    mode.files[0].kind == "mode" and mode.files[0].new_mode == "100755",
    rename.files[0].kind == "renamed" and rename.files[0].new_name == "new",
    quoted.files[0].new_path == binary("space\tfile"),
    invalid.source == invalid_bytes and invalid.files[0].added == 1,
    invalid_header.files[0].hunks[0].header == format(byte_header,"mark"),
    diff.parse(prefix ++ "@@ -1,2 +1,2 @@\n-a\n+b\n") ^ { contains(^.message,"hunk line counts") },
    diff.parse(prefix ++ "@@ -1 +1 @@\n-a\n+b\n+extra\n") ^ { contains(^.message,"outside a hunk") },
    diff.parse("diff --cc file\n@@@ -1 -1 +1 @@@\n") ^ { contains(^.message,"combined") },
    diff.parse(prefix ++ "@@ -1 +1 @@\n-a\n+b\n",{unknown:true}) ^ { contains(^.message,"unknown option") },
    diff.parse(prefix ++ "@@ -1 +1 @@\n-a\n+b") ^ { contains(^.message,"unterminated") },
    diff.parse(prefix ++ "@@ -1,2 +1,2 @@\n a\n\\ No newline at end of file\n b\n") ^ { contains(^.message,"newline marker") },
    diff.parse(prefix ++ "@@ -2 +2 @@\n-a\n+b\n@@ -1 +1 @@\n-c\n+d\n") ^ { contains(^.message,"out-of-order") },
    diff.parse(prefix ++ "Binary files a/file and b/file differ\n+extra\n") ^ { contains(^.message,"outside a hunk") },
    diff.parse("diff --git a/a b/a\nGIT binary patch\nliteral 1\nAcme\n\n")^.files[0].binary,
    diff.parse(replace(ordinary,"\n","\r\n"))^.files[0].hunks[0].lines[0].end -
        diff.parse(replace(ordinary,"\n","\r\n"))^.files[0].hunks[0].lines[0].start == 5,
    diff.parse(prefix ++ "@@ -1 +1 @@\n-a\n+b\n",{max_records:1}) ^ { contains(^.message,"max_records") }
]
