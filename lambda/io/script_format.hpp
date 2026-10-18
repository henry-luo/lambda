#ifndef LAMBDA_SCRIPT_FORMAT_HPP
#define LAMBDA_SCRIPT_FORMAT_HPP
#include <string.h>
#include "wiki_format.hpp"

// Routing only; source syntax belongs to the script packages (D7.1.1).
inline const char* script_format_target(const char* type, const char* flavor = nullptr) {
    if (!type) return nullptr;
    if (!strncmp(type, "markup:", 7)) return script_format_target(type + 7);
    if (!strcmp(type, "markup")) return script_format_target(flavor);
    static const struct { const char* alias; const char* target; } formats[] = {
        {"asciidoc", "asciidoc"}, {"adoc", "asciidoc"},
        {"man", "man"}, {"troff", "man"}, {"typst", "typst"}, {"eml", "eml"},
        {"vcf", "vcf"}, {"vcard", "vcf"}, {"ics", "ics"}, {"ical", "ics"}, {"icalendar", "ics"}
    };
    for (const auto& entry : formats) if (!strcmp(type, entry.alias)) return entry.target;
    return nullptr;
}

struct ScriptFormatRoute {
    const char* package;
    const char* target;
    const char* option;
};

inline ScriptFormatRoute script_format_route(const char* type, const char* flavor = nullptr) {
    // Math conversion runs above IO, including XML serialization via the package.
#ifndef LAMBDA_NO_MATH_FORMAT
    if (type && !strcmp(type, "math"))
        return {"lambda.doc.math.format", flavor && *flavor ? flavor : "latex", "flavor"};
    if (type && !strncmp(type, "math-", 5)) {
        const char* target = type + 5;
        if (!strcmp(target, "latex") || !strcmp(target, "ascii") ||
                !strcmp(target, "mathml") || !strcmp(target, "typst"))
            return {"lambda.doc.math.format", target, "flavor"};
    }
#endif
    if (const char* target = wiki_format_flavor(type, flavor))
        return {"lambda.doc.wiki", target, "flavor"};
    if (const char* target = script_format_target(type, flavor))
        return {"lambda.doc.misc", target, "type"};
    return {};
}
#endif
