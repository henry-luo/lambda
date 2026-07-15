export function bufferCopyAndMutate(value) {
    const copy = Buffer.from(value);
    copy[0] = 0x11;
    return copy;
}

export function uint8CopyAndMutate(value) {
    const copy = new Uint8Array(value);
    copy[1] = 0x22;
    return copy;
}

export function uint8From(value) {
    return Uint8Array.from(value);
}

export function clampedCopy(value) {
    return new Uint8ClampedArray(value);
}

export function middleDataView(value) {
    const copy = new Uint8Array(value);
    return new DataView(copy.buffer, 1, 2);
}

export function sharedView(value) {
    return new Uint8Array(value);
}

export function mutateSiblingViews(view) {
    const sibling = new Uint8Array(view.buffer, view.byteOffset, view.byteLength);
    const dataView = new DataView(view.buffer, view.byteOffset, view.byteLength);
    sibling[0] = 0x33;
    dataView.setUint8(1, 0x44);
    return view;
}

export function middleUint8View(value) {
    const view = new Uint8Array(value);
    return new Uint8Array(view.buffer, 1, 2);
}

export function sharedArrayBufferView(value) {
    const source = new Uint8Array(value);
    const shared = new SharedArrayBuffer(source.byteLength);
    const view = new Uint8Array(shared);
    view.set(source);
    return view;
}

export function mutateSharedArrayBufferView(view) {
    view[0] = 0x55;
    return view;
}
