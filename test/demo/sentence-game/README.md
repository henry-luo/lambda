# Build Sentence

A native Lambda/Radiant game with three levels: sentence building, word-card
puzzles, and picture description.

From the repository root:

```sh
./lambda.exe demo sentence-game
```

## Choose a level

The opening screen offers all three levels. The level tabs and **All levels**
button let you switch at any time, keeping each level’s draft and sentence book
in memory. Completing Levels 1 and 2 offers a button for the next level.
**Start over** resets only the current level. Progress lasts for this session.

## Level 1: Build a sentence

**Screen 1: Pick a pattern.** Choose from fourteen colored sentence recipes.
The eight core patterns are:

| Pattern | Parts |
| --- | --- |
| Start with two | Who + Doing what |
| Add an object | Who + Verb + To what |
| Tell us where | Who + Doing what + Where |
| Tell us when | Who + Doing what + When |
| Object and place | Who + Verb + To what + Where |
| Object and time | Who + Verb + To what + When |
| Set the scene | Who + Doing what + Where + When |
| The whole story | Who + Verb + To what + Where + When |

Six advanced recipes add manner, reason and purpose:

| Pattern | Parts | Example |
| --- | --- | --- |
| Tell us how | Who + Doing what + How | The musician plays a little drum quietly. |
| Action and manner | Who + Verb + To what + How | The scientist examines a shiny crystal carefully. |
| Give a reason | Who + Doing what + Why | The monkey tosses a ripe banana to have fun. |
| How and where | Who + Verb + To what + How + Where | The gardener waters bright flowers slowly in the garden. |
| When and why | Who + Verb + To what + When + Why | The chef stirs a pot of soup in the morning to help a friend. |
| Add every detail | Who + Verb + To what + How + Where + When + Why | The bear picks sweet berries carefully in the forest today to have fun. |

**Screen 2: Build a sentence.** Pick one picture card from each pile. Who is
orange, the action is yellow, **To what?** is green, where is blue, when is brown,
how is purple, and why is pink.
The preview follows that same color scheme. A separate To what slot splits the
complete predicate into its verb and object; Where and When alone keep the
complete predicate. Each deal offers three choices per pile, including a
familiar subject/action/object match alongside imaginative combinations.

**Change pattern** returns to the first screen. **Back to my sentence**, or
choosing the same pattern, keeps your draft and cards. Choosing another
pattern deals fresh cards for the current clause and keeps earlier clauses.
**New cards** deals another hand for the current clause, keeping earlier clauses.
**Scramble cards** combines all available cards into one shuffled group across
word categories. Category headings and rows disappear; each card’s color is
the category hint. It keeps your selections, joined clauses and saved sentences.
**Scramble again** reshuffles the group. Recipe changes, new cards and joined
clauses keep the mixed layout; each round remembers its own layout and draft.

Complete a clause, then click **and +**, **but +**, **or +**, **so +**, **yet +**
or **because +** to add another clause. **Although**, **while**, **when**, and
**if** introduce contrast, simultaneous actions, time, and conditions. For example,
“The teacher reads a book while the dog chases a ball.” Keep going to build longer sentences,
mixing patterns if you like. A new clause starts with the current pattern;
use **Change pattern** to choose a different one. **Undo join** restores the
previous clause and its cards. Coordinating conjunctions receive a comma;
“because” joins the following clause without one. Articles are lowercased
inside the sentence, while “Grandma” keeps its capital letter.

Press **Save my sentence** to save the whole sentence to your five-sentence
book. The same finished sentence cannot be added twice, including across
patterns. **Next sentence** opens the next unfinished round: new rounds start
at the pattern picker, and visited drafts resume. **Start over** starts a fresh
book at the pattern picker.

### Jump between rounds

Click any of the five dots in the red bar to go directly to that round, including
an unfinished or unvisited one. Each round keeps its pattern, cards, choices,
and joined clauses. Completed rounds reopen for review without adding a second
book entry. Yellow dots show completed rounds; the ring marks the current one.

You can finish rounds in any order. **Next sentence** goes to the next unfinished
round, wrapping to the beginning when needed. The book is ordered by round.
Undoing a join keeps sentences saved in other rounds. The dots also remain
available on the pattern picker and finished-book screen.

## Level 2: Unscramble a sentence

Eight lessons offer a clue and a scrambled set of individual word cards,
including a full stop. Click cards in sentence order. Each card can be placed
once; repeated words have separate cards. Tap a placed word to return it to
the bank, use its arrow buttons to swap it with a neighbor, or use **Undo last
card** and **Clear the line**.

