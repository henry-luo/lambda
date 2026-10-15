import io
import c: .common
import g: .grammar
import parser: .parse

let environment = {GIT_OPTIONAL_LOCKS:"0",GIT_TERMINAL_PROMPT:"0",LC_ALL:"C",GIT_PAGER:"",
    GIT_DIR:null,GIT_WORK_TREE:null,GIT_INDEX_FILE:null,GIT_COMMON_DIR:null,GIT_EXTERNAL_DIFF:null,
    GIT_DIFF_OPTS:null,GIT_CONFIG_PARAMETERS:null,GIT_CONFIG_COUNT:null}
let diff_flags = ["--no-color","--no-ext-diff","--no-textconv","--no-relative","--find-renames=50%",
    "--src-prefix=a/","--dst-prefix=b/","--line-prefix=","--output-indicator-new=+",
    "--output-indicator-old=-","--output-indicator-context= ","--ignore-submodules=none","--submodule=short"]

fn opts(value) map^ => c.options(value,["timeout_ms","max_output_bytes"])^
pn exec_git(root,args,options,codes = [0]) map^ {
    let result = io.exec("git",["--no-pager","--literal-pathspecs",
        "-c","core.quotePath=true","-c","diff.autoRefreshIndex=false",*args],
        {cwd:root,env:environment,timeout_ms:c.get(options,"timeout_ms",30000),
         max_output_bytes:c.get(options,"max_output_bytes",16777216)})^
    let finished = c.require(not result.timed_out and not result.cancelled,"Git command timed out or was cancelled")^
    let complete = c.require(not result.truncated,"Git output exceeds max_output_bytes")^
    let success = c.require(result.exit_code in codes,"Git failed (" ++ string(result.exit_code) ++ "): " ++ c.display(result.stderr)^)^;
    return result
}
pn run(root,args,options,codes = [0]) map^ {
    var overrides = []
    if (args[0] == "diff") {
        // Attribute filters can otherwise execute while Git reads worktree content, even with --no-textconv.
        let drivers = exec_git(root,["config","--null","--name-only","--get-regexp",
            "^filter\\..*\\.(clean|smudge|process|required)$"],options,[0,1])^
        let keys = lambda.sys.parse(if (drivers.stdout == null) "" else drivers.stdout,g.path_list)^
        overrides = [for (node in content(keys)) for (part in ["-c",c.text(node.value)^ ++
            (if (ends_with(c.text(node.value)^,".required")) "=false" else "=")]) part]
    }
    return exec_git(root,[*overrides,*args],options,codes)^
}
fn output_line(bytes) => if (len(bytes) > 0 and bytes[len(bytes) - 1] == 10) take(bytes,len(bytes) - 1) else bytes
pn resolve(root,revision,options,optional = false) {
    let checked = c.require(revision is string and revision != "","revision must be a nonempty string")^
    let result = run(root,["rev-parse","--verify","--end-of-options",revision ++ "^{tree}"],options,
        if (optional) [0,1,128] else [0])^;
    return if (result.exit_code == 0) c.text(output_line(result.stdout))^ else null
}
pub pn open_repo(path,options = null) map^ {
    let checked = opts(options)^
    let valid = c.require(path is string or path is binary,"repository path must be local text or bytes")^
    let root_result = run(path,["rev-parse","--show-toplevel"],checked)^
    let root = output_line(root_result.stdout)
    let directory = run(root,["rev-parse","--absolute-git-dir"],checked)^
    let head = resolve(root,"HEAD",checked,true)^;
    return {kind:"git-repository",root:root,name:c.display(root)^,git_dir:output_line(directory.stdout),head:head}
}
fn raw_entry(source,node) map^ {
    let renamed = c.kind(node) == "Renamed"
    let before = if (renamed) node.before.value else node.path.value
    let after = if (renamed) node.after.value else node.path.value
    let status = c.text(node.status)^
    let letter = slice(status,0,1)
    let fields = node[0];
    {id:node.start,old_path:if (letter == "A") null else binary(before),
     new_path:if (letter == "D") null else binary(after),
     old_name:if (letter == "A") null else c.display(before)^,
     new_name:if (letter == "D") null else c.display(after)^,
     kind: {A:"added",D:"deleted",M:"modified",R:"renamed",C:"copied",T:"type",U:"unmerged",X:"unknown",B:"modified"}[letter],
     old_mode:c.text(fields.old_mode)^,new_mode:c.text(fields.new_mode)^,
     old_oid:c.text(fields.old_oid)^,new_oid:c.text(fields.new_oid)^,
     raw:slice(source,node.start,node.end),loaded:false,added:null,removed:null}
}
pub fn parse_manifest(source) array^ {
    let tree = lambda.sys.parse(if (source == null) "" else binary(source),g.raw,{spans:true})^;
    [for (node in content(tree)) raw_entry(binary(source),node)^]
}
fn untracked_entry(node) map^ {
    let path = binary(node.value)
    let label = c.display(path)^;
    {id:node.start,old_path:null,new_path:path,old_name:null,new_name:label,kind:"untracked",
     old_mode:null,new_mode:null,old_oid:null,new_oid:null,raw:null,loaded:false,added:null,removed:null}
}
fn select_paths(change) => unique([for (path in [change.old_path,change.new_path] where path != null) path])
pub pn compare(repo,comparison = "unstaged",options = null) map^ {
    let checked = opts(options)^
    let descriptor = if (comparison is string or comparison is symbol) {mode:comparison} else comparison
    let valid = c.require(repo.kind == "git-repository" and descriptor is map,"invalid repository or comparison")^
    let fields = c.options(descriptor,["mode","base","target"])^
    let mode = c.text(c.get(descriptor,"mode","unstaged"))^
    let supported = c.require(mode in ["unstaged","staged","working","commits"],"unknown comparison mode")^
    var before = null
    var after = null
    if (mode == "commits") {
        before = resolve(repo.root,descriptor.base,checked)^
        after = resolve(repo.root,descriptor.target,checked)^
    } else if (mode == "staged" or mode == "working") {
        before = resolve(repo.root,"HEAD",checked,mode == "staged")^
    }
    let arguments = ["diff",*diff_flags,
        *if (mode == "staged") ["--cached",*if (before != null) [before] else []]
         else if (mode == "working") [before]
         else if (mode == "commits") [before,after] else []]
    let captured = run(repo.root,[*arguments,"--raw","-z","--no-abbrev","--"],checked)^
    let tracked = parse_manifest(captured.stdout)^
    let conflicts = [for (change in tracked where change.kind == "unmerged") change.new_path]
    let distinct = [for (change in tracked where change.kind == "unmerged" or not (change.new_path in conflicts)) change]
    var others = []
    if (mode == "unstaged" or mode == "working") {
        let result = run(repo.root,["ls-files","--others","--exclude-standard","-z"],checked)^
        let tree = lambda.sys.parse(if (result.stdout == null) "" else result.stdout,g.path_list)^
        others = [for (node in content(tree)) untracked_entry(node)^]
    }
    let files = [for (i,change in [*distinct,*others]) {*:change,id:i}];
    return {kind:"diff-manifest",repo:repo,mode:mode,before:before,after:after,arguments:arguments,files:files}
}
pub pn load_file(repo,snapshot,id,options = null) map^ {
    let checked = opts(options)^
    let valid = c.require(snapshot.kind == "diff-manifest" and id is int and id >= 0 and id < len(snapshot.files),"invalid file identity")^
    let change = snapshot.files[id]
    if (change.kind == "unmerged") {
        return {kind:"diff",origin:"git",source:null,files:[{*:change,hunks:[],loaded:true,
            binary:false,submodule:false,added:0,removed:0}],complete:true,added:0,removed:0,diagnostics:[]}
    }
    let paths = select_paths(change)
    let arguments = if (change.kind == "untracked") ["diff",*diff_flags,"--no-index","--patch","--unified=3","--","/dev/null",change.new_path]
        else [*snapshot.arguments,"--patch","--unified=3","--",*paths]
    if (change.raw != null) {
        let current = run(repo.root,[*snapshot.arguments,"--raw","-z","--no-abbrev","--",*paths],checked)^
        let unchanged = c.require(current.stdout == change.raw,"comparison changed; refresh the file list")^
    }
    let captured = run(repo.root,arguments,checked,if (change.kind == "untracked") [0,1] else [0])^
    // Two equal captures establish a stable observation without claiming an atomic worktree snapshot.
    if (snapshot.mode != "commits") {
        let verified = run(repo.root,arguments,checked,if (change.kind == "untracked") [0,1] else [0])^
        let stable = c.require(captured.stdout == verified.stdout,"file changed while loading; refresh")^
    }
    let document = parser.parse(captured.stdout)^
    let one = c.require(len(document.files) == 1,"file changed or disappeared while loading; refresh")^
    let body = {*:document.files[0],id:id,old_path:change.old_path,new_path:change.new_path,
        old_name:change.old_name,new_name:change.new_name,kind:change.kind};
    return {*:document,origin:"git",comparison:snapshot,files:[body]}
}
