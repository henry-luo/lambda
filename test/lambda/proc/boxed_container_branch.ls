// A conditional joins the producer's actual Item carrier, not its machine word type (D2.4.1–D2.4.3).
fn failure(message) => {ok:false,error:message}
fn success(book) => {ok:true,book:book}
fn choose(book, category) {
    if (not contains(["asset"], category)) failure("invalid")
    else success({*:book,accounts:[*book.accounts,{category:category}]})
}
fn left_array() => [1,2]
fn right_array() => [3,4]
fn choose_array(flag) => if (flag) left_array() else right_array()
pn main() {
    let accepted = choose({accounts:[]}, "asset")
    let rejected = choose({accounts:[]}, "bad")
    print(accepted.ok and accepted.book.accounts[0].category == "asset"); print("\n")
    print(not rejected.ok and rejected.error == "invalid"); print("\n")
    print(choose_array(true)[0] == 1 and choose_array(false)[1] == 4)
}
