// S13.3.1v2: a document-owned task parks on the converter process so import cannot block window events.
import importer: ~~.mod_import
import paths: ~~.finder.mod_model

pub fn document_argument(args,base=sys.proc.self.cwd#,home=sys.home#) any^ {
    let options = [for (i,arg in args where arg=="--doc" or starts_with(arg,"--doc=")) i];
    if (len(options)==0) null
    else if (len(options)>1) raise error("Word: use --doc only once")
    else {
        let i = options[0]
        let separate = args[i]=="--doc"
        let path = if (separate) args[i+1] else slice(args[i],6);
        if (not (path is string) or trim(path)=="" or (separate and starts_with(path,"--")))
            raise error("Word: --doc requires a document path")
        else {path:paths.absolute(path,base,home)}
    }
}

pub fn accepts(dialog,detail) => dialog.kind=="open" and dialog.loading==true and dialog.seq==detail.seq
pub fn process_reply(result) map^ => importer.process_reply(result,"Word")^
pub pn run(root,request) {
    importer.run(root,{*:request,label:"Word",helper:"_open_document.ls",environment:"LAMBDA_WORD_OPEN_PATH",owner:"word",event:"word_open"})
}
