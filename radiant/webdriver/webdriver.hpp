/**
 * @file webdriver.hpp
 * @brief WebDriver Protocol Support for Radiant
 *
 * Implements W3C WebDriver specification for browser automation.
 * Enables testing Radiant with Selenium, Puppeteer, Playwright, etc.
 */

#ifndef RADIANT_WEBDRIVER_HPP
#define RADIANT_WEBDRIVER_HPP

#include "../view.hpp"
#include "../state_store.hpp"
#include "../../lib/hashmap.h"
#include "../../lib/arena.h"
#include "../../lib/arraylist.h"
#include "../../lib/serve/server.h"
#include "../../lib/serve/http_handler.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// WebDriver Error Codes
// ============================================================================

typedef enum WebDriverError {
    WD_SUCCESS = 0,
    WD_ERROR_INVALID_SESSION_ID,
    WD_ERROR_NO_SUCH_ELEMENT,
    WD_ERROR_NO_SUCH_FRAME,
    WD_ERROR_NO_SUCH_WINDOW,
    WD_ERROR_STALE_ELEMENT_REFERENCE,
    WD_ERROR_ELEMENT_NOT_INTERACTABLE,
    WD_ERROR_INVALID_ELEMENT_STATE,
    WD_ERROR_INVALID_ARGUMENT,
    WD_ERROR_INVALID_SELECTOR,
    WD_ERROR_TIMEOUT,
    WD_ERROR_UNKNOWN_COMMAND,
    WD_ERROR_UNKNOWN_ERROR,
    WD_ERROR_UNSUPPORTED_OPERATION,
    WD_ERROR_SESSION_NOT_CREATED,
    WD_ERROR_MOVE_TARGET_OUT_OF_BOUNDS,
    WD_ERROR_JAVASCRIPT_ERROR,  // Not applicable (no JS engine)
} WebDriverError;

/**
 * Get error name string for W3C response
 */
const char* webdriver_error_name(WebDriverError error);

/**
 * Get HTTP status code for error
 */
int webdriver_error_http_status(WebDriverError error);

// ============================================================================
// Element Reference
// ============================================================================

#define WD_ELEMENT_ID_LEN 37  // UUID + null terminator

typedef struct ElementRef {
    char id[WD_ELEMENT_ID_LEN];    // UUID string
    View* view;                     // Pointer to View
    uint64_t document_version;      // For stale detection
} ElementRef;

typedef struct ElementRegistry {
    HashMap* refs;                  // ID -> ElementRef*
    Arena* arena;
    uint64_t next_id;
} ElementRegistry;

/**
 * Create element registry
 */
ElementRegistry* element_registry_create(Arena* arena);

/**
 * Add element to registry, returns element ID
 */
const char* element_registry_add(ElementRegistry* reg, View* view, uint64_t doc_version);

/**
 * Get element by ID
 */
View* element_registry_get(ElementRegistry* reg, const char* id);

/**
 * Check if element reference is stale
 */
bool element_registry_is_stale(ElementRegistry* reg, const char* id, uint64_t current_version);

/**
 * Clear all references
 */
void element_registry_clear(ElementRegistry* reg);

// ============================================================================
// WebDriver Session
// ============================================================================

typedef struct WebDriverSession {
    char id[WD_ELEMENT_ID_LEN];     // Session UUID
    UiContext* uicon;               // Radiant UI context
    DomDocument* document;          // Current document
    ElementRegistry* elements;      // Element references
    Arena* arena;                   // Session memory arena
    Pool* pool;                     // Memory pool (for cleanup)
    
    // Timeouts (milliseconds)
    int implicit_wait_ms;           // Element finding timeout (default 0)
    int page_load_ms;               // Navigation timeout (default 300000)
    int script_ms;                  // Not used (no JS)
    
    // Window state
    int window_width;
    int window_height;
    bool headless;
    
    // Document version for stale element detection
    uint64_t document_version;
    
    // Session capabilities
    bool accept_insecure_certs;
} WebDriverSession;

/**
 * Create a new WebDriver session
 */
WebDriverSession* webdriver_session_create(int width, int height, bool headless);

/**
 * Destroy a WebDriver session
 */
void webdriver_session_destroy(WebDriverSession* session);

/**
 * Navigate to URL
 */
WebDriverError webdriver_session_navigate(WebDriverSession* session, const char* url);

/**
 * Get current URL
 */
const char* webdriver_session_get_url(WebDriverSession* session);

/**
 * Get page title
 */
const char* webdriver_session_get_title(WebDriverSession* session);

/**
 * Get page source
 */
char* webdriver_session_get_source(WebDriverSession* session);

