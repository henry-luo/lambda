"use strict";

class NestedMethodArrow {
  constructor() {
    this.total = 0;
  }

  run(command) {
    this.total += command();
  }

  makeHandler() {
    let closed = 0;
    const close = () => { closed++; };
    return [7].map(value => () => {
      this.run(() => value);
      close();
    })[0];
  }
}

const editor = new NestedMethodArrow();
const other = new NestedMethodArrow();
const editorHandler = editor.makeHandler();
const otherHandler = other.makeHandler();
editorHandler();
otherHandler();
console.log(editor.total + ":" + other.total);
