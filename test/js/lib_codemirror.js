(0, eval)(require('fs').readFileSync('test/js/codemirror6.min.js', 'utf8'));

var state = CodeMirror6.EditorState.create({
  doc: 'hello world',
  selection: { anchor: 5 }
});
var editor = new CodeMirror6.EditorView({
  state: state,
  parent: document.getElementById('editor')
});

editor.dispatch({ changes: { from: 5, to: 11, insert: ' DOM2' } });
editor.dispatch({
  changes: { from: 0, to: 5, insert: 'Radiant' },
  selection: { anchor: 7, head: 7 }
});

console.log('codemirror:create:' + (editor instanceof CodeMirror6.EditorView));
console.log('codemirror:doc:' + editor.state.doc.toString());
console.log('codemirror:selection:' +
  editor.state.selection.main.anchor + ':' + editor.state.selection.main.head);
console.log('codemirror:dom:' + document.querySelector('.cm-content').textContent);
editor.destroy();
