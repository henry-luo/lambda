zig cc -Ilambda/tree-sitter/lib/include lambda/tree-sitter/libtree-sitter.a \
  lambda/tree-sitter-lambda/src/parser.c lambda/lambda-parser.c\
  lib/strbuf.c lib/strview.c lib/arraylist.c \
  lib/mem-pool/src/variable.c lib/mem-pool/src/buffer.c lib/mem-pool/src/utils.c \
  -I/usr/local/include /usr/local/lib/libmir.a \
  lambda/transpile.c lambda/build_ast.c lambda/mir.c lambda/lambda.c \
  -o transpile.exe -fms-extensions