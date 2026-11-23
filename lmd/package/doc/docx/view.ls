// The converter owns DOCX semantics; this module only presents its result (D9.3.4).
import reader: .read
import css: .css

pub fn preview(result) element {
    <div class:"docx-preview-content",
        if (not (result.error is null)) {
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

fn viewer_document(result, streaming = false) element {
    let title = if (result.metadata.title != null and result.metadata.title != "")
        result.metadata.title else "Word document";
    <html lang:"en",
        <head <meta charset:"UTF-8"> <title title> <style (css.STYLESHEET ++ if (streaming)
            " .docx-preview .docx-document{display:flow-root;position:relative;z-index:0}.docx-preview .docx-document:after{content:none}"
            else "")>>
        <body class:"docx-viewer", <main id:"docx-preview", class:"docx-preview", preview(result)>>>
}

pub fn render_document_file(source, options = null) element^ {
    let result = reader.convert(source, {include_empty_paragraphs:true,presentation:"source",
        *:if (options == null) {} else options}, false)^;
    viewer_document(result)
}

pub fn preview_setup(source, options = null) map^ => reader.preview_setup(source)^

pub fn preview_batch(cursor, parsed, first, eof) map^ {
    let batch = reader.preview_batch(cursor,parsed,eof)^;
    {*:batch,document:if (first) viewer_document(batch,true) else batch.document}
}
