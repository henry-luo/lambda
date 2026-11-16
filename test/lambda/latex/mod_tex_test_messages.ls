// keep existing document-message assertions while also checking the full native stream.
pub fn document(messages, preload) any^ => slice(messages, len(preload), len(messages))^
