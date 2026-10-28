// Document-owned tasks park on converter processes so expensive imports leave window events live (S13.3.1v2).
import dom
import resources: .mod_resources
import paths: lambda.io.path

pub fn process_reply(result,label) map^ {
    if (result.timed_out==true) raise error(label ++ ": opening the document timed out. Try another file.")
    else if (result.cancelled==true) raise error(label ++ ": opening the document was cancelled.")
    else if (result.truncated==true) raise error(label ++ ": the document converter reply is too large.")
    else if (result.exit_code!=0) raise error(label ++ ": the document converter could not finish.")
    else {
        let source = if (result.stdout is binary) parse(result.stdout,'text')^ else result.stdout
        let reply = parse(source,'json')^;
        if (reply.error is string) raise error(reply.error) else reply
    }
}
pub pn run(root,request) {
    let helper = paths.resolve_path(paths.dirname(resources.entry_path()^),request.helper)
    var reply = null
    // handlers discard success values (S7.6.7v4); capture both outcomes before dispatch.
    io.exec(sys.proc.self.argv#[0],["--no-log","run",helper],
        {env:{[request.environment]:request.path},timeout_ms:60000,max_output_bytes:16777216}) ^ { reply = {error:^.message} }
        ~ { process_reply(~,request.label) ^ { reply = {error:^.message} } ~ { reply = ~ } }
    let owner = dom.get_element_by_id(root,request.owner)
    if (owner!=null) dom.dispatch(owner,{type:request.event,bubbles:false,detail:{seq:request.seq,reply:reply}})
}
