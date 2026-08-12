# Ownership and Lifecycle Modeling

Survey of DSLs, formalisms, languages, and tools for modeling **data ownership** (who holds an object, who may alias it, who frees it) and **object lifecycle** (which abstract states an object passes through, and which operations are legal in each state). Written as a structured record of a design discussion; the Lambda-specific conclusions are in §7.

## 1. Motivation and Scope

The Lambda runtime enforces non-trivial ownership and lifecycle rules informally: three-tier string allocation (namepool / arena / GC heap), GC-managed vs `Input`-arena-owned containers, precise rooting via `RootFrame` / `Rooted<T>` / `PersistentRooted`, and destination-owned scalar storage. The normative rulings live in `doc/Lambda_Formal_Design.md` — **D4 Memory Management** and **D5 Execution State: Stacks and Rooting** — but they are stated in prose. The question this doc answers: what is the best existing formalism, DSL, or tool for stating such rules precisely, and optionally checking them?

The short answer, expanded below:

- There is no single winner. **Separation logic** is the formalism underneath every serious ownership tool; **typestate** is the term of art for statically-checked object lifecycle. Those two keywords unlock most of the literature.
- For *design-level modeling with mechanical checking*, **Alloy 6** is the sweet spot for heap-shaped ownership invariants; **TLA+** for protocol-shaped lifecycles.
- For *vocabulary to use in prose specs*, **Rust** (own / borrow / lifetime), **Pony reference capabilities**, and **region calculus** are the most precise compact vocabularies.
- For *code-level enforcement in C++*, the practical ceiling is the Core Guidelines lifetime profile, Clang consumed attributes, and the Rust typestate pattern hand-translated (state-per-type + `&&`-qualified transitions + `bugprone-use-after-move`).
- For *machine-checked proofs*, the separation-logic tools (VeriFast, RefinedC, CN, Viper) — months of effort, reserved for the trickiest core.

## 2. The Two Axes

**Ownership** answers: who is responsible for an object's storage, who may hold references to it, with what rights (read / write / transition / free), and how those rights transfer. Its failure modes are use-after-free, double-free, leaks, and stale aliases.

**Lifecycle** answers: which abstract states does an object pass through (built → initialized → active → released), and which operations are legal in each state. Its failure modes are protocol violations: read-after-close, use of a moved-from value, `build()` before required fields are set.

The two axes interlock: static lifecycle knowledge is invalidated by aliasing (a state transition through one reference silently stales the knowledge attached to another), so sound lifecycle checking *requires* ownership/aliasing control. This is the single most important technical fact in the whole space — it explains why typestate research (§5) and ownership research co-evolved, and why Rust is the language where both finally work together in the mainstream.

## 3. Prior Art: Modeling Data Ownership

### 3.1 Foundational formalisms

| Formalism | Origin | Core idea |
|---|---|---|
| Linear logic / linear types | Girard 1987; Wadler 1990 ("Linear types can change the world!") | A value must be used *exactly once*; consumption transfers it. The root of all substructural type systems. |
| Affine types | Substructural weakening of linear | *At most once* — values may be dropped. This is Rust's actual discipline. |
| Uniqueness types | Barendsen & Smetsers, Clean (~1993) | A uniquely-referenced value may be mutated in place safely; dual of linearity (guarantee about the past, not the future). |
| Region calculus | Tofte & Talpin 1994 | Memory is allocated in lexically-scoped regions; a value may not outlive its region; whole regions free at once. The formal model of **arena allocation** — matches Lambda's `Input` arena and pools almost exactly. |
| Ownership types | Clarke, Potter & Noble 1998 (OOPSLA) | Every object has an owner; owners-as-dominators confines an object graph inside its owner's boundary. The academic lineage behind the word "ownership". |
| Separation logic | Reynolds / O'Hearn / Ishtiaq 2001–02 | Hoare logic where assertions describe *disjoint* heap fragments; `x ↦ v` asserts ownership of one cell; the frame rule gives local reasoning. Ownership *is* resource. The formalism under Viper, VeriFast, RefinedC, CN, Infer. |
| Fractional permissions | Boyland 2003 | A permission can split into read-only fractions and recombine to a full write permission — the formal account of shared-xor-mutable (`&`/`&mut`, RwLock). |
| Concurrent separation logic / Iris | O'Hearn 2007; Jung et al. 2015+ | CSL extends ownership reasoning to concurrency; **Iris** is the modern Coq framework unifying it (ghost state, invariants). The research gold standard — verified GCs and runtimes are built on it (e.g. RustBelt). |

