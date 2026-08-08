// Tune16 D-e: a member assignment must execute when it is the only statement
// in an if branch; the branch result is discarded, not its side effect.

pn main() {
    var data = {destination: 0}
    if (data.destination == 0) {
        data.destination = 1
    }
    print(data.destination)
    print("\n")
}
