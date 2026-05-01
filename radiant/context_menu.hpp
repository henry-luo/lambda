#pragma once

// F8 (Radiant_Design_Form_Input.md §3.10): native context menu for text
// controls. Items: Cut, Copy, Paste, Delete, Select All. Right-click on a
// text control opens it; click outside / Esc closes it.
//
// Callers must include "view.hpp" and "render.hpp" before this header so
// that View / RenderContext are full types (both are typedefs of
// anonymous structs and cannot be forward-declared).

#include <stdint.h>

struct RadiantState;

// Number of items always 5: Cut, Copy, Paste, Delete, Select All.
#define CTX_MENU_ITEM_COUNT 5

enum CtxMenuItem {
    CTX_MENU_CUT        = 0,
    CTX_MENU_COPY       = 1,
    CTX_MENU_PASTE      = 2,
    CTX_MENU_DELETE     = 3,
    CTX_MENU_SELECT_ALL = 4,
};

// Open the menu at the given screen-space (physical px) coordinates,
// targeting the focused/clicked text control. No-op if `target` is not a
// text control. Computes width/height and stores hit-test rect in state.
void context_menu_open(RadiantState* state, View* target, float x, float y);

// Close the menu (no-op if already closed).
void context_menu_close(RadiantState* state);

// Mouse-move hit test inside the open menu; updates `context_menu_hover`.
// Returns true if (x,y) is inside the menu rect.
bool context_menu_hover(RadiantState* state, float x, float y);

// Mouse-up hit test; if the cursor is over an enabled item, executes the
// command against `context_menu_target` and closes the menu. Returns true
// if the click landed inside the menu rect (whether or not it triggered
// an action).
bool context_menu_click(RadiantState* state, float x, float y);

// True iff (x,y) is inside the popup. Used to keep clicks inside the menu
// from being routed to the underlying view.
bool context_menu_contains(RadiantState* state, float x, float y);

// Whether a given item should render disabled. Wraps the per-item rules
// (Cut/Copy/Delete need a non-empty selection; Paste needs clipboard text).
bool context_menu_item_enabled(RadiantState* state, int item);

// Render the popup overlay. Called from render.cpp after the dropdown
// overlay so it appears on top.
void context_menu_render(RenderContext* rdcon, RadiantState* state);
