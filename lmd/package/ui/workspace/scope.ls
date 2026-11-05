// Protect authored content without increasing the specificity of chrome rules.
let document_guard = ":not(:where(.workspace-document,.workspace-document *))"
pub fn isolate(css:string) string => replace(css,"__workspace_document__",document_guard)
