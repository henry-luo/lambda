# Radiant Form & FormData WPT Conformance

**Goal**: Bring Radiant's HTML form element IDL and the `FormData` constructor API to a state where the core WPT test suites in `ref/wpt/html/semantics/forms/` and `ref/wpt/xhr/formdata/` can pass, without touching the visual rendering pipeline or implementing actual form submission.

**Tracking test**: `test/wpt/test_wpt_form_gtest.exe` (added alongside this doc).

---

## §1  Current State

### §1.1  What exists

| Component | Location | Status |
|-----------|----------|--------|
| Layout geometry | `radiant/layout_form.cpp` | ✅ intrinsic sizes, positioning |
| Visual rendering | `radiant/render_form.cpp` | ✅ checkbox, radio, text, select, etc. |
| `FormControlProp` struct | `radiant/form_control.hpp` | ✅ `value`, `checked`, `disabled`, `required`, `current_value`, selection range |
| `input.value` getter/setter | `lambda/js/js_dom.cpp` ~3295, ~3650 | ✅ text / textarea |
| `input.checked` getter/setter | `lambda/js/js_dom.cpp` ~3278, ~3639 | ✅ checkbox / radio |
| `input.defaultValue` getter/setter | `lambda/js/js_dom.cpp` ~3302, ~3690 | ✅ |
| `input.disabled` getter | `lambda/js/js_dom.cpp` ~3281 | ✅ read-only |
| `input` / `change` / `submit` events | `lambda/js/js_dom_events.cpp` ~1602, ~1623 | ✅ fired by event bridge |
| Selection range API | `lambda/js/js_dom.cpp` ~2737 | ✅ `selectionStart/End/Direction`, `setSelectionRange()` |

### §1.2  Gap analysis

The following IDL members are **missing or incomplete** in `lambda/js/js_dom.cpp`, causing the majority of WPT failures:

#### A. `HTMLFormElement` IDL (spec: §4.10.3)
| Property / Method | Gap |
|-------------------|-----|
| `form.elements` | Not implemented — WPT tests `form-elements-*.html` all fail |
| `form.length` | Not implemented |
| `form.action` / `form.method` / `form.enctype` / `form.encoding` / `form.target` / `form.acceptCharset` / `form.autocomplete` / `form.noValidate` | Not implemented (attribute reflection) |
| `form.submit()` | Not implemented (headless: no navigation needed, just fire `submit` event) |
| `form.reset()` | Not implemented — fires `reset` event + resets all controls to default |
| `form.checkValidity()` | Not implemented |
| `form.reportValidity()` | Not implemented |
| `document.forms` | Not implemented — live `HTMLFormControlsCollection` |
| Named access via `document.forms.formId` | Not implemented |

#### B. `HTMLInputElement` IDL (spec: §4.10.18)
| Property / Method | Gap |
|-------------------|-----|
| `input.type` getter/setter | Partially — getter via getAttribute, setter (type switch algorithm) not implemented |
| `input.defaultChecked` getter/setter | Not implemented — `checked` content attribute |
| `input.indeterminate` getter/setter | Not implemented |
| `input.name` getter/setter | Not implemented |
| `input.required` getter/setter | Read-only via attribute; setter missing |
| `input.disabled` setter | Missing (only getter) |
| `input.readOnly` getter/setter | Missing |
| `input.placeholder` getter/setter | Missing |
| `input.maxLength` / `input.minLength` | Missing |
| `input.min` / `input.max` / `input.step` | Missing |
| `input.pattern` | Missing |
| `input.size` | Missing |
| `input.multiple` | Missing |
| `input.autocomplete` | Missing |
| `input.form` (back-ref to owning `<form>`) | Missing |
| `checkValidity()` / `reportValidity()` | Missing |
| `setCustomValidity(msg)` | Missing |
| `willValidate` | Missing |
| `validity` (`ValidityState`) | Missing |
| `validationMessage` | Missing |
| `select()` | Missing |
| `labels` | Missing |
| `valueAsNumber` / `valueAsDate` / `stepUp()` / `stepDown()` | Missing |

