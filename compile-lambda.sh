clang -Ilambda/tree-sitter/lib/include lambda/tree-sitter/libtree-sitter.a \
  lambda/tree-sitter-lambda/src/parser.c lambda/parse.c\
  lib/strbuf.c lib/strview.c lib/arraylist.c lib/file.c \
  lib/mem-pool/src/variable.c lib/mem-pool/src/buffer.c lib/mem-pool/src/utils.c \
  -I/usr/local/include /usr/local/lib/libmir.a /usr/local/lib/libzlog.a \
  lambda/main.c lambda/runner.c \
  lambda/transpile.c lambda/build_ast.c lambda/mir.c lambda/lambda.c lambda/pack.c lambda/print.c \
  -o transpile.exe -fms-extensions -Werror=format -Werror=incompatible-pointer-types -Werror=multichar

  # todo:  -Werror=incompatible-pointer-types