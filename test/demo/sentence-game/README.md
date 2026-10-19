# Sentence Match

A native Lambda/Radiant sentence-building game inspired by the illustrated
subject-and-predicate cards in the reference picture.

From the repository root:

```sh
./lambda.exe demo sentence-game
```

Choose **2 parts** for a subject plus a complete predicate, or **3 parts** for
a subject, verb and object. For example, “The joker” + “wears” + “a funny cap.”
The three-part mode splits the complete predicate into its verb and object.
Each card uses singular subject–verb agreement, so familiar and silly mixes
both make grammatical sentences. Changing the mode deals fresh cards for the
current clause.

Complete a clause, then click **and +**, **but +**, **or +**, **so +**, **yet +**
or **because +** to add another clause. Keep going to build longer sentences,
mixing two- and three-part clauses if you like. **Undo join** restores the
previous clause and its cards. Coordinating conjunctions receive a comma;
“because” joins the following clause without one. Articles are lowercased
inside the sentence, while “Grandma” keeps its capital letter.

Press **Make my sentence** to save the whole sentence to your five-sentence
book. The same finished sentence cannot be added twice, including across
modes. **Next sentence** clears the current clauses and deals new cards;
**Start over** starts a fresh book in the selected mode.

Use the mouse or native button keyboard navigation (Tab, Enter and Space).
The shortcuts **A / S / D** choose the left, middle or right subject, and
**J / K / L** choose the predicate or verb, and **Z / X / C** choose the object.
The layout stacks the piles in narrow windows. The book stays in memory for
the current session. The direct entry point is still available:

```sh
./lambda.exe view test/demo/sentence-game/sentence-game.ls
```

## Files

| File | Purpose |
| --- | --- |
| `sentence-game.ls` | Reactive view instance and event handlers |
| `mod_model.ls` | Card catalogs, seeded deals, clause chains and game transitions |
| `mod_scene.ls` | Card table, sentence preview and finished book |
| `mod_art.ls`, `assets/` | Local SVG illustration assets |
| `sentence-game.css` | Responsive card-table theme |

The model returns immutable state values (**S1.4**). The view constructs its
document from those values; mutations happen in event handlers (**S12.1.3**).
Each deal includes a familiar match and other combinations. All illustrations
and styles are local, with no downloads or external services.

## Focused checks

The model golden checks both modes, required objects, duplicates, all six
conjunctions, mixed clauses, punctuation, capitalization, undo, eight-clause
chains, completion, restart and preservation of earlier states:

```sh
mkdir -p temp/sentence-game
./lambda.exe test/demo/sentence-game/tests/model_test.ls > temp/sentence-game/model-test-output.txt
diff -u test/demo/sentence-game/tests/model_test.txt temp/sentence-game/model-test-output.txt
```

Native replays cover the original five-sentence flow, three-part clauses,
conjunctions, undo, keyboard and pointer selection, and the narrow layout.
Captures and results go under `temp/sentence-game/`:

```sh
./lambda.exe demo sentence-game --headless \
  --event-file test/demo/sentence-game/tests/gameplay.json \
  --event-result temp/sentence-game/gameplay-result.json
./lambda.exe demo sentence-game --headless \
  --event-file test/demo/sentence-game/tests/multipart.json \
  --event-result temp/sentence-game/multipart-result.json
./lambda.exe demo sentence-game --headless \
  --event-file test/demo/sentence-game/tests/compact.json \
  --event-result temp/sentence-game/compact-result.json
```
