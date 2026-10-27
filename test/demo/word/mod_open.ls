// S13.3.1v2: a document-owned task parks on the converter process so import cannot block window events.
import importer: ~~.mod_import

pub fn accepts(dialog,detail) => dialog.kind=="open" and dialog.loading==true and dialog.seq==detail.seq
pub fn process_reply(result) map^ => importer.process_reply(result,"Word")^
pub pn run(root,request) {
    importer.run(root,{*:request,label:"Word",helper:"_open_document.ls",environment:"LAMBDA_WORD_OPEN_PATH",owner:"word",event:"word_open"})
}
