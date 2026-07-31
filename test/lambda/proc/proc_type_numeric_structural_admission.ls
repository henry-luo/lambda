// Recursive numeric admission must clone dynamic structures before retagging
// their nested scalar values, including the detached COW path for a write.
fn dynamic(value) any { value }

type Child = {score: int}
type Person = {age: int, child: Child, scores: int[], choice: int | string}

pn reject_fractional_nested_write(var person: Person) Person^ {
    person.child.score = dynamic(3.5)
    return person
}

pn main() {
    var source = {age: 3.0, child: {score: 4.0}, scores: [5.0, 6.0], choice: 7.0, note: "open"}
    let person: Person = dynamic(source)
    let typed_scores: int[] = dynamic([7.0, 8.0])

    var updated: Person = {age: 1, child: {score: 2}, scores: [3], choice: "kept"}
    let snapshot = updated
    updated.child.score = dynamic(9.0)

    var rejected: Person = {age: 1, child: {score: 2}, scores: [3], choice: "kept"}
    let rejected_snapshot = rejected
    let ignored^write_error = reject_fractional_nested_write(rejected)

    print(string([
        source.age is float, source.child.score is float, source.scores[0] is float,
        person.age, person.child.score, person.scores[0], person.choice,
        person.age is int, person.child.score is int, person.scores[0] is int,
        person.choice is int,
        typed_scores[0] is int, typed_scores[1] is int,
        snapshot.child.score, updated.child.score, updated.child.score is int,
        ^write_error, rejected.child.score, rejected_snapshot.child.score
    ]) ++ "\n")
}
