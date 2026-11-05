// S10.4.3v2/S10.5.3v2: a cursor carries lineage; stored values do not.
fn inspect(value: any) any => match value {
    case map: [~.child.~~ == ~, ~.child./ == ~, ~.~~ is null,
               ~.child.~~.~~ is null, ~./.~~ is null]
    default: false
}
fn parent(value: any) any => value.~~
fn root(value: any) any => value./
fn nested(value: any) any => match value {
    case map: match ~.child { case map: [~.~~ == value, ~./ == value] default: false }
    default: false
}
pn main() {
    let tree = {child: {n: 7}}
    print(inspect(tree)); print("\n")
    print(nested(tree)); print("\n")
    let detached = tree.child
    print([parent(detached) is null, root(detached) == detached]); print("\n")
    print([parent(17) is null, root(17) == 17, parent(null) is null, root(null) is null]); print("\n")
    print([parent(\.a.b), root(\.a.b), parent(/.a.b), root(/.a.b)]); print("\n")
    print([\.a.b.~~, /.a.b./]); print("\n")
    print([for (key in [1, 2]) \.row[key].name.~~]); print("\n")
}
