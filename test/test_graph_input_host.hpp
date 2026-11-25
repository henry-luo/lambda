#pragma once

#include "lambda/input/input.hpp"
#include "lambda/runtime/transpiler.hpp"

// The hosted suite uses the same request-scoped parser as the CLI.
class GraphInputHost {
    Runtime runtime_;
    InputScriptParser parser_;

public:
    GraphInputHost() {
        runtime_init(&runtime_);
        parser_ = lambda_input_script_parser(&runtime_);
    }

    ~GraphInputHost() { runtime_cleanup(&runtime_); }

    Input* read(const char* source, Url* url, String* type, String* flavor) {
        InputParseOptions options = {};
        options.script_parser = &parser_;
        return input_from_source_with_options(source, url, type, flavor, &options);
    }

    const InputScriptParser* parser() const { return &parser_; }
};
