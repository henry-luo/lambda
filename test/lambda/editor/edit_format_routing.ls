import app: lambda.editor.app

[
  app.format_for_path("drawing.SVG").surface == 'source',
  app.format_for_path("diagram.mmd").surface == 'source',
  app.format_for_path("diagram.d2").surface == 'source',
  app.format_for_path("diagram.dot").surface == 'source',
  app.format_for_path("diagram.gv").surface == 'source',
  app.format_for_path("document.latex").surface == 'source',
  app.format_for_path("deck.slides").surface == 'source',
  app.format_for_path("changes.patch").surface == 'source',
  app.format_for_path("notes.md").surface == 'rich_text',
  app.format_for_path("notes.rtf").surface == 'rich_text',
  app.format_for_path("notes.txtl").surface == 'rich_text',
  app.format_for_path("calendar.ics").surface == 'source',
  app.format_for_path("calendar.ical").surface == 'source',
  app.format_for_path("manual.1m").surface == 'source',
  app.format_for_path("manual.3p").surface == 'source',
  app.format_for_path("contact.vcf").surface == 'source',
  app.format_for_path("document.mark").surface == 'source',
  app.is_non_text_path("picture.PNG"),
  app.is_non_text_path("document.pdf"),
  app.is_non_text_path("document.docx"),
  not app.is_non_text_path("drawing.svg")
]
