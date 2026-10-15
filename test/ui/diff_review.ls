import diff: lambda.doc.diff
let body = join([for (i in 1 to 40) " line " ++ string(i)],"\n")
let source = "--- old\n+++ new\n@@ -1,40 +1,40 @@ first\n" ++ body ++
    "\n@@ -50 +50 @@ second\n-old tail\n+new tail 😀\n" ++
    "diff --git a/mode b/mode\nold mode 100644\nnew mode 100755\n"
// Use one profile throughout the fixture; the first file also has a Git header.
let document = diff.parse("diff --git a/new b/new\n" ++ source)^
diff.viewer(document,{rows:6,title:"Review fixture"})^
