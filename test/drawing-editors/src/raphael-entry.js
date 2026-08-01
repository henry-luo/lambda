import Raphael from "raphael";
import { install_drawing_probe } from "./drawing-probe.js";

install_drawing_probe({
  library: "raphael",
  create(host, publish) {
    const paper = Raphael(host, 420, 260);
    const rectangle = paper.rect(40, 40, 120, 70, 8).attr({
      fill: "#dbeafe", stroke: "#2563eb", "stroke-width": 2
    });
    rectangle.node.setAttribute("id", "raphael-source");
    const circle = paper.circle(275, 82, 34).attr({
      fill: "90-#fef3c7-#f97316", stroke: "#c2410c", "stroke-width": 2
    });
    circle.node.setAttribute("id", "raphael-circle");
    const path = paper.path("M55,175 L165,145 L205,205").attr({
      stroke: "#0f766e", "stroke-width": 4
    });
    path.node.setAttribute("id", "raphael-path");
    const text = paper.text(210, 215, "Raphael SVG").attr({ "font-size": 17 });
    const image = paper.image(
      "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScL9NgAAAABJRU5ErkJggg==",
      350, 180, 1, 1);
    function make_draggable(element, name) {
      let drag_x = 0;
      let drag_y = 0;
      element.drag((dx, dy) => {
        element.transform(`T${drag_x + dx},${drag_y + dy}`);
        host.setAttribute("data-raphael-dragged", "true");
        publish();
      }, () => {
        const transform = element.transform();
        // Raphaël reports dx/dy relative to this press, so retain the prior
        // translation at drag start instead of snapping back on repeat drags.
        drag_x = transform[0] ? transform[0][1] : 0;
        drag_y = transform[0] ? transform[0][2] : 0;
        // This callback runs only after the SVG leaf accepted the native press.
        host.setAttribute("data-raphael-hit", name);
      });
    }

    make_draggable(rectangle, "rectangle");
    make_draggable(circle, "circle");
    make_draggable(path, "path");
    return { paper, rectangle, circle, path, text, image };
  },
  destroy(drawing) {
    drawing.paper.remove();
  },
  serialize(drawing) {
    return {
      elements: [drawing.rectangle, drawing.circle, drawing.path, drawing.text, drawing.image]
        .map((element) => ({ type: element.type, attrs: element.attr() })),
      draggableTransforms: {
        rectangle: drawing.rectangle.transform().toString(),
        circle: drawing.circle.transform().toString(),
        path: drawing.path.transform().toString()
      }
    };
  }
});
