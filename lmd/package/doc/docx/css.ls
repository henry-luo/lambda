// Shared document presentation for direct viewing and the file browser (D7.2.4).
pub let STYLESHEET = "
  .docx-viewer { margin: 0; background: #eef1f5; color: #232a35;
                 font-family: Arial, sans-serif; }
  .docx-preview { box-sizing: border-box; padding: 28px; background: #eef1f5; }
  .docx-preview .docx-document { box-sizing: border-box; max-width: 816px;
    min-height: 900px; margin: 0 auto; padding: 56px 64px; background: #fff;
    box-shadow: 0 2px 14px #d9dee7; font: 16px/1.6 Arial, sans-serif;
    overflow-wrap: anywhere; white-space: pre-wrap; }
  .docx-document p { margin: 0 0 .85em; }
  .docx-document h1, .docx-document h2, .docx-document h3,
  .docx-document h4, .docx-document h5, .docx-document h6 {
    margin: 0; font-size: inherit; line-height: inherit; }
  .docx-document ul, .docx-document ol { padding-left: 1.8em; margin: .7em 0; }
  .docx-document li > p { margin: .2em 0; }
  .docx-document table { width: 100%; border-collapse: collapse; margin: 0;
                         white-space: normal; }
  .docx-document td, .docx-document th { border: 0; padding: 0 7.2px;
    text-align: left; vertical-align: top; white-space: pre-wrap; }
  .docx-document th { font-weight: inherit; }
  .docx-document td p, .docx-document th p { margin: 0; }
  .docx-document img { max-width: 100%; }
  .docx-document .docx-chart { display: inline-block; max-width: 100%; white-space: normal; }
  .docx-chart svg { max-width: 100%; height: auto; }
  .docx-diagram, .docx-object { display: inline-block; max-width: 100%; white-space: normal; }
  .docx-diagram svg, .docx-object svg { max-width: 100%; height: auto; }
  .docx-document:after { content: ''; display: block; clear: both; }
  .docx-document a { color: #1769c2; }
  .docx-document blockquote { margin: 1em 0; padding-left: 1em;
    border-left: 3px solid #a6badc; color: #4c5c70; }
  .docx-document pre { white-space: pre-wrap; }
  .docx-document hr.docx-page-break, .docx-document .docx-section-break {
    border: 0; border-top: 1px dashed #cbd3df; margin: 2em 0; }
  .docx-import-notes, .docx-preview .preview-error { box-sizing: border-box;
    max-width: 816px; margin: 0 auto 18px; padding: 14px 20px;
    background: #fff7e6; color: #75521c; font: 14px/1.5 Arial, sans-serif; }
  .docx-import-notes p { margin: 0 0 .4em; }
  .docx-import-notes ul { margin: 0; padding-left: 1.5em; }
  @media (max-width: 700px) {
    .docx-preview { padding: 16px; }
    .docx-preview .docx-document { padding: 28px 24px; min-height: 600px; }
  }
"
