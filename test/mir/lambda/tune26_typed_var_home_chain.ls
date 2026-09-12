// Tune26 T26-4: a typed var parameter marked shared by a plain snapshot call
// must publish its later detach through both direct-call home links.

pn tune26_typed_var_home_snapshot(values: bool[]) any {
    values[0] = false
}

pn tune26_typed_var_home_inner(var values: bool[]) any {
    values[0] = true
}

pn tune26_typed_var_home_outer(var values: bool[]) any {
    tune26_typed_var_home_snapshot(values)
    tune26_typed_var_home_inner(values)
}

pn main() {
    var values: bool[] = [false]
    tune26_typed_var_home_outer(values)
    print(values[0]); print("\n")
}
