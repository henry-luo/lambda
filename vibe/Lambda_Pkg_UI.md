# Lambda UI Package — `lambda.ui.dtna`

> **Status:** implementation started; no new language or engine contract is ratified here.
> **Date:** 2026-10-09.
> **Requested namespace:** `lambda.ui.dtna` (`dtna` reverses `antd`).
> **Phase 1 target:** native Radiant, confirmed by the user. Interactive browser support is deferred.
> **Scope:** a comprehensive Ant Design-inspired component package, reusable UI foundations, and an executable conformance workload for Lambda styling and interaction.
> **Implementation status:** initial native subset implemented; [progress and open gates](impl/Lambda_Impl_UI_Dtna.md). All 73 entries remain in scope; the feature manifest marks 34 partial and 39 planned. Performance and AntD pixel parity are unmeasured.
> **Authority:** [formal semantics](../doc/Lambda_Formal_Semantics.md), [formal design](../doc/Lambda_Formal_Design.md), then the existing [reactive UI design](Lambda_Design_Reactive_UI.md) and DOM design records. Implementation planning is collected in the appendices, following [Doc Convention](../doc/Doc_Convention.md).

| Subject | Formal linkage | Consequence for this proposal |
|---|---|---|
| Shipped package namespace, source distribution and resolution | **D7.2.1–D7.2.4, D7.2.6; S16.9.8** | Ship Lambda sources under `lmd/package/ui/`; resolve the requested import without a new namespace exception. |
| Functional rendering and procedural interaction | **S12.1.1v2–S12.1.3** | Constructors and presentation remain pure; handlers perform mutation and I/O. |
| State ownership and value semantics | **S1.4, S9.1.4, S9.1.7, S9.2.4v2, S9.3.1** | No mutable package singleton, closure-based state cell, or implicit shared mutable props. |
| DOM behavior ownership | **D7.2.5** | Reuse the shipped DOM behavior package for controls and editing; component code must not recreate browser default actions. |
| Runtime and presentation lifetimes | **D4.5.1v4, D4.5.2, D5.3.3** | Copy or precisely root retained values; validate native handles and document ownership. |
| Execution tier independence | **D8.1.1v17** | The same component and interaction contract must work on T0 and MIR Direct. |

Where the formal specifications do not settle a contract, this document names the existing working record and leaves the proposed extension explicitly open for consultation (§11). Approval of a component roadmap alone does not resolve those questions.

## 1. Purpose and Phase 1 boundary

Develop an enterprise UI library written primarily in Lambda Script, with the component breadth and visual language of Ant Design. Applications should be able to build a complete data-entry and administration interface with one package: navigation, forms, tables, trees, pickers, dialogs, feedback, and responsive page structure.

The package has two equal responsibilities:

1. **Application library:** provide useful, composable components with consistent sizing, appearance, keyboard behavior, localization, and documented APIs.
2. **Lambda UI conformance workload:** expose defects and missing support in templates, state, event routing, text editing, CSS, layout, rendering, overlays, animation, and resource lifetime through realistic applications.

Phase 1 means the complete **dtna product phase**, divided into milestones M0–M7 below. A first button-and-form milestone is useful progress, but does not satisfy the full Phase 1 goal. Every catalog entry in §7 belongs to Phase 1; difficult entries remain visible requirements rather than quietly moving to a later theme release.

The primary execution surface is `lambda view` and embedded Radiant documents. Static HTML, SVG, PNG, and PDF outputs are useful for documentation and visual checks where the existing output pipeline supports the content. They are snapshots of declared state; serialization does not preserve live Lambda handlers. **Interactive browser output, hydration, JavaScript component bindings, and browser distribution are future work.** Chromium remains useful as an AntD reference renderer, independently of the product target.

Phase 1 establishes an Ant-inspired default light appearance. Dark mode, compact density, alternate brands, and other style families follow later. The foundation must permit them without rewriting component behavior, but implementing those themes is not a Phase 1 completion requirement.

## 2. Ant Design reference and compatibility target

