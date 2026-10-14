// The converter owns DOCX semantics; this module only presents its result (D7.2.4).
import docx: lambda.doc.docx
import css: .css

pub fn preview(result) element {
    <div class:"docx-preview-content",
        if (result.error != null) {
            <p class:"preview-error", "Unable to open Word document: " ++ result.error>
        } else {
            if (len(result.diagnostics) > 0) {
                <aside class:"docx-import-notes",
                    <p "Some document content could not be displayed:">
                    <ul *[for (issue in result.diagnostics) <li issue.message>]>>
            }
            result.document
        }>
}

pub fn render_document_file(source, options = null) element^ {
    let result = docx.convert(source, {include_empty_paragraphs:true,presentation:"source",
        *:if (options == null) {} else options})^
    let title = if (result.metadata.title != null and result.metadata.title != "")
        result.metadata.title else "Word document";
    <html lang:"en",
        <head <meta charset:"UTF-8"> <title title> <style css.STYLESHEET>>
        <body class:"docx-viewer", <main id:"docx-preview", class:"docx-preview", preview(result)>>>
}
