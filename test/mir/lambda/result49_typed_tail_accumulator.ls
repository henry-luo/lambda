type Cell = {v: int}

fn build_direct(seed: Cell[], remaining: int, acc: Cell[]) Cell[] {
    if (remaining == 0) acc
    else build_direct(seed, remaining - 1, acc ++ [seed[0]])
}

fn build_conditional(seed: Cell[], remaining: int, acc: Cell[]) Cell[] {
    if (remaining == 0) acc
    else {
        let next = if (remaining % 2 == 0) acc ++ [seed[0]]
            else acc ++ [seed[1]]
        build_conditional(seed, remaining - 1, next)
    }
}

fn build_pair(seed: Cell[], remaining: int, acc: Cell[]) Cell[] {
    if (remaining == 0) acc
    else build_pair(seed, remaining - 1, acc ++ [seed[0], seed[1]])
}

fn build_list(seed: Cell[], remaining: int, acc: Cell[]) Cell[] {
    if (remaining == 0) acc
    else build_list(seed, remaining - 1, acc ++ [(seed[0], seed[1])])
}

pn main() {
    let seed: Cell[] = [{v: 1}, {v: 2}]
    let direct: Cell[] = build_direct(seed, 4, seed)
    let conditional: Cell[] = build_conditional(seed, 5, seed)
    print(len(seed)); print(" "); print(len(direct)); print(" ")
    print(len(conditional)); print(" "); print(conditional[2].v); print("\n")
    var changed: Cell[] = direct
    changed[0].v = 9
    print(seed[0].v); print(" "); print(direct[0].v); print(" ")
    print(changed[0].v); print("\n")
    print(build_direct([], 1, []) is error); print("\n")
    let pair: Cell[] = build_pair(seed, 3, seed)
    print(len(pair)); print(" "); print(pair[3].v); print(" ")
    print(len(seed)); print("\n")
    print(build_pair([{v: 3}], 1, []) is error); print("\n")
    let from_list: Cell[] = build_list(seed, 2, seed)
    print(len(from_list)); print(" "); print(from_list[5].v); print("\n")
}
