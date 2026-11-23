// Public PresentationML import is a shipped script package, like DOCX (D9.3.1/D9.3.4).
import reader: .pptx.read
import slide: lambda.slide

pub fn convert(source,options=null) map^ {
    let result = reader.read(source,options)^;
    {*:result,html:format(slide.handout(result.document)^,'html')^}
}
pub fn to_elements(source,options=null) element^ => convert(source,options)^.document
pub fn to_html(source,options=null) string^ => convert(source,options)^.html
pub fn extract_text(source,options=null) string^ => convert(source,options)^.text
