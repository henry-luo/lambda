# Radiant Form Elements Support - Design Proposal

## Overview

This proposal adds HTML form element support to Radiant for rendering `<input>`, `<button>`, `<select>`, `<textarea>`, `<label>`, and `<fieldset>` elements with proper layout and visual presentation.

**Scope:** Static rendering and layout only. Interactive editing (typing, selection) is out of scope for initial implementation.

---

## 1. Form Element Types

### 1.1 Supported Elements

| Element | Input Types | Display | Notes |
|---------|-------------|---------|-------|
| `<input>` | text, password, email, url, search, tel, number | inline-block | Single-line text box |
| `<input>` | checkbox, radio | inline-block | Toggle controls |
| `<input>` | submit, reset, button | inline-block | Button appearance |
| `<input>` | hidden | none | No visual representation |
| `<input>` | range | inline-block | Slider control |
| `<button>` | submit, reset, button | inline-block | Content-based button |
| `<select>` | - | inline-block | Dropdown (closed state only) |
| `<textarea>` | - | inline-block | Multi-line text box |
| `<label>` | - | inline | Text label |
| `<fieldset>` | - | block | Group container with border |
| `<legend>` | - | block | Fieldset caption |

### 1.2 Replaced Element Model

Form controls are **replaced elements** (like `<img>`), meaning their intrinsic size comes from the control type, not content flow.

---

## 2. Architecture

### 2.1 New Structures

```cpp
// view.hpp additions

// Form control types (for ViewFormControl)
enum FormControlType {
    FORM_CONTROL_TEXT = 1,      // text, password, email, etc.
    FORM_CONTROL_CHECKBOX,
    FORM_CONTROL_RADIO,
    FORM_CONTROL_BUTTON,
    FORM_CONTROL_SELECT,
    FORM_CONTROL_TEXTAREA,
    FORM_CONTROL_RANGE,
};

// Form control property structure
struct FormControlProp {
    FormControlType control_type;
    const char* input_type;     // Original type attribute
    const char* value;          // Current value
    const char* placeholder;    // Placeholder text
    const char* name;           // Form field name

    // Dimensions
    int size;                   // Character width for text inputs
    int cols, rows;             // For textarea

    // State flags
    uint8_t disabled : 1;
    uint8_t readonly : 1;
    uint8_t checked : 1;        // For checkbox/radio
    uint8_t required : 1;

    // Range input
    float min, max, step;

    // Intrinsic dimensions (computed)
    float intrinsic_width;
    float intrinsic_height;
};

// Extend HTM_TAG enum (already exists for INPUT, BUTTON, etc.)
// HTM_TAG_INPUT, HTM_TAG_BUTTON, HTM_TAG_SELECT, HTM_TAG_TEXTAREA - already defined
```

### 2.2 Integration Points

| File | Changes |
|------|---------|
| `view.hpp` | Add `FormControlProp`, form control enum |
| `dom_element.hpp` | Add `FormControlProp* form` to DomElement union |
| `resolve_htm_style.cpp` | Default styles for form elements |
| `resolve_css_style.cpp` | CSS property handling for form controls |
| `layout_block.cpp` | Layout dispatch for form controls |
| `render.cpp` | Rendering for form controls |

---

## 3. Layout Algorithm

### 3.1 Intrinsic Sizing

Form controls have UA-defined intrinsic sizes:

```cpp
// Default intrinsic sizes (in CSS pixels, multiply by pixel_ratio)
struct FormControlDefaults {
    // Text input: ~20 chars wide, 1 line tall + padding
    static constexpr float TEXT_WIDTH = 173;   // Chrome default
    static constexpr float TEXT_HEIGHT = 21;

    // Checkbox/Radio: square controls
    static constexpr float CHECK_SIZE = 13;

    // Button: content-based + padding
    static constexpr float BUTTON_PADDING_H = 12;
    static constexpr float BUTTON_PADDING_V = 4;

    // Select: similar to text input
    static constexpr float SELECT_WIDTH = 173;
    static constexpr float SELECT_HEIGHT = 21;

    // Textarea: cols/rows based
    static constexpr int TEXTAREA_COLS = 20;
    static constexpr int TEXTAREA_ROWS = 2;
};
```

### 3.2 Layout Function

```cpp
// layout_form.cpp (new file)

void layout_form_control(LayoutContext* lycon, DomElement* elem) {
    FormControlProp* form = elem->form;
    float pixel_ratio = lycon->ui_context->pixel_ratio;

    switch (form->control_type) {
    case FORM_CONTROL_TEXT:
        // Width from size attr or CSS width or default
        // Height from line-height + padding or default
        break;
    case FORM_CONTROL_CHECKBOX:
    case FORM_CONTROL_RADIO:
        // Fixed intrinsic size
        break;
    case FORM_CONTROL_BUTTON:
        // Content-based width + padding
        break;
    case FORM_CONTROL_TEXTAREA:
        // cols * char_width, rows * line_height
        break;
    // ...
    }
}
```

---

## 4. Rendering

### 4.1 Native-Style Rendering

Draw form controls with system-like appearance:

