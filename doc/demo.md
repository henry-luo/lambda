# Lambda Demo — Document Viewer

The Lambda demo is a document viewer written in Lambda. It is a reactive Lambda
UI ([`test/ui/doc_viewer.ls`](../test/ui/doc_viewer.ls)) that runs on the Radiant
HTML/CSS layout engine. It lists the files of a project in a tree and shows the
selected file as a rendered document, a property tree, a table or its source.
Every format is parsed into the same Lambda/Mark data model and rendered by the
same engine.

## Running the demo

From the root of a Lambda checkout or of an unzipped release:

```bash
./lambda.exe demo
```

`demo` is short for `lambda view test/ui/doc_viewer.html`. It opens a window
with the animated startup splash. The splash loads the viewer in the background
and hands over to it after about two seconds.

To skip the splash and open the viewer directly:

```bash
./lambda.exe view
```

With no file argument, `view` opens `test/ui/doc_viewer.ls`. The file tree is
rooted at the current working directory, so run the command from the folder you
want to browse.

To open a single document in its own window, without the file browser:

```bash
./lambda.exe view README.md
```

## Viewer features

- **Project tree.** Folders load only when you open them, so large worktrees open
  quickly. Folders are listed before files, and each file has an icon for its
  type. Dot files, `build*`/`release*` output folders and `*.exe` binaries are
  hidden.
- **File-name filter.** Type in the filter box to narrow the tree to matching
  file names. Opened folders stay open. Press `Esc` or click ✕ to clear it.
- **View / Source tabs.** A file that can be rendered opens in the **View** tab.
  **Source** shows the raw text of the same file. Any other file (Lambda scripts,
  C++, logs, …) opens as plain source.
- **Rendered documents.** The viewer renders these formats as documents:
  - Markdown with inline and display math
  - MediaWiki, reStructuredText, Org, AsciiDoc, Textile and man pages
  - RTF
  - HTML
  - LaTeX, through Lambda's LaTeX package
  - PDF, converted to HTML
  - TikZ/PGF pictures and plots
- **Diagrams.** Mermaid (`.mmd`), Graphviz (`.dot`) and D2 (`.d2`) sources are
  laid out by Lambda's graph engine and drawn inline.
- **Images.** PNG, JPEG, GIF and SVG files are shown in the View tab.
- **Property trees.** JSON, YAML, TOML, INI, Java `.properties`, iCalendar
  (`.ics`) and vCard (`.vcf`) open as trees you can expand and collapse. Each
  collapsed node shows a summary, such as `Object · 3 properties`. A property
  filter searches the tree, and long lists show more items on request. Dotted
  `.properties` keys are grouped into nested nodes.
- **XML.** An XML file opens as a node tree with its attributes. If the file
  declares an `<?xml-stylesheet?>`, the viewer renders it with that CSS instead.
- **Email.** An `.eml` message shows a header table above its decoded body. HTML
  bodies inside multipart messages are rendered.
- **Tables.** CSV and TSV files open as tables. You can drag a column edge to
  resize the column, and long files load more rows on request.
- **Scripted testing.** The whole UI can be driven headlessly with
  `--event-file` scripts (clicks, typing, assertions, `render` to PNG). The
  snapshots below were captured that way.

## Snapshots

All snapshots were captured at a window width of 1024 px. Click a snapshot to
open it at full size.

