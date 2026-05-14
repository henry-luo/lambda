# Radiant Memory Safety Issues

This note summarizes the remaining Radiant memory-safety surfaces after the
ownership-helper and cast-helper migration work. As of this pass,
`radiant/view_pool.cpp`, `radiant/state_store.cpp`,
`radiant/dom_range_resolver.cpp`, and `radiant/source_pos_bridge.cpp` are also
inside `make check-radiant-casts`.

## Remaining Risk Areas

1. Raw `View*` / `Dom*` downcasts outside the migrated gate

   The largest rendering/event/WebDriver/state files are now covered by
   `make check-radiant-casts`, including `view_pool.cpp`, `state_store.cpp`,
   `dom_range_resolver.cpp`, and `source_pos_bridge.cpp`. Remaining smaller
   sites in context menu, text editing, CSS animation, UI context, and state
   machine code still rely on runtime tags being correct without expressing
   that precondition through `lib/tagged.hpp`.

2. Manual ownership islands

   Some subsystems still use plain `malloc` / `free` / `strdup`, notably:
   `radiant/clipboard.cpp`, `radiant/source_pos_bridge.cpp`,
   `radiant/event_state_log.cpp`, and a form value path in
   `radiant/state_store.cpp`. These may be internally valid, but they are not
   covered by the ownership templates, so leaks, double-free, and dangling
   ownership remain easier to introduce.

3. Retained view/state pointers across relayout and navigation

   State, focus, selection, drag/drop, event targeting, and display-list code
   retain or borrow pointers into DOM/view objects that can be rebuilt. There
   are cleanup and rebind mechanisms, but this is still a likely use-after-free
   class if an invalidation path is missed.

4. Unchecked dynamic arrays and `ArrayList->data`

   Many paths cast `void*` list entries and index raw arrays directly. This is
   normal for the codebase, but type and bounds safety are convention-based.

5. Direct `mem_realloc` assignment

   Some grid/flex helpers assign `mem_realloc` directly back to owned fields.
   If allocation failure is possible, the original pointer can be lost or
   partial state can remain. This is lower likelihood in typical runs, but it
   is a hardening target.

6. `/tmp` usage in WebDriver screenshots

   `radiant/webdriver/webdriver_actions.cpp` still writes screenshots to
   `/tmp/radiant_screenshot.png`. That violates the repo rule to use `./temp/`
   and also has collision/TOCTOU risk.

## Recommended Next Migration Step

Continue with the smaller unguarded Radiant files, especially state-machine,
context-menu, text-editing, CSS-animation, and UI-context paths. After each
batch, expand `make check-radiant-casts` again so the migration stays enforced.
