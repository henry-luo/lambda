const editor = document.getElementById("editor");
const selection = getSelection();

function firstTextNode(root) {
  function walk(node) {
    if (!node) return null;
    if (node.nodeType === 3) return node;
    for (let child = node.firstChild; child; child = child.nextSibling) {
      const found = walk(child);
      if (found) return found;
    }
    return null;
  }
  return walk(root);
}

function selectText(root, start, end) {
  const text = firstTextNode(root);
  selection.setBaseAndExtent(text, start, text, end);
}

function selectionSummary(root) {
  const text = firstTextNode(root);
  return (selection.anchorNode === text) + ":" +
    selection.anchorOffset + ":" +
    (selection.focusNode === text) + ":" +
    selection.focusOffset;
}

function runHistoryCommand(command, expectedType) {
  editor.innerHTML = "abcd";
  selectText(editor, 2, 2);

  let before = "";
  let inputCount = 0;
  editor.onbeforeinput = function(event) {
    before = event.inputType + "|" + event.getTargetRanges().length;
    event.preventDefault();
  };
  editor.oninput = function() {
    inputCount++;
  };

  const supported = document.queryCommandSupported(command);
  const enabled = document.queryCommandEnabled(command);
  const ok = document.execCommand(command);

  console.log(
    command +
    " supported=" + supported +
    " enabled=" + enabled +
    " ok=" + ok +
    " before=" + before +
    " inputs=" + inputCount +
    " html=" + editor.innerHTML +
    " expected=" + (
      supported &&
      enabled &&
      ok &&
      before === expectedType + "|0" &&
      inputCount === 0 &&
      editor.innerHTML === "abcd"));
}

runHistoryCommand("undo", "historyUndo");
runHistoryCommand("redo", "historyRedo");

function runNativeRestore() {
  editor.innerHTML = "abcd";
  selectText(editor, 2, 2);

  const beforeTypes = [];
  const inputTypes = [];
  editor.onbeforeinput = function(event) {
    if (event.inputType === "historyUndo" ||
        event.inputType === "historyRedo") {
      beforeTypes.push(event.inputType + "|" + event.getTargetRanges().length);
    }
  };
  editor.oninput = function(event) {
    inputTypes.push(event.inputType);
  };

  const insertOk = document.execCommand("insertText", false, "X");
  const afterInsert = editor.innerHTML;
  const undoOk = document.execCommand("undo");
  const afterUndo = editor.innerHTML;
  const undoSelection = selectionSummary(editor);
  const redoOk = document.execCommand("redo");
  const afterRedo = editor.innerHTML;
  const redoSelection = selectionSummary(editor);

  console.log(
    "native-restore" +
    " insert=" + insertOk +
    " undo=" + undoOk +
    " redo=" + redoOk +
    " afterInsert=" + afterInsert +
    " afterUndo=" + afterUndo +
    " afterRedo=" + afterRedo +
    " undoSel=" + undoSelection +
    " redoSel=" + redoSelection +
    " before=" + beforeTypes.join(",") +
    " inputs=" + inputTypes.join(",") +
    " expected=" + (
      insertOk &&
      undoOk &&
      redoOk &&
      afterInsert === "abXcd" &&
      afterUndo === "abcd" &&
      afterRedo === "abXcd" &&
      undoSelection === "true:2:true:2" &&
      redoSelection === "true:3:true:3" &&
      beforeTypes.join(",") === "historyUndo|0,historyRedo|0" &&
      inputTypes.join(",") === "insertText,historyUndo,historyRedo"));
}

runNativeRestore();
