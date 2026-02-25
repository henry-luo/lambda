# Radiant WebDriver Support Proposal

## Executive Summary

This proposal outlines a plan to implement WebDriver protocol support in the Radiant HTML/CSS rendering engine, enabling automated testing with standard browser automation frameworks like Selenium, Puppeteer, and Playwright. By leveraging Radiant's existing event simulation infrastructure, we can provide WebDriver-compatible automation with minimal architectural changes.

---

## 1. Motivation

### 1.1 Current State

Radiant currently supports automated testing through:
- **Event Simulator** (`radiant/event_sim.cpp`) - JSON-based event playback with assertions
- **State Store** (`radiant/state_store.cpp`) - Centralized UI state management (caret, selection, focus, hover, etc.)
- **Event System** (`radiant/event.cpp`) - Full event handling with pseudo-state updates

While the Event Simulator is useful for internal testing, it requires Radiant-specific JSON formats that are not compatible with standard browser automation tools.

### 1.2 Benefits of WebDriver Support

1. **Standard Automation**: Use existing Selenium/Puppeteer/Playwright tests with Radiant
2. **Cross-Engine Comparison**: Run the same test suite against Radiant and real browsers
3. **CI/CD Integration**: Leverage existing browser automation infrastructure
4. **Accessibility Testing**: Use standard accessibility testing tools
5. **Visual Regression**: Integrate with screenshot comparison tools
6. **Developer Adoption**: Lower barrier to entry for testing with Radiant

---

## 2. Architecture Overview

### 2.1 WebDriver Protocol

