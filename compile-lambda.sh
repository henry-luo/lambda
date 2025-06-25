#!/bin/bash

output=$(clang -Ilambda/tree-sitter/lib/include lambda/tree-sitter/libtree-sitter.a \
  lambda/tree-sitter-lambda/src/parser.c lambda/parse.c\
  lib/strbuf.c lib/strview.c lib/arraylist.c lib/file.c lib/hashmap.c \
  lib/mem-pool/src/variable.c lib/mem-pool/src/buffer.c lib/mem-pool/src/utils.c \
  -I/usr/local/include /usr/local/lib/libmir.a /usr/local/lib/libzlog.a \
  -I/opt/homebrew/include -L/opt/homebrew/lib -lgmp \
  lambda/main.c lambda/runner.c \
  lambda/transpile.c lambda/build_ast.c lambda/mir.c lambda/pack.c lambda/print.c \
  lambda/lambda-eval.c lambda/lambda-mem.c lambda/input-json.c lambda/input-csv.c lambda/input-ini.c \
  -o transpile.exe -fms-extensions -Werror=format -Werror=incompatible-pointer-types -Werror=multichar \
  -pedantic -fcolor-diagnostics 2>&1)

# Output the captured, colorized messages
echo -e "$output"

# Count errors and warnings (ignores color codes)
num_errors=$(echo "$output" | grep -c "error:")
num_warnings=$(echo "$output" | grep -c "warning:")

# Print summary with optional coloring
RED="\033[0;31m"
YELLOW="\033[1;33m"
RESET="\033[0m"

echo
echo -e "${YELLOW}Summary:${RESET}"
if [ "$num_errors" -gt 0 ]; then
    echo -e "${RED}Errors:   $num_errors${RESET}"
else
    echo -e "Errors:   $num_errors"
fi
echo -e "${YELLOW}Warnings: $num_warnings${RESET}"


