const canvas = document.createElement("canvas");
const context = canvas.getContext("2d");
context.save();
context.scale(2, 2);
context.fillStyle = "#ff0000";
context.beginPath();
context.moveTo(0, 0);
context.lineTo(10, 10);
context.bezierCurveTo(1, 2, 3, 4, 5, 6);
context.closePath();
context.arc(10, 10, 5, 0, Math.PI * 2);
context.fill();
context.stroke();
context.restore();
context.fillStyle = "rgb(1 2 3)";
context.strokeStyle = "rgba(4, 5, 6, 0.5)";
context.lineWidth = 4;
context.lineCap = "round";
context.lineJoin = "bevel";
context.textAlign = "center";
context.font = "12px serif";
context.save();
context.fillStyle = "#ff0000";
context.lineWidth = 7;
context.lineCap = "square";
context.lineJoin = "miter";
context.textAlign = "right";
context.font = "20px sans-serif";
context.restore();
const state_restore = context.fillStyle === "#010203" &&
  context.strokeStyle === "#04050680" && context.lineWidth === 4 &&
  context.lineCap === "round" && context.lineJoin === "bevel" &&
  context.textAlign === "center" && context.font === "12px serif";
context.beginPath();
context.rect(1, 1, 28, 14);
context.quadraticCurveTo(16, 0, 30, 10);
context.clip();
context.fillText("Canvas", 16, 12, 28);
context.clearRect(0, 0, 1, 1);
context.fillRect(1, 1, 8, 8);
context.strokeRect(1, 1, 8, 8);
context.clearRect(2, 2, 1, 1);
canvas.width = 32;
canvas.height = 16;
const property_resize = canvas.width === 32 && canvas.height === 16;
canvas.setAttribute("width", "20");
const attribute_reset = canvas.width === 20 && context.fillStyle === "#000000";
canvas.removeAttribute("width");
const attribute_remove_reset = canvas.width === 300;
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
  typeof context.fillRect,
  typeof context.strokeRect,
  typeof context.quadraticCurveTo,
  typeof context.clip,
  typeof context.fillText,
  typeof context.createLinearGradient,
  state_restore,
  property_resize,
  attribute_reset,
  attribute_remove_reset,
  canvas.getContext("webgl") === null,
  typeof offscreen.getContext,
  offscreen.getContext("2d").canvas === offscreen,
  rejected_non_canvas_receiver
].join(" "));
