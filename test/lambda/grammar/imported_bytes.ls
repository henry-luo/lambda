// An AUTO satellite must keep an imported grammar as a type value, never read an absent T0 slot (D7.2.2).
import g: lambda.doc.diff.grammar
fn records(source) {
    let tree = lambda.sys.parse(source,g.path_list)^;
    content(tree)
}
all([for (i in 1 to 60) len(records(binary("path\u0000"))) == 1])