#### C. `HTMLSelectElement` IDL (spec: §4.10.7)
| Property / Method | Gap |
|-------------------|-----|
| `select.value` getter/setter | Not wired through `FormControlProp.selected_index` |
| `select.selectedIndex` getter/setter | Missing |
| `select.options` / `select.selectedOptions` | Missing |
| `select.multiple` getter/setter | Missing |
| `select.size` getter/setter | Missing |
| `select.name` / `select.disabled` / `select.required` | Missing |
| `select.form` | Missing |
| `select.add()` / `select.remove()` | Missing |
| `checkValidity()` / `reportValidity()` / `setCustomValidity()` | Missing |
| `select.length` | Missing |

#### D. `HTMLTextAreaElement` IDL (spec: §4.10.11)
| Property / Method | Gap |
|-------------------|-----|
| `textarea.defaultValue` getter/setter | Partially implemented |
| `textarea.rows` / `textarea.cols` / `textarea.wrap` | Missing |
| `textarea.name` / `textarea.disabled` / `textarea.required` / `textarea.readOnly` | Missing |
| `textarea.placeholder` | Missing |
| `textarea.maxLength` / `textarea.minLength` | Missing |
| `textarea.textLength` | Missing |
| `textarea.form` | Missing |
| `checkValidity()` / `reportValidity()` / `setCustomValidity()` | Missing |

#### E. `HTMLButtonElement` IDL (spec: §4.10.6)
| Property / Method | Gap |
|-------------------|-----|
| `button.value` / `button.name` / `button.type` | Missing |
| `button.disabled` setter | Missing |
| `button.form` | Missing |
| `checkValidity()` / `reportValidity()` | Missing |

#### F. `HTMLFieldSetElement` / `HTMLLabelElement` / `HTMLOutputElement` IDL
| Property / Method | Gap |
|-------------------|-----|
| `fieldset.disabled` — disables all descendant controls | Not implemented |
| `fieldset.elements` | Missing |
| `label.htmlFor` / `label.control` | Missing |
| `label.form` | Missing |
| `output.value` / `output.defaultValue` / `output.name` | Missing |

#### G. Constraint Validation API (spec: §4.10.20, applies to all form controls)
| API | Gap |
|-----|-----|
| `ValidityState` object with all flags (`valueMissing`, `typeMismatch`, `patternMismatch`, `tooLong`, `tooShort`, `rangeOverflow`, `rangeUnderflow`, `stepMismatch`, `badInput`, `customError`, `valid`) | Not implemented |
| `checkValidity()` on form controls + form | Not implemented |
| `reportValidity()` | Not implemented |
| `setCustomValidity(msg)` | Not implemented |
| `willValidate` | Not implemented |
| `invalid` event (fires on failed `checkValidity`) | Not implemented |

#### H. `FormData` API (spec: §XHR §2.6 / Fetch spec)
| API | Gap |
|-----|-----|
| `new FormData()` constructor | Not implemented — no `FormData` global |
| `new FormData(form)` constructor | Not implemented |
| `fd.append(name, value)` / `fd.append(name, blob, filename)` | Not implemented |
| `fd.set(name, value)` | Not implemented |
| `fd.get(name)` / `fd.getAll(name)` | Not implemented |
| `fd.has(name)` | Not implemented |
| `fd.delete(name)` | Not implemented |
| `fd.entries()` / `fd.keys()` / `fd.values()` / `for...of` | Not implemented |

---

## §2  Proposed Implementation Phases

### F-0: Attribute reflection helpers (prerequisite for all phases)

**Files**: `lambda/js/js_dom.cpp`

Add a shared `_reflect_string_attr(elem, attr)` helper that does attribute-backed getter/setter for string-type IDL attributes (returns `""` when absent, per spec). Use it to implement the most-tested reflected attributes in a single pass:

