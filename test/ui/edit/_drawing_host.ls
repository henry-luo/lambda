// Dedicated host for drawing-surface regressions; `lambda edit` opens SVG as
// source, while the package drawing surface still has independent clients.
import svg: lambda.editor.format.svg
import shell: lambda.editor.app.shell
import files: lambda.editor.app.files
import tools: lambda.editor.app.toolbar
import sess: lambda.editor.session
import lambda.editor.model

pub fn page(path) element^ {
  let source = input(path, 'text')^
  let loaded = svg.import_text(source)^
  let session = {*: sess.new_session(path, svg.descriptor, source, loaded), rich_format: null};
  <html <head <style files.css ++ tools.css ++ shell.surface_css>>
    shell.app(session, edit_open(loaded.doc, svg.schema, null), "Opened drawing.")>
}
