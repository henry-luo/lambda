/**
 * @file webdriver_actions.cpp
 * @brief Element interactions and action chains
 * 
 * Reuses event simulation from radiant/event_sim.cpp
 */

#include "webdriver.hpp"
#include "../event.hpp"
#include "../state_store.hpp"
#include "../layout.hpp"
#include "../render_img.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lambda/input/css/dom_element.hpp"
#include <cstring>
#include <cstdlib>
#include <GLFW/glfw3.h>

// Event handling from radiant (reusing existing infrastructure)
extern void handle_event(UiContext* uicon, DomDocument* doc, RdtEvent* event);

// ============================================================================
// Event Simulation Helpers (adapted from event_sim.cpp)
// ============================================================================

static void sim_mouse_move(UiContext* uicon, int x, int y) {
    RdtEvent event;
    event.mouse_position.type = RDT_EVENT_MOUSE_MOVE;
    event.mouse_position.timestamp = 0;  // Will be set by handler
    event.mouse_position.x = x;
    event.mouse_position.y = y;
    handle_event(uicon, uicon->document, &event);
}

static void sim_mouse_button(UiContext* uicon, int x, int y, int button, int mods, bool is_down) {
    // First move to position
    sim_mouse_move(uicon, x, y);
    
    // Then press/release
    RdtEvent event;
    event.mouse_button.type = is_down ? RDT_EVENT_MOUSE_DOWN : RDT_EVENT_MOUSE_UP;
    event.mouse_button.timestamp = 0;
    event.mouse_button.x = x;
    event.mouse_button.y = y;
    event.mouse_button.button = button;
    event.mouse_button.clicks = 1;
    event.mouse_button.mods = mods;
    handle_event(uicon, uicon->document, &event);
}

static void sim_click(UiContext* uicon, int x, int y, int button) {
    sim_mouse_button(uicon, x, y, button, 0, true);
    sim_mouse_button(uicon, x, y, button, 0, false);
}

static void sim_key(UiContext* uicon, int key, int mods, bool is_down) {
    RdtEvent event;
    event.key.type = is_down ? RDT_EVENT_KEY_DOWN : RDT_EVENT_KEY_UP;
    event.key.timestamp = 0;
    event.key.key = key;
    event.key.scancode = 0;
    event.key.mods = mods;
    handle_event(uicon, uicon->document, &event);
}

static void sim_text_input(UiContext* uicon, uint32_t codepoint) {
    RdtEvent event;
    event.text_input.type = RDT_EVENT_TEXT_INPUT;
    event.text_input.timestamp = 0;
    event.text_input.codepoint = codepoint;
    handle_event(uicon, uicon->document, &event);
}

static void sim_scroll(UiContext* uicon, int x, int y, float dx, float dy) {
    RdtEvent event;
    event.scroll.type = RDT_EVENT_SCROLL;
    event.scroll.timestamp = 0;
    event.scroll.x = x;
    event.scroll.y = y;
    event.scroll.xoffset = dx;
    event.scroll.yoffset = dy;
    handle_event(uicon, uicon->document, &event);
}

// ============================================================================
// Element Center Calculation
// ============================================================================

static void get_element_center(View* view, float* cx, float* cy) {
    if (!view) {
        *cx = 0;
        *cy = 0;
        return;
    }
    
    // Calculate absolute position by walking up the tree
    float abs_x = 0, abs_y = 0;
    View* current = view;
    
    while (current) {
        abs_x += current->x;
        abs_y += current->y;
        current = current->parent;
    }
    
    // Get element dimensions
    float width = 0, height = 0;
    if (view->is_block()) {
        ViewBlock* block = (ViewBlock*)view;
        width = block->width;
        height = block->height;
    } else if (view->view_type == RDT_VIEW_INLINE) {
        ViewSpan* span = (ViewSpan*)view;
        width = span->width;
        height = span->height;
    }
    
    *cx = abs_x + width / 2;
    *cy = abs_y + height / 2;
}

// ============================================================================
// Element Actions
// ============================================================================

