// S13.3.1v2: `owner: 'document'` names the running document as the owner.
// A script has none, so the start is an error rather than an unowned task;
// an ordinary start in the same procedure keeps its scoped join.
pn child() { 7 }
pn main() {
    let owned: any | error = start(child, [], {owner: 'document'})
    let scoped = start(child, [])
    let value = wait(scoped)^;
    [owned is error, owned.code, owned.message, value]
}
