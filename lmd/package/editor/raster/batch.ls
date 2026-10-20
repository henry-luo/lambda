// S12.1.1v3: jobs are plain data; each result is validated before atomic file publication.
import editor: lambda.editor.raster
import actions: .actions
import files: .files
import model: .model

fn job_checked(job,base) any^ {
    if (not (job is map) or any([for (key,v in job) not (string(key) in ["source","target","format","layer"])]) or
        not (job.source is string) or len(job.source)<1 or not (job.target is string) or len(job.target)<1 or
        not (job.format in ["photos","png","psd"]) or not ends_with(lower(job.target),"." ++ job.format) or
        (job.layer != null and not (job.layer is string))) raise error("Raster: invalid batch job or destination format")
    else {*:job,source:url_resolve(base,job.source),target:url_resolve(base,job.target)}
}
pub fn preflight(action,jobs,base,options={}) any^ {
    let checked = actions.validate(action)^;
    if (not (jobs is array) or len(jobs)<1 or len(jobs)>32 or not (options is map) or
        any([for (key,v in options) string(key) != "continue_on_error"]) or
        (options.continue_on_error != null and not (options.continue_on_error is bool))) raise error("Raster: batch needs 1 to 32 explicit jobs")
    else {
        let ready = [for (job in jobs) job_checked(job,base)^]
        let targets = [for (job in ready) job.target]
        if (len(unique(targets)) != len(targets) or any([for (job in ready) job.source in targets]))
            raise error("Raster: batch outputs must be distinct and must not overwrite any batch input")
        else ready
    }
}
pn run_job(action,job) any^ {
    let loaded = files.open_document(job.source)^
    let chosen = if (job.layer != null) job.layer else [for (row in editor.layers(loaded.doc) where name(row.node) == 'layer') row.id][0]
    let selected = editor.exec(loaded,{id:"layer.select",ids:[chosen]})^
    let changed = editor.exec(selected,{id:"action.replay",action:action})^
    let saved = files.write_document(changed,job.target,job.format)^;
    return {source:job.source,target:job.target,layer:chosen,report:saved.report}
}
pub pn run(action,jobs,options={}) any^ {
    let base = "file://" ++ sys.proc.self.cwd# ++ "/"
    // preflight the entire list before opening or publishing any job.
    let ready = preflight(action,jobs,base,options)^
    var results = []
    for (job in ready) {
        var outcome = null
        run_job(action,job) ^ { outcome = {source:job.source,target:job.target,ok:false,error:^.message} }
            ~ { outcome = {*:~,ok:true} }
        results = [*results,outcome]
        if (not outcome.ok and options.continue_on_error == false) { break }
    }
    return {requested:len(ready),completed:len(results),succeeded:len([for (result in results where result.ok) result]),
        failed:len([for (result in results where not result.ok) result]),results:results}
}