WebDriverError webdriver_element_click(WebDriverSession* session, View* element) {
    if (!session || !element) return WD_ERROR_INVALID_ARGUMENT;
    
    // Check if element is displayed and enabled
    if (!webdriver_element_is_displayed(session, element)) {
        return WD_ERROR_ELEMENT_NOT_INTERACTABLE;
    }
    
    if (!webdriver_element_is_enabled(session, element)) {
        return WD_ERROR_ELEMENT_NOT_INTERACTABLE;
    }
    
    // Get element center
    float cx, cy;
    get_element_center(element, &cx, &cy);
    
    log_info("webdriver: clicking element at (%.1f, %.1f)", cx, cy);
    
    // Simulate click
    sim_click(session->uicon, (int)cx, (int)cy, 0);
    
    return WD_SUCCESS;
}

WebDriverError webdriver_element_clear(WebDriverSession* session, View* element) {
    if (!session || !element) return WD_ERROR_INVALID_ARGUMENT;
    
    // Check if element is editable (input, textarea, contenteditable)
    if (!element->is_element()) return WD_ERROR_INVALID_ELEMENT_STATE;
    
    ViewElement* elem = (ViewElement*)element;
    uintptr_t tag = elem->tag();
    
    if (tag != HTM_TAG_INPUT && tag != HTM_TAG_TEXTAREA) {
        // Check for contenteditable
        const char* ce = elem->get_attribute("contenteditable");
        if (!ce || strcmp(ce, "true") != 0) {
            return WD_ERROR_INVALID_ELEMENT_STATE;
        }
    }
    
    // Click to focus
    webdriver_element_click(session, element);
    
    // Select all and delete
    sim_key(session->uicon, RDT_KEY_A, RDT_MOD_CTRL, true);
    sim_key(session->uicon, RDT_KEY_A, RDT_MOD_CTRL, false);
    sim_key(session->uicon, RDT_KEY_BACKSPACE, 0, true);
    sim_key(session->uicon, RDT_KEY_BACKSPACE, 0, false);
    
    return WD_SUCCESS;
}

WebDriverError webdriver_element_send_keys(WebDriverSession* session, View* element,
                                            const char* text) {
    if (!session || !element || !text) return WD_ERROR_INVALID_ARGUMENT;
    
    // Click to focus first
    webdriver_element_click(session, element);
    
    // Send each character as text input
    const unsigned char* p = (const unsigned char*)text;
    while (*p) {
        uint32_t codepoint;
        
        // Decode UTF-8
        if ((*p & 0x80) == 0) {
            codepoint = *p++;
        } else if ((*p & 0xE0) == 0xC0) {
            codepoint = (*p++ & 0x1F) << 6;
            codepoint |= (*p++ & 0x3F);
        } else if ((*p & 0xF0) == 0xE0) {
            codepoint = (*p++ & 0x0F) << 12;
            codepoint |= (*p++ & 0x3F) << 6;
            codepoint |= (*p++ & 0x3F);
        } else if ((*p & 0xF8) == 0xF0) {
            codepoint = (*p++ & 0x07) << 18;
            codepoint |= (*p++ & 0x3F) << 12;
            codepoint |= (*p++ & 0x3F) << 6;
            codepoint |= (*p++ & 0x3F);
        } else {
            p++;  // Invalid UTF-8, skip
            continue;
        }
        
        // Handle special characters (WebDriver uses Unicode PUA for special keys)
        if (codepoint >= 0xE000 && codepoint <= 0xE00F) {
            // WebDriver special keys
            int key = 0;
            switch (codepoint) {
                case 0xE003: key = RDT_KEY_BACKSPACE; break;
                case 0xE004: key = RDT_KEY_TAB; break;
                case 0xE006: key = RDT_KEY_ENTER; break;
                case 0xE00C: key = RDT_KEY_ESCAPE; break;
                case 0xE010: key = RDT_KEY_END; break;
                case 0xE011: key = RDT_KEY_HOME; break;
                case 0xE012: key = RDT_KEY_LEFT; break;
                case 0xE013: key = RDT_KEY_UP; break;
                case 0xE014: key = RDT_KEY_RIGHT; break;
                case 0xE015: key = RDT_KEY_DOWN; break;
                case 0xE017: key = RDT_KEY_DELETE; break;
                default: continue;
            }
            sim_key(session->uicon, key, 0, true);
            sim_key(session->uicon, key, 0, false);
        } else {
            // Regular text input
            sim_text_input(session->uicon, codepoint);
        }
    }
    
    return WD_SUCCESS;
}

