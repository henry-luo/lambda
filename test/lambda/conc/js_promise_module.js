export function later(value) {
    return new Promise((resolve) => setTimeout(() => resolve(value + 1), 1));
}

export function rejectLater() {
    return Promise.reject(new Error("js boom"));
}