**Check my sentence** becomes available after every card is placed. An
incorrect attempt stays editable, with feedback. A correct sentence is saved,
and **Next puzzle** continues to an unfinished puzzle. **Need a hint?** reveals the opening words.
The checker follows each lesson’s clue and accepts the listed alternate
readings; it does not attempt unrestricted English grammar checking.
The eight puzzle dots also allow direct switching. Each puzzle retains its
word order and hint state, and **Next puzzle** skips completed puzzles.

Keys **1–9** and **0** select the corresponding available word card.
**Backspace** returns the last placed word. Tab and Enter reach all controls.

## Level 3: Describe a picture

Choose from six polished picture-book scenes: a park, a kitchen, a beach, a
harvest farm, a woodland gathering, and a school discovery fair.
Each picture has a related, fixed bank of subjects, actions, objects, places
and times, plus related How and Why cards for the advanced recipes. The large
picture has a descriptive alternative text.

Choose a recipe and select cards to describe what you see. **Save description**
adds the sentence to your book and clears the sentence line. All cards remain
available: reuse them in any number of descriptions or in joined clauses.
There is no five-sentence limit. Duplicate sentences are blocked.

**Choose another picture** changes the scene and card bank while keeping your
saved descriptions. **Clear this sentence** clears the draft. Recipes, letter
shortcuts, conjunctions and **Undo join** work as in Level 1. This level checks
that the selected sentence parts are complete; players judge how accurately
their descriptions match the picture.

Use **Scramble cards** in the red story header to combine all the available
cards into one shuffled bank, with colors as category hints. The same cards
remain reusable, and your current sentence and book stay intact. Mixed cards
stay mixed when you save or clear a description, change recipes or add clauses.

### Advanced pictures

Each new scene has **six subjects and 32 reusable cards** across all recipes:
six subjects, six actions, six objects, six places, two times, three manners
and three reasons. A recipe shows the card types it needs; **Add every detail**
offers the whole 32-card bank.

| Picture | Six subjects | Some things to describe |
| --- | --- | --- |
| Harvest morning on the farm | Farmer, gardener, chef, musician, rabbit, hen | Growing a pumpkin, watering flowers, stirring soup, playing a drum, nibbling a carrot, laying eggs |
| Stories in the woodland | Grandma, teacher, bear, fox, monkey, dog | Reading, painting a rainbow, picking berries, following a butterfly, tossing a banana, chasing a ball |
| The school discovery fair | Scientist, astronaut, robot, teacher, dragon, firefighter | Examining a crystal, carrying a flag, dancing, reading, painting, holding a hose |

These scenes reuse the existing subject, action and object cards, with new
scene-specific place cards such as **beside the barn**, **along the path** and
**beside the moon display**. The scene picker shows the subject and card counts.

## Cards and controls

The Level 1 deck now has **108 cards**, including **60 newly added cards**:

| Kind | Total | Newly added |
| --- | --- | --- |
| Subjects | 22 | 10 |
| Actions | 22 | 10 |
| Objects | 22 | 10 |
| Places | 16 | 10 |
| Times | 16 | 10 |
| How | 5 | 5 |
| Why | 5 | 5 |

New subjects include a chef, firefighter, farmer, gardener, musician, scientist,
bear, fox, penguin and monkey. New props include soup, a hose, a pumpkin,
flowers, a drum, a crystal, berries, a butterfly, a sled and a banana. There
are 40 new original SVG illustrations, shared where cards show the same prop.

Use the mouse or native button keyboard navigation (Tab, Enter and Space).
In a mixed bank, **1–9** and **0** select the first ten cards in display order;
Tab and Enter reach every card, including those after the tenth. Category
letter shortcuts apply only while cards are grouped. They select the first,
second or third card of the corresponding pile:

| Pile | Keys |
| --- | --- |
| Subject | A / S / D |
| Predicate or verb | J / K / L |
| Object | Z / X / C |
| Where | Q / W / E |
| When | U / I / O |
| How | F / G / H |
| Why | V / B / N |

### Read aloud

**Read aloud**, beside *Your sentence*, reads the finished sentence in a
girl’s voice: the best installed US English female voice (Samantha on a stock
Mac), pitched up to sound young and at a slightly unhurried pace. A better female
voice installed under System Settings → Accessibility → Spoken Content →
Manage Voices is picked automatically. While it reads, the button becomes
**Stop**. In Levels 1 and 3 it waits
for a complete sentence; in Level 2 it reads the words placed so far, so you
can hear an attempt before checking it.