// ============================================================================
// Element Properties
// ============================================================================

char* webdriver_element_get_text(WebDriverSession* session, View* element) {
    if (!session || !element) return NULL;
    
    StrBuf* buf = strbuf_new();
    
    // Extract text recursively - simplified for now
    // TODO: Full text extraction
    
    char* result = strdup(buf->str ? buf->str : "");
    strbuf_free(buf);
    return result;
}

const char* webdriver_element_get_attribute(WebDriverSession* session, View* element,
                                             const char* name) {
    if (!session || !element || !name) return NULL;
    if (!element->is_element()) return NULL;
    
    ViewElement* elem = (ViewElement*)element;
    return elem->get_attribute(name);
}

const char* webdriver_element_get_css(WebDriverSession* session, View* element,
                                       const char* property) {
    if (!session || !element || !property) return NULL;
    
    // TODO: Lookup computed CSS value
    return "";
}

void webdriver_element_get_rect(WebDriverSession* session, View* element,
                                 float* x, float* y, float* width, float* height) {
    if (!session || !element) {
        *x = *y = *width = *height = 0;
        return;
    }
    
    // Calculate absolute position
    float abs_x = 0, abs_y = 0;
    View* current = element;
    while (current) {
        abs_x += current->x;
        abs_y += current->y;
        current = current->parent;
    }
    
    *x = abs_x;
    *y = abs_y;
    
    if (element->is_block()) {
        ViewBlock* block = (ViewBlock*)element;
        *width = block->width;
        *height = block->height;
    } else if (element->view_type == RDT_VIEW_INLINE) {
        ViewSpan* span = (ViewSpan*)element;
        *width = span->width;
        *height = span->height;
    } else {
        *width = *height = 0;
    }
}

bool webdriver_element_is_enabled(WebDriverSession* session, View* element) {
    if (!session || !element) return false;
    if (!element->is_element()) return true;
    
    DomElement* dom_elem = (DomElement*)element;
    
    // Check disabled pseudo-state
    if (dom_elem->pseudo_state & PSEUDO_STATE_DISABLED) {
        return false;
    }
    
    // Check disabled attribute
    ViewElement* elem = (ViewElement*)element;
    const char* disabled = elem->get_attribute("disabled");
    if (disabled) {
        return false;
    }
    
    return true;
}

bool webdriver_element_is_displayed(WebDriverSession* session, View* element) {
    if (!session || !element) return false;
    
    // Check if element has dimensions
    if (element->is_block()) {
        ViewBlock* block = (ViewBlock*)element;
        if (block->width <= 0 || block->height <= 0) {
            return false;
        }
        
        // Check display: none
        if (block->display.outer == CSS_VALUE_NONE) {
            return false;
        }
        
        // Check visibility: hidden
        if (element->is_element()) {
            DomElement* dom = (DomElement*)element;
            // TODO: Check computed visibility property
        }
    }
    
    return true;
}

bool webdriver_element_is_selected(WebDriverSession* session, View* element) {
    if (!session || !element) return false;
    if (!element->is_element()) return false;
    
    DomElement* dom_elem = (DomElement*)element;
    
    // Check checked pseudo-state
    if (dom_elem->pseudo_state & PSEUDO_STATE_CHECKED) {
        return true;
    }
    
    // Check selected attribute (for option elements)
    ViewElement* elem = (ViewElement*)element;
    const char* selected = elem->get_attribute("selected");
    if (selected) {
        return true;
    }
    
    const char* checked = elem->get_attribute("checked");
    if (checked) {
        return true;
    }
    
    return false;
}

// ============================================================================
// Screenshots
// ============================================================================

