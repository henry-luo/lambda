#include "html_encoder.hpp"

namespace html {

std::string HtmlEncoder::escape(std::string_view text) {
    if (!needs_escaping(text)) {
        return std::string(text);
    }
    
    std::string result;
    result.reserve(text.length() * 1.2); // Estimate 20% growth
    
    for (char c : text) {
        switch (c) {
            case '&':
                result += "&amp;";
                break;
            case '<':
                result += "&lt;";
                break;
            case '>':
                result += "&gt;";
                break;
            case '"':
                result += "&quot;";
                break;
            default:
                result += c;
        }
    }
    
    return result;
}

std::string HtmlEncoder::escape_attribute(std::string_view text) {
    std::string result;
    result.reserve(text.length() * 1.2);
    
    for (char c : text) {
        switch (c) {
            case '&':
                result += "&amp;";
                break;
            case '<':
                result += "&lt;";
                break;
            case '>':
                result += "&gt;";
                break;
            case '"':
                result += "&quot;";
                break;
            case '\'':
                result += "&#39;";
                break;
            default:
                result += c;
        }
    }
    
    return result;
}

bool HtmlEncoder::needs_escaping(std::string_view text) {
    for (char c : text) {
        if (c == '&' || c == '<' || c == '>' || c == '"') {
            return true;
        }
    }
    return false;
}

} // namespace html
