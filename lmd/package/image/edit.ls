// Image sessions reuse the editor's snapshot transaction history (S9.1.2).
import history: lambda.editor.mod_history
import img: lambda.image

pub fn session(image) map => {doc: image, original: image, hist: history.history_new()}
pub fn record(session, image, group = null) map {
    let tx = {doc_before: session.doc, doc_after: image, steps: [],
        sel_before: null, sel_after: null,
        meta: if (group == null) [] else [{name: "historyGroup", value: group}]}
    {doc: image, original: session.original, hist: history.history_push(session.hist, tx)}
}
pub fn undo(session) map {
    let result = history.history_undo(session.hist, session.doc)
    {doc: result.doc, original: session.original, hist: result.hist}
}
pub fn redo(session) map {
    let result = history.history_redo(session.hist, session.doc)
    {doc: result.doc, original: session.original, hist: result.hist}
}
pub fn can_undo(session) bool^ => history.can_undo(session.hist)^
pub fn can_redo(session) bool^ => history.can_redo(session.hist)^

// Retaining bytes alone cannot prove an edited document is safe to pass through.
pub fn original_bytes(session) binary^ {
    if (session.doc != session.original) raise error("image.original_bytes: document changed")
    else {
        let sources = if (img.assets(session.original) == null) [] else
            [for (asset in content(img.assets(session.original)) where asset.id == "source") asset]
        if (len(sources) != 1 or not (sources[0].data is binary))
            raise error("image.original_bytes: captured source is unavailable")
        else sources[0].data
    }
}
