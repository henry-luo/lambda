import { Graph, InternalEvent, ModelXmlSerializer, UndoManager } from "@maxgraph/core";
import { install_drawing_probe } from "./drawing-probe.js";

function cell_snapshot(cell) {
  const geometry = cell.getGeometry();
  return {
    id: cell.getId(),
    value: cell.getValue(),
    x: geometry ? geometry.x : null,
    y: geometry ? geometry.y : null,
    width: geometry ? geometry.width : null,
    height: geometry ? geometry.height : null
  };
}

function graph_snapshot(drawing) {
  const view = drawing.graph.getView();
  const modelXml = new ModelXmlSerializer(drawing.graph.getDataModel()).export({ pretty: false });
  return {
    modelXml,
    selectedCell: drawing.graph.getSelectionCell()
      ? drawing.graph.getSelectionCell().getId() : null,
    source: cell_snapshot(drawing.source),
    target: cell_snapshot(drawing.target),
    added: drawing.added ? cell_snapshot(drawing.added) : null,
    edge: {
      id: drawing.edge.getId(),
      source: drawing.edge.getTerminal(true).getId(),
      target: drawing.edge.getTerminal(false).getId()
    },
    view: { scale: view.scale, translateX: view.translate.x, translateY: view.translate.y },
    svg: {
      groups: drawing.host.querySelectorAll("svg g").length,
      defs: drawing.host.querySelectorAll("svg defs").length,
      markers: drawing.host.querySelectorAll("svg marker").length
    }
  };
}

install_drawing_probe({
  library: "maxgraph",
  create(host, publish) {
    host.setAttribute("tabindex", "0");
    const graph = new Graph(host);
    graph.setConnectable(true);
    const parent = graph.getDefaultParent();
    let source = null;
    let target = null;
    let edge = null;
    graph.batchUpdate(() => {
      source = graph.insertVertex(parent, "source", "Source", 40, 45, 110, 50);
      target = graph.insertVertex(parent, "target", "Target", 255, 150, 110, 50);
    });
    edge = graph.insertEdge(parent, "edge", "", source, target, { endArrow: "classic" });
    const undo = new UndoManager();
    const record_edit = (_sender, event) => undo.undoableEditHappened(event.getProperty("edit"));
    graph.getDataModel().addListener(InternalEvent.UNDO, record_edit);
    graph.getView().addListener(InternalEvent.UNDO, record_edit);
    graph.addMouseListener({
      mouseDown(_sender, event) {
        const cell = event.getCell();
        if (cell) host.setAttribute("data-maxgraph-pointer-down", cell.getId());
      },
      mouseMove() {},
      mouseUp() {}
    });
    const drawing = { graph, parent, source, target, edge, added: null, undo, host };
    const refresh = () => publish();
    graph.addListener(InternalEvent.CELLS_RESIZED, refresh);
    graph.addListener(InternalEvent.CELLS_ADDED, refresh);
    graph.addListener(InternalEvent.CELLS_MOVED, refresh);
    host.addEventListener("wheel", (event) => {
      event.preventDefault();
      graph.zoomIn();
      host.setAttribute("data-maxgraph-wheel", "zoomed");
      publish();
    });
    host.addEventListener("keydown", (event) => {
      if (event.key !== "m") return;
      graph.moveCells([source], 10, 5);
      host.setAttribute("data-maxgraph-key", "moved");
      publish();
    });
    drawing.actions = {
      addVertex() {
        if (!drawing.added) {
          drawing.added = graph.insertVertex(parent, "added", "Added", 170, 40, 90, 45);
        }
        graph.setSelectionCell(drawing.added);
      },
      moveSource() { graph.moveCells([source], 20, 15); },
      resizeSource() {
        const geometry = source.getGeometry();
        graph.resizeCell(source, {
          x: geometry.x, y: geometry.y, width: 145, height: 65
        });
      },
      reconnectEdge() {
        if (!drawing.added) drawing.actions.addVertex();
        graph.connectCell(edge, drawing.added, false);
      },
      panZoom() {
        graph.getView().setScale(1.2);
        graph.getView().setTranslate(15, 10);
      },
      undo() { undo.undo(); },
      redo() { undo.redo(); }
    };
    return drawing;
  },
  destroy(drawing) {
    drawing.graph.destroy();
  },
  serialize(drawing) {
    return graph_snapshot(drawing);
  }
});
