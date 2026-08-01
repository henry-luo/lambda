import { install_drawing_probe } from "./drawing-probe.js";
import { Graph } from "jointjs/src/dia/Graph.mjs";
import { Paper } from "jointjs/src/dia/Paper.mjs";
import { Link } from "jointjs/src/dia/Link.mjs";
import { Rectangle } from "jointjs/src/shapes/standard.mjs";
import { ToolsView } from "jointjs/src/dia/ToolsView.mjs";
import { Boundary } from "jointjs/src/linkTools/Boundary.mjs";

// Keep the upstream modules as a static ESM graph. The module loader sees the
// complete dependency graph before evaluation, so shared chunks compile once.

function joint_snapshot(drawing) {
  // JointJS renders element labels as SVG text but keeps link-label markup private.
  const textNodes = drawing.host.querySelectorAll("text");
  const textNode = textNodes.length > 0 ? textNodes[0] : null;
  const textBounds = textNode ? textNode.getBBox() : null;
  const sourcePosition = drawing.source.position();
  const target = drawing.link.target();
  const scale = drawing.paper.scale();
  const translate = drawing.paper.translate();
  return {
    graph: drawing.graph.toJSON(),
    sourceId: drawing.source.id,
    sourcePosition,
    targetId: drawing.target.id,
    linkId: drawing.link.id,
    linkTarget: target.id || null,
    paper: { scaleX: scale.sx, scaleY: scale.sy, translateX: translate.tx, translateY: translate.ty },
    selectedCell: drawing.selectedCell ? drawing.selectedCell.id : null,
    toolsVisible: !!drawing.toolsVisible,
    textBounds: textBounds && textBounds.width > 0 && textBounds.height > 0
      ? { width: textBounds.width, height: textBounds.height } : null
  };
}

install_drawing_probe({
  library: "jointjs",
  create(host, publish) {
    host.setAttribute("tabindex", "0");
    const graph = new Graph();
    const paper = new Paper({
      el: host,
      model: graph,
      width: 420,
      height: 260,
      gridSize: 10,
      interactive: true
    });
    const source = new Rectangle({ id: "joint-source" });
    source.position(35, 45);
    source.resize(115, 55);
    source.attr({
      body: { fill: "#dbeafe", stroke: "#2563eb", strokeWidth: 2 },
      label: { text: "Source", fill: "#0f172a" }
    });
    const target = new Rectangle({ id: "joint-target" });
    target.position(260, 150);
    target.resize(115, 55);
    target.attr({
      body: { fill: "#dcfce7", stroke: "#15803d", strokeWidth: 2 },
      label: { text: "Target", fill: "#0f172a" }
    });
    const link = new Link({ id: "joint-link" });
    link.source(source);
    link.target(target);
    link.labels([{ attrs: { text: { text: "link" } } }]);
    graph.addCells([source, target, link]);
    const drawing = { graph, paper, source, target, link, selectedCell: null,
      toolsVisible: false, host };
    paper.on("element:pointerdown", (elementView) => {
      drawing.selectedCell = elementView.model;
      host.setAttribute("data-jointjs-pointer-down", elementView.model.id);
      publish();
    });
    graph.on("change:position change:size change:target", publish);
    host.addEventListener("wheel", (event) => {
      event.preventDefault();
      paper.scale(1.1, 1.1);
      host.setAttribute("data-jointjs-wheel", "scaled");
      publish();
    });
    host.addEventListener("keydown", (event) => {
      if (event.key !== "m") return;
      source.translate(10, 5);
      host.setAttribute("data-jointjs-key", "moved");
      publish();
    });
    drawing.actions = {
      moveSource() { source.translate(30, 20); },
      resizeSource() { source.resize(145, 65); },
      reconnectLink() { link.target(source); },
      panZoom() {
        paper.scale(1.2, 1.2);
        paper.translate(15, 10);
      },
      addTools() {
        const sourceView = paper.findViewByModel(source);
        sourceView.addTools(new ToolsView({ tools: [new Boundary({ padding: 5 })] }));
        drawing.toolsVisible = true;
      },
      removeTools() {
        paper.findViewByModel(source).removeTools();
        drawing.toolsVisible = false;
      }
    };
    return drawing;
  },
  destroy(drawing) {
    drawing.paper.remove();
    drawing.graph.clear();
  },
  serialize(drawing) {
    return joint_snapshot(drawing);
  }
});