### 3.2 Languages where ownership is first-class

| Language | Status | Ownership model |
|---|---|---|
| **Rust** | Mainstream | Affine ownership + borrows (`&` shared-read / `&mut` unique-write) + named lifetimes checked by the borrow checker. The de-facto vocabulary standard: *own*, *borrow*, *move*, *outlive*. |
| **Cyclone** | Dead (research, ~2001–06) | Safe C dialect with region-typed pointers; direct ancestor of Rust's lifetimes. The best prior art for retrofitting regions onto C. |
| **Pony** | Niche | Reference capabilities `iso`/`trn`/`ref`/`val`/`box`/`tag` — a six-word deny-based vocabulary for exactly which alias+mutation rights a reference grants. The most compact precise notation for aliasing rights ever shipped. |
| **Mezzo** | Dormant (research, INRIA ~2013) | ML variant where the *permission* to use a value is tracked separately from the value; permissions duplicate, split, and transfer. |
| **Clean** | Niche | Uniqueness types for in-place mutation and I/O in a pure lazy language. |
| **Swift** | Mainstream | Since 5.9: noncopyable types (`~Copyable`), `consuming`/`borrowing` parameter modifiers — an opt-in affine subset grafted onto ARC. |
| **Vale** | Research | Generational references + region borrowing; explores memory safety without GC or pervasive borrow checking. |
| **Austral** | Research | Deliberately small systems language with linear types and capability-based effects; readable spec that doubles as a linear-types tutorial. |
| **Verona** | Research (Microsoft) | Region-based concurrent ownership: a region is owned by at most one behaviour at a time; aimed at safe infrastructure code. |
| **Ada/SPARK** | Industrial niche | SPARK added a borrow-checker-like ownership analysis for access (pointer) types (2019+), targeting proof-carrying embedded code. |
| **C++** | Mainstream, library-level | `unique_ptr`/`shared_ptr`/`weak_ptr` encode owner kinds as library types; move semantics approximate affine transfer but use-after-move is not rejected statically. Ownership is convention plus tooling (§3.5), not language guarantee. |

### 3.3 Code-level verification tools built on ownership formalisms

