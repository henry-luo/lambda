// latex/css.ls — CSS stylesheet for LaTeX-to-HTML output

// ============================================================
// Default stylesheet (article class)
// ============================================================

pub STYLESHEET = ".latex-document {
  max-width: 800px;
  margin: 0 auto;
  padding: 2em;
  font-family: 'Computer Modern Serif', 'Latin Modern Roman', Georgia, serif;
  font-size: 12pt;
  line-height: 1.6;
  color: #333;
}
.latex-document p {
  margin: 0.5em 0;
  text-align: justify;
  text-indent: 1.5em;
}
.latex-document p:first-child,
.latex-document h1 + p,
.latex-document h2 + p,
.latex-document h3 + p,
.latex-document h4 + p {
  text-indent: 0;
}
.latex-title { text-align: center; margin-bottom: 2em; }
.latex-title .title { font-size: 1.7em; font-weight: bold; margin-bottom: 0.3em; }
.latex-title .author { font-size: 1.1em; margin: 0.3em 0; }
.latex-title .date { font-size: 0.95em; color: #555; margin: 0.3em 0; }
.latex-document h1 { font-size: 1.6em; margin: 1.5em 0 0.6em; }
.latex-document h2 { font-size: 1.4em; margin: 1.3em 0 0.5em; }
.latex-document h3 { font-size: 1.2em; margin: 1.1em 0 0.4em; }
.latex-document h4 { font-size: 1.05em; margin: 1em 0 0.3em; font-style: italic; }
.latex-document h5 { font-size: 1em; margin: 0.8em 0 0.2em; font-style: italic; }
.sec-num { margin-right: 0.5em; }
.latex-abstract {
  margin: 1.5em 3em;
  font-size: 0.95em;
}
.latex-abstract .abstract-title {
  font-weight: bold;
  text-align: center;
  margin-bottom: 0.5em;
}
.latex-blockquote {
  margin: 1em 2em;
  font-style: italic;
}
.latex-center { text-align: center; }
.latex-flushleft { text-align: left; }
.latex-flushright { text-align: right; }
pre.latex-verbatim {
  font-family: 'Computer Modern Typewriter', 'Latin Modern Mono', monospace;
  white-space: pre;
  background: #f8f8f8;
  border: 1px solid #e0e0e0;
  padding: 0.8em 1em;
  margin: 0.8em 0;
  overflow-x: auto;
  font-size: 0.9em;
  line-height: 1.4;
}
code.latex-code {
  font-family: 'Computer Modern Typewriter', 'Latin Modern Mono', monospace;
  background: #f5f5f5;
  padding: 0.1em 0.3em;
  border-radius: 2px;
  font-size: 0.9em;
}
.latex-footnote-ref {
  vertical-align: super;
  font-size: 0.75em;
  line-height: 0;
  text-decoration: none;
  color: #0066cc;
}
.latex-footnotes {
  border-top: 1px solid #ccc;
  margin-top: 2em;
  padding-top: 0.5em;
  font-size: 0.85em;
}
.latex-footnote { margin: 0.3em 0; }
.latex-footnote-num { font-weight: bold; margin-right: 0.3em; }
table.latex-tabular {
  border-collapse: collapse;
  margin: 1em auto;
}
table.latex-tabular td,
table.latex-tabular th {
  padding: 0.3em 0.8em;
}
table.latex-tabular .hline-top { border-top: 1px solid black; }
table.latex-tabular .hline-bottom { border-bottom: 1px solid black; }
.latex-figure {
  margin: 1.5em auto;
  text-align: center;
}
.latex-caption {
  margin-top: 0.5em;
  font-size: 0.9em;
}
.latex-caption .caption-label { font-weight: bold; margin-right: 0.5em; }
.math-display-container {
  text-align: center;
  margin: 1em 0;
}
.latex-equation {
  display: flex;
  align-items: center;
  justify-content: center;
  margin: 1em 0;
}
.latex-eq-number { margin-left: auto; padding-right: 1em; }
.latex-multicol {
  column-gap: 2em;
}
.latex-toc { margin: 1em 0 2em; }
.latex-toc .toc-title { font-size: 1.4em; font-weight: bold; margin-bottom: 0.5em; }
.latex-toc ul { list-style: none; padding-left: 0; }
.latex-toc li { margin: 0.2em 0; }
.latex-toc .toc-l2 { padding-left: 1.5em; }
.latex-toc .toc-l3 { padding-left: 3em; }
.latex-toc .toc-l4 { padding-left: 4.5em; }
.latex-toc a { text-decoration: none; color: inherit; }
.latex-toc a:hover { text-decoration: underline; }
.latex-sf { font-family: 'Computer Modern Sans', 'Latin Modern Sans', sans-serif; }
.latex-sc { font-variant: small-caps; }
.latex-sl { font-style: oblique; }
"

// return the stylesheet
pub fn get_stylesheet() {
    STYLESHEET
}
