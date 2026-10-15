// Each escaped task captures immutable request data (S13.1.4); the view owns cancellation.
import git: .git
import c: .common

pn capture(path,repo,comparison,preferred,generation,options) map^ {
    let opened = if (repo == null) git.open_repo(path,options)^ else repo
    let manifest = git.compare(opened,comparison,options)^
    let matches = [for (change in manifest.files where preferred != null and
        (change.new_path == preferred or change.old_path == preferred)) change.id]
    let selected = if (len(matches) > 0) matches[0] else if (len(manifest.files) > 0) 0 else null
    var document = null
    var message = ""
    if (selected != null) {
        git.load_file(opened,manifest,selected,options) ^ { message = ^.message } ~ { document = ~ }
    }
    return {kind:"capture",generation:generation,repo:opened,manifest:manifest,selected:selected,document:document,message:message}
}
pn load_request(repo,manifest,id,generation,options) map^ {
    let document = git.load_file(repo,manifest,id,options)^;
    return {kind:"file",generation:generation,selected:id,document:document,message:""}
}
pub pn start_capture(path,repo,comparison,preferred,generation,options) {
    return start(capture,[path,repo,comparison,preferred,generation,options])
}
pub pn start_load(repo,manifest,id,generation,options) {
    return start(load_request,[repo,manifest,id,generation,options])
}