| Tool | Target | Notes |
|---|---|---|
| **Viper** (ETH Zurich) | Its own intermediate verification language | Permission-based IVL: `acc(x.f)` access predicates, fractional permissions, magic wands. The cleanest standalone "ownership DSL". Frontends: Prusti (Rust), Gobra (Go), VerCors (Java/concurrency), Nagini (Python). |
| **VeriFast** | C, Java | Separation-logic annotations in comments; predicates for open/closed handles, malloc blocks. Mature; used on real C (e.g. parts of embedded stacks). |
| **RefinedC** | C | Ownership + refinement types for C, foundationally proved in Coq/Iris (MPI-SWS). Research-grade rigor, research-grade effort. |
| **CN** | C | Separation-logic specifications for C from the Cerberus project (Cambridge); applied to pKVM (Android hypervisor) with Google. The most "industrial" of the SL-for-C generation. |
| **Iris** | Coq framework | Not a tool for end users; the foundation the above increasingly build on. RustBelt (soundness proof of Rust's core) is its flagship. |
| **Dafny** | Its own language | Dynamic frames instead of separation logic: ghost `Repr` set + `Valid()` predicate idiom models ownership explicitly; auto-verified by Z3. The gentlest on-ramp to ownership verification if modeling *beside* the code rather than in it. |
| **Frama-C / ACSL** | C | Contract language with memory predicates (`\valid`, `\separated`, `\initialized`); weaker at ownership transfer than SL tools but industrially packaged. |
| **Infer / Pulse** (Meta) | C, C++, ObjC, Java | Static analysis derived from separation logic (biabduction lineage); finds use-after-lifetime, leaks. Not a modeling DSL — a bug finder — but the highest-value/effort ratio in this table. |
| **Creusot / Kani / Miri** (Rust) | Rust | Deductive verifier (prophecy-based), bounded model checker, and UB interpreter respectively; listed for completeness as the Rust-side proof stack. |

### 3.4 Design-level modeling tools

| Tool | Fit | Notes |
|---|---|---|
| **Alloy 6** | Best for heap-shaped models | Relational logic + linear temporal operators; model owners, objects, references as relations; assert invariants ("every live container has exactly one owner", "nothing reachable from a root is freed") and the Analyzer finds counterexample heaps in seconds within a bounded scope. Hours to first counterexample, not months. |
| **TLA+ / PlusCal** | Best for protocol-shaped models | State machines + temporal properties, model-checked by TLC. Ownership must be hand-encoded as functions/relations — less ergonomic for heap shapes, unmatched for async/concurrent lifecycle protocols. |
| **P** (Microsoft) | Async state machines | Communicating state machines with model checking; used for device drivers and AWS services. Lifecycle-protocol oriented, not ownership. |
| **UML composition/aggregation** | Informal | Filled vs hollow diamond is a (vague) ownership notation; state machine diagrams for lifecycle. Prior art as *notation*, not as checking. |

### 3.5 Lightweight in-code checking for C++

| Mechanism | What it checks |
|---|---|
| C++ Core Guidelines lifetime profile (Sutter, P1179) | `[[gsl::Owner(T)]]` / `[[gsl::Pointer(T)]]` type annotations; clang-tidy (`cppcoreguidelines-*`) and MSVC's lifetime analysis flag dangling and owner-escape patterns. Shallow but shipping. |
| Clang consumed attributes | `[[clang::consumable]]`, `[[clang::callable_when]]`, `[[clang::set_typestate]]` — actual (three-state) typestate for C++; see §6.3. |
| clang-tidy `bugprone-use-after-move` | Patches the affine gap: flags use of moved-from objects. |
| Microsoft SAL | `_In_`, `_Out_`, `_Post_valid_`, handle-state annotations for C APIs; weak typestate at API boundaries. |
| Sanitizers (ASan, LSan) | Dynamic, not modeling — the runtime complement that catches what the static story above misses. |

## 4. Prior Art: Modeling Object Lifecycle

### 4.1 Statecharts and runtime state machines

The standard *notation* for lifecycle is the finite state machine, and its canonical enrichment is the **Harel statechart** (1987): hierarchy (nested states), orthogonal regions, history states. UML State Machines standardized it; **SCXML** is the W3C interchange format; **XState** (JS) is the dominant executable statechart library; Boost.SML / Boost.MSM and Spring StateMachine are the C++/Java runtime equivalents; **Mermaid `stateDiagram-v2`** renders statecharts directly in markdown docs. All of these hold the state in a runtime variable and catch violations (at best) at runtime — they are models and executors, not static checkers.

### 4.2 Protocol modeling and model checking

Where the lifecycle is a *protocol* — especially concurrent or distributed — the model-checking family applies: **TLA+** (temporal properties over state machines), **SPIN/Promela** (LTL over communicating processes), **Ivy** (decidable-fragment protocol verification), **P** (executable communicating state machines), and **Alloy 6** (now with temporal operators, so lifecycle traces can be checked alongside structural invariants in one model).

### 4.3 Typestate

The static-enforcement branch of lifecycle modeling. Significant enough to get its own sections: §5 (concept) and §6 (support).

### 4.4 Session types

Typestate applied to *communication channels* instead of objects: the channel's type evolves as the protocol advances (send → receive → choice → end). Binary session types are Honda 1993; multiparty session types are Honda–Yoshida–Carbone 2008. **Scribble** is the practical DSL: describe a multiparty protocol once, generate per-participant state-machine APIs (Java, Scala, Go, F#); **StMungo** bridges Scribble to the Mungo typestate checker; **Sing#** (Microsoft's Singularity OS) shipped channel contracts as a language feature in 2006 and remains the best systems-level prior art.

