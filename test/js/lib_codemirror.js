(0, eval)(require('fs').readFileSync('test/js/dom2_library_probe.js', 'utf8'));
dom2_probe('codemirror', 'test/js/codemirror6.min.js', function () {
  var state = CodeMirror6.EditorState.create({ doc: 'hello' });
  return !!new CodeMirror6.EditorView({ state: state, parent: document.getElementById('editor') });
});