```cpp
// render_form.cpp (new file)

void render_form_control(RenderContext* rdcon, ViewBlock* block) {
    FormControlProp* form = block->form;

    switch (form->control_type) {
    case FORM_CONTROL_TEXT:
        render_text_input(rdcon, block);
        break;
    case FORM_CONTROL_CHECKBOX:
        render_checkbox(rdcon, block, form->checked);
        break;
    case FORM_CONTROL_RADIO:
        render_radio(rdcon, block, form->checked);
        break;
    case FORM_CONTROL_BUTTON:
        render_button(rdcon, block);
        break;
    // ...
    }
}

void render_text_input(RenderContext* rdcon, ViewBlock* block) {
    // 1. Draw border (typically inset-style)
    // 2. Draw background (white)
    // 3. Draw value text or placeholder (grayed)
}

void render_checkbox(RenderContext* rdcon, ViewBlock* block, bool checked) {
    // 1. Draw box border
    // 2. If checked, draw checkmark
}

void render_button(RenderContext* rdcon, ViewBlock* block) {
    // 1. Draw 3D border effect
    // 2. Draw background gradient/color
    // 3. Render button text (child content or value)
}
```

### 4.2 CSS Styling Support

Form elements should respect CSS properties:

- `width`, `height` - Override intrinsic size
- `padding`, `margin`, `border` - Box model
- `background-color`, `color` - Colors
- `font-*` - Text styling
- `border-radius` - Rounded corners

---

## 5. Default Styles

Add to `resolve_htm_style.cpp`:

```cpp
case HTM_TAG_INPUT: {
    const char* type = elem->get_attribute("type");
    if (!type) type = "text";

    // Allocate form control prop
    if (!block->form) {
        block->form = alloc_form_control_prop(lycon);
        parse_input_attributes(block->form, elem);
    }

    // Set display based on type
    if (strcmp(type, "hidden") == 0) {
        block->display.outer = CSS_VALUE_NONE;
    } else {
        block->display.outer = CSS_VALUE_INLINE_BLOCK;
    }

    // Default border for text-like inputs
    if (is_text_input(type)) {
        init_input_border(block, lycon);
    }
    break;
}

case HTM_TAG_BUTTON:
    block->display.outer = CSS_VALUE_INLINE_BLOCK;
    init_button_style(block, lycon);
    break;

case HTM_TAG_TEXTAREA:
    block->display.outer = CSS_VALUE_INLINE_BLOCK;
    init_textarea_style(block, lycon);
    break;

case HTM_TAG_SELECT:
    block->display.outer = CSS_VALUE_INLINE_BLOCK;
    init_select_style(block, lycon);
    break;

case HTM_TAG_FIELDSET:
    block->display.outer = CSS_VALUE_BLOCK;
    // Default: 2px groove border, 0.35em padding
    init_fieldset_style(block, lycon);
    break;

case HTM_TAG_LEGEND:
    block->display.outer = CSS_VALUE_BLOCK;
    // Positioned within fieldset border
    break;
```

---

## 6. Implementation Plan

### Phase 1: Basic Structure (2-3 days)
- [ ] Add `FormControlProp` to view.hpp
- [ ] Add form field to DomElement union
- [ ] Default styles in resolve_htm_style.cpp
- [ ] Basic layout dispatch

### Phase 2: Text Inputs (2-3 days)
- [ ] Layout for text, password, email, search
- [ ] Render text input box with value/placeholder
- [ ] CSS property support (width, padding, border)

### Phase 3: Buttons & Toggle Controls (2 days)
- [ ] Button layout and rendering
- [ ] Checkbox/radio rendering (checked state)
- [ ] Submit/reset button appearance

### Phase 4: Select & Textarea (2 days)
- [ ] Select dropdown (closed state)
- [ ] Textarea multi-line display
- [ ] Fieldset/legend positioning

### Phase 5: Testing & Polish (2 days)
- [ ] Add layout test cases
- [ ] Browser comparison tests
- [ ] Edge cases (disabled, readonly states)

---

## 7. Files to Create/Modify

### New Files
- `radiant/form_control.hpp` - Form control types and defaults
- `radiant/layout_form.cpp` - Form element layout
- `radiant/render_form.cpp` - Form element rendering

### Modified Files
- `radiant/view.hpp` - FormControlProp struct
- `lambda/input/css/dom_element.hpp` - Add form prop to union
- `radiant/resolve_htm_style.cpp` - Default form styles
- `radiant/layout_block.cpp` - Layout dispatch
- `radiant/render.cpp` - Render dispatch
- `build_lambda_config.json` - Add new source files

---

## 8. Testing Strategy

### Layout Tests
```
test/layout/data/form/
├── input_text.html      # Text input sizing
├── input_checkbox.html  # Checkbox/radio layout
├── button.html          # Button with content
├── select.html          # Select dropdown
├── textarea.html        # Multi-line textarea
├── fieldset.html        # Fieldset/legend
└── form_styled.html     # CSS-styled forms
```

### Verification
- Compare against Chrome/Firefox reference layouts
- Verify intrinsic sizes match browser defaults
- Test CSS override behavior

---

## 9. Out of Scope (Future Work)

- Text editing/caret support
- Input validation feedback
- Dropdown menu expansion
- File input (`<input type="file">`)
- Date/time pickers
- Color picker
- Form submission
- Event handlers (onclick, onchange)

---

## References

- [HTML Living Standard - Forms](https://html.spec.whatwg.org/multipage/forms.html)
- [CSS Basic User Interface Module](https://www.w3.org/TR/css-ui-4/)
- [Replaced Elements (MDN)](https://developer.mozilla.org/en-US/docs/Web/CSS/Replaced_element)
