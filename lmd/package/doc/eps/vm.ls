import obj: .objects

pub let SIGNATURES = {'save': "", 'restore': "x"}

fn valid_ref(value, count) => not obj.is_ref(value) or value.id < count

pub pn dispatch(var m, op, args) int^ {
    if (op == "save") {
        let allowed = obj.require(m, len(m.saves) < m.limits.max_graphics_depth, "limitcheck: VM saves")^
        let id = len(m.saves)
        push(m.saves, {heap: m.heap, graphics: m.graphics, gstack: m.gstack, fonts: m.fonts})
        push(m.stack, {kind: "save", id: id})
        return 0
    }
    let token = args[0]
    let valid = obj.require(m, token is map and token.kind == "save" and token.id >= 0 and token.id < len(m.saves), "invalidrestore: save object")^
    let saved = m.saves[token.id]
    let n = len(saved.heap)
    let stacks = obj.require(m, all([for (v in m.stack) valid_ref(v, n)]) and
        all([for (v in m.dicts) valid_ref(v, n)]) and
        all([for (frame in m.exec) valid_ref(frame.value, n) and valid_ref(frame.composite, n)]), "invalidrestore: live composite")^
    // PLRM 3.7.3: string bytes survive restore; other local composite contents rewind.
    let restored = [for (i in 0 to (n - 1)) if (saved.heap[i].kind == "string") m.heap[i] else saved.heap[i]]
    m.heap = restored
    m.graphics = saved.graphics
    m.gstack = saved.gstack
    m.fonts = saved.fonts
    m.saves = slice(m.saves, 0, token.id)
    return 0
}