The official [component overview](https://ant.design/components/overview/) and [versioned upstream package](https://github.com/ant-design/ant-design/blob/6.6.5/package.json), consulted on 2026-10-09, establish **Ant Design 6.6.5** as the initial reference. The overview contains **73 entries**: General 4, Layout 7, Navigation 7, Data Entry 18, Data Display 21, Feedback 11, and Other 5. This includes the deprecated List entry, the newer Listy entry, and BorderBeam. The separately linked Pro Components are outside this core catalog.

Before implementation, freeze the reference in a machine-readable manifest: upstream tag and resolved commit, component/variant names, example source hashes, local reference application lockfile, viewport, device scale, fonts, locale, timezone, and motion settings. Official documentation is mutable; a reference update must be an explicit reviewed change. Maintain references under `test/ui/dtna_reference/` and generated captures under `temp/ui_dtna/`.

The target is **visual and behavioral correspondence through an idiomatic Lambda API**, rather than React API compatibility. AntD component names remain the discovery vocabulary in documentation; exported function names use Lambda's `snake_case`. React hooks, refs, JSX, CSS-in-JS execution, Day.js objects, and React-specific extension points receive Lambda equivalents or explicit applicability notes.

For each catalog entry, maintain a feature matrix with:

- Reference version and component documentation URL.
- Main variants, public props, subcomponents, slots, and customization points.
- Controlled/uncontrolled state, emitted actions, keyboard and pointer behavior.
- Localization, accessibility semantics, motion, and static-output behavior.
- Visual, model, interaction, and stress fixtures.
- Status: `planned`, `blocked`, `partial`, or `complete`, plus any unsupported feature and its reason.

Count component coverage separately from feature coverage. A named component that only draws its idle state is partial. A documented unsupported prop must be rejected or diagnosed, never silently accepted and ignored. React-only mechanisms can be classified as inapplicable when their user-visible purpose has a tested Lambda equivalent; this cannot excuse a missing table sorter or keyboard picker.

## 3. Architecture and package composition

### 3.1 Three layers

| Layer | Responsibility | Boundary |
|---|---|---|
| Shared UI foundations | Component data contracts, action envelopes, configuration values, focus/overlay coordination, collection algorithms, and common test adapters | Internal `lambda.ui` modules; extract shared implementations when real uses establish the common shape. |
| `lambda.ui.dtna` | Ant-inspired components, token defaults, style recipes, semantic parts, icons, locale assets, and documentation | Public package requested by the user. |
| Existing Lambda and Radiant facilities | Templates, state, DOM dispatch/default actions, CSS, layout, painting, text controls, native input, and scheduling | Extend general engine mechanisms only when a reproduced package fixture demonstrates the need. |

Component policy belongs in Lambda. Do not add native `dtna` tags, a native AntD widget hierarchy, a parallel renderer, or component-specific branches to the CSS/layout engine. Ordinary HTML and SVG presentation should exercise the same pipeline used by other Lambda documents. A reusable engine mechanism such as reliable pointer capture or overlay positioning can be added when required, with its own contract and regression fixtures.

The public entry point maps directly to `lmd/package/ui/dtna.ls` under **D7.2.4/D7.2.6**. Internal component modules live in `lmd/package/ui/dtna/`. There is no new built-in module named `lambda.ui.dtna`, and no import-resolver special case is needed.

### 3.2 Component data and presentation

Recommended public shape: pure constructor functions accept a props map and, where appropriate, child content. They produce **logical component elements** such as `<dtna kind:'button'>`; installed `view` templates present them as HTML/SVG through `apply`. Plain Lambda element content remains a valid slot value. Pattern matches are specific to dtna tags, avoiding broad map/catch-all templates that could intercept unrelated application models.

Keep construction distinct from application so a component source can be retained and rendered again. Do not describe an HTML `id` or a proposed `key` prop as an implemented state-identity mechanism. Current template state is keyed by source item and template identity; rebuilding a constructor result may create a new source identity. The repeated-child/reorder contract is an M0 prerequisite (§6.1 and §11).

The package import must register its author templates in the active runtime and retain their module ownership on both execution tiers. It must coexist with, and never register itself as, the DOM package's UA behavior templates. The facade may import component modules eagerly for the first release; measure that cost before promising selective loading. Direct component-module imports can offer a smaller entry point without duplicating implementation.

### 3.3 Configuration and future theme boundaries

Proposed ConfigProvider and App equivalents establish an explicit application root containing immutable configuration and document-owned services. Configuration includes tokens, locale, direction, default size, disabled defaults, motion preference, and overlay ownership. Nested providers override supplied values and inherit the remainder. Public component behavior must not depend on a process-global theme or mutable module binding (**D7.2.1; S9.1.7**).

CSS inheritance and scoped theme selectors can carry visual tokens. Nonvisual configuration must travel through explicit component data/render context or a separately reviewed runtime facility. **No general ambient context API is assumed to exist.** M0 compares these options and settles provider propagation without breaking source identity. Detached overlays must resolve the originating provider's configuration rather than accidentally using the outermost root.

Future style families should share state machines, action contracts, focus rules, and data algorithms. They may supply different token derivation, icons, spacing, motion, and structural recipes. Allow structural differences where a new design system requires them; a theme need not be only a color replacement. Avoid creating a speculative universal widget framework before dtna establishes reusable requirements.

## 4. Proposed public API

### 4.1 Naming and common contracts

| Area | Proposed contract |
|---|---|
| Import | `import ui: lambda.ui.dtna` |
| Construction | `ui.button(props, children)`, `ui.select(props)`, `ui.table(props)`, and equivalent constructors listed in §7 |
| Application | `ui.page(root, options)` creates a full Radiant document; `ui.render(root, options)` supplies embeddable presentation without an additional document shell. |
| Common props | `id`, `class`, `style`, `size`, `disabled`, `status`, `variant`, semantic-part overrides, and applicable role/label attributes |
| Values | `value`/`default_value`, `checked`/`default_checked`, `open`/`default_open`; presence of the controlled prop determines ownership. |
| Content | Named slots and child content; pure render functions for table cells, options, and similar data projections |
| Actions | Named custom events routed with `emit`, carrying a common envelope and component-specific payload |
| Services | Root-scoped procedural operations for messages, notifications, dialogs, focus, scrolling, and transfer; no ambient singleton |
| Diagnostics | Component name, instance ID, prop/feature name, and actionable explanation; stable diagnostic identifiers once the API is ratified |

`null`, `false`, an empty string, and an omitted prop have different meanings under Lambda's value semantics. Controlledness requires a property-presence check, not a truthiness test. For collections, stable item keys are mandatory wherever selection, editing, expansion, or drag ordering persists across updates. Duplicate keys are an error, not a reason to fall back to array indices.

A proposed application composition looks like this. The APIs are not implemented; these examples are deliberately marked `no-run` and must become executable fixtures before the corresponding milestone closes.

```lambda no-run
import ui: lambda.ui.dtna

let content = ui.layout({id: "admin"}, [
    ui.layout_header({}, [ui.title({level: 3}, "Orders")]),
    ui.layout_content({}, [
        ui.space({}, [
            ui.button({id: "create-order", variant: 'primary'}, "New order"),
            ui.input({id: "order-search", placeholder: "Search orders"})
        ]),
        ui.table({id: "orders", row_key: "id",
            columns: [{key: "name", title: "Name", data_index: "name"}],
            data: [{id: "o1", name: "Example order"}]})
    ])
])

ui.page(content, {title: "Orders", locale: "en-US"})
```

### 4.2 Events and application-owned state

Recommended event families are `ui_action`, `ui_change`, `ui_open_change`, `ui_submit`, `ui_validate`, `ui_select`, and `ui_reorder`. They are proposed package protocol names, not new built-in events. An envelope identifies `component`, `id`, `action`, and the relevant `value`, `key`, `field_path`, or source metadata. Preserve typed values and original option keys; labels are presentation.

Application `view`/`edit` templates handle these requests. Read-only controls can emit a requested change; only a caller with the appropriate authority changes its model. Render functions remain pure and event operations remain procedural (**S12.1.1v2–S12.1.3; S9.1.4**).

```lambda no-run
import ui: lambda.ui.dtna

view <counter_panel> state count: 0 {
    ui.render(ui.space({}, [
        ui.text({}, string(count)),
        ui.button({id: "increment"}, "Increment")
    ]))
}
on ui_action(action) {
    if (action.id == "increment") { count = count + 1 }
}

ui.page(apply(<counter_panel>))
```

This second example intentionally contains only stateless child presentation. Reconstructing a child with local input, popup, or validation state requires the identity decision in §11; the example does not establish that contract by implication.

DOM events and `emit` have different propagation rules. Custom notification goes to the nearest enclosing template declaring the name; it is not a broadcast or a synchronous return channel. Composed controls must forward an event deliberately when they consume it. Avoid turning one physical activation into both a component update and a second UA default update. The existing dispatch record **ES22–ES29**, especially **ES24/ES29**, governs cancellation where the formal specs leave details to the working design.

## 5. Style and design tokens

### 5.1 Token model

Use the reference's separation of seed, derived, semantic, and component tokens as a guide. AntD documents its token hierarchy and per-component customization in [Customize Theme](https://ant.design/docs/react/customize-theme/). dtna should expose ordinary immutable Lambda data and produce styles for Radiant; it does not need a React CSS-in-JS runtime.

| Token group | Examples and use |
|---|---|
| Seed | Primary/status colors, base font, font size, radius, control height, spacing unit, and motion preference |
| Derived palette and scales | Hover/active/disabled colors, background/border steps, type scale, spacing scale, and control sizes |
| Semantic aliases | Text hierarchy, surface/container/elevated backgrounds, focus ring, error outline, separators, and overlay layers |
| Component tokens | Button padding, input affordances, table row/header treatment, picker cells, menu indentation, and dialog dimensions |

Reference starting values include primary blue `#1677ff`, a 14px base font, 32px standard control height, and 6px base radius, subject to pinned visual fixtures. These are the dtna preset, not hard-coded assumptions in component logic. Derive colors and scales in one tested token module. Typography, border, shadow, focus, icon sizing, and spacing are all tokens; changing primary color alone is insufficient theming.

### 5.2 CSS delivery and isolation

Generate deterministic, scoped CSS once per distinct configuration and reuse it across updates. Prefer CSS custom properties for inherited visual tokens after verifying cascade, fallback, and invalidation. If some required capability is incomplete, create a minimal engine reproducer and track the real defect. Choosing an explicit supported style contract is legitimate; quietly approximating a broken token or layout with fixed coordinates is not.

Use a `dtna-` class prefix, a provider/theme scope, and stable semantic parts such as `root`, `label`, `control`, `prefix`, `suffix`, `popup`, `header`, `body`, and `footer`. Semantic-part overrides should survive internal markup refactoring. Define cascade precedence explicitly: defaults → provider tokens → component tokens/variants → instance part styles → ordinary authored CSS under normal CSS precedence. Do not introduce blanket `!important` rules or a document-wide reset.

Cover hover, active, focus-visible, checked, selected, expanded, disabled, readonly, loading, invalid, and empty states. Test long labels, CJK, bidirectional text, wrapping, ellipsis, zoom/device scale, nested overflow, and narrow viewports. Grid should provide the familiar 24-column abstraction and responsive spans; it should exercise genuine flex/grid sizing rather than screenshot-specific geometry.

Styles are derived presentation. UI state remains the source of truth; a class name or painted checkbox is not a second state store. Theme updates should invalidate affected style/layout/paint without resetting text, selection, open state, or table edits.

## 6. Interaction, identity, and lifetime

### 6.1 Canonical state and instance identity

For each component, specify the owner of committed value, transient edit buffer, selection, open state, active option, expansion, and scroll position. Controlled props belong to the application; uncontrolled component state initializes from a default once. Explicitly define reset, removal, and controlled/uncontrolled transitions. Do not mirror every prop into local state or maintain a second native text/checked value.

The established template key is conceptually **(source item, template identity, state name)**, per the reactive UI record §§5.1–5.4. Durable/repeated presentation, immutable source replacement, and anonymous-template identity remain open there and in **RS7/RSO1/RSO2/RSO10** of [State Management](radiant/Radiant_Design_State_Management.md).

M0 must decide and test how a logical component retains state through parent regeneration, list reordering, conditional hiding, and repeated presentation in two locations. The recommended direction is an explicit logical identity scoped to its owning application/template instance, with removal/reset rules. This is a **proposal requiring consultation**, not permission to bolt an undocumented `key` behavior onto `apply`. Until settled, a prototype may retain source nodes explicitly; it must declare that limitation and cannot pass the general composition gate.

### 6.2 Text and native defaults

Build Input, TextArea, Password, Search, OTP, InputNumber, and editable cells on the shared text-control model and DOM behavior package. Preserve caret, selection, composition, clipboard, undo/redo, readonly/disabled behavior, and committed-change ordering. Test Unicode with explicit offset units: UTF-8 bytes, Lambda code points, and DOM UTF-16 indices are not interchangeable.

Avoid handwritten append/delete input logic that bypasses the established editor. Keyboard activation, form submission, reset, checkbox/radio defaults, and label activation must follow the existing DOM route (**D7.2.5; ES22–ES29**). An author cancellation must suppress the appropriate default exactly once.

### 6.3 Shared overlay and focus contract

Dropdown, Select, Cascader, TreeSelect, pickers, Tooltip, Popover, Popconfirm, Modal, Drawer, Tour, Image preview, and floating feedback should share overlay coordination. Required behavior includes:

- Anchor measurement, preferred placement, viewport collision handling, scroll/resize repositioning, transformed ancestors, and clipping.
- Deterministic stacking, nested popup ownership, outside-click rules, Escape behavior, and isolation between application roots.
- Focus entry, keyboard navigation, modal focus containment, focus restoration, and a defined fallback when the trigger was removed.
- Logical template ownership and provider inheritance even when visual content is placed outside its ordinary layout ancestry.
- Cleanup of pointer capture, hover/active/focus references, pending timers, and listeners when content or the document disappears.

A shared overlay root is the recommended package design. Whether existing positioned DOM is sufficient, or a general presentation-attachment facility is required, is an M0/M2 investigation. Do not assume React portals, a browser top layer, or a special Radiant layer already provides these contracts. Full clipping, hit-testing, event ownership, and cleanup are acceptance conditions.

### 6.4 Async work and resources

Form validation, remote search, tree loading, upload, modal confirmation, and infinite lists require pending/error/cancel states. Every request should carry a component owner and generation so stale completions cannot overwrite a newer value or a closed/replaced document. A debounce timer has the same lifetime obligation as a network completion.

Application services are owned by a mounted root. Their procedural APIs perform work from handlers or `pn` entry points; a constructor cannot perform I/O. An ordinary reducer may return descriptive data, but that is not a new reified-effect language facility (**S12.1.1v2**). Delivering background results to an arbitrary template and author lifecycle cleanup are not fully settled in the reactive UI record §§7.7/8/18; resolve the necessary contract before presenting async components as complete.

Retained native values follow **D4.5.2**: copy to the document owner or keep them under a registered precise root. Native handles must be generation checked. Do not restore conservative native-stack scanning to keep component callbacks alive.

### 6.5 Accessibility and localization

Use native semantic elements where appropriate and provide names, labels, descriptions, error associations, roles, state attributes, and visible keyboard focus. Keyboard patterns should follow the applicable [WAI-ARIA Authoring Practices](https://www.w3.org/WAI/ARIA/apg/patterns/) for comboboxes, menus, tabs, trees, grids, dialogs, and sliders. Check semantics independently of appearance.

Radiant's native accessibility-tree/platform bridge must be audited separately. Correct ARIA markup and passing keyboard fixtures do not prove VoiceOver or other assistive-technology support. The coverage report must state any platform accessibility gap explicitly; consult on adding a bridge if M0 finds that a required mechanism is absent.

Provide `en-US` and `zh-CN` as initial locale fixtures, externalized UI messages, configurable date/number formatting and first weekday, and an RTL test locale. Locale, direction, and timezone belong to the application configuration. Full translation coverage and platform assistive-technology claims require their own evidence.

## 7. Phase 1 component catalog

The following tables enumerate all 73 core overview entries. The final column gives the target milestone, not current availability. Listed capabilities are proposed acceptance scope; exact prop coverage is expanded in the versioned feature manifest. Compound APIs below are separate exports over shared implementations.

### 7.1 General — 4

| AntD entry | Proposed Lambda API | Required scope | Milestone |
|---|---|---|---|
| Button | `button`, `button_group` | Primary/default/dashed/text/link appearances; sizes, shapes, icon, danger, loading, block, disabled, keyboard activation | M1 |
| FloatButton | `float_button`, `float_button_group`, `back_top` | Floating action, grouped menu, badges, tooltip, scroll-to-top, correct placement and keyboard reachability | M6 |
| Icon | `icon` | Consistent SVG symbols, outlined/filled/two-tone variants, sizing/rotation, decorative versus named semantics | M1 |
| Typography | `title`, `text`, `paragraph`, `link` | Hierarchy, emphasis, code, copy/edit, expandable ellipsis, tooltips, selection and link semantics | M1; editing M3 |

### 7.2 Layout — 7

| AntD entry | Proposed Lambda API | Required scope | Milestone |
|---|---|---|---|
| Divider | `divider` | Horizontal/vertical, label position, dashed/plain, spacing | M1 |
| Flex | `flex` | Direction, wrap, alignment, gap, flex sizing and intrinsic constraints | M1 |
| Grid | `row`, `col` | 24 columns, spans/offset/order, gutters, responsive breakpoints and nesting | M1 |
| Layout | `layout`, `layout_header`, `layout_footer`, `layout_sider`, `layout_content` | App shells, collapsible/responsive sider, scrolling and fixed/sticky regions | M1 |
| Masonry | `masonry` | Responsive columns, gaps, mixed item heights, stable order, relayout after image/font loading | M6 |
| Space | `space`, `space_compact` | Horizontal/vertical spacing, wrap, splitters, joined control borders and focus states | M1 |
| Splitter | `splitter`, `splitter_panel` | Horizontal/vertical resize, limits, collapse, nested panels, pointer capture and keyboard resize | M6 |

### 7.3 Navigation — 7

| AntD entry | Proposed Lambda API | Required scope | Milestone |
|---|---|---|---|
| Anchor | `anchor` | Scroll target links, active section, offsets, scroll containers, keyboard use | M6 |
| Breadcrumb | `breadcrumb` | Items, separators, links, dropdown items, current-page semantics | M1; popup M2 |
| Dropdown | `dropdown`, `dropdown_button` | Click/hover/context activation, nested menus, placement, selection, dismiss and focus | M2 |
| Menu | `menu` | Horizontal/vertical/inline, groups, dividers, submenus, selected/open keys, collapse, keyboard/typeahead | M2 |
| Pagination | `pagination` | Page/size controls, total, quick jump, simple/small modes, disabled and controlled state | M1 |
| Steps | `steps` | Horizontal/vertical, status, progress, icons/dots, selectable steps and responsive labels | M1 |
| Tabs | `tabs` | Positions, card variants, overflow, editable add/remove, lazy/retained panels and keyboard selection | M2 |

### 7.4 Data Entry — 18

| AntD entry | Proposed Lambda API | Required scope | Milestone |
|---|---|---|---|
| AutoComplete | `auto_complete` | Search text, suggestions, groups, async results, keyboard choice, IME-safe completion | M4 |
| Cascader | `cascader` | Hierarchical single/multiple selection, search, lazy children, clear, path labels and disabled nodes | M4 |
| Checkbox | `checkbox`, `checkbox_group` | Controlled/uncontrolled checked state, indeterminate, groups, labels, disabled and keyboard behavior | M1 |
| ColorPicker | `color_picker` | Palette and custom color, alpha, format conversion, presets, gradients, clear, keyboard-adjustable channels | M4 |
| DatePicker | `date_picker`, `range_picker` | Date/week/month/quarter/year panels, date-time, multiple/range, bounds, disabled cells, presets and locale | M4 |
| Form | `form`, `form_item`, `form_list`, `form_error_list`, `form_provider` | Nested fields, layouts, binding, dependencies, sync/async validation, reset, dynamic arrays and submit | M3 |
| Input | `input`, `text_area`, `password`, `search`, `otp` | Text editing, sizes/variants/status, clear, prefix/suffix/addons, autosize, password reveal and OTP paste | M1; advanced M3 |
| InputNumber | `input_number` | Exact numeric values, draft text, bounds, steps, precision, formatter/parser, keyboard/wheel policy | M3 |
| Mentions | `mentions` | Trigger search, suggestion popup, insertion/range preservation, async data and multiline composition | M4 |
| Radio | `radio`, `radio_group` | Native group exclusivity, button variant, controlled selection, disabled and arrow-key navigation | M1 |
| Rate | `rate` | Full/half values, custom symbols, clear/readonly, keyboard adjustment and labels | M3 |
| Select | `select` | Single/multiple/tags, search, grouped options, remote data, clear, custom render, limits and virtualization | M4 |
| Slider | `slider` | Single/range, steps, marks, vertical/reverse, tooltips, constrained thumbs, pointer and keyboard input | M3 |
| Switch | `switch` | Checked, labels/icons, loading/disabled, form binding, keyboard activation | M1 |
| TimePicker | `time_picker`, `time_range_picker` | Hour/minute/second, 12/24 hour, steps, disabled times, range and confirmation | M4 |
| Transfer | `transfer` | Dual lists, search, pagination, selected/target keys, disabled items and custom list/tree content | M5 |
| TreeSelect | `tree_select` | Search, hierarchy, checked/multiple values, label strategy, lazy loading and virtual popup | M4 |
| Upload | `upload`, `upload_dragger` | File selection/drop/paste where supported, lists/previews, filtering, progress, abort/retry and transport adapter | M6 |

### 7.5 Data Display — 21

| AntD entry | Proposed Lambda API | Required scope | Milestone |
|---|---|---|---|
| Avatar | `avatar`, `avatar_group` | Image/icon/initials, shapes, fallback, sizing and group overflow | M1 |
| Badge | `badge`, `badge_ribbon` | Counts/dots/status, overflow, offsets, ribbon and accessible descriptions | M1 |
| Calendar | `calendar` | Month/year views, selection, range/bounds, cell content, first weekday and locale | M4 |
| Card | `card`, `card_grid`, `card_meta` | Header/actions/tabs/cover, borders/hover, loading, size, nested/grid content | M1 |
| Carousel | `carousel` | Arrows/dots, swipe/drag, autoplay/pause, orientation, transitions and keyboard use | M6 |
| Collapse | `collapse` | Accordion/multiple panels, icons, disabled sections, retained/lazy content and keyboard use | M2 |
| Descriptions | `descriptions`, `description_item` | Responsive columns, spans, label placement, borders, sizes | M1 |
| Empty | `empty` | Preset/custom illustration, description and action slots | M1 |
| Image | `image`, `image_preview_group` | Loading/fallback, preview, grouped navigation, zoom/pan/rotate and image lifecycle | M6 |
| List — deprecated upstream | `list`, `list_item`, `list_item_meta` | Conventional rich-item layout, grid, actions, loading, pagination; reuse collection foundation and recommend Listy for new virtual lists | M5 |
| Listy | `listy` | Virtual scrolling, grouped sticky headers, rich rows, infinite loading, drag reorder and scroll-to-item | M5 |
| Popover | `popover` | Rich content, trigger modes, placement, controlled open state and focus/dismiss rules | M2 |
| QRCode | `qr_code` | Standards-based encoding, error correction, quiet zone, colors/icon, status and SVG/export | M6 |
| Segmented | `segmented` | Single selection, icons, sizes, block/vertical presentation and keyboard use | M1 |
| Statistic | `statistic`, `countdown` | Formatting, prefix/suffix, precision, loading and cancellable timed countdown | M1; timer M6 |
| Table | `table` | Columns, sorting/filtering, pagination, selection, expansion/tree rows, fixed regions, edit, summaries and virtualization | M5 |
| Tag | `tag`, `checkable_tag` | Semantic/custom colors, icons, close/cancel, selection and keyboard actions | M1 |
| Timeline | `timeline` | Direction, alternating/labelled items, status, pending state and custom markers | M1 |
| Tooltip | `tooltip` | Hover/focus/click, delays, arrow/placement, wrapping, disabled triggers and dismiss | M2 |
| Tour | `tour` | Target geometry, spotlight/mask, steps, scroll-to-target, missing targets and keyboard exit | M6 |
| Tree | `tree`, `directory_tree` | Expansion, checked/selected keys, disabled nodes, lazy load, drag/drop, typeahead and virtualization | M5 |

### 7.6 Feedback — 11

| AntD entry | Proposed Lambda API | Required scope | Milestone |
|---|---|---|---|
| Alert | `alert`, `alert_error_boundary` | Status, description/icon/action, close/cancel, banner; error wrapper only after failure-isolation contract is settled | M1; boundary M7 |
| Drawer | `drawer` | Four sides, sizes, mask, push/nesting, loading, focus containment and restoration | M2 |
| Message | `message_host`, root-scoped `message` operation | Status toasts, update by key, duration, stacking, dismissal and inherited configuration | M2 |
| Modal | `modal`, root-scoped `confirm` operation | Dialog slots, confirm variants, pending OK/cancel, mask/Escape, destroy/retain and nested focus | M2; async M3 |
| Notification | `notification_host`, root-scoped `notify` operation | Rich notices, placements, keyed updates, progress/timing, stacking and cleanup | M2; timer M6 |
| Popconfirm | `popconfirm` | Anchored confirmation, buttons, async pending/error, cancellation and focus return | M2; async M3 |
| Progress | `progress` | Line/circle/dashboard, steps, status, labels and multi-segment colors | M1 |
| Result | `result` | Success/error/warning/info and result-page variants, illustrations and actions | M1 |
| Skeleton | `skeleton`, skeleton subparts | Text/avatar/button/input/image shapes, loading transition and optional motion | M1 |
| Spin | `spin` | Sizes, delay, label, nested/fullscreen loading, custom indicator and busy semantics | M1 |
| Watermark | `watermark` | Repeated text/image, rotation, spacing, multi-line text and deterministic export; decorative treatment, not a security guarantee | M6 |

### 7.7 Other — 5

| AntD entry | Proposed Lambda API | Required scope | Milestone |
|---|---|---|---|
| Affix | `affix` | Top/bottom offsets, custom scroll container, resize and sticky/fixed transition | M6 |
| App | `app` | Application scope, service hosts, stylesheet installation and cleanup | M0/M1 |
| BorderBeam | `border_beam` | Border-following decoration, gradients, direction/speed/size, multiple beams, hover and reduced motion | M6 |
| ConfigProvider | `config_provider` | Nested immutable configuration, locale/direction/size/disabled/motion, tokens and overlay propagation | M0/M1 |
| Util | `util` module | Reference utilities classified individually; responsive/debounce/value helpers where applicable, React hook/ref helpers mapped or marked inapplicable | M0; audit M7 |

The [Listy reference](https://ant.design/components/listy/) explicitly covers virtualization, grouped headers and imperative scrolling; these belong to the collection workload. The [BorderBeam reference](https://ant.design/components/border-beam/) supplies a useful demanding animation/rendering workload. They must not disappear from coverage merely because older AntD component lists omit them.

## 8. Contracts for the difficult component families

### 8.1 Forms and validation

The [Form reference](https://ant.design/components/form/) is a functional subsystem, not a styled container. Represent field paths explicitly as segments, preserving the distinction between a dotted field name and a nested path. Define values, initial values, touched/dirty, errors/warnings, validating, dependencies, and submit state. Dynamic arrays require stable row identities and deterministic insertion/removal/reordering behavior.

Bind controls through one field adapter that maps the committed value and change protocol; checkbox/switch checked state must use the same path as text and selection controls. Support required/type/range/length/pattern rules, custom pure validators, procedural async validators, validation triggers, cross-field dependencies, and localized messages. Reuse Lambda's validator where its contract fits; do not force schema validation to impersonate interaction state.

Reset restores documented initial values and clears the intended metadata. Hidden/unmounted field retention is explicit. Submission validates one coherent value snapshot, focuses the first invalid field, prevents duplicate submission, and reports stale async results. Modal-close/reopen, list reorder, IME text input, and dependent validation are mandatory end-to-end scenarios.

### 8.2 Collections, tables, and trees

One shared keyed-collection foundation should serve Select, Transfer, Tree, TreeSelect, List/Listy, and Table where their requirements overlap. Extract filtering, selection membership, tree traversal, and viewport calculations rather than maintaining six copies. Preserve distinctions such as single selection, checked hierarchy, and active keyboard option.

The [Table reference](https://ant.design/components/table/) includes fixed regions, editable data, expansion, and virtual lists. Provide native table semantics for ordinary data and an explicit accessible strategy for virtualized presentation. Local operations and application/remote operations must be distinguishable, with stable sorting, documented null comparison, typed keys, pending/error/empty states, and coherent selection across pagination and filtering.

Virtualization must preserve edit state, focus, selection, row identity, scroll anchoring, and grouped headers. Cover uniform and measured variable-height rows. Require proof that mounted row count is bounded by the viewport plus overscan; rendering every row behind an overflow container is not virtualization. Fixed columns/header, tree expansion, merged cells, and variable-height combinations need an explicit feature matrix rather than assumed support.

### 8.3 Pickers and exact values

The [DatePicker reference](https://ant.design/components/date-picker/) distinguishes multiple date granularities and range selection. Use Lambda datetime/calendar facilities and an explicit value contract, not Day.js-compatible objects. Separate civil dates and local times from timestamps/timezones. Freeze the clock in tests; cover leap days, month ends, week numbering, locale first weekday, invalid input, range ordering, and daylight-saving transitions where a zoned value is involved.

InputNumber separates the edit buffer from the committed numeric value. A temporarily incomplete `-` or decimal separator is editable text, not a zero or a failed forced conversion. Preserve integers/decimals when selected by the API; never route exact values through float merely for display. Bounds, step, formatter/parser, and precision obey **S1.3** and the applicable **S4** numeric rulings.

The [Select reference](https://ant.design/components/select/) covers virtual options and rich selection modes. Test composition, keyboard highlight/commit, hidden selected options, custom labels, duplicate labels with distinct typed values, and late remote responses.

### 8.4 File transfer and native integration

The [Upload reference](https://ant.design/components/upload/) separates file-list presentation from configurable transport. dtna should expose a transport adapter and explicit file metadata, progress, cancellation, retry, validation, and preview lifetime. Use existing file/DOM/platform mechanisms; an absent picker, directory-drop, multipart stream, or progress mechanism is an engine/platform gap to reproduce and resolve.

No default upload endpoint or hidden network request is appropriate. Application code supplies the operation. Use a local deterministic test server and synthetic files for automated transfer checks. Document per-platform limits for file dialogs, clipboard files, directory upload, and drag/drop instead of claiming browser-equivalent behavior from an idle Upload screenshot.

### 8.5 Motion, timing, and failure isolation

Use one scheduling/animation path for fades, collapse, carousel, spinner, countdown, notifications, and BorderBeam. Deterministic tests use a controlled clock and explicit settling. Reduced motion removes nonessential motion while preserving state and completion signals. Closing a root cancels its work; static export samples a declared frame rather than depending on wall time.

An Alert error-boundary equivalent requires an explicit template/handler failure-isolation contract. The reactive UI design §18.1 lists lifecycle and error boundaries as unsettled. Do not swallow arbitrary errors and continue with half-mutated state. M7 either implements the reviewed contract with recovery fixtures or reports this feature blocked; the rest of Alert can ship earlier.

## 9. The package as a Lambda UI test program

Maintain a capability matrix alongside component coverage. Each row names a minimal fixture, owning subsystem, observed behavior, and required gate. The following are investigation targets, not claims that all currently fail.

| Capability under test | Representative dtna workload | Required evidence |
|---|---|---|
| Imported templates and retained callbacks | Package facade, nested component wrappers, two documents | Both tiers, correct module owner, no duplicate registration or stale callbacks |
| State identity and reactive composition | Reordered form rows, editable table, popup inside a rerendering parent | Values/focus retained or reset exactly as specified; no cross-instance leakage |
| Observer invalidation and settling | Cross-field validation, table summary, nested providers | All dependent results update; unrelated subtrees and no-op updates stay unchanged |
| CSS token/cascade support | Nested providers, custom parts, hover/focus/error states | Computed style and rendered pixels agree; dynamic updates invalidate correctly |
| Layout and intrinsic sizing | Grid, joined controls, long labels, fixed table columns, Masonry | Browser-reference geometry plus Radiant rendered assertions at several viewports |
| Overflow, transforms, stacking and hit-testing | Popup in a scroller, nested Drawer/Modal, tooltip near viewport edge | Visible placement and real pointer target agree under scrolling and transforms |
| Text, selection, IME and clipboard | Input/OTP/Mentions and editable cells | Real key/paste/composition paths, defined offset units, caret preservation |
| Event cancellation and UA defaults | Form submit/reset, checkbox, keyboard button activation | One activation, correct cancellation, one coherent committed value |
| Continuous input and capture | Slider, Splitter, tree drag/drop | Drag outside bounds, cancellation, lost focus, release and document removal |
| Virtualization and retained painting | Large Table/Listy/Tree/Select | Bounded nodes, accurate hit-testing/scroll/focus, retained-state stress and timings |
| Timers and async delivery | Toast, validation, search, Upload | Cancellation, stale-generation rejection, teardown, deterministic pending/error states |
| SVG, clipping, images and export | Icon, QRCode, progress ring, Tour, Watermark, BorderBeam | Raster and supported SVG/PDF output checked visually and structurally |
| Accessibility mechanisms | Keyboard-only form, menu/tree navigation, modal focus | Semantic assertions, keyboard scripts, platform bridge status reported separately |
| Precise ownership and cleanup | Repeated root open/close, popup disposal, pending async teardown | No invalid handles, stale interaction references or unrooted retained values |

For any discovered failure: preserve the minimal reproducer, trace the responsible subsystem, and fix the general cause. An AntD visual baseline is evidence, not a ruling that overrides Lambda semantics. Add concrete defects to the existing central [issue ledger](Lambda_Issue_Ledger.md); do not create a second issue ledger in this proposal.

## 10. Deliverables and completion definition

Phase 1 delivers:

1. The source package and public API, scoped styles, initial theme, locale/icon assets, and notices for any reused upstream material.
2. A versioned 73-entry component manifest with feature-level status, documented differences, and links to acceptance fixtures.
3. A native component explorer: searchable catalog, variant/state gallery, token controls, localization/direction controls, event/action viewer, and keyboard navigation demonstrations.
4. At least three complete example applications: an administration dashboard with virtual editable data, a validated multi-step form, and a split-pane document/file workbench. They must exercise actual interactions and composed overlays.
5. Model/golden, layout, rendered visual, pointer/keyboard, lifetime, and release performance evidence; aggregate baseline results reported separately from focused checks.
6. User-facing package/API documentation and a follow-on implementation record under `vibe/impl/` when implementation begins.

**Phase 1 is complete only when** all 73 entries have a reviewed feature disposition, every required applicable feature is implemented and tested, M0–M7 acceptance gates pass, examples work through the real interaction path, and platform/assistive-technology limits are stated precisely. A blocked required feature keeps Phase 1 partial. Any accepted scope reduction requires an explicit review and must remain visible in the release report.

AntD Pro Components, Ant Design Charts/X/Mobile/Mini/Web3, a router, business backends, a browser runtime, and unrelated design themes are outside this phase. Existing Lambda chart/graph/document packages can be embedded in the example applications without importing those ecosystems into dtna's compatibility claim.

## 11. Decisions for consultation before implementation

The namespace, Ant-inspired Phase 1 direction, comprehensive catalog goal, and Radiant-only interactive target come from the user's request. The following recommendations remain proposed; they do not change formal rulings by being written here.

| Open contract | Recommendation | Consultation/acceptance point |
|---|---|---|
| Public construction surface | Pure constructors returning logical elements, plus `view`/`apply` presentation; keep direct tag syntax possible | Confirm the facade in M0 with one complete interactive vertical slice. |
| Stable identity | Explicit component/item identity scoped by owner and presentation location, with defined retain/reset/removal behavior | Resolve **RS7/RSO1/RSO2/RSO10** before claiming stateful nested composition. |
| Nonvisual provider propagation | Compare explicit render/config data against a minimal reviewed host facility; avoid mutable ambient globals | Decide in M0; demonstrate nested providers without child state loss. |
| Overlay attachment | Shared package overlay ownership, reusing general DOM/layout mechanisms; define logical event ancestry separately from paint position | Decide in M0/M2 against clipping, nested ownership and focus fixtures. |
| Async destinations and lifecycle | Root-scoped owner/generation delivery and explicit cancellation/cleanup | Resolve reactive UI §§7.7/8/18 requirements before async features close. |
| Failure isolation | Reviewed template/handler boundary with deterministic recovery and rollback expectations | Resolve before the Alert error-boundary feature closes. |
| Native accessibility | Audit available platform bridges and agree on required initial platform coverage | Report in M0; do not equate ARIA attributes with native screen-reader support. |
| Exact prop parity | Translate React-specific APIs by purpose; keep all user-visible capabilities in a reviewed matrix | Approve M0 matrix; no silent exclusions during later milestones. |

When a decision changes an existing semantics/design ruling, update both formal specification and working design, revise the formal ruling in place with its version suffix/doc semver change, and regenerate the formal index, as required by the project convention. Missing contracts are questions for review; implementation behavior cannot silently settle them.

## 12. Later phases

- **Phase 2 — additional styles and themes:** Ant-family dark/compact presets, then other visual families under appropriate `lambda.ui.*` packages or presets, reusing proven behavior and tests. Confirm the public naming when those styles are selected.
- **Future browser phase:** investigate an interactive browser host/runtime, handler execution, ownership, DOM integration, distribution, and cross-host parity. Static export from Phase 1 is not a browser implementation.
- **Optional ecosystem work:** richer application shells, specialized data grids, mobile interaction, and business-oriented composites after core dtna coverage is established.

## Appendix A. Implementation milestones and dependencies

These are subdivisions of **Phase 1**, not replacements for its comprehensive scope. Estimated effort is intentionally left until M0 establishes engine gaps; the catalog alone is not a reliable schedule.

| Milestone | Work and dependencies | Acceptance and completion criterion |
|---|---|---|
| **M0 — contracts and reference freeze** | Audit live template/DOM/CSS facilities; pin AntD; produce feature manifest; prototype Button + Input + Select popup in a rerendering parent; consult on §11 | Exact facade import works on T0/MIR; two roots isolated; state identity/provider/overlay decisions recorded; real click/type/open/close survives parent updates; every catalog entry has an owner and required-feature list. |
| **M1 — foundation and ordinary components** | Depends on M0. Facade, component descriptors, App/ConfigProvider, default tokens/styles, icons/locales, basic inputs, layout, static display and feedback | All M1 catalog rows complete at their stated scope; explorer opens natively; resize, disabled/focus/error states and scoped styles have geometry/pixel fixtures; initial form controls reuse UA defaults. |
| **M2 — navigation and overlays** | Depends on M1 and overlay contract. Menu/Dropdown/Tabs/Collapse, Tooltip/Popover/Popconfirm, Modal/Drawer and feedback hosts | Nested overlay ownership, collision/clipping, outside-click/Escape, keyboard navigation, focus restoration and trigger-removal tests pass; originating provider reaches overlay content. |
| **M3 — forms and advanced editing** | Depends on M1/M2 and state/lifecycle decisions. Form subsystem, numeric/slider/rate controls, advanced Input/Typography and async confirmation | Nested dynamic form with reorder, validation dependencies, reset, async stale-result rejection, IME and submit works through pointer/keyboard tests; exact numeric cases pass on both tiers. |
| **M4 — selection and calendar families** | Depends on M2/M3; shares collection foundation with M5. Select/AutoComplete/Cascader/TreeSelect/Mentions, date/time/calendar/color pickers | Single/multiple/tag/tree/range flows, async search/loading, keyboard/typeahead, bounds, leap/week/locale cases and viewport placement pass; virtual options obey bounded-node tests. |
| **M5 — collections and enterprise data** | Depends on M1–M4. Table, Tree, Transfer, List/Listy, shared virtualization, drag ordering and editable data | Large keyed datasets with fixed regions, expansion, edit, sort/filter/page, selection and group scrolling pass; reorder/filter/remove retain the specified state/focus; node bounds and release metrics recorded. |
| **M6 — native integration and remaining catalog** | Depends on relevant earlier foundations. Upload, Image preview, Carousel, Tour, QRCode, Masonry, Splitter, Affix/Anchor/FloatButton, countdown/timers, Watermark and BorderBeam | Real drag/resize/file/preview/tour paths and cancellation pass; QR output is decoded by an independent oracle; animation samples, reduced motion, supported export and teardown are verified. |
| **M7 — coverage audit and hardening** | Depends on M0–M6. Error-boundary contract, Util applicability audit, all examples, documentation, cross-platform checks and performance review | 73-entry/feature audit closes with no hidden required gaps; focused and aggregate gates pass; native platform and accessibility boundaries explicit; no unresolved correctness/lifetime regressions. |

M4/M5 may develop together because they share collection requirements; they must reuse one implementation. Promote real shared helpers before adding a third near-identical variant. Each milestone adds fixtures before broadening to the next component family. Do not stabilize a broken visual fixture by changing only its golden.

## Appendix B. Proposed files and live integration points

```text
lmd/package/ui/
  dtna.ls                         # public facade: lambda.ui.dtna
  core/
    component.ls                  # common descriptor/prop/action contracts
    collection.ls                 # keyed selection/filter/tree algorithms
    overlay.ls                    # shared coordination after contract review
    focus.ls                      # package focus policy over DOM mechanisms
    virtual.ls                    # viewport algorithms and row measurements
  dtna/
    tokens.ls                     # immutable seed/derived/component tokens
    style.ls                      # deterministic scoped style generation
    locale.ls                     # locale selection and messages
    icons.ls                      # SVG icon facade and asset provenance
    general.ls, layout.ls          # split further as coherent modules grow
    navigation.ls, feedback.ls
    input.ls, form.ls, select.ls
    picker.ls, table.ls, tree.ls
    list.ls, upload.ls, image.ls
    motion.ls, tour.ls, util.ls
    assets/                       # SVGs/locales/notices, loaded without cwd assumptions
test/lambda/ui_dtna/               # pure/API tests; every .ls has a .txt golden
test/ui/dtna/                      # real Radiant interaction fixtures
test/ui/dtna_reference/            # pinned AntD reference app and source manifest
test/ui/dtna_gallery.ls            # component explorer entry point
test/ui/dtna_admin.ls              # complete applications, with companion fixtures
test/ui/dtna_form.ls
test/ui/dtna_workbench.ls
vibe/impl/Lambda_Impl_UI_Dtna.md    # create when implementation starts
doc/Lambda_UI.md                   # create alongside the implemented public API
```

This is a module-responsibility sketch, not a requirement to create empty files. Helpers remain in their owning modules until there is a real reuse need. Resolve assets relative to the package/module or Lambda home, and verify loading from a foreign working directory and a release bundle.

| Existing location/symbol | Proposed integration or investigation |
|---|---|
| `lambda/runtime/build_ast.cpp`: `append_shipped_package_module_path`, `lambda_resolve_import_module_path` | Verify ordinary mapping of `lambda.ui.dtna` and relative component imports; add no namespace exception. |
| `lambda/runtime/template_registry.{h,cpp}`: `TemplateEntry`, `template_registry_add`, `template_call_event_handler`, `fn_apply1`/`fn_apply2` | Imported author-template registration, module ownership, matching and dispatch on both tiers. |
| `lambda/runtime/template_state.{h,cpp}`: `TemplateStateKey`, `tmpl_state_get_or_init` | Identity/initialization/retention experiments and precise root ownership. |
| `lambda/runtime/template_host.{h,cpp}`: `TemplateHostSession`, `TemplateHostBinding` | Retained session entry/exit and ownership; reuse existing host machinery. |
| `lambda/runtime/render_map.{h,cpp}`, `edit_bridge.{h,cpp}` | Source/result ownership, parent-child notification and reactive replacement. |
| `lambda/dom/dom_events.{h,cpp}`, DOM geometry/CSSOM and platform APIs | Shared dispatch, measurement, style mutation, focus and platform transport. |
| `lmd/package/dom/form.ls`, `focus.ls` and sibling behavior modules | Reuse form, keyboard, text/default actions; do not duplicate UA policy. |
| `radiant/state_store.cpp`, `state_machine.cpp`, `form_control_model.cpp` | Canonical control/focus/selection state, detached-view cleanup and lifetime. |
| `radiant/event.cpp`, `event_sim.cpp` | Real hit-tested interaction, continuous input and assertion support. |
| `lambda/input/css/`, `radiant/css_variable.cpp`, `resolve_css_style.cpp` | Token CSS, inherited values, selectors, scoped cascade and dynamic invalidation. |
| `radiant/layout_flex*.cpp`, `layout_grid*.cpp`, `layout_table*.cpp`, positioned layout | Responsive/intrinsic/virtual content and overlay geometry, guided by minimized fixtures. |
| `radiant/css_animation.cpp`, `animation.cpp`, `frame_clock.cpp`, shared render walk | Motion scheduling, deterministic sampling and renderer/export parity. |
| `test/test_lambda_gtest.cpp`, `test/ui/ui_test_manifest.json` | Register the new golden directory and interaction suite; verify nonzero discovery. |
| `Makefile`, `build_lambda_config.json` | Add convenience gates only after fixture registration; native projects via JSON, never generated Lua. |
| `doc/Lambda_Packages.md`, `doc/HTML_CSS_SVG_Support.md` | Publish implemented package status and genuinely added engine capabilities. |

These live locations were inspected at `b058d6559` with unrelated working-tree changes present. This proposal does not depend on, edit, or claim validation of those changes. Native source edits follow C++17/C+ conventions and custom `lib/` containers; Radiant dimensions remain `float`. No vendor source changes are planned.

## Appendix C. Acceptance fixtures and measurements

### C.1 Fixture groups

| Fixture group | Minimum acceptance scenarios |
|---|---|
| Package/API | Exact import, direct module imports, default/unknown/invalid props, typed values, two runtime contexts, foreign cwd, release assets |
| Theme/style | Default light, every common size/state, nested token overrides, semantic parts, RTL/CJK, runtime token change preserving input/focus |
| Composition | Child source replacement/reorder, two presentations, conditional hide/show, reset/removal, nested wrappers and event forwarding |
| Overlay | Popup in overflow/transformed containers, scroll/resize, viewport edges, nested modal/drawer/select, outside-click, Escape, deleted trigger |
| Forms/text | Actual pointer focus, tab/shift-tab, Enter/Space, selection/copy/paste, IME commit/cancel, OTP paste, numeric draft, dynamic field arrays, async validation |
| Data | Table sort/filter/page/edit/expand, tree check/lazy/drag, transfer, Listy groups/infinite scroll, virtual focus and scroll-to-item |
| Integration | File dialog/drop and abort, preview pan/zoom, splitter drag/keyboard, carousel pause, missing Tour target, independently decoded QR |
| Ownership | Repeated open/close, root removal while popup/drag/validation/upload/timer is active, two documents, forced-GC retained callback checks |
| End-to-end | Complete admin CRUD flow, multi-step validated submission, workbench selection/resize/preview without state loss |

Every applicable component receives normal, disabled, focus/keyboard, controlled/uncontrolled, empty/error/loading, narrow/long-content, and repeated-update cases. Assert rendered content and geometry/pixels as well as model output. A direct reducer invocation does not replace the real pointer or keyboard route.

The current UI manifest's baseline glob covers only top-level `test/ui/*.json`. A nested `test/ui/dtna/*.json` directory requires explicit registration. Add a `dtna` suite and include its required fixtures in the baseline gate; verify the selected count so an empty green run cannot close a milestone. Likewise add `test/lambda/ui_dtna` to the core golden discovery list. Illustrative fixture IDs include `dtna_overlay_nested`, `dtna_form_async_reorder`, `dtna_table_virtual_edit`, and `dtna_root_teardown`.

### C.2 Visual reference protocol

Capture a local, pinned AntD example with Chromium and an equivalent Lambda model in Radiant. Match dimensions, scale, fonts, content, locale, clock, and motion state. Compare geometry, typography, colors, borders, shadows, icons, and state transitions. Keep masks/tolerances narrow and explain unavoidable rasterizer differences; geometry and semantic assertions remain exact where possible.

For hover, focus, open popup, invalid field, loading, selected row and dragging, establish the state through input and then capture it. Store authored reference inputs and provenance; generated artifacts go under `temp/ui_dtna/`. On macOS reference captures can use Puppeteer's headless shell through `CHROME_HEADLESS_SHELL` as documented by the project. A screenshot from the mutable public website is not a reproducible baseline.

### C.3 Release performance and resource gates

Initial stress fixtures: 10,000 table rows × 20 columns, 100,000 Listy records, 10,000 Select options, a 10,000-node mixed-depth tree, and a 200-field dependency-driven form. Freeze data and expected results; report visible-node counts and workload shape. These are proposed test sizes, not measured capacity claims.

Measure cold import/compile separately from warm interaction. Report input-to-settled-frame p50/p95, handler time, cascade/layout/paint work, frame misses, peak/retained memory, node counts and teardown growth. Use release binaries only, verify their exact identity/backend, and distinguish workload timing from process startup. Establish reference-hardware budgets after M0 measurements; a provisional 16.7ms warm-frame target at 60Hz is an engineering objective, not a promised result.

Require viewport-bounded mounted nodes for virtual collections and no cumulative growth of live component/overlay/session entries after 100 settled open/close cycles. Arena retention is distinct from a leak; report logical live counts and retained bytes separately. Confirm improvements with matched inputs, outputs and paired/interleaved runs, including control/control noise checks, before attributing them to an optimization.

## Appendix D. Validation commands and reporting

The following use current repository entry points; component paths and the `dtna` suite are **planned** and only become runnable after their registration. Do not report these commands as executed for this proposal.

```bash
# build/discovery checks after adding package/tests
make build
make build-test
./test/test_lambda_gtest.exe --gtest_list_tests --gtest_filter='AutoDiscovered/*ui_dtna*'

# focused API/golden checks on both supported execution tiers
LAMBDA_EXEC_BACKEND=interp ./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/*ui_dtna*'
LAMBDA_EXEC_BACKEND=jit ./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/*ui_dtna*'

# focused real interaction checks, after adding the dtna manifest suite
./test/test_ui_automation_gtest.exe --suite dtna --jobs 1
./lambda.exe view test/ui/dtna_gallery.ls

# existing reactive interaction gate and required aggregate gates
make test-reactive-ui ARGS='--jobs 1'
make test-lambda-baseline
make test-radiant-baseline
node test/test_run.js --target=radiant --category=baseline

# when JS/DOM adapter code changes, include the LambdaJS gate
make test262-baseline

# native Radiant edits and final diff hygiene
make lint ARGS='--rule ^no-int-cast-radiant$'
git diff --check
```

The current core suite uses explicit golden directories; confirm that the `ui_dtna` filter actually selects tests before relying on it. Add focused layout/render fixtures through the existing `make layout test=<fixture>` and `make test-render test=<fixture>` workflows, registering baseline coverage in the owning corpus. Foreign-cwd/release-bundle checks need explicit `LAMBDA_HOME` and a verified packaged asset set.

For performance, build a release executable in the main checkout with `make release`, then use its verified artifact after checking `doc/dev/Developer_Guide.md` §7. Never run `make release` in a linked worktree; use `make build-release-compile` there. Baseline targets may replace `lambda.exe` with debug, so do not reuse it for timing without re-verification. All scratch data and logs stay under `./temp/`.

Milestone reports must list completed catalog/features, focused pass counts, aggregate results, reference/binary provenance, measured limits, unresolved required features, and platform coverage. Run the full relevant aggregate gates when engine mechanisms change and at M7; focused checks alone do not establish overall closure. macOS is the initial development host; Linux and Windows smoke/interaction evidence is required before describing dtna as cross-platform complete.

## Appendix E. Reference sources

Consulted 2026-10-09. Upstream references guide proposed correspondence, while Lambda's formal specifications retain authority.

- [Ant Design component overview](https://ant.design/components/overview/) — catalog baseline.
- [Ant Design 6.6.5 package](https://github.com/ant-design/ant-design/blob/6.6.5/package.json) — version anchor; resolve and record its commit during M0.
- [Theme customization](https://ant.design/docs/react/customize-theme/) and [ConfigProvider](https://ant.design/components/config-provider/) — token/configuration reference.
- [Form](https://ant.design/components/form/), [Table](https://ant.design/components/table/), [Select](https://ant.design/components/select/), [DatePicker](https://ant.design/components/date-picker/), [Modal](https://ant.design/components/modal/) and [Upload](https://ant.design/components/upload/) — complex interaction/data scope.
- [Listy](https://ant.design/components/listy/) and [BorderBeam](https://ant.design/components/border-beam/) — recent catalog entries requiring explicit coverage.
- [Upstream license](https://github.com/ant-design/ant-design/blob/6.6.5/LICENSE) — preserve applicable notices when reusing source/assets; track icon and other dependency provenance separately.
- [WAI-ARIA interaction patterns](https://www.w3.org/WAI/ARIA/apg/patterns/) — semantic/keyboard reference, not proof of a native accessibility bridge.
- [Lambda Packages](../doc/Lambda_Packages.md), [Reactive UI](../doc/Reactive_UI.md), [Reactive UI design](Lambda_Design_Reactive_UI.md), [DOM dispatch](Lambda_Design_DOM_Dispatch.md), [DOM state](Lambda_Design_DOM_State.md), [Radiant overview](../doc/dev/radiant/RAD_00_Overview.md) and [test map](../test/README.md) — live package and engine integration context.