Double-click any card to hear its words: a picture card reads its phrase, a
word card its word, and the full-stop card says “full stop”. A new reading
replaces the one in progress. A double-click begins with an ordinary click, so
it still selects a picture card, or places (or returns) a Level 2 word. Its
second click is ignored, so only that word moves and it is the word you hear.

Speech runs locally through the Radiant speech service
(`vibe/radiant/Radiant_Design_Speech.md`). The demo is a local document, so it
may start audio from any handler (**D7.5.4**). Where no speech engine is
available, reading stays silent and the button returns to **Read aloud**.

### Further advanced pattern ideas

| Pattern | Example |
| --- | --- |
| Two objects: recipient + thing | Grandma gives the child a book. |
| Relative clause | The dog that chased the ball is tired. |
| Passive voice | The ball was caught by the dog. |
| Comparison | The fox runs faster than the bear. |

These are possible additions beyond the current manner, reason, purpose,
condition, contrast, and time-clause patterns.

The layout stacks the cards in narrow windows. The book stays in memory for
the current session. The direct entry point is still available:

```sh
./lambda.exe view test/demo/sentence-game/sentence-game.ls
```

## Files

| File | Purpose |
| --- | --- |
| `sentence-game.ls` | Reactive view instance, event handlers and read-aloud speech |
| `mod_game.ls` | Level selection, session routing and separate progress |
| `mod_game_scene.ls` | Level picker, word puzzles and picture workspace |
| `mod_model.ls` | Patterns, card catalogs, seeded deals, clause chains and game transitions |
| `mod_scene.ls` | Pattern picker, card table, sentence preview and finished book |
| `mod_puzzle.ls` | Scrambled lessons, card order, hints, checking and progression |
| `mod_rounds.ls` | Shared round snapshots, navigation and ordered saved entries |
| `mod_picture.ls` | Picture metadata, related cards and unlimited descriptions |
| `mod_art.ls`, `assets/` | SVG card illustrations and painted PNG scenes |
| `artwork/prompts.json` | Scene artwork prompts and source references |
| `sentence-game.css` | Responsive card-table theme |

The model returns immutable state values (**S1.4**). The view constructs its
document from those values; mutations happen in event handlers (**S12.1.3**).
Each deal includes a familiar match and other combinations. All illustrations
and styles are local, with no downloads or external services.

### Scene artwork

The six Level 3 scenes use full-resolution PNG artwork generated with the
built-in imagegen tool. They share a warm picture-book style with expressive
characters, natural poses, textured surfaces, and detailed lighting. The park
painting provides the style reference for the other scenes. Each scene
keeps the actors, actions and props used by its sentence cards.

The final assets are `assets/scene-{park,kitchen,beach,harvest,woodland,discovery}.png`.
Prompts, dimensions and source references are recorded in
[artwork/prompts.json](artwork/prompts.json). The original three scene SVGs
provide composition references for future redraws; the advanced scenes are new
compositions. Artwork is bundled locally; playing the game makes no
image-generation or network requests.

## Focused checks

The model golden covers the pattern flow, required objects, duplicates, all
conjunctions, mixed clauses, punctuation, capitalization, undo, eight-clause
chains, completion, restart and preservation of earlier states:

```sh
mkdir -p temp/sentence-game
./lambda.exe test/demo/sentence-game/tests/model_test.ls > temp/sentence-game/model-test-output.txt
diff -u test/demo/sentence-game/tests/model_test.txt temp/sentence-game/model-test-output.txt
```

The existing Level 1 native replays describe the five-sentence flow, patterns with objects, places
and times, conjunctions, undo, keyboard and pointer selection, and the narrow layout.
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

The read-aloud replay covers the sentence reading and its **Stop** toggle,
card double-clicks in Levels 1 and 2, and keyboard selection alongside them.
It uses the deterministic fake speech engine on offline audio output.
`lambda.exe demo` does not apply the fixture's `env` block, so set both
variables yourself; otherwise the real system voice speaks on wall time:

```sh
LAMBDA_SPEECH_ENGINE=fake LAMBDA_AUDIO_OUTPUT=offline ./lambda.exe demo sentence-game --headless \
  --event-file test/demo/sentence-game/tests/read_aloud.json \
  --event-result temp/sentence-game/read-aloud-result.json
```
