import { install_drawing_probe } from "./drawing-probe.js";

install_drawing_probe({
  library: "loop-closure",
  create() {
    const drawing = { count: 0 };
    drawing.actions = {
      addVertex() { drawing.count += 1; },
      moveSource() { drawing.count += 10; },
      resizeSource() { drawing.count += 100; },
      reconnectEdge() { drawing.count += 1000; },
      panZoom() { drawing.count += 10000; },
      undo() { drawing.count -= 1; },
      redo() { drawing.count += 1; }
    };
    return drawing;
  },
  destroy() {},
  serialize(drawing) {
    return { count: drawing.count };
  }
});
