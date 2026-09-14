// Tune27 T27-1: a plain `T[]` boundary over a union element admits each element
// once; the whole-array validator walk is skipped, while nested and recursive
// element contracts still hold (D3.2.2).
type Tune27Doc = {kind: string} | {kind: string, parts: Tune27Doc[]}

fn tune27_leaf(k: string) Tune27Doc => {kind: k}

fn tune27_group(parts: Tune27Doc[]) Tune27Doc => {kind: "group", parts: parts}

fn tune27_size(doc: Tune27Doc) int =>
    if (doc.kind == "group") tune27_size_all(doc.parts, 0, 1) else 1

fn tune27_size_all(parts: Tune27Doc[], index: int, acc: int) int =>
    if (index >= len(parts)) acc
    else tune27_size_all(parts, index + 1, acc + tune27_size(parts[index]))

fn tune27_matrix(rows: int[][]) int => len(rows) * 10 + len(rows[0])

pn main() {
    let leaves = [for (i in 1 to 4) tune27_leaf(string(i))]
    let nested = tune27_group([tune27_group(leaves), tune27_leaf("x"), tune27_group([])])
    print([tune27_size(nested), tune27_size_all(leaves, 0, 0),
        tune27_matrix([[1, 2, 3], [4, 5, 6]])])
    print("\n")
}
