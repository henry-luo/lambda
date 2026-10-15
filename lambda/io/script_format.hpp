#ifndef LAMBDA_SCRIPT_FORMAT_HPP
#define LAMBDA_SCRIPT_FORMAT_HPP
#include <string.h>
#include "wiki_format.hpp"

// Routing only; all source syntax belongs to lambda.doc.misc (D7.1.1).
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
    if (const char* target = wiki_format_flavor(type, flavor))
        return {"lambda.doc.wiki", target, "flavor"};
    if (const char* target = script_format_target(type, flavor))
        return {"lambda.doc.misc", target, "type"};
    return {};
}
#endif
