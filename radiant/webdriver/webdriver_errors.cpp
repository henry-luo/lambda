/**
 * @file webdriver_errors.cpp
 * @brief WebDriver error handling implementation
 */

#include "webdriver.hpp"

const char* webdriver_error_name(WebDriverError error) {
    switch (error) {
        case WD_SUCCESS:
            return "success";
        case WD_ERROR_INVALID_SESSION_ID:
            return "invalid session id";
        case WD_ERROR_NO_SUCH_ELEMENT:
            return "no such element";
        case WD_ERROR_NO_SUCH_FRAME:
            return "no such frame";
        case WD_ERROR_NO_SUCH_WINDOW:
            return "no such window";
        case WD_ERROR_STALE_ELEMENT_REFERENCE:
            return "stale element reference";
        case WD_ERROR_ELEMENT_NOT_INTERACTABLE:
            return "element not interactable";
        case WD_ERROR_INVALID_ELEMENT_STATE:
            return "invalid element state";
        case WD_ERROR_INVALID_ARGUMENT:
            return "invalid argument";
        case WD_ERROR_INVALID_SELECTOR:
            return "invalid selector";
        case WD_ERROR_TIMEOUT:
            return "timeout";
        case WD_ERROR_UNKNOWN_COMMAND:
            return "unknown command";
        case WD_ERROR_UNKNOWN_ERROR:
            return "unknown error";
        case WD_ERROR_UNSUPPORTED_OPERATION:
            return "unsupported operation";
        case WD_ERROR_SESSION_NOT_CREATED:
            return "session not created";
        case WD_ERROR_MOVE_TARGET_OUT_OF_BOUNDS:
            return "move target out of bounds";
        case WD_ERROR_JAVASCRIPT_ERROR:
            return "javascript error";
        default:
            return "unknown error";
    }
}

int webdriver_error_http_status(WebDriverError error) {
    switch (error) {
        case WD_SUCCESS:
            return 200;
        case WD_ERROR_INVALID_SESSION_ID:
        case WD_ERROR_NO_SUCH_ELEMENT:
        case WD_ERROR_NO_SUCH_FRAME:
        case WD_ERROR_NO_SUCH_WINDOW:
        case WD_ERROR_STALE_ELEMENT_REFERENCE:
            return 404;
        case WD_ERROR_ELEMENT_NOT_INTERACTABLE:
        case WD_ERROR_INVALID_ELEMENT_STATE:
        case WD_ERROR_INVALID_ARGUMENT:
        case WD_ERROR_INVALID_SELECTOR:
        case WD_ERROR_MOVE_TARGET_OUT_OF_BOUNDS:
            return 400;
        case WD_ERROR_SESSION_NOT_CREATED:
            return 500;
        case WD_ERROR_TIMEOUT:
        case WD_ERROR_UNKNOWN_ERROR:
        case WD_ERROR_JAVASCRIPT_ERROR:
            return 500;
        case WD_ERROR_UNKNOWN_COMMAND:
        case WD_ERROR_UNSUPPORTED_OPERATION:
            return 404;
        default:
            return 500;
    }
}