## 5. Typestate as a Semantic Concept

### 5.1 Definition

Typestate extends a type with an *abstract state*: the type tracks not just what an object **is**, but what state it is **in**, so the set of legal operations changes as the object moves through its lifecycle. Operations declare which states they accept (precondition state) and which state they leave the object in (postcondition state / transition). A checker tracks each object's state flow-sensitively through the program and rejects, at **compile time**, any path that could invoke an operation in a wrong state. Plain type systems answer "does this operation exist on this type?"; typestate answers "does it exist *now*?".

### 5.2 Origin and history

The term and concept are from **Strom & Yemini, "Typestate: A Programming Language Concept for Enhancing Software Reliability" (IEEE TSE, 1986)**, introduced in IBM's NIL language and carried into its successor **Hermes**. The motivating examples were exactly the durable ones: uninitialized variables, files that must be opened before reading, resources that must be released once. The idea lay dormant through the 1990s and was revived by Microsoft Research's Vault (2001) and Fugue (2004), CMU's Plaid (2009), and finally reached mainstream practice as the Rust "typestate pattern" (~2015+).

### 5.3 The checking model

A typestate system attaches a finite state machine to a type: a set of abstract states, and per-operation transition signatures (e.g. Clang's `callable_when("unconsumed")` + `set_typestate(consumed)`). The checker performs a flow-sensitive dataflow analysis: at every program point, every tracked reference has a known (set of) state(s); calls transition it; control-flow joins must reconcile branch states (merge to a state set or reject); loops must reach a fixpoint. The analysis is necessarily **conservative**: it may reject correct programs it cannot prove safe, and it requires the state abstraction to be finite and statically trackable. Transitions that depend on runtime data exceed plain typestate and need dependent types (§6.2, Idris/ATS).

### 5.4 Typestate vs state machine

Every typestate system has a state machine inside it; the difference is where the state lives and when violations are caught. **State machine is to typestate what dynamic typing is to static typing** — the same underlying notion, moved from runtime to compile time.

| | Runtime state machine | Typestate |
|---|---|---|
| What it is | A behavioral **model** (states, events, transitions), usually also executed | A **static enforcement mechanism** for that model |
| Where state lives | A runtime field (`conn->status`, an enum) | The static type of each reference at each program point; can compile away entirely (zero-cost) |
| What is checked | The object's behavior as it executes | All *possible* uses of the object in the source, before running |
| Failure mode | Runtime error / exception / UB — if a check was written | Compile error; the bad program is rejected |
| Aliasing | Irrelevant — the field is the single truth any alias reads | The breaking problem — needs ownership/linearity to be sound (§5.5) |
| Expressiveness | Unbounded, data-dependent state; no proof of caller correctness | Finite abstraction, conservative; but *proves* every caller respects the protocol |

They compose rather than compete: draw the lifecycle as a statechart in the design doc (the spec of record), then enforce as much of it as the implementation language allows as typestate in the code (the enforcement shadow).

### 5.5 The aliasing problem — why typestate needs ownership

If two references point to the same object and one of them transitions its state (closes it, frees it, moves out of it), the static knowledge attached to the other reference is silently stale — the checker would approve a `read()` on a file some alias already closed. Sound typestate therefore requires that transition rights be **unique**: linearity/affinity (Rust: transitions take `self` by value), borrow discipline (no live aliases across a transition), or scoped alias-recovery mechanisms (Vault's adoption/focus, Plaid's access permissions, Pony's `iso`). This is the deep reason ownership (§3) and lifecycle (§4) are one design space, and why typestate never worked well in unrestricted-aliasing languages like Java and C++ — the checkers either go unsound, go conservative to the point of uselessness, or restrict themselves to narrow idioms (§6.3).

### 5.6 Boundaries and relatives

Session types are typestate for channels (§4.4). Typestate with runtime-data-dependent transitions escalates to indexed/dependent types (Idris 2's linear `ST`/protocol idioms, ATS views). Contract systems (Eiffel, ACSL, Dafny pre/postconditions over a ghost state field) subsume typestate expressiveness but lose its push-button decidability. Typestate is also distinct from *effect systems* (which track what a computation does, not what state an object is in), though the two meet in capability-effect designs like Austral's.

## 6. Typestate Support in Languages and Tools

### 6.1 Research languages designed around typestate

| Language | Era | Contribution |
|---|---|---|
| **NIL / Hermes** (IBM) | 1980s | Origin of the concept; typestate as the core reliability mechanism of a distributed-systems language. |
| **Vault** (MSR: DeLine, Fähndrich) | 2001 | Typestate ("keys") for C-like systems code; **adoption/focus** as the first practical answer to aliasing. |
| **Fugue** (MSR) | 2004 | First typestate checker for an OO mainstream setting (.NET annotations); documented the OO-specific problems (subclassing, aliasing). |
| **Sing#** (Microsoft Singularity) | 2006 | Channel contracts — session-typed IPC — in a real (research) OS; typestate at the systems level. |
| **Plaid** (CMU, Aldrich et al.) | 2009–14 | **Typestate-oriented programming**: objects change class as they change state (`open File` and `closed File` are different things with different members); access permissions integrated in the type system. The purest embodiment. |
| **Obsidian** (CMU) | ~2020 | Typestate + linear assets for smart contracts; notable for real usability studies of typestate with working programmers. |
| **Mungo / StMungo** (Glasgow) | 2016+ | Typestate protocols for Java objects, with a bridge from Scribble session protocols. |

### 6.2 Mainstream languages — encodings and native support

| Language | Support | Notes |
|---|---|---|
| **Rust** | Encoding (the "typestate pattern"), sound | Each state a distinct type (often a zero-sized generic parameter: `Conn<Open>`); transitions consume `self` and return the new state's type. No dedicated checker — affine moves + borrow checking make the ordinary type checker enforce the protocol, at zero runtime cost. The practical best-in-class today. |
| **Idris 2 / ATS** | Native-adjacent (linear + dependent types) | Strictly more expressive: transitions may depend on runtime values (`ST` indexed protocols, ATS views). Research-adjacent ergonomics. |
| **Haskell** | Encoding | Indexed monads, GADTs, phantom state parameters; `LinearTypes` (GHC 9.0+) closes the aliasing gap in principle; ergonomics remain heavy. |
| **Swift** | Encoding (5.9+) | Noncopyable types (`~Copyable`) + `consuming` methods enable the Rust pattern with compiler-enforced consumption. |
| **C++** | Encoding, *almost* sound | State-per-type + `&&`-qualified transition methods (must `std::move` to transition) + `[[nodiscard]]` on returned states. Gap: the language cannot reject use of the moved-from object; patched in practice by clang-tidy `bugprone-use-after-move`. Plus the Clang consumed attributes (§6.3) for the narrow three-state case. Worked example: §6.5. |
| **TypeScript** | Weak encoding | Discriminated unions + narrowing give state-dependent members, but mutation and aliasing make it unsound as enforcement; fine as documentation-grade typing. XState covers the runtime side. |
| **Java / Kotlin / C#** | None native | Unrestricted aliasing; support arrives only via external checkers (§6.3). Fluent-builder APIs with generic phantom states are the folk encoding. |

### 6.3 Checkers and analysis tools

| Tool | Target | Model |
|---|---|---|
| **Clang consumed attributes** | C++ | `[[clang::consumable(...)]]` on the class; `[[clang::callable_when("unconsumed")]]`, `[[clang::set_typestate(consumed)]]`, `return_typestate`, `param_typestate`, `test_typestate` on members. Exactly three states (unconsumed / consumed / unknown), intraprocedural, flow-sensitive. Shipping today; shallow but real. |
| **Checker Framework** (Java) | Java | The Called-Methods checker (`@CalledMethods`) and Resource-Leak checker implement an *accumulation analysis* (Kellogg et al. 2022) — a monotone fragment of typestate that stays sound **without** aliasing control. Deployed at scale (AWS, Meta); the most successful industrial typestate descendant. |
| **Infer Topl** (Meta) | Java (on Pulse) | The closest thing to "write the automaton, check the code": temporal properties written as small state-machine DSL programs, checked over inferred program traces. |
| **Fugue** | .NET | Historical (see §6.1) — the prototype for annotation-driven typestate checking. |
| **Microsoft SAL** | C | Handle-state and init-state annotations checked by MSVC `/analyze`; typestate-lite at API boundaries. |
| **CodeQL / Semgrep** | Any | API-protocol rules ("`lock()` must reach `unlock()`") approximate typestate as queries; no soundness, high reach. |
| **Scribble toolchain** | Multi-language | Protocol-as-DSL, generating session-typed (i.e. channel-typestate) endpoint APIs. |

### 6.4 What "best" means, per goal

| Goal | Best current answer |
|---|---|
| Write production code with enforced typestate | Rust (pattern + affine types); Swift 5.9+ as runner-up |
| Express a protocol as an explicit spec and check code against it | Infer Topl (Java); Scribble for multiparty protocols; nothing good exists for C++ |
| Enforce lifecycle in C++ specifically | State-per-type pattern + `&&`-qualified transitions + `[[nodiscard]]` + clang-tidy `bugprone-use-after-move`; Clang consumed attributes where the 3-state model fits (worked example: §6.5) |
| Model and check lifecycle at design level | Alloy 6 (structural + temporal), TLA+ (protocol-shaped) |
| Document lifecycle | Harel statecharts, Mermaid `stateDiagram-v2` |
| Study the concept | Strom & Yemini 1986; Plaid papers (Aldrich et al., "Typestate-Oriented Programming", Onward! 2009) |

### 6.5 Worked example: the typestate pattern in C++

Expansion of the C++ row of §6.2 — the two concrete representations, the mechanics that make them enforce state, and the design questions they raise (inheritance, common base, runtime-unknown state).

#### 6.5.1 Representation 1: sibling state structs

Each state is a **separate, unrelated type** holding only the data valid in that state; the transition consumes one state (`&&`-qualified, so the caller must `std::move`) and constructs the next from the surviving fields:

```cpp
struct ClosedFile {
    OpenFile open() &&;          // reopen: Closed → Open
private:
    Str* path;                   // all a closed file is, is a path
    friend struct OpenFile;
};

struct OpenFile {
    Data read();                 // exists only on the Open type
    ClosedFile close() && {
        ::close(fd);
        return ClosedFile{path}; // fd does not survive the transition
    }
private:
    int fd;                      // only exists while open
    Str* path;
};
```

The per-state representation difference (`fd` exists only in `OpenFile`) is itself documentation. Degenerate case: if nothing can be done with a closed file, `ClosedFile` can be an empty `[[nodiscard]]` receipt struct, or omitted entirely (`void close() &&`) — a distinct state type earns its place only when there are onward transitions or data to carry.

#### 6.5.2 Representation 2: phantom state parameter (C++20)

The direct translation of Rust's `File<Open>` / `PhantomData` encoding — one class template, one representation, states as empty tag types, and per-state method sets carved out by trailing `requires` clauses:

```cpp
struct Open {}; struct Closed {};

template <typename State>
class File {
    int fd; Str* path;
public:
    Data read()             requires std::same_as<State, Open>;
    File<Closed> close() && requires std::same_as<State, Open>;
    File<Open>   open()  && requires std::same_as<State, Closed>;
};
```

`std::same_as<A, B>` is a C++20 **concept** (`<concepts>`): a compile-time predicate that holds exactly when `A` and `B` are the same type — the concept-world successor of the trait `std::is_same_v` (defined symmetrically as `is_same_v<A,B> && is_same_v<B,A>` so the compiler may treat `same_as<A,B>` and `same_as<B,A>` as interchangeable during constraint subsumption).

Its role here is the entire enforcement mechanism: a trailing `requires` clause on a member of a class template makes that member **exist only when the constraint holds** for the instantiation at hand. For `File<Open>` the constraint is satisfied and `read()` is a real member; for `File<Closed>` it is *removed from the interface* — absent from overload resolution, not merely erroring inside a body. A wrong-state call diagnoses as "constraints not satisfied: `State` = `Closed`, required `same_as<State, Open>`" at the call site, which is precisely the typestate semantics: *the operation does not exist in this state*. This beats a `static_assert(std::is_same_v<State, Open>)` in the body, which fires only at body instantiation, points the error inside the function, and leaves the method visible to overload sets and IDE completion.

Crucially, `File<Open>` and `File<Closed>` remain **unrelated types** — distinct template instantiations have no subtype relation in C++ — which is exactly the property the pattern needs (see §6.5.3) and the reason this encoding is safe while inheritance is not. Prefer this form when many states share an identical representation; prefer sibling structs (§6.5.1) when states carry different data.

#### 6.5.3 No inheritance between states, no common public base

`ClosedFile` must **not** extend `OpenFile`: it would inherit `read()`, and substitutability (LSP) would let any `OpenFile&` parameter accept a closed file — the precise bug the pattern exists to make unrepresentable. Subtyping says "everything the base can do, the derived can also do"; typestate states *differ* in what they can do, so states are siblings, never ancestors.

A common **public** base (`class File` with `OpenFile`/`ClosedFile` derived) fails for two distinct reasons: (a) **state erasure** — a `File&`/`File*` means "a file in some state I don't statically know", so every operation needs a runtime `is_open()` check again, rebuilding the runtime state machine while still paying the typestate boilerplate; and (b) **aliasing across transitions** — a `File&` bound before `std::move(f).close()` remains live after it, a stale-state alias, which is the §5.5 soundness problem reintroduced via the base class. This holds even for an empty base: the reference-level aliasing alone breaks it.

Legitimate sharing needs have non-inheritance answers: shared representation and always-legal operations (e.g. `path()`) go in a common **member struct** (composition), a private-inheritance detail, or a CRTP mixin — none of which create a convertible-to-base relationship; the phantom-parameter form (§6.5.2) shares by construction.

#### 6.5.4 Runtime-unknown state: sum type, not supertype

When code genuinely cannot know the state statically (mixed-state containers, state decided by input), the correct type is a **closed sum**, not an open hierarchy: `std::variant<OpenFile, ClosedFile>`. Inheritance offers an open set of subtypes plus silent substitutability (both wrong here); a variant offers a closed set plus exhaustive matching at each use site (both right — a typestate FSM has a fixed, known set of states). After matching, the caller holds a concrete state type again and is back inside the static protocol. Rust does the same: `File<Open>`/`File<Closed>` share no supertype, and runtime-state code writes `enum AnyFile { Open(File<Open>), Closed(File<Closed>) }`. (In the Lambda codebase, where `std::` types are off-limits, the sum type is a hand-rolled tagged union — exactly the runtime-wide `Item`/`TypeId` idiom, so the spelling is already native.)

#### 6.5.5 C++17 spellings and the affine gap

Lambda is C++17, where `requires` is unavailable. The pre-C++20 spellings of conditional member existence are: SFINAE via a re-dependent defaulted parameter (the condition must depend on the *function's own* template parameters to SFINAE away, hence `S = State`):

```cpp
template <typename S = State, typename = std::enable_if_t<std::is_same_v<S, Open>>>
Data read();
```

or a `static_assert` in the body (simpler, weaker — method stays visible), or explicit specializations of `File<Open>` / `File<Closed>` listing only their own methods (no metaprogramming, some duplication). All are noisier than the C++20 form, which is why sibling structs (§6.5.1) are usually the cleaner choice in a C++17 codebase — each state type declares only its own operations, so conditional existence comes for free. (`same_as` / `is_same_v` are compile-time-only with zero runtime footprint, so the usual runtime-cost argument against `std::` machinery does not apply — but the sibling-struct form sidesteps the style question entirely.)

Either representation shares the C++ soundness caveat from §6.2: after `auto closed = std::move(f).close();` the moved-from `OpenFile f` still sits in scope — C++ cannot end its lifetime early. That lingering husk is the affine-types gap, patched in practice by clang-tidy `bugprone-use-after-move` plus `[[nodiscard]]` on transition results so a returned state cannot be dropped silently.

## 7. Application to Lambda

The runtime's rules are already typestate-and-ownership shaped; the D-rulings are the spec of record (D4 Memory Management, D5 Execution State: Stacks and Rooting). What the survey suggests, in increasing order of investment:

1. **Vocabulary now.** State rulings in Rust/separation-logic vocabulary — "X *owns* Y", "Y *borrows* Z and may not outlive it", "transition consumes the handle" — so the prose is unambiguous without adopting any tool. Region-calculus terms ("value may not outlive its region") state the `Input`-arena and pool rules exactly.
2. **Statecharts per runtime object kind.** A Mermaid `stateDiagram-v2` in the design docs for each kind, e.g. GC container: `built → rooted → reachable ⇄ unreachable → collected`; Input-owned Mark data: `building → sealed (immutable) → freed-with-arena`; `Rooted<T>` handle: valid only within its `RootFrame` scope. Each chart annotated with the operations legal per state (dereference, mutate, root, free). This is typestate as a documentation discipline — no checker required, and it directly captures rules like "a GC container is dereferenceable only while rooted or reachable" (D5).
3. **Alloy for the ownership invariants.** A small Alloy 6 model of owners (GC heap, `Input` arena, pool, namepool), objects, references, and roots, asserting e.g. exactly-one-owner, no-object-outlives-its-owner, everything-reachable-from-a-RootFrame-is-live. Bounded counterexample search is cheap and finds design-level holes (e.g. an object handed from arena to GC ownership mid-lifecycle) before they become C++ bugs.
4. **C++-level enforcement where the pattern fits.** For handle-like types with clear consume points (builders, `Rooted` guards, transferable buffers): state-per-type with `&&`-qualified transitions + `[[nodiscard]]`, `bugprone-use-after-move` in the lint sweep, and Clang consumed attributes where three states suffice.
5. **Heavy verification only if ever warranted.** VeriFast / RefinedC / CN-grade proof effort is justified at most for the rooting/GC core (D5), not the runtime at large.

## 8. Key References

- R. Strom, S. Yemini. *Typestate: A Programming Language Concept for Enhancing Software Reliability.* IEEE TSE 12(1), 1986.
- J. Girard. *Linear Logic.* TCS 50, 1987; P. Wadler. *Linear types can change the world!* 1990.
- M. Tofte, J.-P. Talpin. *Region-Based Memory Management.* 1994/1997.
- D. Clarke, J. Potter, J. Noble. *Ownership Types for Flexible Alias Protection.* OOPSLA 1998.
- J. Reynolds. *Separation Logic: A Logic for Shared Mutable Data Structures.* LICS 2002; P. O'Hearn. *Resources, Concurrency, and Local Reasoning.* 2007.
- J. Boyland. *Checking Interference with Fractional Permissions.* SAS 2003.
- R. DeLine, M. Fähndrich. *Enforcing High-Level Protocols in Low-Level Software* (Vault). PLDI 2001; *Typestates for Objects* (Fugue). ECOOP 2004.
- J. Aldrich, J. Sunshine, D. Saini, Z. Sparks. *Typestate-Oriented Programming.* Onward! 2009 (Plaid).
- K. Honda, N. Yoshida, M. Carbone. *Multiparty Asynchronous Session Types.* POPL 2008; Scribble: scribble.org.
- R. Jung et al. *Iris / RustBelt.* POPL 2015–2018.
- P. Müller, M. Schwerhoff, A. Summers. *Viper: A Verification Infrastructure for Permission-Based Reasoning.* VMCAI 2016.
- M. Kellogg et al. *Accumulation Analysis.* ECOOP 2022 (the Checker Framework's sound-without-aliasing typestate fragment).
- H. Sutter. *Lifetime safety: preventing common dangling.* P1179; C++ Core Guidelines lifetime profile.
- Clang docs: *Consumed Annotation Checking* (attribute reference); clang-tidy `bugprone-use-after-move`.
- D. Harel. *Statecharts: A Visual Formalism for Complex Systems.* Sci. Comput. Program. 1987.
- Alloy 6: alloytools.org; L. Lamport. *Specifying Systems* (TLA+).
