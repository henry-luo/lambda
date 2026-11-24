// Public DOCX module, resolved as lambda.doc.docx (D9.3.4).
import reader: .docx.read
import writer: .docx.write

pub fn convert(source, options = null) map^ => reader.convert(source,options,true)^
pub fn to_elements(source, options = null) element^ => convert(source,options)^.document
pub fn to_html(source, options = null) string^ => convert(source,options)^.html
pub fn extract_text(source, options = null) string^ => convert(source,options)^.text
// Export creates a new Word package; it does not re-emit unrecognized source parts.
pub fn export_document(document,options=null) map^ => writer.export_document(document,options)^
pub pn save(document,path,options=null) map^ { return writer.save(document,path,options)^ }
pub fn export_file(path,options=null) map^ => writer.export_file(path,options)^
