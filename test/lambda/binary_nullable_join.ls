// S2.2.2v2: empty binary conversion remains null through native joins.
fn assemble(parts) any^ {
    var result = binary([])^
    var index = 0
    while (index < len(parts)) { result = result ++ parts[index]; index = index+1 }
    return result
};
[assemble([binary([1,2])^,binary([3,4])^])^ == binary([1,2,3,4])^,
 binary([])^ == null,binary([])^ ++ binary([1,2])^ == binary([1,2])^,
 assemble([])^ == null]
