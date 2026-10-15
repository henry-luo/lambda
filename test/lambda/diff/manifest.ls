import git: lambda.doc.diff.git

let nul = binary([0])
let oid = join(fill(40,"1"),"")
let other = join(fill(40,"2"),"")
let raw = binary(":100644 100644 " ++ oid ++ " " ++ other ++ " M") ++ nul ++ binary("line\nspace file") ++ nul
let rename = binary(":100644 100644 " ++ oid ++ " " ++ oid ++ " R100") ++ nul ++ binary("old name") ++ nul ++ binary("new name") ++ nul
let manifest = git.parse_manifest(raw ++ rename)^;
[len(manifest) == 2,manifest[0].new_path == binary("line\nspace file"),
 manifest[1].kind == "renamed",manifest[1].old_name == "old name",manifest[1].new_name == "new name",
 git.parse_manifest(take(raw,len(raw) - 1)) ^ { contains(^.message,"parse") }]
