// Shared presentation for the browser pane and the editing application (D9.3.4).
pub let css = "
  :root {
    --document-font: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
    --document-mono: 'SF Mono', Menlo, Consolas, monospace;
    --document-chrome: #fbfcfe;
    --document-status: #f6f8fa;
    --document-rule: #e2e6ec;
    --document-ink: #212936;
    --document-muted: #68778a;
    --document-source: #fcfcfd;
    --document-prose: #232a35;
    --document-code: #eef1f5;
    --document-code-block: #f1f3f6;
    --document-stripe: #f7f9fb;
    --document-hover: #edf4ff;
    --document-key: #1268c0;
    --document-value: #ab2778;
  }
  .document-title { min-width: 0; }
  .document-path { color: #7b8798; font: 12px var(--document-mono);
                   overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .document-name { margin: 0; color: var(--document-ink); font-size: 18px; font-weight: 650;
                   overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .preview-kind { flex: 0 0 auto; padding: 4px 9px; border-radius: 999px; font-size: 12px; font-weight: 600; }
  .preview-kind.rendered { background: #e3f4eb; color: #1b7447; }
  .preview-kind.source { background: #e8edf5; color: #536275; }
  .preview-kind.editing { background: #dce9fd; color: #1c5da6; }
"

pub fn title(path, name, name_class = "document-name") =>
  <div class:"document-title",
    <div class:"document-path", title:path, path>
    <h1 class:name_class, name>
  >

pub fn badge(label, kind) => <span class:("preview-kind " ++ kind), label>

pub fn collection_summary(kind, count) =>
  (if (kind == 'object') "Object · " else "Array · ") ++ string(count) ++
  (if (kind == 'object') (if (count == 1) " property" else " properties")
   else if (count == 1) " item" else " items")

pub fn header(path, name, label, kind) =>
  <header class:"document-header", *[title(path, name), badge(label, kind)]>