- `HTMLInputElement`: `name`, `type` (getter only at this stage), `placeholder`, `autocomplete`, `pattern`, `min`, `max`, `step`, `accept`, `required`, `readOnly` / `readonly`, `disabled` setter, `multiple`, `size` (reflected as integer).
- `HTMLSelectElement`: `name`, `disabled`, `required`, `multiple`, `size`.
- `HTMLTextAreaElement`: `name`, `rows`, `cols`, `wrap`, `placeholder`, `disabled`, `required`, `readOnly`, `maxLength`, `minLength`.
- `HTMLButtonElement`: `name`, `type`, `value`, `disabled`.
- `HTMLFormElement`: `action`, `method`, `enctype`, `encoding` (alias for enctype), `acceptCharset`, `autocomplete`, `noValidate`, `target`.

**WPT unlocked**: `attributes-common-to-form-controls/`, `the-input-element/input-types.js`, many attribute reflection tests.

### F-1: `HTMLFormElement.elements` + `form.length` + named item access

**Files**: `lambda/js/js_dom.cpp`

`form.elements` returns an `HTMLFormControlsCollection` — a live ordered list of the form's listed elements (input, button, select, textarea, fieldset that are descendants or form-associated, excluding `type=image` for certain operations).

For Lambda's headless runtime a **snapshot array** wrapped in a JS Array-like proxy is sufficient for all currently-skippable WPT tests. Implement:

- `form.elements` → walk DOM subtree, collect all `<input>`, `<button>`, `<select>`, `<textarea>`, `<object>` that are form-owned, return as JS array.
- Named access: `form.elements["name"]` → single element or `RadioNodeList` (array) for same-name radio/checkbox groups.
- `form.length` → `form.elements.length`.
- `document.forms` → live collection of all `<form>` elements; named access `document.forms["id"]`.

**WPT unlocked**: `form-elements-*.html`, `form-elements-sameobject.html`, `form-indexed-element.html`, `form-checkvalidity.html`.

### F-2: `input.defaultChecked`, `input.indeterminate`, `input.form`

**Files**: `lambda/js/js_dom.cpp`

- `input.defaultChecked` getter: returns whether `checked` HTML attribute is present.  
  `input.defaultChecked` setter: adds/removes `checked` HTML attribute; does NOT change `.checked` state if it was already set by user.
- `input.indeterminate` getter/setter: stored in `FormControlProp` (add a 1-bit field `indeterminate`); no HTML attribute reflection.
- `input.form` (and same for button/select/textarea/output): walk ancestors to find nearest `<form>`, or look up `form` attribute as ID; return wrapped DomElement for the form or `null`.

**WPT unlocked**: `clone.html` (defaultChecked retention), `checkbox.html` partial, `form-control-infrastructure/` partial.

### F-3: `form.reset()` + reset algorithm

**Files**: `lambda/js/js_dom.cpp`

Implement `form.reset()`:
1. Fire `reset` event (bubbles, cancelable) on the form element.
2. If not cancelled, for each listed control:
   - `<input type=text|search|tel|url|email|password>`: set `current_value` to the `value` content attribute (or `""` if absent); clear dirty flag.
   - `<input type=checkbox|radio>`: set `.checked` = `.defaultChecked`; clear dirty flag.
   - `<textarea>`: set `current_value` to text content (`.defaultValue`).
   - `<select>`: restore each option's `.selected` to `.defaultSelected`.
   - `<output>`: restore to `defaultValue`.

**WPT unlocked**: `resetting-a-form/reset.html`, `resetting-a-form/reset-event.html`.

### F-4: Constraint Validation API (`ValidityState`)

**Files**: `lambda/js/js_dom.cpp` (new section), `radiant/form_control.hpp` (add `custom_validity_msg`)

Implement the `ValidityState` interface as a plain JS object constructed on-demand:

```
{
  valueMissing, typeMismatch, patternMismatch,
  tooLong, tooShort, rangeOverflow, rangeUnderflow,
  stepMismatch, badInput, customError, valid
}
```

Each flag is computed from `FormControlProp` state:

- `valueMissing`: `required && value == ""`
- `tooLong`: `maxLength >= 0 && utf16_len(value) > maxLength` (only when value dirty)
- `tooShort`: `minLength > 0 && utf16_len(value) < minLength && value != ""`
- `patternMismatch`: `pattern` attribute set + RE2 match fails
- `typeMismatch`: type-specific validity (email, url, color)
- `rangeOverflow`/`rangeUnderflow`/`stepMismatch`: numeric/date types only
- `badInput`: can signal the browser has a partial value (always `false` in headless)
- `customError`: `custom_validity_msg != ""`
- `valid`: all other flags false

