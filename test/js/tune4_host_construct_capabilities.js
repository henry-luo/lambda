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
    constructable(matchMedia, ["screen"])
];
console.log(results.join(" "));
