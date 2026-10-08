// D4.5.2 regression: a UI-mode script result lives in the document's untraced
// arena. Runtime symbols, non-spreadable arrays and GC elements (group results)
// placed in element content must be copied there; run under forced, poisoning
// collection by RadiantViewTest.UiScriptContentSurvivesForcedGc.
fn row(n) { ["r" ++ string(n), symbol("s" ++ string(n)), "e" ++ string(n * 10)] }
fn sym_of(n) { symbol("k" ++ string(n)) }
fn words(n) { ["w" ++ string(n), "w" ++ string(n), "v" ++ string(n)] }
fn configured(attrs) => <sample position: [0.0, 0.0, 0.0], label: "default", *: attrs>
// overriding existing fields must retain their new values across collection.
let configured_item = configured({position: [9.0, 6.0, 13.0], label: "override" ++ string(7)})
let churn = [for (i in 1 to 128) [i, i + 1, i + 2, i + 3]];
<html
    <body
        <p id: "a", row(1)>
        <p id: "b", sym_of(2)>
        <div id: "c", [for (i in [1, 2, 3]) "n" ++ string(i)]>
        <div id: "g", for (x in words(1) group by x into g) g>
        <p id: "d", row(4)>
        <p id: "e", row(5)>
        <p id: "attrs", string(configured_item.position) ++ configured_item.label ++ string(len(churn))>
    >
>