// ============================================================================
// Element Locator Strategies
// ============================================================================

typedef enum LocatorStrategy {
    LOCATOR_CSS_SELECTOR,
    LOCATOR_LINK_TEXT,
    LOCATOR_PARTIAL_LINK_TEXT,
    LOCATOR_TAG_NAME,
    LOCATOR_XPATH,  // Future: optional XPath support
} LocatorStrategy;

/**
 * Parse locator strategy from string
 */
LocatorStrategy webdriver_parse_strategy(const char* strategy);

/**
 * Find first matching element
 */
View* webdriver_find_element(WebDriverSession* session, LocatorStrategy strategy, 
                              const char* value, View* root);

/**
 * Find all matching elements
 */
int webdriver_find_elements(WebDriverSession* session, LocatorStrategy strategy,
                             const char* value, View* root, ArrayList* results);

// ============================================================================
// Element Actions
// ============================================================================

/**
 * Click on element
 */
WebDriverError webdriver_element_click(WebDriverSession* session, View* element);

/**
 * Clear element (text input)
 */
WebDriverError webdriver_element_clear(WebDriverSession* session, View* element);

/**
 * Send keys to element
 */
WebDriverError webdriver_element_send_keys(WebDriverSession* session, View* element, 
                                            const char* text);

/**
 * Get element text content
 */
char* webdriver_element_get_text(WebDriverSession* session, View* element);

/**
 * Get element attribute
 */
const char* webdriver_element_get_attribute(WebDriverSession* session, View* element,
                                             const char* name);

/**
 * Get element CSS property
 */
const char* webdriver_element_get_css(WebDriverSession* session, View* element,
                                       const char* property);

/**
 * Get element bounding rect
 */
void webdriver_element_get_rect(WebDriverSession* session, View* element,
                                 float* x, float* y, float* width, float* height);

/**
 * Check if element is enabled
 */
bool webdriver_element_is_enabled(WebDriverSession* session, View* element);

/**
 * Check if element is displayed
 */
bool webdriver_element_is_displayed(WebDriverSession* session, View* element);

/**
 * Check if element is selected (checkbox, radio, option)
 */
bool webdriver_element_is_selected(WebDriverSession* session, View* element);

// ============================================================================
// Screenshot
// ============================================================================

/**
 * Take full page screenshot, returns base64-encoded PNG
 */
char* webdriver_screenshot(WebDriverSession* session);

/**
 * Take element screenshot, returns base64-encoded PNG
 */
char* webdriver_element_screenshot(WebDriverSession* session, View* element);

// ============================================================================
// WebDriver Server
// ============================================================================

typedef struct WebDriverServer {
    server_t* http_server;          // From lib/serve
    HashMap* sessions;              // Session ID -> WebDriverSession*
    Arena* arena;
    Pool* pool;
    const char* host;
    int port;
    bool running;
} WebDriverServer;

/**
 * Create WebDriver server
 */
WebDriverServer* webdriver_server_create(const char* host, int port);

/**
 * Start server (non-blocking, starts event loop in background)
 */
int webdriver_server_start(WebDriverServer* server);

/**
 * Run server (blocking, runs event loop)
 */
int webdriver_server_run(WebDriverServer* server);

/**
 * Stop server
 */
void webdriver_server_stop(WebDriverServer* server);

/**
 * Destroy server
 */
void webdriver_server_destroy(WebDriverServer* server);

// ============================================================================
// Actions API (W3C Actions)
// ============================================================================

typedef enum ActionType {
    ACTION_PAUSE,
    ACTION_KEY_DOWN,
    ACTION_KEY_UP,
    ACTION_POINTER_DOWN,
    ACTION_POINTER_UP,
    ACTION_POINTER_MOVE,
    ACTION_POINTER_CANCEL,
    ACTION_SCROLL,
} ActionType;

typedef struct WebDriverAction {
    ActionType type;
    int duration_ms;        // For pause and pointer move
    union {
        struct {
            int key;        // Virtual key code or Unicode codepoint
        } key;
        struct {
            int button;     // Mouse button (0=left, 1=middle, 2=right)
            int x, y;       // Position for move
            View* origin;   // Origin element for relative move
        } pointer;
        struct {
            int x, y;       // Position
            int dx, dy;     // Scroll delta
        } scroll;
    };
} WebDriverAction;

/**
 * Perform a sequence of actions
 */
WebDriverError webdriver_perform_actions(WebDriverSession* session, 
                                          ArrayList* actions);

/**
 * Release all pressed keys and buttons
 */
WebDriverError webdriver_release_actions(WebDriverSession* session);

#ifdef __cplusplus
}
#endif

#endif // RADIANT_WEBDRIVER_HPP
