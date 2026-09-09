const canvas = document.createElement("canvas");
const context = canvas.getContext("2d");
context.save();
context.scale(2, 2);
context.beginPath();
context.arc(10, 10, 5, 0, Math.PI * 2);
context.stroke();
context.restore();
const offscreen = new OffscreenCanvas(1, 1);
let rejected_non_canvas_receiver = false;
try {
  HTMLCanvasElement.prototype.getContext.call(document.createElement("div"), "2d");
} catch (_) {
  rejected_non_canvas_receiver = true;
}
console.log([
  canvas instanceof HTMLCanvasElement,
  context instanceof CanvasRenderingContext2D,
  context === canvas.getContext("2d"),
  context.canvas === canvas,
  typeof context.measureText,
  context.measureText("canvas").width > 0,
  canvas.getContext("webgl") === null,
  typeof offscreen.getContext,
  offscreen.getContext("2d").canvas === offscreen,
  rejected_non_canvas_receiver
].join(" "));