Add to `FormControlProp`: `char* custom_validity_msg` (heap-owned C string, `nullptr` = no custom error).

Implement:
- `elem.validity` getter → `ValidityState` object.
- `elem.checkValidity()` → if not valid, fire `invalid` event (not bubbles, not cancelable), return `false`.
- `elem.reportValidity()` → same as `checkValidity()` in headless (no UI).
- `elem.setCustomValidity(msg)` → set/clear `custom_validity_msg`.
- `elem.willValidate` → `true` for listed elements that are not disabled, `type=hidden`, `type=button`, `type=reset`, `type=submit`, `type=image`, or output elements.
- `elem.validationMessage` → `custom_validity_msg` or default message string.
- `form.checkValidity()` → call `checkValidity()` on each listed element; return `false` if any.
- `form.reportValidity()` → same.

**WPT unlocked**: `constraints/form-validation-*.html` (all ~20 files).

### F-5: `select.value`, `select.selectedIndex`, `select.options`

**Files**: `lambda/js/js_dom.cpp`

Wire `HTMLSelectElement` IDL to `FormControlProp`:

- `select.selectedIndex` getter: `form->selected_index` (−1 if no option selected).
- `select.selectedIndex` setter: update `form->selected_index`, update option `selected` attributes.
- `select.value` getter: `options[selectedIndex].value` (or `""`).
- `select.value` setter: find first option whose `.value` matches, set `selectedIndex`.
- `select.options` getter: snapshot array of `<option>` child elements.
- `select.length` getter: option count.
- `option.value` / `option.text` / `option.selected` / `option.defaultSelected` / `option.index`.
- `select.add(option, before)` / `select.remove(index)`.

**WPT unlocked**: `the-select-element/select-add.html`, `option-selectedness-script-mutation.html`, partial `select-ask-for-reset.html`.

### F-6: `FormData` constructor and methods

**Files**: `lambda/js/js_dom.cpp` (or new `lambda/js/js_formdata.cpp`)

`FormData` is a global constructor producing objects that store an ordered list of `(name: string, value: string | File)` entries. For WPT compliance without file upload:

**Internal representation**: store as a heap-allocated `ArrayList` of `{char* name, char* value, char* filename}` triples (no File support needed for basic WPT).

**Constructor**:
- `new FormData()` → empty entry list.
- `new FormData(form)` → build entry list by walking `form.elements`: for each listed control with a non-empty `name`, append `(name, value)`. Checkbox/radio only if `checked`; `type=submit`/`type=image` excluded.
- `new FormData(null)` / `new FormData("string")` → throw `TypeError` (per WPT `constructor.any.js`).

**Methods**:
- `append(name, value)` → push entry.
- `append(name, blob, filename)` → push File entry (headless: store as `{name, "", filename}`).
- `set(name, value)` → replace all entries with `name` with one new entry, then keep the rest.
- `get(name)` → first matching value or `null`.
- `getAll(name)` → array of all matching values.
- `has(name)` → boolean.
- `delete(name)` → remove all entries with name.
- `entries()` / `keys()` / `values()` → iterators over entry list.
- `forEach(callback)` → iterate entries.
- `Symbol.iterator` → same as `entries()`.

**Register as global**: add `"FormData"` constructor to `js_dom_install_globals()`.

**WPT unlocked**: `xhr/formdata/append.any.js`, `get.any.js`, `has.any.js`, `delete.any.js`, `set.any.js`, `foreach.any.js`, `iteration.any.js`, `constructor.any.js`, `constructor-formelement.html`, and several `-formelement.html` variants.

---

## §3  WPT Test Suite Scope

The new gtest `test/wpt/test_wpt_form_gtest.exe` covers two WPT directories:

### §3.1  `ref/wpt/html/semantics/forms/` (769 files total)

Targeted subdirectories for initial coverage:

