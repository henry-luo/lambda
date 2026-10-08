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
canvas.width = 8;
canvas.height = 8;
const blank_url = canvas.toDataURL();
context.fillStyle = "#ff0000";
context.fillRect(0, 0, 4, 4);
const painted_url = canvas.toDataURL();
const stable_bitmap = painted_url === canvas.toDataURL() && painted_url !== blank_url;
context.clearRect(0, 0, 8, 8);
const clear_bitmap = canvas.toDataURL() === blank_url;
const fallback_png = canvas.toDataURL("unsupported/type") === blank_url;
canvas.width = 0;
const empty_bitmap = canvas.toDataURL() === "data:,";
let rejected_data_url_receiver = false;
try {
  HTMLCanvasElement.prototype.toDataURL.call(document.createElement("div"));
} catch (error) {
  rejected_data_url_receiver = error instanceof TypeError;
}
const conversion_error = new Error("type conversion");
let propagated_type_conversion = false;
try {
  canvas.toDataURL({toString() { throw conversion_error; }});
} catch (error) {
  propagated_type_conversion = error === conversion_error;
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
  rejected_non_canvas_receiver,
  blank_url.startsWith("data:image/png;base64,"),
  stable_bitmap,
  clear_bitmap,
  fallback_png,
  empty_bitmap,
  rejected_data_url_receiver,
  propagated_type_conversion,
  HTMLCanvasElement.prototype.toDataURL.length === 0
].join(" "));
