// Text benchmark: bounded character diff over six source-like edit pairs.
// The LCS table keeps this port's edit decisions deterministic across engines.

pn score_diff(left, right) int {
    let n = len(left)
    let m = len(right)
    let cols = m + 1
    var table: int[] = fill((n + 1) * cols, 0)
    var i: int = n - 1
    while (i >= 0) {
        var j: int = m - 1
        while (j >= 0) {
            if (left[i] == right[j]) {
                table[i * cols + j] = table[(i + 1) * cols + j + 1] + 1
            } else {
                var down: int = table[(i + 1) * cols + j]
                var across: int = table[i * cols + j + 1]
                table[i * cols + j] = if (down >= across) down else across
            }
            j = j - 1
        }
        i = i - 1
    }

    // Accumulate the same shape as the JS diff checksum: one contribution per
    // contiguous equal/insert/delete part, including its operation and length.
    i = 0
    var j: int = 0
    var operation: int = 2
    var part_length: int = 0
    var part_count: int = 0
    var score: int = 0
    while (i < n or j < m) {
        var next_operation: int = 0
        if (i < n and j < m and left[i] == right[j]) {
            next_operation = 0
        } else if (j >= m or (i < n and table[(i + 1) * cols + j] >= table[i * cols + j + 1])) {
            next_operation = -1
        } else {
            next_operation = 1
        }
        if (part_length > 0 and next_operation != operation) {
            score = score + operation * 31 + part_length
            part_count = part_count + 1
            part_length = 0
        }
        operation = next_operation
        part_length = part_length + 1
        if (next_operation == 0) {
            i = i + 1
            j = j + 1
        } else if (next_operation == -1) {
            i = i + 1
        } else {
            j = j + 1
        }
    }
    if (part_length > 0) {
        score = score + operation * 31 + part_length
        part_count = part_count + 1
    }
    return score + part_count * 17
}

pn main() {
    let pairs = [
        ["function parse(input) {\n  return input.trim();\n}\n", "function parse(source) {\n  const value = source.trim();\n  return normalize(value);\n}\n"],
        ["const config = {\n  retries: 2,\n  timeout: 1000\n};\n", "const config = {\n  retries: 3,\n  timeout: 1500,\n  backoff: true\n};\n"],
        ["class Renderer {\n  render(node) {\n    return node.text;\n  }\n}\n", "class Renderer {\n  render(node) {\n    return escapeHtml(node.text);\n  }\n\n  flush() {\n    return true;\n  }\n}\n"],
        ["import { readFile } from \"fs\";\n\nexport function load(path) {\n  return readFile(path);\n}\n", "import { readFile } from \"fs/promises\";\n\nexport async function load(path) {\n  const source = await readFile(path, \"utf8\");\n  return source;\n}\n"],
        ["line one\nline two\nline three\nline four\n", "line zero\nline one\nline 2 changed\nline three\nline four\nline five\n"],
        ["The quick brown fox jumps over the lazy dog.\nThis paragraph is stable.\n", "The quick red fox leaps over the sleepy dog.\nThis paragraph has changed.\n"]
    ]
    var checksum: int = 0
    var round: int = 0
    let t0 = clock()
    while (round < 256) {
        var index: int = 0
        while (index < len(pairs)) {
            checksum = checksum + score_diff(pairs[index][0], pairs[index][1])
            index = index + 1
        }
        round = round + 1
    }
    if (checksum == 0) {
        print("fast_diff: FAIL\n")
    } else {
        print("fast_diff: CHECKSUM:" ++ checksum ++ "\n")
    }
    print("__TIMING__:" ++ ((clock() - t0) * 1000.0) ++ "\n")
}