| Subdirectory | Key files | Phase required |
|---|---|---|
| `the-form-element/` | `form-checkvalidity.html`, `form-elements-*.html`, `form-action-reflection.html`, `form-autocomplete.html` | F-0, F-1, F-4 |
| `resetting-a-form/` | `reset.html`, `reset-event.html` | F-3 |
| `constraints/` | `form-validation-checkValidity.html`, `form-validation-validity-*.html` | F-4 |
| `attributes-common-to-form-controls/` | attribute reflection tests | F-0 |
| `the-input-element/` (static subset) | `clone.html`, `button.html`, `input-type-button.html`, `input-type-change-value.html`, `cloning-steps.html`, `defaultValue-clobbering.html` | F-0, F-2 |
| `the-select-element/` (static subset) | `select-add.html`, `select-ask-for-reset.html`, `option-selectedness-script-mutation.html` | F-5 |
| `the-textarea-element/` | attribute reflection | F-0 |
| `the-fieldset-element/` | `fieldset-elements.html`, `disabled-fieldset.html` | F-0 |
| `the-label-element/` | `label-htmlfor.html`, `label-control.html` | F-0 |

**Skipped**: tests requiring `test_driver`, `async_test` with real user gestures (typing, focus), `requestAnimationFrame`, `iframe`, shadow DOM, form submission navigation, date/time picker UI.

### §3.2  `ref/wpt/xhr/formdata/` (18 files)

All `.any.js` tests targeted for coverage. The `-formelement.html` variants require a parsed form in the document (need `form.elements` from F-1). The `set-blob.any.js` and blob-related tests need a partial `Blob` constructor (just `size`, `type`, `name`, `lastModified` properties — no actual binary data).

---

## §4  Skipped Subtests (permanent)

The following categories cannot pass in Lambda's headless runtime and should remain in the skip list:

| Category | Reason |
|----------|--------|
| Tests using `test_driver.send_keys()` / `test_driver.click()` | `test_driver` is a WebDriver harness; not available headless |
| `async_test` tests that wait for user focus/typing (`change-set-value.html`, `email-set-value.html`, etc.) | Require real user input simulation |
| Form submission navigation (`form-action-submission.html`, etc.) | Lambda has no navigation/network in headless mode |
| Date/time input type UI (`datetime-local-valueasdate.html`, etc.) | Requires specialized date picker |
| `customizable-combobox/` | Custom element + popover + `::picker()` CSS; too new |
| Shadow DOM form association (`form-indexed-element-shadow.html`, `form-associated/`) | No shadow DOM |
| `constraints/support/validator.js` tests that dynamically set attributes mid-test | Already covered by simpler targeted tests |

---

## §5  Implementation Order

Suggested order based on WPT test unlock count per phase:

1. **F-6 (FormData)** — highest bang for effort: 8+ `.any.js` tests unlock immediately with no DOM dependency. Self-contained new file.
2. **F-0 (attribute reflection)** — unblocks all phases; many tests currently fail on `TypeError: Cannot read property 'name' of undefined`.
3. **F-1 (form.elements)** — unlocks `form-elements-*.html` suite (largest set after FormData).
4. **F-4 (ValidityState)** — unlocks all 20 constraint tests.
5. **F-3 (form.reset)** — small, clean, unlocks reset tests.
6. **F-2 (defaultChecked / indeterminate)** — needed for `clone.html`.
7. **F-5 (select IDL)** — needed for select-specific tests.

---

## §6  Non-Goals

- Actual form submission (HTTP POST/GET) — Lambda headless has no navigation.
- Date/time type sanitization / `valueAsDate` / `valueAsNumber` — out of scope.
- File upload (`<input type=file>`) — no filesystem picker in headless mode.
- `customizable-combobox` / `<selectlist>` — too new, not in stable spec.
- Visual rendering changes — `render_form.cpp` / `layout_form.cpp` are not touched.
- `form.requestSubmit()` — deferred; spec-compliant submit button selection required.

---

## §7  Baseline Tracking

All existing baselines must be preserved after each phase:

| Suite | Target |
|-------|--------|
| WPT DOM Events | 43/52/0 |
| UI Automation | 67/67 |
| Lambda baseline | 2744/2764 |
| Radiant baseline | 5713/5717 |
