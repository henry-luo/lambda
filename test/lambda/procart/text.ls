// procart.text (Lambda_Pkg_Proc_Art.md §4, P3): Tracery-style grammars and Markov names.
// Choices are keyed (exact tier); oracle.py checks the Markov model and names.
import rng: lambda.procart.rng
import text: lambda.procart.text

"1 grammars with one option per symbol are fixed text";
let fixed = {origin: "#who.capitalize# saw #what.a# and #what2.a#; #who.capitalizeAll# #verb.ed#.", who: "the old knight",
    what: "owl", what2: "unicorn", verb: "dash"};
[text.expand(fixed, rng.key(1))^];
[text.expand({origin: "#x.inQuotes# #y.comma# #z.comma#", x: "hi", y: "so", z: "end."}, rng.key(1))^];
[for (w in ["cat", "box", "wish", "city", "day", "bake", "fix"]) text.expand({origin: "#w.s#/#w.ed#", w: w}, rng.key(1))^];
[for (w in ["apple", "unicorn", "umbrella", "hour", ""]) text.expand({origin: "[w:" ++ w ++ "]#w.a#"}, rng.key(1))^];

"2 keyed choices";
let tale = {origin: "#[hero:#name#]story#", story: "#hero.capitalize# found #artifact.a#; #hero# kept #artifact.s#.",
    name: ["ada", "ming", "uriel"], artifact: ["owl", "ember", "lantern", "box"]};
[for (i in 0 to 3) text.expand(tale, rng.at(rng.key(2), i))^];
[text.expand(tale, rng.key(3))^ == text.expand(tale, rng.key(3))^];
let picks = [for (i in 0 to 599) text.expand({origin: "#c#", c: ["a", "b", "c"]}, rng.at(rng.key(4), i))^];
[for (c in ["a", "b", "c"]) abs(len([for (p in picks where p == c) p]) - 200) < 40];

"3 actions push and pop";
[text.expand({origin: "[c:red]#c#, [c:blue]#c#, [c:POP]#c#, [c:POP]#c#", c: "green"}, rng.key(5))^];
[text.expand({origin: "#[c:inner]c# then #c#", c: "outer"}, rng.key(5))^];
[text.expand({origin: "[pair:#a# #b#]#pair#!", a: "x", b: "y"}, rng.key(5))^];

"4 escapes and plain text";
[text.expand({origin: "a \\#tag\\# and \\[act\\] and \\\\"}, rng.key(6))^, text.expand({}, rng.key(6), "no tags")^];

"5 rejected grammars";
[text.expand({origin: "#nope#"}, rng.key(7)) ^ { "unknown" }, text.expand({origin: "#origin#"}, rng.key(7)) ^ { "deep" },
 text.expand({origin: "#x"}, rng.key(7)) ^ { "unclosed" }, text.expand({origin: "#x.shout#", x: "a"}, rng.key(7)) ^ { "modifier" },
 text.expand({origin: "[x]"}, rng.key(7)) ^ { "action" }];

"6 Markov names";
let tiny = text.markov(["ab", "ac"], 1)^;
// chr(2) and chr(3) mark a name's start and end
fn shown(c) => if (c == chr(2)) "<start>" else if (c == chr(3)) "<end>" else c;
[for (key, v at tiny.table) [shown(string(key)), [for (n in v) [shown(n[0]), n[1]]]]];
let model = text.markov(["aldric", "beatrix", "cedric", "dorian", "elowen", "fenwick", "gareth", "helena", "isolde", "jareth", "kestrel", "lorien"], 2)^;
let names = [for (i in 0 to 7) text.name(rng.at(rng.key(8), i), model)^];
names;
[all([for (n in names) len(n) >= 3 and len(n) <= 12 and not (n in model.samples)]),
 text.name(rng.key(9), model, {min: 5, max: 6})^, text.name(rng.key(9), model, {min: 40, attempts: 5}) ^ { "none" },
 text.markov([], 2) ^ { "empty" }, text.markov(["a"], 0) ^ { "order" }]
