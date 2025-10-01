#include "html_comparison.h"
#include <regex>
#include <algorithm>
#include <sstream>
#include <cctype>
#include <iostream>

HtmlComparator::HtmlComparator()
    : ignore_whitespace_(true)
    , normalize_attributes_(true)
    , case_sensitive_(false) {
}

HtmlComparator::~HtmlComparator() {}

void HtmlComparator::set_ignore_whitespace(bool ignore) {
    ignore_whitespace_ = ignore;
}

void HtmlComparator::set_normalize_attributes(bool normalize) {
    normalize_attributes_ = normalize;
}

void HtmlComparator::set_case_sensitive(bool sensitive) {
    case_sensitive_ = sensitive;
}

std::string HtmlComparator::normalize_whitespace(const std::string& html) {
    if (!ignore_whitespace_) return html;

    // Replace multiple whitespace with single space
    std::regex ws_regex("\\s+");
    std::string result = std::regex_replace(html, ws_regex, " ");

    // Remove whitespace around tags (between tags)
    std::regex tag_ws_regex(">\\s+<");
    result = std::regex_replace(result, tag_ws_regex, "><");

    // Remove whitespace after opening tags
    std::regex after_open_tag_regex(">\\s+");
    result = std::regex_replace(result, after_open_tag_regex, ">");

    // Remove whitespace before closing tags
    std::regex before_close_tag_regex("\\s+<");
    result = std::regex_replace(result, before_close_tag_regex, "<");

    // Trim leading/trailing whitespace
    size_t start = result.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";

    size_t end = result.find_last_not_of(" \t\r\n");
    return result.substr(start, end - start + 1);
}

std::string HtmlComparator::normalize_attributes(const std::string& html) {
    if (!normalize_attributes_) return html;

    // Sort attributes within tags (simplified approach)
    // This is a basic implementation - could be enhanced for more complex cases
    std::regex attr_regex("<([^>]+)>");
    std::string result = html;

    std::sregex_iterator iter(html.begin(), html.end(), attr_regex);
    std::sregex_iterator end;

    for (; iter != end; ++iter) {
        std::string tag_content = iter->str(1);
        // For now, just preserve as-is
        // Could implement attribute sorting here if needed
    }

    return result;
}

std::string HtmlComparator::remove_comments(const std::string& html) {
    std::regex comment_regex("<!--.*?-->");
    return std::regex_replace(html, comment_regex, "");
}

std::string HtmlComparator::normalize_html(const std::string& html) {
    std::string result = html;

    // Remove HTML comments
    result = remove_comments(result);

    // Normalize whitespace
    result = normalize_whitespace(result);

    // Normalize attributes
    result = normalize_attributes(result);

    // Convert to lowercase if not case sensitive
    if (!case_sensitive_) {
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    }

    return result;
}

bool HtmlComparator::compare_normalized(const std::string& expected, const std::string& actual) {
    std::string norm_expected = normalize_html(expected);
    std::string norm_actual = normalize_html(actual);

    return norm_expected == norm_actual;
}

std::string HtmlComparator::extract_context(const std::string& text, size_t position, size_t context_size) {
    size_t start = (position >= context_size) ? position - context_size : 0;
    size_t length = std::min(context_size * 2, text.length() - start);

    std::string context = text.substr(start, length);

    // Add markers to show the position
    if (position >= start && position < start + length) {
        size_t relative_pos = position - start;
        context.insert(relative_pos, ">>>");
        context.insert(relative_pos + 3, "<<<");
    }

    return context;
}

void HtmlComparator::find_differences(const std::string& expected, const std::string& actual) {
    last_differences_.clear();

    std::string norm_expected = normalize_html(expected);
    std::string norm_actual = normalize_html(actual);

    // Simple character-by-character comparison
    size_t min_length = std::min(norm_expected.length(), norm_actual.length());

    for (size_t i = 0; i < min_length; ++i) {
        if (norm_expected[i] != norm_actual[i]) {
            HtmlDifference diff;
            diff.type = HtmlDifference::CONTENT_MISMATCH;
            diff.position = i;
            diff.expected = std::string(1, norm_expected[i]);
            diff.actual = std::string(1, norm_actual[i]);
            diff.context = extract_context(norm_expected, i);

            last_differences_.push_back(diff);
            break; // Report first difference for now
        }
    }

    // Check for length differences
    if (norm_expected.length() != norm_actual.length()) {
        HtmlDifference diff;
        diff.type = HtmlDifference::STRUCTURE_MISMATCH;
        diff.position = min_length;
        diff.expected = "Length: " + std::to_string(norm_expected.length());
        diff.actual = "Length: " + std::to_string(norm_actual.length());
        diff.context = "Length mismatch";

        last_differences_.push_back(diff);
    }
}

bool HtmlComparator::compare_html(const std::string& expected, const std::string& actual) {
    return compare_normalized(expected, actual);
}

bool HtmlComparator::compare_html_detailed(const std::string& expected, const std::string& actual,
                                          std::vector<HtmlDifference>& differences) {
    bool result = compare_normalized(expected, actual);

    if (!result) {
        find_differences(expected, actual);
        differences = last_differences_;
    }

    return result;
}

const std::vector<HtmlDifference>& HtmlComparator::get_last_differences() const {
    return last_differences_;
}

std::string HtmlComparator::get_comparison_report() const {
    if (last_differences_.empty()) {
        return "HTML comparison successful - no differences found.";
    }

    std::stringstream report;
    report << "HTML comparison failed with " << last_differences_.size() << " difference(s):\n";

    for (size_t i = 0; i < last_differences_.size(); ++i) {
        const auto& diff = last_differences_[i];
        report << "\n" << (i + 1) << ". ";

        switch (diff.type) {
            case HtmlDifference::CONTENT_MISMATCH:
                report << "Content mismatch";
                break;
            case HtmlDifference::STRUCTURE_MISMATCH:
                report << "Structure mismatch";
                break;
            case HtmlDifference::ATTRIBUTE_MISMATCH:
                report << "Attribute mismatch";
                break;
            case HtmlDifference::WHITESPACE_DIFFERENCE:
                report << "Whitespace difference";
                break;
        }

        report << " at position " << diff.position << "\n";
        report << "   Expected: " << diff.expected << "\n";
        report << "   Actual:   " << diff.actual << "\n";
        report << "   Context:  " << diff.context << "\n";
    }

    return report.str();
}
