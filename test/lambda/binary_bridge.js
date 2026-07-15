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
