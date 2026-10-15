import io
import diff: lambda.doc.diff

pn git(root,args) {
    let result = io.exec("git",["-C",root,*args])^
    if (result.exit_code != 0) raise error({message:"fixture Git command failed"})
    return result
}
pn main() {
    let root = "temp/diff_git_" ++ string(int(clock() * 1000000))
    let initialized = io.exec("git",["-c","init.templateDir=","init","-q",root])^
    git(root,["config","user.name","Diff Tests"])^
    git(root,["config","user.email","diff@example.invalid"])^
    output("before\n",root ++ "/tracked.txt","text")^
    output("same\n",root ++ "/old name.txt","text")^
    git(root,["add","--","tracked.txt","old name.txt"])^
    git(root,["-c","commit.gpgSign=false","commit","-q","--no-verify","-m","baseline"])^
    output("staged\n",root ++ "/tracked.txt","text")^
    git(root,["add","--","tracked.txt"])^
    output("worktree\n",root ++ "/tracked.txt","text")^
    output("new\n",root ++ "/untracked.txt","text")^
    let repo = diff.open_repo(root)^
    let unstaged = diff.compare(repo,"unstaged")^
    let staged = diff.compare(repo,"staged")^
    let working = diff.compare(repo,"working")^
    let unstaged_body = diff.load_file(repo,unstaged,0)^
    let staged_body = diff.load_file(repo,staged,0)^
    let untracked_body = diff.load_file(repo,unstaged,1)^
    let unchanged = diff.compare(repo,{mode:"commits",base:"HEAD",target:"HEAD"})^
    git(root,["mv","--","old name.txt","new name.txt"])^
    let renamed = diff.compare(repo,"staged")^
    let rename_id = [for (change in renamed.files where change.kind == "renamed") change.id][0]
    let rename_body = diff.load_file(repo,renamed,rename_id)^
    let checks = [len(unstaged.files) == 2,len(staged.files) == 1,len(working.files) == 2,
        contains(parse(unstaged_body.source,"text")^,"+worktree"),
        contains(parse(staged_body.source,"text")^,"+staged"),
        untracked_body.files[0].kind == "untracked" and untracked_body.added == 1,
        len(unchanged.files) == 0,any([for (change in renamed.files) change.kind == "renamed"])];
    output("",root ++ "/empty.txt","text")^
    let created = io.exec("python3",["-c","import os; open('binary.bin','wb').write(b'\\x00\\xff'); open('odd😀','wb').write(b'bytes\\n'); os.symlink('tracked.txt','link')"],{cwd:root})^
    let special = diff.compare(repo,"unstaged")^
    let empty_id = [for (change in special.files where change.new_name == "empty.txt") change.id][0]
    let binary_id = [for (change in special.files where change.new_name == "binary.bin") change.id][0]
    let link_id = [for (change in special.files where change.new_name == "link") change.id][0]
    let empty_body = diff.load_file(repo,special,empty_id)^
    let binary_body = diff.load_file(repo,special,binary_id)^
    let link_body = diff.load_file(repo,special,link_id)^
    output("tracked.txt filter=noexec\n",root ++ "/.gitattributes","text")^
    git(root,["config","filter.noexec.clean","python3 -c \"open('filter-ran','w').write('bad')\""])^
    git(root,["config","filter.noexec.required","true"])^
    let filtered = diff.compare(repo,"unstaged")^
    let filtered_id = [for (change in filtered.files where change.new_name == "tracked.txt") change.id][0]
    let filtered_body = diff.load_file(repo,filtered,filtered_id)^
    let untouched = io.exec("python3",["-c","import os,sys; sys.exit(13 if os.path.exists('filter-ran') else 0)"],{cwd:root})^
    git(root,["config","--remove-section","filter.noexec"])^
    let linked_path = parse(repo.root,"text")^ ++ "-linked"
    git(root,["worktree","add","--detach",linked_path,"HEAD"])^
    let directory = io.exec("python3",["-c","import os; os.mkdir('nested')"],{cwd:linked_path})^
    let linked = diff.open_repo(linked_path ++ "/nested")^
    git(root,["worktree","remove","--force",linked_path])^
    let unborn_path = root ++ "_unborn"
    let unborn_init = io.exec("git",["-c","init.templateDir=","init","-q",unborn_path])^
    git(unborn_path,["config","user.name","Diff Tests"])^
    git(unborn_path,["config","user.email","diff@example.invalid"])^
    output("base\n",unborn_path ++ "/a.txt","text")^
    git(unborn_path,["add","--","a.txt"])^
    let unborn = diff.open_repo(unborn_path)^
    let unborn_staged = diff.compare(unborn,"staged")^
    let unborn_body = diff.load_file(unborn,unborn_staged,0)^
    git(unborn_path,["-c","commit.gpgSign=false","commit","-q","--no-verify","-m","base"])^
    git(unborn_path,["checkout","-q","-b","left"])^
    output("left\n",unborn_path ++ "/a.txt","text")^
    git(unborn_path,["add","--","a.txt"])^
    git(unborn_path,["-c","commit.gpgSign=false","commit","-q","--no-verify","-m","left"])^
    git(unborn_path,["checkout","-q","-b","right","HEAD~1"])^
    output("right\n",unborn_path ++ "/a.txt","text")^
    git(unborn_path,["add","--","a.txt"])^
    git(unborn_path,["-c","commit.gpgSign=false","commit","-q","--no-verify","-m","right"])^
    let merged = io.exec("git",["-c","commit.gpgSign=false","merge","--no-edit","left"],{cwd:unborn_path})^
    let conflict_repo = diff.open_repo(unborn_path)^
    let conflict = diff.compare(conflict_repo,"unstaged")^
    let conflict_body = diff.load_file(conflict_repo,conflict,0)^
    output("staged\n",root ++ "/tracked.txt","text")^
    var stale = false
    diff.load_file(repo,unstaged,0) ^ { stale = contains(^.message,"refresh") }
    let edges = [rename_body.files[0].old_name == "old name.txt" and rename_body.files[0].new_name == "new name.txt",
        created.exit_code == 0,empty_body.added == 0 and len(empty_body.files) == 1,
        binary_body.files[0].binary,link_body.files[0].new_mode == "120000",
        any([for (change in special.files) change.new_path == binary("odd😀")]),
        untouched.exit_code == 0 and contains(parse(filtered_body.source,"text")^,"+worktree"),
        directory.exit_code == 0 and linked.root != repo.root and linked.head == repo.head,
        unborn.head == null and unborn_body.files[0].old_path == null and unborn_body.added == 1,
        merged.exit_code == 1 and len(conflict.files) == 1 and conflict.files[0].kind == "unmerged",
        conflict_body.files[0].kind == "unmerged" and len(conflict_body.files[0].hunks) == 0,stale];
    io.delete(unborn_path)^
    io.delete(root)^
    return [*checks,*edges]
}