// Simple base64 encoding table
static const char base64_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char* base64_encode_data(const unsigned char* data, size_t len) {
    size_t out_len = 4 * ((len + 2) / 3);
    char* result = (char*)malloc(out_len + 1);
    if (!result) return NULL;
    
    size_t i = 0, j = 0;
    while (i < len) {
        uint32_t octet_a = i < len ? data[i++] : 0;
        uint32_t octet_b = i < len ? data[i++] : 0;
        uint32_t octet_c = i < len ? data[i++] : 0;
        
        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
        
        result[j++] = base64_chars[(triple >> 18) & 0x3F];
        result[j++] = base64_chars[(triple >> 12) & 0x3F];
        result[j++] = base64_chars[(triple >> 6) & 0x3F];
        result[j++] = base64_chars[triple & 0x3F];
    }
    
    // Add padding
    size_t mod = len % 3;
    if (mod == 1) {
        result[out_len - 2] = '=';
        result[out_len - 1] = '=';
    } else if (mod == 2) {
        result[out_len - 1] = '=';
    }
    
    result[out_len] = '\0';
    return result;
}

char* webdriver_screenshot(WebDriverSession* session) {
    if (!session || !session->uicon) return NULL;
    
    // Render to temporary file
    const char* tmp_file = "/tmp/radiant_screenshot.png";
    
    if (render_uicontext_to_png(session->uicon, tmp_file) != 0) {
        log_error("webdriver: screenshot failed");
        return NULL;
    }
    
    // Read file and base64 encode
    FILE* f = fopen(tmp_file, "rb");
    if (!f) {
        log_error("webdriver: failed to open screenshot file");
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    unsigned char* data = (unsigned char*)malloc(size);
    if (!data) {
        fclose(f);
        return NULL;
    }
    
    fread(data, 1, size, f);
    fclose(f);
    
    // Base64 encode
    char* encoded = base64_encode_data(data, size);
    free(data);
    
    // Clean up temp file
    remove(tmp_file);
    
    return encoded;
}

char* webdriver_element_screenshot(WebDriverSession* session, View* element) {
    // TODO: Implement element-specific screenshot with clipping
    // For now, return full page screenshot
    return webdriver_screenshot(session);
}

// ============================================================================
// Actions API
// ============================================================================

WebDriverError webdriver_perform_actions(WebDriverSession* session, ArrayList* actions) {
    if (!session || !actions) return WD_ERROR_INVALID_ARGUMENT;
    
    for (int i = 0; i < actions->length; i++) {
        WebDriverAction* action = (WebDriverAction*)actions->data[i];
        
        switch (action->type) {
            case ACTION_PAUSE:
                // Sleep for duration (in real implementation, would use async)
                // For now, skip pause
                break;
                
            case ACTION_KEY_DOWN:
                sim_key(session->uicon, action->key.key, 0, true);
                break;
                
            case ACTION_KEY_UP:
                sim_key(session->uicon, action->key.key, 0, false);
                break;
                
            case ACTION_POINTER_DOWN:
                sim_mouse_button(session->uicon, 
                                 action->pointer.x, action->pointer.y,
                                 action->pointer.button, 0, true);
                break;
                
            case ACTION_POINTER_UP:
                sim_mouse_button(session->uicon,
                                 action->pointer.x, action->pointer.y,
                                 action->pointer.button, 0, false);
                break;
                
            case ACTION_POINTER_MOVE: {
                int x = action->pointer.x;
                int y = action->pointer.y;
                
                // If origin is an element, calculate relative to element center
                if (action->pointer.origin) {
                    float cx, cy;
                    get_element_center(action->pointer.origin, &cx, &cy);
                    x += (int)cx;
                    y += (int)cy;
                }
                
                sim_mouse_move(session->uicon, x, y);
                break;
            }
                
            case ACTION_SCROLL:
                sim_scroll(session->uicon,
                           action->scroll.x, action->scroll.y,
                           (float)action->scroll.dx, (float)action->scroll.dy);
                break;
                
            case ACTION_POINTER_CANCEL:
                // Release all buttons
                break;
        }
    }
    
    return WD_SUCCESS;
}

WebDriverError webdriver_release_actions(WebDriverSession* session) {
    if (!session) return WD_ERROR_INVALID_ARGUMENT;
    
    // Release any pressed keys/buttons
    // TODO: Track pressed state and release
    
    return WD_SUCCESS;
}
