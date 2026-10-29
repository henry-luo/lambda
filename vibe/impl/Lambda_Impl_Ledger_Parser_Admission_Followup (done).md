# Ledger parser, admission and formatting follow-up

**Status:** done for this bounded ledger pass; both required aggregate gates pass. **Base:** `33d98cb08`.

Existing rulings: **S7.6.2v2 / S7.11.1v2–S7.11.2v2 / S16.9.5 /
S16.10.1v3 / S11.1.6v3 / S17.8.1 / D3.2.4v4 / D5.3.3**;
force-fragment spelling is **PTH33**. No ruling changes.

| Selected defect | Root-cause repair |
|---|---|
| LR10-20 | T0 abandons packed-map construction when a field sets a control signal. The old loop kept filling native float slots after propagation with placeholder values; LLDB identifies `Item::get_double` through `set_field_value` and `eval_map`. The original unsupported-TikZ-dimension example now returns its ordinary diagnostic. |
| LR10-11 | Interpreter depth exhaustion uses the same pre-reserved fault route as generated stack guards. An unused recursive call cannot turn the failure into error data and continue the boundary. |
| S16.9.5 map/pattern slice | `ShapeEntry` carries field-presence metadata independently of the value contract. Static checks, full/fast validation and map admission agree; missing optional fields remain absent keys, while present null still requires a nullable value type. JIT layout proofs require stored fields before using common offsets; missing slots cannot be accessed using the declared full shape. Structural contract equality also excludes map/element/array carrier-only matches: an admitted open record cannot prove the contract of an extra field when crossing into an optional-field record. |
| LR02-21 | Reserved words are excluded from ordinary value identifiers and retained as data names. Only `apply(...)` keeps its call reading. |
| LR02-22 | Explicit fragment/start-end scanner guards preserve tight `#name`, keyword, integer and symbol fragments while keeping `# name` as separate expressions. Shape checks prevent acceptance of the old silent split. |
| LR02-25 | Complete type patterns reject a tight integer brace that cannot continue a count; legal array/run suffixes, function signatures, method heads and spaced bodies retain their grammar paths. Declaration-return grammar remains separate. |
| LR09-32 | JSON compact/indent options reach a shared writer context. Maps, arrays, elements, nominal records and virtual maps use the same punctuation/indentation controls. Existing default formatting, complex-value rejection and virtual-failure checking remain in the shared dispatch. Invalid option values return an owned formatting error. |

Nominal object constructors still discard their optional-field marker, and
the element validator still treats every missing attribute as permitted. The
broader S16.9.5 item stays partial for those scopes. This pass fixes the
selected typed-map rejection, the map field/value distinction, and attribute
pattern metadata/present-value checks. The preserved attribute-residue probe
returns `[true, false, true]` for absent optional, present-null non-nullable,
and absent required-nullable attributes.

## Regression coverage and fixture migration

Seven paired positive scripts cover packed-map propagation, optional reads and
stores/COW, JSON indentation/compact output and errors, keyword data names,
and the original TikZ failure. Eight paired negative scripts cover unused
recursion exhaustion, four reserved values, nullable-versus-optional rejection,
and an open-record-to-optional-record crossing with an invalid extra field.
The all-tier qualification checks every new failure; the existing negative
harness also retains permanent boundary and parser regressions.

The first baseline exposed two legacy parser fixtures using newly rejected
reserved names and two schema fixtures using `T?` to permit missing fields.
Their sources now use legal parameter names and explicit `field?: T?`, with
all old assertions and schema goldens preserved. The chart dashboard changes
only four paint IDs and their references because the requested compact JSON
now takes effect. `chart-paint-identity-migration.json` proves each old/new ID
decodes to the same JSON; substitution matches the actual HTML byte for byte.
No renderer source or pixel expectation changed.

## Qualification

Both required aggregate gates passed. Temporary evidence is under
`temp/ledger-batch3-20261029/`; original triage probes are under
`temp/ledger-next-triage-20261029/`.

- Lambda baseline: **8441/8441** (input **2112/2112**, runtime **6329/6329**), `lambda-baseline-qualified.log`.
- Test262 baseline: **40261/40261**, zero regressions, `test262-baseline-qualified.log`.
- Production S16 harness: **442/442**, `c-s16-qualified.log`.
- Reference S16 harness: **428/428**, `ts-s16-qualified.log`, including tree-shape assertions for fragments.
- Release interpreter/JIT/AUTO checks, normal and forced/poisoned GC: **90/90**, `release-focused.json` (seven positive and eight negative cases per tier/GC combination).
- Final ASan interpreter/JIT checks with forced/poisoned GC: **14/14**, `asan-final.json`.
- MIR emission **279/279**, size ratchet **20/20**, negative runtime checks **291/291** in the final aggregate run; no budget changes.

The sandbox initially blocked localhost binding and reference-grammar loading;
the same checks run with access pass. The final Lambda gate uses localhost
access. Earlier failed/intermediate logs remain for provenance; only the final
receipts listed here qualify completion.

Pinned release SHA-256:
`5fa7e41f3779f4c81ba5ba79fbc4389349822efbe2811c8d96495b66b99eb6aa`.
Pinned ASan SHA-256:
`414bf73118e5006d6aa7bd0f39239527a5cc7d29203f1849f44b69a3d5420097`.
`binary-provenance.json` records the preserved base and qualified binaries;
`reviewed-manifest.json` records every changed source/fixture/document hash.
The formal index is regenerated; new document links and `git diff --check`
pass. Changes are uncommitted.