<table>
<tr>
<td width="33%" align="center"><a href="img/demo/splash.png"><img src="img/demo/splash.png" alt="Startup splash"></a><br><sub><b>Startup splash</b> — <code>lambda demo</code></sub></td>
<td width="33%" align="center"><a href="img/demo/markdown.png"><img src="img/demo/markdown.png" alt="Markdown"></a><br><sub><b>Markdown</b> — README.md with badges and an SVG figure</sub></td>
<td width="33%" align="center"><a href="img/demo/markdown_math.png"><img src="img/demo/markdown_math.png" alt="Markdown math"></a><br><sub><b>Markdown + math</b> — inline LaTeX math</sub></td>
</tr>
<tr>
<td width="33%" align="center"><a href="img/demo/wiki.png"><img src="img/demo/wiki.png" alt="MediaWiki"></a><br><sub><b>MediaWiki</b> — <code>.wiki</code></sub></td>
<td width="33%" align="center"><a href="img/demo/rst.png"><img src="img/demo/rst.png" alt="reStructuredText"></a><br><sub><b>reStructuredText</b> — <code>.rst</code> with a contents directive</sub></td>
<td width="33%" align="center"><a href="img/demo/org.png"><img src="img/demo/org.png" alt="Org mode"></a><br><sub><b>Org mode</b> — <code>.org</code> with math</sub></td>
</tr>
<tr>
<td width="33%" align="center"><a href="img/demo/asciidoc.png"><img src="img/demo/asciidoc.png" alt="AsciiDoc"></a><br><sub><b>AsciiDoc</b> — <code>.adoc</code></sub></td>
<td width="33%" align="center"><a href="img/demo/textile.png"><img src="img/demo/textile.png" alt="Textile"></a><br><sub><b>Textile</b> — <code>.textile</code></sub></td>
<td width="33%" align="center"><a href="img/demo/man.png"><img src="img/demo/man.png" alt="Man page"></a><br><sub><b>Man page</b> — troff <code>.man</code></sub></td>
</tr>
<tr>
<td width="33%" align="center"><a href="img/demo/rtf.png"><img src="img/demo/rtf.png" alt="RTF"></a><br><sub><b>Rich Text Format</b> — <code>.rtf</code> laid out as a page</sub></td>
<td width="33%" align="center"><a href="img/demo/html.png"><img src="img/demo/html.png" alt="HTML"></a><br><sub><b>HTML + CSS</b> — borders and gradients in Radiant</sub></td>
<td width="33%" align="center"><a href="img/demo/latex.png"><img src="img/demo/latex.png" alt="LaTeX"></a><br><sub><b>LaTeX</b> — <code>.tex</code> with a table of contents</sub></td>
</tr>
<tr>
<td width="33%" align="center"><a href="img/demo/pdf.png"><img src="img/demo/pdf.png" alt="PDF"></a><br><sub><b>PDF</b> — converted to HTML</sub></td>
<td width="33%" align="center"><a href="img/demo/eml.png"><img src="img/demo/eml.png" alt="Email"></a><br><sub><b>Email</b> — <code>.eml</code> headers and HTML body</sub></td>
<td width="33%" align="center"><a href="img/demo/tikz.png"><img src="img/demo/tikz.png" alt="TikZ / PGF"></a><br><sub><b>TikZ / PGF</b> — PGFPlots log-axis plot</sub></td>
</tr>
<tr>
<td width="33%" align="center"><a href="img/demo/mermaid.png"><img src="img/demo/mermaid.png" alt="Mermaid"></a><br><sub><b>Mermaid</b> — <code>.mmd</code> flowchart</sub></td>
<td width="33%" align="center"><a href="img/demo/dot.png"><img src="img/demo/dot.png" alt="Graphviz DOT"></a><br><sub><b>Graphviz</b> — <code>.dot</code> with clusters and styled edges</sub></td>
<td width="33%" align="center"><a href="img/demo/d2.png"><img src="img/demo/d2.png" alt="D2"></a><br><sub><b>D2</b> — <code>.d2</code> system diagram</sub></td>
</tr>
<tr>
<td width="33%" align="center"><a href="img/demo/image.png"><img src="img/demo/image.png" alt="Raster image"></a><br><sub><b>Image</b> — JPEG/PNG/GIF</sub></td>
<td width="33%" align="center"><a href="img/demo/svg.png"><img src="img/demo/svg.png" alt="SVG"></a><br><sub><b>SVG</b> — vector image</sub></td>
<td width="33%" align="center"><a href="img/demo/xml.png"><img src="img/demo/xml.png" alt="XML with stylesheet"></a><br><sub><b>XML + CSS</b> — rendered with its <code>xml-stylesheet</code></sub></td>
</tr>
<tr>
<td width="33%" align="center"><a href="img/demo/json.png"><img src="img/demo/json.png" alt="JSON"></a><br><sub><b>JSON</b> — property tree</sub></td>
<td width="33%" align="center"><a href="img/demo/yaml.png"><img src="img/demo/yaml.png" alt="YAML"></a><br><sub><b>YAML</b> — property tree</sub></td>
<td width="33%" align="center"><a href="img/demo/toml.png"><img src="img/demo/toml.png" alt="TOML"></a><br><sub><b>TOML</b> — property tree</sub></td>
</tr>
<tr>
<td width="33%" align="center"><a href="img/demo/ini.png"><img src="img/demo/ini.png" alt="INI"></a><br><sub><b>INI</b> — sections as nodes</sub></td>
<td width="33%" align="center"><a href="img/demo/ics.png"><img src="img/demo/ics.png" alt="iCalendar"></a><br><sub><b>iCalendar</b> — <code>.ics</code> events and to-dos</sub></td>
<td width="33%" align="center"><a href="img/demo/vcf.png"><img src="img/demo/vcf.png" alt="vCard"></a><br><sub><b>vCard</b> — <code>.vcf</code> contacts</sub></td>
</tr>
<tr>
<td width="33%" align="center"><a href="img/demo/csv.png"><img src="img/demo/csv.png" alt="CSV"></a><br><sub><b>CSV</b> — table with resizable columns</sub></td>
<td width="33%" align="center"><a href="img/demo/tsv.png"><img src="img/demo/tsv.png" alt="TSV"></a><br><sub><b>TSV</b> — tab-separated table</sub></td>
<td width="33%" align="center"><a href="img/demo/source.png"><img src="img/demo/source.png" alt="Source view"></a><br><sub><b>Source view</b> — a Lambda script (<code>.ls</code>)</sub></td>
</tr>
</table>
