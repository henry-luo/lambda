#ifndef LAMBDA_INPUT_PARSERS_H
#define LAMBDA_INPUT_PARSERS_H

/**
 * @file input-parsers.h
 * @brief Forward declarations for all input parser entry points.
 *
 * Centralises the ad-hoc forward declarations previously scattered at
 * the top of input.cpp.  Include this header when you need to call a
 * parser by name (e.g. from the dispatch table in input.cpp).
 */

#include "../lambda-data.hpp"   // Item, Input, Element

// ── Structured data parsers (C++ linkage) ──────────────────────────

void parse_json(Input* input, const char* json_string);
Item parse_json_to_item(Input* input, const char* json_string);
Item parse_json_to_item_strict(Input* input, const char* json_string, bool* ok);
void parse_csv(Input* input, const char* csv_string);
void parse_ini(Input* input, const char* ini_string);
void parse_properties(Input* input, const char* prop_string);
void parse_toml(Input* input, const char* toml_string);
void parse_yaml(Input* input, const char* yaml_str);
void parse_xml(Input* input, const char* xml_string);
void parse_css(Input* input, const char* css_string);

// ── Relational database (C++ linkage) ──────────────────────────────

// open a database file and produce a <db> element
Input* input_rdb_from_path(const char* pathname, const char* type);
// detect whether a path/type should be handled as RDB; returns driver name or NULL
const char* rdb_detect_format(const char* pathname, const char* type);

// ── Document parsers (C++ linkage) ─────────────────────────────────

Element* html5_parse(Input* input, const char* html);

struct Html5ParseOptions;
Element* html5_parse_ex(Input* input, const char* html, Html5ParseOptions* opts);

void parse_rtf(Input* input, const char* rtf_string);
void parse_pdf(Input* input, const char* pdf_string, size_t pdf_length);
void parse_mark(Input* input, const char* mark_string);

// ── Contact / calendar / email (C++ linkage) ───────────────────────

void parse_eml(Input* input, const char* eml_string);
void parse_vcf(Input* input, const char* vcf_string);
void parse_ics(Input* input, const char* ics_string);

// ── Script / JSX (C++ linkage) ─────────────────────────────────────

void parse_jsx(Input* input, const char* jsx_string);

// ── Math (C++ linkage) ─────────────────────────────────────────────

void parse_math(Input* input, const char* math_string, const char* flavor);

// ── Markup and LaTeX (C linkage — called from MIR JIT) ─────────────

#ifdef __cplusplus
extern "C" {
#endif

void parse_latex_ts(Input* input, const char* latex_string);
Item input_markup_modular(Input* input, const char* content);
Item input_markup_commonmark(Input* input, const char* content);

#ifdef __cplusplus
}
#endif

// C++ only — old markup parser and format-aware entry point
#ifdef __cplusplus
Item input_markup(Input* input, const char* content);

#include "markup-parser.h"
Item input_markup_with_format(Input* input, const char* content, MarkupFormat format);
#endif

#endif // LAMBDA_INPUT_PARSERS_H