The [W3C WebDriver specification](https://www.w3.org/TR/webdriver2/) defines a REST-based protocol for browser automation. Key components:

```
┌─────────────────┐     HTTP/JSON      ┌───────────────────────────┐
│  Test Script    │◄──────────────────►│  WebDriver Server         │
│  (Selenium/     │                    │  (radiant-webdriver)      │
│   Playwright)   │                    │                           │
└─────────────────┘                    └───────────────────────────┘
                                                    │
                                                    ▼
                                       ┌───────────────────────────┐
                                       │  Radiant Engine           │
                                       │  - DOM Document           │
                                       │  - State Store            │
                                       │  - Event Handler          │
                                       │  - View Tree              │
                                       └───────────────────────────┘
```

### 2.2 Proposed Components

| Component | Location | Purpose |
|-----------|----------|---------|
| `lib/serve/server.h` | **Existing** | HTTP server infrastructure (libevent) |
| `lib/serve/http_handler.h` | **Existing** | Request/response handling |
| `radiant/webdriver.hpp` | New | WebDriver API declarations |
| `radiant/webdriver.cpp` | New | WebDriver command handlers |
| `radiant/webdriver_server.cpp` | New | WebDriver-specific routing (uses lib/serve) |
| `radiant/webdriver_session.hpp` | New | Session and element management |
| `radiant/webdriver_locator.cpp` | New | CSS selector, XPath, etc. |

---

## 3. Reusing Existing Infrastructure

### 3.1 Event Simulation Layer

The existing `event_sim.cpp` provides essential primitives that map directly to WebDriver actions:

| Event Simulator Function | WebDriver Command |
|--------------------------|-------------------|
| `sim_mouse_move()` | Element Click (move phase) |
| `sim_mouse_button()` | Element Click, Mouse Actions |
| `sim_key()` | Send Keys, Key Actions |
| `sim_scroll()` | Scroll, Wheel Actions |
| `find_text_position()` | Element location (text-based) |

**Existing code to reuse:**

```cpp
// From event_sim.cpp - can be extracted to webdriver_actions.cpp
static void sim_mouse_move(UiContext* uicon, int x, int y);
static void sim_mouse_button(UiContext* uicon, int x, int y, int button, int mods, bool is_down);
static void sim_key(UiContext* uicon, int key, int mods, bool is_down);
static void sim_scroll(UiContext* uicon, int x, int y, float dx, float dy);
```

### 3.2 State Store Integration

The `RadiantState` structure already tracks the UI state needed for WebDriver:

| State | WebDriver Use |
|-------|---------------|
| `caret` | Get active element, text input cursor |
| `selection` | Get selected text |
| `focus` | Get focused element |
| `hover_target` | Verify hover state |
| `active_target` | Verify click state |

### 3.3 DOM Element Access

The `DomElement` structure provides necessary element metadata:

```cpp
// From dom_element.hpp
struct DomElement {
    const char* tag_name;        // For element info
    const char* id;              // For ID locator
    const char** class_names;    // For class locator
    Element* native_element;     // For attribute access
    uint32_t pseudo_state;       // For enabled/visible checks
    // ... layout properties for bounds
};
```

---

## 4. WebDriver Commands Implementation

### 4.1 Session Management

| Endpoint | Command | Implementation |
|----------|---------|----------------|
| `POST /session` | New Session | Create UiContext, load capabilities |
| `DELETE /session/{id}` | Delete Session | Cleanup, close window |
| `GET /session/{id}/timeouts` | Get Timeouts | Return timeout values |
| `POST /session/{id}/timeouts` | Set Timeouts | Set implicit/page load/script timeouts |

### 4.2 Navigation

| Endpoint | Command | Implementation |
|----------|---------|----------------|
| `POST /session/{id}/url` | Navigate To | `load_html_doc()` with new URL |
| `GET /session/{id}/url` | Get Current URL | Return `doc->url` |
| `POST /session/{id}/back` | Back | History navigation (requires history stack) |
| `POST /session/{id}/forward` | Forward | History navigation |
| `POST /session/{id}/refresh` | Refresh | Reload current document |
| `GET /session/{id}/title` | Get Title | Extract `<title>` content |

### 4.3 Element Finding

| Endpoint | Command | Implementation |
|----------|---------|----------------|
| `POST /session/{id}/element` | Find Element | CSS selector, ID, XPath lookup |
| `POST /session/{id}/elements` | Find Elements | Return array of matches |
| `POST /session/{id}/element/{id}/element` | Find From Element | Scoped search |
| `GET /session/{id}/element/active` | Get Active Element | `state->focus->current` |

**Element Locator Strategies:**

```cpp
typedef enum {
    LOCATOR_CSS_SELECTOR,      // CSS selector (primary)
    LOCATOR_LINK_TEXT,         // <a> text content
    LOCATOR_PARTIAL_LINK_TEXT, // Partial <a> text
    LOCATOR_TAG_NAME,          // Element tag
    LOCATOR_XPATH,             // XPath expression (optional)
} LocatorStrategy;

// Proposed API
View* webdriver_find_element(DomDocument* doc, LocatorStrategy strategy, const char* value);
ArrayList* webdriver_find_elements(DomDocument* doc, LocatorStrategy strategy, const char* value);
```

### 4.4 Element Interaction

| Endpoint | Command | Implementation |
|----------|---------|----------------|
| `POST /session/{id}/element/{id}/click` | Click | `sim_mouse_button()` at element center |
| `POST /session/{id}/element/{id}/clear` | Clear | Clear text input value |
| `POST /session/{id}/element/{id}/value` | Send Keys | `sim_key()` sequence |
| `GET /session/{id}/element/{id}/text` | Get Text | Extract text content |
| `GET /session/{id}/element/{id}/attribute/{name}` | Get Attribute | `elem->get_attribute()` |
| `GET /session/{id}/element/{id}/property/{name}` | Get Property | DOM property access |
| `GET /session/{id}/element/{id}/css/{name}` | Get CSS Value | Computed style lookup |
| `GET /session/{id}/element/{id}/rect` | Get Rect | Element bounds from layout |
| `GET /session/{id}/element/{id}/enabled` | Is Enabled | Check `PSEUDO_STATE_DISABLED` |
| `GET /session/{id}/element/{id}/selected` | Is Selected | Check `PSEUDO_STATE_CHECKED` |
| `GET /session/{id}/element/{id}/displayed` | Is Displayed | Visibility check |

### 4.5 Actions API (W3C Actions)

The Actions API enables complex multi-step interactions:

```cpp
typedef struct WebDriverAction {
    enum { ACTION_PAUSE, ACTION_KEY_DOWN, ACTION_KEY_UP, 
           ACTION_POINTER_DOWN, ACTION_POINTER_UP, ACTION_POINTER_MOVE,
           ACTION_SCROLL } type;
    union {
        struct { int duration; } pause;
        struct { int key; } key;
        struct { int button; int x; int y; } pointer;
        struct { float dx, dy; int x, y; } scroll;
    };
} WebDriverAction;

void webdriver_perform_actions(UiContext* uicon, ArrayList* actions);
```

This maps directly to our existing event simulation functions.

### 4.6 Document/Window

| Endpoint | Command | Implementation |
|----------|---------|----------------|
| `GET /session/{id}/source` | Get Page Source | Serialize DOM to HTML |
| `POST /session/{id}/execute/sync` | Execute Script | Not applicable (no JS engine) |
| `GET /session/{id}/window/rect` | Get Window Rect | Return viewport size |
| `POST /session/{id}/window/rect` | Set Window Rect | Resize viewport |
| `GET /session/{id}/screenshot` | Screenshot | `render_uicontext_to_png()` |
| `GET /session/{id}/element/{id}/screenshot` | Element Screenshot | Clip to element bounds |

---

## 5. Element Reference Management

WebDriver uses opaque element references (UUIDs) to identify elements:

```cpp
typedef struct ElementRef {
    char uuid[37];               // UUID string (e.g., "550e8400-e29b-41d4-a716-446655440000")
    View* view;                  // Pointer to View (or DomElement)
    uint64_t document_version;   // For stale element detection
} ElementRef;

typedef struct ElementRegistry {
    HashMap* refs;               // UUID -> ElementRef
    Arena* arena;                // For ref allocation
    uint64_t next_id;            // For UUID generation
} ElementRegistry;

// API
const char* element_registry_add(ElementRegistry* reg, View* view);
View* element_registry_get(ElementRegistry* reg, const char* uuid);
bool element_registry_is_stale(ElementRegistry* reg, const char* uuid);
void element_registry_clear(ElementRegistry* reg);
```

---

## 6. CSS Selector Engine

For WebDriver's primary locator strategy (CSS selectors), we can leverage the existing selector matcher:

```cpp
// From lambda/input/css/selector_matcher.cpp
bool selector_matcher_matches(SelectorMatcher* matcher, DomElement* element);
```

**New API needed:**

```cpp
// webdriver_locator.cpp

/**
 * Find first element matching CSS selector
 * @param doc Document to search
 * @param selector CSS selector string
 * @return First matching View*, or NULL
 */
View* css_select_one(DomDocument* doc, const char* selector);

/**
 * Find all elements matching CSS selector
 * @param doc Document to search
 * @param selector CSS selector string
 * @param results ArrayList to populate with View*
 * @return Number of matches
 */
int css_select_all(DomDocument* doc, const char* selector, ArrayList* results);
```

**Implementation approach:**
1. Parse selector using existing CSS parser (`css_parse_selector()`)
2. Walk DOM tree (DomNode traversal)
3. Test each element with `selector_matcher_matches()`
4. Return matching elements

---

## 7. HTTP Server Implementation

### 7.1 Reuse Existing `lib/serve/` Infrastructure

The project already has a production-ready HTTP server implementation in `lib/serve/` using libevent. This will be reused for WebDriver:

**Existing files to leverage:**
| File | Purpose |
|------|---------|
| `lib/serve/server.h` | Server lifecycle, configuration |
| `lib/serve/server.c` | Event-driven HTTP server (libevent) |
| `lib/serve/http_handler.h` | Request parsing, response generation |
| `lib/serve/http_handler.c` | HTTP helpers (status codes, headers, body) |

### 7.2 Server Integration

```cpp
// webdriver_server.cpp - wraps lib/serve for WebDriver

#include "../lib/serve/server.h"
#include "../lib/serve/http_handler.h"

typedef struct WebDriverServer {
    server_t* http_server;       // From lib/serve
    HashMap* sessions;           // Session ID -> WebDriverSession
    UiContext* default_uicon;    // For headless sessions
} WebDriverServer;

WebDriverServer* webdriver_server_create(int port) {
    WebDriverServer* wd = calloc(1, sizeof(WebDriverServer));
    
    // Use existing server infrastructure
    server_config_t config = server_config_default();
    config.port = port;
    config.ssl_port = 0;  // WebDriver typically HTTP only
    
    wd->http_server = server_create(&config);
    wd->sessions = hashmap_new(...);
    
    // Register WebDriver route handler
    server_set_default_handler(wd->http_server, webdriver_request_handler, wd);
    
    return wd;
}
```

### 7.3 Request Handler Using `http_handler.h`

```cpp
// Leverage existing http_request_t and http_response_t

static void webdriver_request_handler(struct evhttp_request *req, void *user_data) {
    WebDriverServer* wd = (WebDriverServer*)user_data;
    
    // Use existing request parsing
    http_request_t* request = http_request_create(req);
    http_response_t* response = http_response_create(req);
    
    // Set JSON content type for all WebDriver responses
    http_response_set_header(response, "Content-Type", "application/json; charset=utf-8");
    
    // Route to appropriate WebDriver command handler
    webdriver_route(wd, request, response);
    
    http_response_send(response);
    http_request_destroy(request);
    http_response_destroy(response);
}
```

### 7.4 Existing HTTP Handler Features to Use

From `lib/serve/http_handler.h`:

```cpp
// Request parsing (already implemented)
const char* http_request_get_param(http_request_t *request, const char *name);
const char* http_request_get_header(http_request_t *request, const char *name);
char* http_request_get_body(http_request_t *request);
size_t http_request_get_body_size(http_request_t *request);

// Response generation (already implemented)
void http_response_set_status(http_response_t *response, int status_code);
void http_response_set_header(http_response_t *response, const char *name, const char *value);
void http_response_add_string(http_response_t *response, const char *content);
void http_response_add_printf(http_response_t *response, const char *format, ...);

// Status codes (already defined)
HTTP_STATUS_OK, HTTP_STATUS_BAD_REQUEST, HTTP_STATUS_NOT_FOUND, etc.

// Methods (already defined)
HTTP_METHOD_GET, HTTP_METHOD_POST, HTTP_METHOD_DELETE, etc.
```

### 7.5 JSON Handling

Reuse existing Lambda JSON parser:

```cpp
// From lambda/input/input-json.cpp
void parse_json(Input* input, const char* json_string);

// For response generation, use strbuf
void json_response_ok(StrBuf* buf, const char* value);
void json_response_error(StrBuf* buf, const char* error, const char* message);
```

### 7.6 Benefits of Reusing `lib/serve/`

1. **Already integrated** with project build system
2. **Event-driven** architecture (libevent) for efficient I/O
3. **TLS support** available if needed (HTTPS)
4. **Tested infrastructure** - reduces new code/bugs
5. **Signal handling** for graceful shutdown already implemented
6. **Timeout management** built-in

---

## 8. Session Management

### 8.1 Session Structure

```cpp
typedef struct WebDriverSession {
    char id[37];                 // Session UUID
    UiContext* uicon;            // Radiant UI context
    DomDocument* document;       // Current document
    ElementRegistry* elements;   // Element references
    
    // Timeouts (milliseconds)
    int implicit_wait;           // Element finding timeout
    int page_load;               // Navigation timeout
    int script;                  // Not used (no JS)
    
    // Window state
    int window_width;
    int window_height;
    bool headless;               // Headless mode
    
    // Capabilities
    bool accept_insecure_certs;
    bool strict_file_interaction;
} WebDriverSession;
```

### 8.2 Capabilities

Standard capabilities to support:

```json
{
  "capabilities": {
    "browserName": "radiant",
    "browserVersion": "1.0",
    "platformName": "macos",
    "acceptInsecureCerts": false,
    "pageLoadStrategy": "normal",
    "proxy": {},
    "setWindowRect": true,
    "timeouts": {
      "implicit": 0,
      "pageLoad": 300000,
      "script": 30000
    },
    "strictFileInteractability": false,
    "unhandledPromptBehavior": "dismiss"
  }
}
```

---

## 9. Headless Mode

For CI/CD automation, support headless rendering:

```cpp
// Extend existing headless support
typedef struct HeadlessContext {
    UiContext base;              // Base UI context
    TVG_Canvas* canvas;          // ThorVG canvas for rendering
    int width, height;           // Virtual viewport size
    float pixel_ratio;           // Scale factor
} HeadlessContext;
```

The headless mode already exists for `lambda render` command - reuse that infrastructure.

---

## 10. Implementation Plan

### Phase 1: Core Infrastructure (Week 1-2)

1. **Extract event simulation** from `event_sim.cpp` to reusable module
2. **Implement element registry** with UUID management
3. **Implement CSS selector engine** (`css_select_one`, `css_select_all`)
4. **Create WebDriver routing** using existing `lib/serve/` HTTP server

**Files:**
- `radiant/webdriver_core.hpp` / `.cpp`
- `radiant/webdriver_locator.cpp`
- `radiant/webdriver_server.cpp` (wraps `lib/serve/server.h`)

### Phase 2: Basic Commands (Week 3-4)

1. **Session management** (new, delete, status)
2. **Navigation** (go, url, title)
3. **Element finding** (element, elements)
4. **Element info** (text, attribute, rect, enabled, displayed)

**Files:**
- `radiant/webdriver_session.hpp` / `.cpp`
- `radiant/webdriver_commands.cpp`

### Phase 3: Interaction Commands (Week 5-6)

1. **Click** with proper coordinate calculation
2. **Send keys** with key code translation
3. **Clear** for form inputs
4. **Screenshots** (full page and element)

### Phase 4: Actions API (Week 7-8)

1. **Pointer actions** (move, down, up)
2. **Key actions** (down, up)
3. **Wheel actions** (scroll)
4. **Action chains** execution

### Phase 5: Polish & Testing (Week 9-10)

1. **Error handling** per W3C spec
2. **Timeout handling**
3. **Compatibility testing** with Selenium
4. **Documentation**

---

## 11. CLI Interface

### 11.1 New Command

```bash
# Start WebDriver server
./lambda.exe webdriver [options]

# Options
--port <num>         WebDriver port (default: 9515)
--headless           Run without window
--viewport <WxH>     Viewport size (default: 1280x720)
--log-level <level>  Logging verbosity
```

### 11.2 Usage with Selenium

```python
from selenium import webdriver
from selenium.webdriver.common.by import By

# Connect to Radiant WebDriver
options = webdriver.ChromeOptions()
driver = webdriver.Remote(
    command_executor='http://localhost:9515',
    options=options
)

# Navigate and interact
driver.get('file:///path/to/test.html')
element = driver.find_element(By.CSS_SELECTOR, 'button.submit')
element.click()

# Get screenshot
driver.save_screenshot('screenshot.png')

driver.quit()
```

---

## 12. Error Handling

WebDriver defines specific error codes:

| Error | HTTP Status | Use Case |
|-------|-------------|----------|
| `invalid session id` | 404 | Session not found |
| `no such element` | 404 | Element not found |
| `stale element reference` | 404 | Element no longer in DOM |
| `element not interactable` | 400 | Hidden/disabled element |
| `invalid argument` | 400 | Bad parameter |
| `timeout` | 500 | Operation timed out |
| `unknown error` | 500 | Internal error |

```cpp
typedef enum WebDriverError {
    WD_SUCCESS = 0,
    WD_INVALID_SESSION_ID,
    WD_NO_SUCH_ELEMENT,
    WD_STALE_ELEMENT_REFERENCE,
    WD_ELEMENT_NOT_INTERACTABLE,
    WD_INVALID_ARGUMENT,
    WD_TIMEOUT,
    WD_UNKNOWN_ERROR,
} WebDriverError;
```

---

## 13. Testing Strategy

### 13.1 Unit Tests

```cpp
// test/test_webdriver_gtest.cpp
TEST(WebDriverLocator, CssSelectorById) {
    // Load test HTML
    DomDocument* doc = load_test_html("<div id='test'>Hello</div>");
    View* elem = css_select_one(doc, "#test");
    ASSERT_NE(elem, nullptr);
    EXPECT_STREQ(elem->as_element()->id, "test");
}

TEST(WebDriverLocator, CssSelectorByClass) {
    DomDocument* doc = load_test_html("<div class='foo bar'>Hello</div>");
    View* elem = css_select_one(doc, ".foo");
    ASSERT_NE(elem, nullptr);
}
```

### 13.2 Integration Tests

Use Selenium WebDriver tests:

```python
# test/webdriver/test_basic.py
def test_click_button(driver):
    driver.get('file:///test/html/form.html')
    btn = driver.find_element(By.ID, 'submit-btn')
    btn.click()
    assert 'submitted' in driver.page_source
```

### 13.3 Compatibility Tests

Run Selenium's own test suite subset:
- Element finding tests
- Click/keyboard tests
- Navigation tests
- Screenshot tests

---

## 14. Limitations

### 14.1 Not Supported (No JavaScript Engine)

| Feature | Reason |
|---------|--------|
| Execute Script | No JS engine |
| Execute Async Script | No JS engine |
| Alerts/Confirms/Prompts | No JS dialogs |
| Cookies (JS access) | No document.cookie |
| Local/Session Storage | No Web Storage API |

### 14.2 Partial Support

| Feature | Status |
|---------|--------|
| Frames/Iframes | Supported via `embed->doc` |
| Shadow DOM | Not implemented |
| File Upload | Basic support (no JS events) |
| Drag and Drop | Basic support (no JS events) |

---

## 15. Future Enhancements

### 15.1 BiDi Protocol

The newer [WebDriver BiDi](https://w3c.github.io/webdriver-bidi/) protocol uses WebSocket for bidirectional communication. Future enhancement for:
- Real-time DOM change notifications
- Console log streaming
- Network request interception

### 15.2 Accessibility Testing

Integration with accessibility testing:
- AXE-core compatible output
- ARIA attribute inspection
- Role/state/property queries

### 15.3 Performance Metrics

- Layout timing
- Paint timing
- Resource loading metrics

---

## 16. Resource Estimates

| Phase | Effort | Dependencies |
|-------|--------|--------------|
| Phase 1: Core | 2 weeks | CSS parser, event system |
| Phase 2: Basic Commands | 2 weeks | Phase 1 |
| Phase 3: Interaction | 2 weeks | Phase 2 |
| Phase 4: Actions API | 2 weeks | Phase 3 |
| Phase 5: Polish | 2 weeks | Phase 4 |

**Total: 10 weeks** for full implementation

---

## 17. File Structure

```
lib/serve/                      # Existing HTTP server (reuse)
├── server.h / .c               # Server lifecycle, libevent integration
├── http_handler.h / .c         # Request/response helpers
├── tls_handler.h / .c          # TLS support (optional)
└── utils.h / .c                # Utilities

radiant/
├── webdriver/
│   ├── webdriver.hpp           # Public API
│   ├── webdriver_server.cpp    # Routes requests (uses lib/serve)
│   ├── webdriver_session.cpp   # Session management
│   ├── webdriver_commands.cpp  # Command handlers
│   ├── webdriver_locator.cpp   # Element finding
│   ├── webdriver_actions.cpp   # Action chains (from event_sim)
│   └── webdriver_errors.hpp    # Error definitions
test/
├── webdriver/
│   ├── test_webdriver_gtest.cpp
│   └── test_selenium.py
```

---

## 18. Conclusion

Implementing WebDriver support in Radiant is feasible by:

1. **Reusing** the existing event simulation infrastructure
2. **Leveraging** the state store for element state queries
3. **Extending** the CSS selector matcher for element location
4. **Adding** a minimal HTTP server for the WebDriver protocol

The modular approach allows incremental delivery, with basic automation available early and advanced features added progressively. This will significantly improve Radiant's testability and integration with standard QA tooling.

---

## Appendix A: WebDriver Command Reference

Full list of W3C WebDriver commands with implementation priority:

| Priority | Command | Endpoint |
|----------|---------|----------|
| P0 | New Session | POST /session |
| P0 | Delete Session | DELETE /session/{id} |
| P0 | Navigate To | POST /session/{id}/url |
| P0 | Get Current URL | GET /session/{id}/url |
| P0 | Find Element | POST /session/{id}/element |
| P0 | Find Elements | POST /session/{id}/elements |
| P0 | Get Element Text | GET /session/{id}/element/{id}/text |
| P0 | Element Click | POST /session/{id}/element/{id}/click |
| P0 | Element Send Keys | POST /session/{id}/element/{id}/value |
| P0 | Take Screenshot | GET /session/{id}/screenshot |
| P1 | Get Title | GET /session/{id}/title |
| P1 | Get Active Element | GET /session/{id}/element/active |
| P1 | Get Element Attribute | GET /session/{id}/element/{id}/attribute/{name} |
| P1 | Get Element Rect | GET /session/{id}/element/{id}/rect |
| P1 | Is Element Enabled | GET /session/{id}/element/{id}/enabled |
| P1 | Is Element Displayed | GET /session/{id}/element/{id}/displayed |
| P1 | Element Clear | POST /session/{id}/element/{id}/clear |
| P2 | Get Page Source | GET /session/{id}/source |
| P2 | Back | POST /session/{id}/back |
| P2 | Forward | POST /session/{id}/forward |
| P2 | Refresh | POST /session/{id}/refresh |
| P2 | Get Window Rect | GET /session/{id}/window/rect |
| P2 | Set Window Rect | POST /session/{id}/window/rect |
| P2 | Perform Actions | POST /session/{id}/actions |
| P2 | Release Actions | DELETE /session/{id}/actions |
| P3 | Get Element Property | GET /session/{id}/element/{id}/property/{name} |
| P3 | Get Element CSS Value | GET /session/{id}/element/{id}/css/{name} |
| P3 | Is Element Selected | GET /session/{id}/element/{id}/selected |
| P3 | Element Screenshot | GET /session/{id}/element/{id}/screenshot |

---

## Appendix B: Key Code Mapping

Reuse key mapping from event_sim.cpp:

```cpp
// From event_sim.cpp - key_name_to_glfw()
// Already handles: a-z, 0-9, space, enter, tab, backspace, delete,
// escape, arrows, home, end, pageup, pagedown, ctrl, shift, alt, super, F1-F12
```

WebDriver uses Unicode code points for character input and special key names for modifier/navigation keys. The existing mapping covers all essential keys.

---

**Document Version:** 1.0  
**Last Updated:** January 2026  
**Author:** Radiant Development Team
