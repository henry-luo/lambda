// S13.3.1v2: a document-owned task parks on the converter process so import cannot block window events.
import dom
import resources: ~~.mod_resources
import paths: lambda.io.path

pub fn accepts(dialog,detail) => dialog.kind=="open" and dialog.loading==true and dialog.seq==detail.seq
pub fn process_reply(result) map^ {
    if (result.timed_out==true) raise error("Word: opening the document timed out. Try another file.")
    else if (result.cancelled==true) raise error("Word: opening the document was cancelled.")
    else if (result.truncated==true) raise error("Word: the document converter reply is too large.")
    else if (result.exit_code!=0) raise error("Word: the document converter could not finish.")
    else {
        let source = if (result.stdout is binary) parse(result.stdout,'text')^ else result.stdout
        let reply = parse(source,'json')^;
        if (reply.error is string) raise error(reply.error) else reply
    }
}
pub pn run(root,request) {
    let helper = paths.resolve_path(paths.dirname(resources.entry_path()^),"_open_document.ls")
    // io.exec owns process cancellation and captures stdout separately from diagnostic stderr.
    var reply = null
    // procedural handlers discard success values (S7.6.7v4), so capture each outcome explicitly.
    io.exec(sys.proc.self.argv#[0],["--no-log","run",helper],
        {env:{LAMBDA_WORD_OPEN_PATH:request.path},timeout_ms:60000,max_output_bytes:16777216}) ^ { reply = {error:^.message} }
        ~ { process_reply(~) ^ { reply = {error:^.message} } ~ { reply = ~ } }
    let owner = dom.get_element_by_id(root,"word")
    if (owner!=null) dom.dispatch(owner,{type:"word_open",bubbles:false,detail:{seq:request.seq,reply:reply}})
}
