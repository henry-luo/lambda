// ES Module: string utilities for parallel import testing
export function repeat(s, n) {
    var result = "";
    for (var i = 0; i < n; i++) {
        result = result + s;
    }
    return result;
}

export function upper(s) {
    return s.toUpperCase();
}

export const SEPARATOR = "-";
