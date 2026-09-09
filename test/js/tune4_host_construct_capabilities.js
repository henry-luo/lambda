function constructable(fn, args) {
    try {
        Reflect.construct(fn, args || []);
        return true;
    } catch (_) {
        return false;
    }
}

const xhr = XMLHttpRequest;
xhr.name = "RenamedXHR";
const offscreen_canvas = new OffscreenCanvas(1, 1);
const offscreen_context = offscreen_canvas.getContext("2d");

const results = [
    constructable(xhr),
    constructable(TextEncoder),
    constructable(TextDecoder),
    constructable(URLSearchParams, ["a=1"]),
    constructable(MessageChannel),
    constructable(DOMException),
    constructable(OffscreenCanvas, [1, 1]),
    constructable(Blob),
    constructable(File, [["x"], "x.txt"]),
    constructable(AbortController),
    constructable(AbortSignal),
    constructable(matchMedia, ["screen"]),
    offscreen_context.canvas === offscreen_canvas,
    typeof offscreen_context.measureText === "function"
];
console.log(results.join(" "));
