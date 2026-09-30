// S7.1.1v3/D2.2.2: a dominated null test narrows a stable home. Rebinding a
// plain pn parameter revokes the proof, even inside the branch it opened.
fn guarded(x: float?) float? => if (x == null) null else x + 1.0
fn guarded_reversed(x: float?) float? => if (x != null) x + 1.0 else null
fn unguarded(x: float?) float? => x + 1.0

pn rebound(x: float?) float? {
    if (x != null) {
        x = null
        return x + 1.0
    }
    return null
}

pn main() {
    print(guarded(1.5)); print(" ")
    print(guarded(null)); print(" ")
    print(guarded_reversed(1.5)); print(" ")
    print(unguarded(null)); print(" ")
    print(rebound(1.5)); print("\n")
}
